// RemoteDeck - cliente de Remote Play para PS5/PS4 pensado para Steam Deck.
// Usa libchiaki (chiaki-ng, AGPL-3.0) como implementación del protocolo.

#include "config.h"
#include "discover.h"
#include "regist.h"
#include "stream.h"

#include <chiaki/common.h>
#include <chiaki/log.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RD_VERSION
#define RD_VERSION "dev"
#endif

static void usage(void)
{
	printf(
		"RemoteDeck " RD_VERSION " - Remote Play para PS5/PS4\n\n"
		"Uso:\n"
		"  remotedeck discover [--host IP] [--timeout SEG]\n"
		"      Busca consolas en la red local.\n"
		"  remotedeck register --host IP --pin PIN [--account-id ID] [--ps4]\n"
		"      Vincula este dispositivo. PIN: Ajustes > Sistema > Uso a distancia > Vincular dispositivo.\n"
		"      ID: base64, hex (0x...) o decimal. Se guarda para la próxima vez.\n"
		"  remotedeck list\n"
		"      Muestra las consolas registradas.\n"
		"  remotedeck wake [CONSOLA]\n"
		"      Enciende la consola desde reposo.\n"
		"  remotedeck play [CONSOLA] [opciones]\n"
		"      --res 360|540|720|1080   (por defecto 720)\n"
		"      --fps 30|60              (por defecto 60)\n"
		"      --bitrate KBPS\n"
		"      --h264                   forzar H.264 en lugar de H.265\n"
		"      --hw vaapi|none|...      decodificador por hardware (por defecto vaapi en Linux)\n"
		"      --windowed               ventana en lugar de pantalla completa\n"
		"      --login-pin CODIGO       si la consola tiene código de inicio de sesión\n"
		"      --no-wake                no intentar despertar la consola\n"
		"  remotedeck remove CONSOLA\n\n"
		"CONSOLA = nombre, IP o MAC. Si se omite, se usa la primera registrada.\n"
		"Opción global: -v / --verbose\n");
}

static const char *state_str(ChiakiDiscoveryHostState s)
{
	switch(s)
	{
		case CHIAKI_DISCOVERY_HOST_STATE_READY: return "encendida";
		case CHIAKI_DISCOVERY_HOST_STATE_STANDBY: return "en reposo";
		default: return "desconocido";
	}
}

/** Busca "--name valor" en argv. */
static const char *opt_value(int argc, char **argv, const char *name)
{
	for(int i = 0; i < argc - 1; i++)
		if(strcmp(argv[i], name) == 0)
			return argv[i + 1];
	return NULL;
}

static bool opt_flag(int argc, char **argv, const char *name)
{
	for(int i = 0; i < argc; i++)
		if(strcmp(argv[i], name) == 0)
			return true;
	return false;
}

/** Primer argumento posicional que no sea opción ni valor de opción. */
static const char *positional(int argc, char **argv)
{
	static const char *with_value[] = { "--res", "--fps", "--bitrate", "--hw", "--login-pin",
		"--host", "--pin", "--account-id", "--timeout", NULL };
	for(int i = 0; i < argc; i++)
	{
		if(argv[i][0] != '-')
			return argv[i];
		for(const char **o = with_value; *o; o++)
			if(strcmp(argv[i], *o) == 0)
			{
				i++;
				break;
			}
	}
	return NULL;
}

static int cmd_discover(ChiakiLog *log, int argc, char **argv)
{
	const char *host = opt_value(argc, argv, "--host");
	const char *timeout = opt_value(argc, argv, "--timeout");
	int timeout_ms = timeout ? (int)(atof(timeout) * 1000) : 3000;

	RdFoundHost found[16];
	printf("Buscando consolas%s%s...\n", host ? " en " : " en la red", host ? host : "");
	int n = rd_discover(log, host, timeout_ms, found, 16);
	if(n == 0)
	{
		printf("No se encontró ninguna. Comprueba que la consola y este equipo están en la misma red\n"
				"y que el Uso a distancia está activado en la consola.\n");
		return 1;
	}
	for(int i = 0; i < n; i++)
		printf("  %-16s %-20s %s  %-12s %s\n", found[i].host_addr, found[i].host_name,
				found[i].ps5 ? "PS5" : "PS4", state_str(found[i].state), found[i].running_app);
	return 0;
}

static int cmd_register(ChiakiLog *log, RdConfig *cfg, int argc, char **argv)
{
	const char *host = opt_value(argc, argv, "--host");
	const char *pin_str = opt_value(argc, argv, "--pin");
	const char *account = opt_value(argc, argv, "--account-id");
	bool ps5 = !opt_flag(argc, argv, "--ps4");
	if(!account && cfg->psn_account_id[0])
		account = cfg->psn_account_id;

	if(!host || !pin_str)
	{
		fprintf(stderr, "Faltan --host y/o --pin\n");
		return 1;
	}
	if(!account)
	{
		fprintf(stderr, "Falta --account-id (tu PSN Account ID en base64). Ver README.\n");
		return 1;
	}
	if(strlen(pin_str) != 8 || strspn(pin_str, "0123456789") != 8)
	{
		fprintf(stderr, "El PIN debe tener 8 dígitos\n");
		return 1;
	}

	RdHost reg;
	printf("Registrando en %s...\n", host);
	if(!rd_register(log, host, ps5, account, (uint32_t)strtoul(pin_str, NULL, 10), &reg))
		return 1;

	snprintf(cfg->psn_account_id, sizeof(cfg->psn_account_id), "%s", account);
	if(!rd_config_upsert_host(cfg, &reg) || !rd_config_save(cfg))
	{
		fprintf(stderr, "No se pudo guardar la configuración\n");
		return 1;
	}
	char path[1024];
	rd_config_path(path, sizeof(path));
	printf("Registrada \"%s\" (%s). Guardado en %s\n", reg.nickname, reg.host, path);
	return 0;
}

