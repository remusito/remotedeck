#include "stream.h"
#include "input.h"

#include <chiaki/ffmpegdecoder.h>
#include <chiaki/opusdecoder.h>

#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>

#include <SDL.h>

#include <stdio.h>
#include <string.h>

// Latencia máxima de audio en cola antes de descartar (ms).
#define RD_AUDIO_MAX_LATENCY_MS 100

typedef struct rd_stream_t
{
	ChiakiLog *log;
	const RdStreamSettings *settings;
	ChiakiSession session;
	ChiakiFfmpegDecoder decoder;
	ChiakiOpusDecoder opus;

	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	int tex_w, tex_h;
	Uint32 tex_fmt;
	bool warned_pix_fmt;

	SDL_AudioDeviceID audio_dev;
	uint32_t audio_rate, audio_channels;

	Uint32 ev_base; // eventos propios: base+0 frame, +1 quit, +2 rumble, +3 conectado
	SDL_atomic_t frame_pending;

	RdInput input;
	ChiakiQuitReason quit_reason;
	char quit_reason_str[256];
} RdStream;

enum { RD_EV_FRAME, RD_EV_QUIT, RD_EV_RUMBLE, RD_EV_CONNECTED, RD_EV_COUNT };

void rd_stream_settings_default(RdStreamSettings *s)
{
	memset(s, 0, sizeof(*s));
	// La pantalla del Steam Deck es 1280x800: 720p60 es el punto óptimo.
	s->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	s->fps = CHIAKI_VIDEO_FPS_PRESET_60;
	s->fullscreen = true;
}

static void push_event(RdStream *st, int which, int code, void *data1)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = st->ev_base + (Uint32)which;
	ev.user.code = code;
	ev.user.data1 = data1;
	SDL_PushEvent(&ev);
}

/* ---------- Callbacks de hilos de chiaki (¡no tocar SDL video aquí!) ---------- */

static void frame_available_cb(ChiakiFfmpegDecoder *decoder, void *user)
{
	(void)decoder;
	RdStream *st = user;
	// Un único evento pendiente: si el main loop va retrasado, pull_frame se queda con el último.
	if(SDL_AtomicCAS(&st->frame_pending, 0, 1))
		push_event(st, RD_EV_FRAME, 0, NULL);
}

static void audio_settings_cb(uint32_t channels, uint32_t rate, void *user)
{
	RdStream *st = user;
	if(st->audio_dev && st->audio_channels == channels && st->audio_rate == rate)
		return;
	if(st->audio_dev)
		SDL_CloseAudioDevice(st->audio_dev);

	SDL_AudioSpec want, have;
	memset(&want, 0, sizeof(want));
	want.freq = (int)rate;
	want.format = AUDIO_S16SYS;
	want.channels = (Uint8)channels;
	want.samples = 512;
	want.callback = NULL; // usamos SDL_QueueAudio
	st->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if(!st->audio_dev)
	{
		CHIAKI_LOGE(st->log, "No se pudo abrir el audio: %s", SDL_GetError());
		return;
	}
	st->audio_rate = rate;
	st->audio_channels = channels;
	SDL_PauseAudioDevice(st->audio_dev, 0);
}

static void audio_frame_cb(int16_t *buf, size_t samples_count, void *user)
{
	RdStream *st = user;
	if(!st->audio_dev)
		return;
	Uint32 bytes_per_ms = st->audio_rate * st->audio_channels * sizeof(int16_t) / 1000;
	if(SDL_GetQueuedAudioSize(st->audio_dev) > bytes_per_ms * RD_AUDIO_MAX_LATENCY_MS)
		SDL_ClearQueuedAudio(st->audio_dev); // mejor un corte que acumular retardo
	SDL_QueueAudio(st->audio_dev, buf, (Uint32)(samples_count * st->audio_channels * sizeof(int16_t)));
}

