#include "regist.h"

#include <chiaki/base64.h>

#include <SDL.h>

#include <stdio.h>
#include <string.h>

typedef struct regist_ctx_t
{
	SDL_sem *done;
	bool success;
	ChiakiRegisteredHost host;
} RegistCtx;

static void regist_cb(ChiakiRegistEvent *event, void *user)
{
	RegistCtx *ctx = user;
	ctx->success = event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && event->registered_host;
	if(ctx->success)
		ctx->host = *event->registered_host;
	SDL_SemPost(ctx->done);
}

bool rd_decode_account_id(const char *b64, uint8_t out[CHIAKI_PSN_ACCOUNT_ID_SIZE])
{
	uint8_t buf[32];
	size_t size = sizeof(buf);
	if(!b64 || chiaki_base64_decode(b64, strlen(b64), buf, &size) != CHIAKI_ERR_SUCCESS)
		return false;
	if(size != CHIAKI_PSN_ACCOUNT_ID_SIZE)
		return false;
	memcpy(out, buf, CHIAKI_PSN_ACCOUNT_ID_SIZE);
	return true;
}

bool rd_register(ChiakiLog *log, const char *host, bool ps5, const char *psn_account_id_b64, uint32_t pin, RdHost *out)
{
	ChiakiRegistInfo info;
	memset(&info, 0, sizeof(info));
	info.target = ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
	info.host = host;
	info.broadcast = false;
	info.psn_online_id = NULL;
	if(!rd_decode_account_id(psn_account_id_b64, info.psn_account_id))
	{
		CHIAKI_LOGE(log, "El PSN Account ID debe ser base64 de exactamente 8 bytes");
		return false;
	}
	info.pin = pin;
	info.holepunch_info = NULL;
	info.rudp = NULL;

	RegistCtx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.done = SDL_CreateSemaphore(0);

	ChiakiRegist regist;
	if(chiaki_regist_start(&regist, log, &info, regist_cb, &ctx) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "No se pudo iniciar el registro");
		SDL_DestroySemaphore(ctx.done);
		return false;
	}
	SDL_SemWait(ctx.done);
	chiaki_regist_fini(&regist);
	SDL_DestroySemaphore(ctx.done);

	if(!ctx.success)
	{
		CHIAKI_LOGE(log, "Registro fallido. Revisa el PIN (caduca en ~5 min) y el Account ID");
		return false;
	}

	memset(out, 0, sizeof(*out));
	snprintf(out->nickname, sizeof(out->nickname), "%.*s",
			(int)sizeof(ctx.host.server_nickname), ctx.host.server_nickname);
	snprintf(out->host, sizeof(out->host), "%s", host);
	rd_hex_encode(ctx.host.server_mac, sizeof(ctx.host.server_mac), out->server_mac);
	out->target = ctx.host.target;
	memcpy(out->regist_key, ctx.host.rp_regist_key, sizeof(out->regist_key));
	memcpy(out->morning, ctx.host.rp_key, sizeof(out->morning));
	return true;
}
