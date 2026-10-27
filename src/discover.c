#include "discover.h"

#include <SDL.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct discover_ctx_t
{
	SDL_mutex *mutex;
	RdFoundHost *out;
	int max;
	int count;
	const char *wanted_addr;
	bool wanted_found;
} DiscoverCtx;

static void copy_str(char *dst, size_t dst_size, const char *src)
{
	snprintf(dst, dst_size, "%s", src ? src : "");
}

static void discovery_cb(ChiakiDiscoveryHost *host, void *user)
{
	DiscoverCtx *ctx = user;
	SDL_LockMutex(ctx->mutex);
	RdFoundHost *slot = NULL;
	for(int i = 0; i < ctx->count; i++)
	{
		if(host->host_id && strcmp(ctx->out[i].host_id, host->host_id) == 0)
		{
			slot = &ctx->out[i];
			break;
		}
	}
	if(!slot && ctx->count < ctx->max)
		slot = &ctx->out[ctx->count++];
	if(slot)
	{
		copy_str(slot->host_addr, sizeof(slot->host_addr), host->host_addr);
		copy_str(slot->host_name, sizeof(slot->host_name), host->host_name);
		copy_str(slot->host_id, sizeof(slot->host_id), host->host_id);
		copy_str(slot->system_version, sizeof(slot->system_version), host->system_version);
		copy_str(slot->running_app, sizeof(slot->running_app), host->running_app_name);
		slot->ps5 = chiaki_discovery_host_is_ps5(host);
		slot->state = host->state;
		if(ctx->wanted_addr && host->host_addr && strcmp(ctx->wanted_addr, host->host_addr) == 0)
			ctx->wanted_found = true;
	}
	SDL_UnlockMutex(ctx->mutex);
}

static void send_search(ChiakiDiscovery *discovery, struct sockaddr_in *addr)
{
	ChiakiDiscoveryPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;

	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
	addr->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS4);
	chiaki_discovery_send(discovery, &packet, (struct sockaddr *)addr, sizeof(*addr));

	packet.protocol_version = CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5;
	addr->sin_port = htons(CHIAKI_DISCOVERY_PORT_PS5);
	chiaki_discovery_send(discovery, &packet, (struct sockaddr *)addr, sizeof(*addr));
}

int rd_discover(ChiakiLog *log, const char *addr, int timeout_ms, RdFoundHost *out, int max)
{
	struct sockaddr_in send_addr;
	memset(&send_addr, 0, sizeof(send_addr));
	send_addr.sin_family = AF_INET;
	if(addr)
	{
		if(inet_pton(AF_INET, addr, &send_addr.sin_addr) != 1)
		{
			CHIAKI_LOGE(log, "Dirección IPv4 no válida: %s", addr);
			return 0;
		}
	}
	else
		send_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	DiscoverCtx ctx = { 0 };
	ctx.mutex = SDL_CreateMutex();
	ctx.out = out;
	ctx.max = max;
	ctx.wanted_addr = addr;

	ChiakiDiscovery discovery;
	if(chiaki_discovery_init(&discovery, log, AF_INET) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "No se pudo iniciar el descubrimiento");
		SDL_DestroyMutex(ctx.mutex);
		return 0;
	}

	ChiakiDiscoveryThread thread;
	if(chiaki_discovery_thread_start(&thread, &discovery, discovery_cb, &ctx) != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "No se pudo iniciar el hilo de descubrimiento");
		chiaki_discovery_fini(&discovery);
		SDL_DestroyMutex(ctx.mutex);
		return 0;
	}

	// Reenviamos la búsqueda cada 500 ms: UDP puede perder paquetes.
	for(int elapsed = 0; elapsed < timeout_ms; elapsed += 100)
	{
		if(elapsed % 500 == 0)
			send_search(&discovery, &send_addr);
		SDL_Delay(100);
		SDL_LockMutex(ctx.mutex);
		bool done = ctx.wanted_found;
		SDL_UnlockMutex(ctx.mutex);
		if(done)
			break;
	}

	chiaki_discovery_thread_stop(&thread);
	chiaki_discovery_fini(&discovery);
	SDL_DestroyMutex(ctx.mutex);
	return ctx.count;
}

ChiakiErrorCode rd_wakeup(ChiakiLog *log, const RdHost *host)
{
	// La credencial de wakeup es la regist_key interpretada como hexadecimal.
	char key[sizeof(host->regist_key) + 1];
	memcpy(key, host->regist_key, sizeof(host->regist_key));
	key[sizeof(host->regist_key)] = '\0';
	if(strlen(key) > 8)
	{
		CHIAKI_LOGE(log, "regist_key no válida para wakeup");
		return CHIAKI_ERR_INVALID_DATA;
	}
	uint64_t credential = (uint64_t)strtoull(key, NULL, 16);
	return chiaki_discovery_wakeup(log, NULL, host->host, credential, chiaki_target_is_ps5(host->target));
}

bool rd_ensure_awake(ChiakiLog *log, const RdHost *host, int timeout_sec)
{
	RdFoundHost found;
	if(rd_discover(log, host->host, 2000, &found, 1) == 0)
	{
		CHIAKI_LOGW(log, "%s no responde. ¿Está encendida y en la misma red?", host->host);
		return false;
	}
	if(found.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
		return true;

	CHIAKI_LOGI(log, "%s está en reposo, despertando...", host->nickname);
	if(rd_wakeup(log, host) != CHIAKI_ERR_SUCCESS)
		return false;

	for(int i = 0; i < timeout_sec; i++)
	{
		SDL_Delay(1000);
		if(rd_discover(log, host->host, 1000, &found, 1) > 0 && found.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
		{
			CHIAKI_LOGI(log, "Consola lista");
			return true;
		}
	}
	CHIAKI_LOGE(log, "La consola no despertó a tiempo");
	return false;
}