static void event_cb(ChiakiEvent *event, void *user)
{
	RdStream *st = user;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			push_event(st, RD_EV_CONNECTED, 0, NULL);
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			if(st->settings->login_pin && !event->login_pin_request.pin_incorrect)
			{
				const char *pin = st->settings->login_pin;
				chiaki_session_set_login_pin(&st->session, (const uint8_t *)pin, strlen(pin));
			}
			else
			{
				CHIAKI_LOGE(st->log, event->login_pin_request.pin_incorrect
						? "Código de inicio de sesión incorrecto"
						: "La consola pide código de inicio de sesión: usa --login-pin");
				chiaki_session_stop(&st->session);
			}
			break;
		case CHIAKI_EVENT_RUMBLE:
			push_event(st, RD_EV_RUMBLE, (event->rumble.left << 8) | event->rumble.right, NULL);
			break;
		case CHIAKI_EVENT_QUIT:
			st->quit_reason = event->quit.reason;
			snprintf(st->quit_reason_str, sizeof(st->quit_reason_str), "%s",
					event->quit.reason_str ? event->quit.reason_str : "");
			push_event(st, RD_EV_QUIT, 0, NULL);
			break;
		default:
			break;
	}
}

/* ---------- Render (hilo principal) ---------- */

static bool ensure_texture(RdStream *st, Uint32 fmt, int w, int h)
{
	if(st->texture && st->tex_fmt == fmt && st->tex_w == w && st->tex_h == h)
		return true;
	if(st->texture)
		SDL_DestroyTexture(st->texture);
	st->texture = SDL_CreateTexture(st->renderer, fmt, SDL_TEXTUREACCESS_STREAMING, w, h);
	if(!st->texture)
	{
		CHIAKI_LOGE(st->log, "SDL_CreateTexture: %s", SDL_GetError());
		return false;
	}
	st->tex_fmt = fmt;
	st->tex_w = w;
	st->tex_h = h;
	CHIAKI_LOGI(st->log, "Vídeo %dx%d", w, h);
	return true;
}

static void present(RdStream *st)
{
	SDL_SetRenderDrawColor(st->renderer, 0, 0, 0, 255);
	SDL_RenderClear(st->renderer);
	if(st->texture)
	{
		// Mantener relación de aspecto (16:9 en una pantalla 16:10 => bandas negras)
		int out_w, out_h;
		SDL_GetRendererOutputSize(st->renderer, &out_w, &out_h);
		float scale = SDL_min((float)out_w / st->tex_w, (float)out_h / st->tex_h);
		SDL_Rect dst;
		dst.w = (int)(st->tex_w * scale);
		dst.h = (int)(st->tex_h * scale);
		dst.x = (out_w - dst.w) / 2;
		dst.y = (out_h - dst.h) / 2;
		SDL_RenderCopy(st->renderer, st->texture, NULL, &dst);
	}
	SDL_RenderPresent(st->renderer);
}

static void render_frame(RdStream *st)
{
	SDL_AtomicSet(&st->frame_pending, 0);
	int32_t frames_lost = 0;
	ChiakiFfmpegFrame frame = chiaki_ffmpeg_decoder_pull_frame(&st->decoder, &frames_lost);
	if(!frame.frame)
		return;

	AVFrame *f = frame.frame;
	AVFrame *sw = NULL;
	if(f->hw_frames_ctx)
	{
		// Frame en GPU (VA-API): lo copiamos a memoria (NV12) para subirlo a una textura SDL.
		sw = av_frame_alloc();
		if(!sw || av_hwframe_transfer_data(sw, f, 0) < 0)
		{
			CHIAKI_LOGE(st->log, "No se pudo transferir el frame de la GPU");
			goto out;
		}
		f = sw;
	}

	switch(f->format)
	{
		case AV_PIX_FMT_NV12:
			if(ensure_texture(st, SDL_PIXELFORMAT_NV12, f->width, f->height))
				SDL_UpdateNVTexture(st->texture, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1]);
			break;
		case AV_PIX_FMT_YUV420P:
		case AV_PIX_FMT_YUVJ420P:
			if(ensure_texture(st, SDL_PIXELFORMAT_IYUV, f->width, f->height))
				SDL_UpdateYUVTexture(st->texture, NULL, f->data[0], f->linesize[0],
						f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
			break;
		default:
			if(!st->warned_pix_fmt)
			{
				CHIAKI_LOGE(st->log, "Formato de píxel no soportado: %d (prueba --hw none)", f->format);
				st->warned_pix_fmt = true;
			}
			goto out;
	}
	present(st);

out:
	if(sw)
		av_frame_free(&sw);
	av_frame_free(&frame.frame);
}

