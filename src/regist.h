// Registro (vinculación) del dispositivo con la consola mediante PIN.
#ifndef RD_REGIST_H
#define RD_REGIST_H

#include "config.h"

#include <chiaki/log.h>

/**
 * pin: código de 8 dígitos de Ajustes > Sistema > Uso a distancia > Vincular dispositivo.
 * psn_account_id_b64: ID de cuenta PSN en base64 (8 bytes).
 * Rellena out si tiene éxito.
 */
bool rd_register(ChiakiLog *log, const char *host, bool ps5, const char *psn_account_id_b64, uint32_t pin, RdHost *out);

bool rd_decode_account_id(const char *b64, uint8_t out[CHIAKI_PSN_ACCOUNT_ID_SIZE]);

#endif