static int cmd_list(RdConfig *cfg)
{
	if(cfg->hosts_count == 0)
	{
		printf("No hay consolas registradas. Usa: remotedeck register --host IP --pin PIN --account-id ID\n");
		return 0;
	}
	for(int i = 0; i < cfg->hosts_count; i++)
	{
		RdHost *h = &cfg->hosts[i];
		printf("  %-20s %-16s %s  mac=%s\n", h->nickname, h->host,
				chiaki_target_is_ps5(h->target) ? "PS5" : "PS4", h->server_mac);
	}
	return 0;
}

static RdHost *require_host(RdConfig *cfg, const char *name)
{
	RdHost *h = rd_config_find_host(cfg, name);
	if(!h && name)
		fprintf(stderr, "No hay ninguna consola registrada como \"%s\"\n", name);
	else if(!h)
		fprintf(stderr, "No hay consolas registradas. Usa 'remotedeck register' primero.\n");
	return h;
}

static int cmd_wake(ChiakiLog *log, RdConfig *cfg, int argc, char **argv)
{
	RdHost *h = require_host(cfg, positional(argc, argv));
	if(!h)
		return 1;
	ChiakiErrorCode err = rd_wakeup(log, h);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "Wakeup fallido: %s\n", chiaki_error_string(err));
		return 1;
	}
	printf("Paquete de encendido enviado a %s\n", h->nickname);
	return 0;
}

static bool parse_res(const char *s, ChiakiVideoResolutionPreset *out)
{
	if(!strcmp(s, "360")) *out = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
	else if(!strcmp(s, "540")) *out = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
	else if(!strcmp(s, "720")) *out = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	else if(!strcmp(s, "1080")) *out = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
	else return false;
	return true;
}

static int cmd_play(ChiakiLog *log, RdConfig *cfg, int argc, char **argv)
{
	RdHost *h = require_host(cfg, positional(argc, argv));
	if(!h)
		return 1;

	RdStreamSettings s;
	rd_stream_settings_default(&s);
	const char *v;
	if((v = opt_value(argc, argv, "--res")) && !parse_res(v, &s.resolution))
	{
		fprintf(stderr, "Resolución no válida: %s\n", v);
		return 1;
	}
	if((v = opt_value(argc, argv, "--fps")))
		s.fps = atoi(v) == 30 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60;
	if((v = opt_value(argc, argv, "--bitrate")))
		s.bitrate = (unsigned int)atoi(v);
	s.h264 = opt_flag(argc, argv, "--h264");
	s.fullscreen = !opt_flag(argc, argv, "--windowed");
	s.hw_decoder = opt_value(argc, argv, "--hw");
	s.login_pin = opt_value(argc, argv, "--login-pin");

	if(!opt_flag(argc, argv, "--no-wake") && !rd_ensure_awake(log, h, 30))
		CHIAKI_LOGW(log, "Intentando conectar de todos modos...");

	return rd_stream_run(log, h, &s);
}

static int cmd_remove(RdConfig *cfg, int argc, char **argv)
{
	const char *name = positional(argc, argv);
	if(!name || !rd_config_remove_host(cfg, name))
	{
		fprintf(stderr, "Consola no encontrada\n");
		return 1;
	}
	if(!rd_config_save(cfg))
		return 1;
	printf("Eliminada %s\n", name);
	return 0;
}

int main(int argc, char **argv)
{
	SDL_SetMainReady();
	if(argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
	{
		usage();
		return argc < 2 ? 1 : 0;
	}

	const char *cmd = argv[1];
	int sub_argc = argc - 2;
	char **sub_argv = argv + 2;

	bool verbose = opt_flag(sub_argc, sub_argv, "-v") || opt_flag(sub_argc, sub_argv, "--verbose");
	ChiakiLog log;
	chiaki_log_init(&log, verbose ? CHIAKI_LOG_ALL : (CHIAKI_LOG_ALL & ~(CHIAKI_LOG_VERBOSE | CHIAKI_LOG_DEBUG)),
			chiaki_log_cb_print, NULL);

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "chiaki_lib_init: %s\n", chiaki_error_string(err));
		return 1;
	}

	RdConfig *cfg = calloc(1, sizeof(RdConfig));
	if(!cfg || !rd_config_load(cfg))
	{
		fprintf(stderr, "No se pudo leer la configuración\n");
		return 1;
	}

	int ret;
	if(!strcmp(cmd, "discover"))
		ret = cmd_discover(&log, sub_argc, sub_argv);
	else if(!strcmp(cmd, "register"))
		ret = cmd_register(&log, cfg, sub_argc, sub_argv);
	else if(!strcmp(cmd, "list"))
		ret = cmd_list(cfg);
	else if(!strcmp(cmd, "wake"))
		ret = cmd_wake(&log, cfg, sub_argc, sub_argv);
	else if(!strcmp(cmd, "play"))
		ret = cmd_play(&log, cfg, sub_argc, sub_argv);
	else if(!strcmp(cmd, "remove"))
		ret = cmd_remove(cfg, sub_argc, sub_argv);
	else
	{
		usage();
		ret = 1;
	}
	free(cfg);
	return ret;
}