/* ---------- Sesión ---------- */

static bool init_decoder(RdStream *st, ChiakiCodec codec)
{
	const char *hw = st->settings->hw_decoder;
#ifdef __linux__
	if(!hw)
		hw = "vaapi"; // GPU AMD del Steam Deck
#endif
	if(hw && strcmp(hw, "none") == 0)
		hw = NULL;

	if(chiaki_ffmpeg_decoder_init(&st->decoder, st->log, codec, st->settings->fps,
			hw, NULL, frame_available_cb, st) == CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGI(st->log, "Decodificador: %s (%s)", chiaki_codec_name(codec), hw ? hw : "software");
		return true;
	}
	if(!hw)
		return false;
	CHIAKI_LOGW(st->log, "Decodificación %s no disponible, usando software", hw);
	return chiaki_ffmpeg_decoder_init(&st->decoder, st->log, codec, st->settings->fps,
			NULL, NULL, frame_available_cb, st) == CHIAKI_ERR_SUCCESS;
}

static bool init_window(RdStream *st, const RdHost *host)
{
	char title[128];
	snprintf(title, sizeof(title), "RemoteDeck - %s", host->nickname);
	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if(st->settings->fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	st->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
	if(!st->window)
		return false;
	SDL_ShowCursor(st->settings->fullscreen ? SDL_DISABLE : SDL_ENABLE);
	// Sin VSync: preferimos mínima latencia a evitar tearing.
	st->renderer = SDL_CreateRenderer(st->window, -1, SDL_RENDERER_ACCELERATED);
	if(!st->renderer)
		st->renderer = SDL_CreateRenderer(st->window, -1, 0);
	if(!st->renderer)
		return false;
	present(st);
	return true;
}

int rd_stream_run(ChiakiLog *log, const RdHost *host, const RdStreamSettings *settings)
{
	static RdStream st; // grande (ChiakiSession): fuera de la pila
	memset(&st, 0, sizeof(st));
	st.log = log;
	st.settings = settings;
	st.quit_reason = CHIAKI_QUIT_REASON_NONE;
	int ret = 1;

	bool ps5 = chiaki_target_is_ps5(host->target);
	ChiakiCodec codec = (ps5 && !settings->h264) ? CHIAKI_CODEC_H265 : CHIAKI_CODEC_H264;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
	{
		CHIAKI_LOGE(log, "SDL_Init: %s", SDL_GetError());
		return 1;
	}
	st.ev_base = SDL_RegisterEvents(RD_EV_COUNT);
	if(st.ev_base == (Uint32)-1 || !init_window(&st, host))
	{
		CHIAKI_LOGE(log, "No se pudo crear la ventana: %s", SDL_GetError());
		goto out_sdl;
	}

	if(!init_decoder(&st, codec))
	{
		CHIAKI_LOGE(log, "No se pudo iniciar el decodificador de vídeo");
		goto out_sdl;
	}
	chiaki_opus_decoder_init(&st.opus, log);
	chiaki_opus_decoder_set_cb(&st.opus, audio_settings_cb, audio_frame_cb, &st);

	ChiakiConnectInfo ci;
	memset(&ci, 0, sizeof(ci));
	ci.ps5 = ps5;
	ci.host = host->host;
	memcpy(ci.regist_key, host->regist_key, sizeof(ci.regist_key));
	memcpy(ci.morning, host->morning, sizeof(ci.morning));
	chiaki_connect_video_profile_preset(&ci.video_profile, settings->resolution, settings->fps);
	if(settings->bitrate)
		ci.video_profile.bitrate = settings->bitrate;
	ci.video_profile.codec = codec;
	ci.video_profile_auto_downgrade = true;
	ci.enable_keyboard = false;
	ci.enable_dualsense = ps5;
	ci.auto_regist = false;
	ci.holepunch_session = NULL;
	ci.rudp_sock = NULL;
	ci.packet_loss_max = 0.05;
	ci.enable_idr_on_fec_failure = true;

	if(chiaki_session_init(&st.session, &ci, log) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "No se pudo iniciar la sesión");
		goto out_decoders;
	}

	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_get_sink(&st.opus, &audio_sink);
	chiaki_session_set_audio_sink(&st.session, &audio_sink);
	chiaki_session_set_video_sample_cb(&st.session, chiaki_ffmpeg_decoder_video_sample_cb, &st.decoder);
	chiaki_session_set_event_cb(&st.session, event_cb, &st);

	rd_input_init(&st.input, ps5);

	CHIAKI_LOGI(log, "Conectando a %s (%s) %u kbps...", host->nickname, host->host, ci.video_profile.bitrate);
	if(chiaki_session_start(&st.session) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "No se pudo arrancar la sesión");
		goto out_session;
	}

	ChiakiControllerState last_sent;
	chiaki_controller_state_set_idle(&last_sent);
	bool running = true;
	while(running)
	{
		SDL_Event ev;
		if(!SDL_WaitEventTimeout(&ev, 100))
			continue;
		do
		{
			if(ev.type == SDL_QUIT)
				running = false;
			else if(ev.type == st.ev_base + RD_EV_FRAME)
				render_frame(&st);
			else if(ev.type == st.ev_base + RD_EV_QUIT)
				running = false;
			else if(ev.type == st.ev_base + RD_EV_CONNECTED)
				CHIAKI_LOGI(log, "Conectado. Para salir: L1+R1+Start+View (o Shift+Esc)");
			else if(ev.type == st.ev_base + RD_EV_RUMBLE)
				rd_input_rumble(&st.input, (uint8_t)(ev.user.code >> 8), (uint8_t)(ev.user.code & 0xff));
			else if(ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				present(&st);
			else if(ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11)
			{
				bool fs = SDL_GetWindowFlags(st.window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
				SDL_SetWindowFullscreen(st.window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
			}
			else if(rd_input_handle_event(&st.input, &ev))
			{
				ChiakiControllerState state;
				rd_input_get_state(&st.input, &state);
				if(!chiaki_controller_state_equals(&state, &last_sent))
				{
					chiaki_session_set_controller_state(&st.session, &state);
					last_sent = state;
				}
			}
			if(st.input.quit_requested)
				running = false;
		} while(running && SDL_PollEvent(&ev));
	}

	chiaki_session_stop(&st.session);
	chiaki_session_join(&st.session);

	if(st.quit_reason == CHIAKI_QUIT_REASON_NONE || !chiaki_quit_reason_is_error(st.quit_reason))
		ret = 0;
	else
		CHIAKI_LOGE(log, "Sesión terminada: %s %s", chiaki_quit_reason_string(st.quit_reason), st.quit_reason_str);

out_session:
	rd_input_fini(&st.input);
	chiaki_session_fini(&st.session);
out_decoders:
	chiaki_opus_decoder_fini(&st.opus);
	chiaki_ffmpeg_decoder_fini(&st.decoder);
out_sdl:
	if(st.audio_dev)
		SDL_CloseAudioDevice(st.audio_dev);
	if(st.texture)
		SDL_DestroyTexture(st.texture);
	if(st.renderer)
		SDL_DestroyRenderer(st.renderer);
	if(st.window)
		SDL_DestroyWindow(st.window);
	SDL_Quit();
	return ret;
}
