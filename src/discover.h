// Descubrimiento de consolas en la LAN y encendido remoto (wakeup).
#ifndef RD_DISCOVER_H
#define RD_DISCOVER_H

#include "config.h"

#include <chiaki/discovery.h>
#include <chiaki/log.h>

typedef struct rd_found_host_t
{
	char host_addr[64];
	char host_name[64];
	char host_id[32];
	char system_version[32];
	char running_app[128];
	bool ps5;
	ChiakiDiscoveryHostState state;
} RdFoundHost;

/**
 * Busca consolas. addr == NULL hace broadcast a toda la red local.
 * Si addr apunta a una IP concreta, vuelve en cuanto esa consola responde.
 * Devuelve el número de consolas encontradas (como mucho max).
 */
int rd_discover(ChiakiLog *log, const char *addr, int timeout_ms, RdFoundHost *out, int max);

/** Envía el paquete de encendido (la consola debe estar en modo reposo). */
ChiakiErrorCode rd_wakeup(ChiakiLog *log, const RdHost *host);

/** Despierta la consola si está en reposo y espera a que esté lista. */
bool rd_ensure_awake(ChiakiLog *log, const RdHost *host, int timeout_sec);

#endif
