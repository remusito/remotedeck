// Almacenamiento de consolas registradas (formato INI sencillo).
#ifndef RD_CONFIG_H
#define RD_CONFIG_H

#include <chiaki/common.h>
#include <chiaki/regist.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RD_MAX_HOSTS 16

typedef struct rd_host_t
{
	char nickname[64];
	char host[256];
	char server_mac[13]; // 12 caracteres hex + '\0'
	ChiakiTarget target;
	char regist_key[CHIAKI_SESSION_AUTH_SIZE]; // rellenado con '\0'
	uint8_t morning[0x10];                     // rp_key devuelta en el registro
} RdHost;

typedef struct rd_config_t
{
	char psn_account_id[64]; // base64, 8 bytes decodificados
	RdHost hosts[RD_MAX_HOSTS];
	int hosts_count;
} RdConfig;

bool rd_config_path(char *buf, size_t buf_size);
bool rd_config_load(RdConfig *cfg);
bool rd_config_save(const RdConfig *cfg);

/** name_or_addr == NULL devuelve la primera consola registrada. */
RdHost *rd_config_find_host(RdConfig *cfg, const char *name_or_addr);
/** Inserta o sustituye (por MAC) una consola. */
RdHost *rd_config_upsert_host(RdConfig *cfg, const RdHost *host);
bool rd_config_remove_host(RdConfig *cfg, const char *name_or_addr);

void rd_hex_encode(const uint8_t *in, size_t in_size, char *out);
bool rd_hex_decode(const char *in, uint8_t *out, size_t out_size);

#endif
