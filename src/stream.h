// Sesión de streaming: vídeo + audio + mando.
#ifndef RD_STREAM_H
#define RD_STREAM_H

#include "config.h"

#include <chiaki/log.h>
#include <chiaki/session.h>

typedef struct rd_stream_settings_t
{
	ChiakiVideoResolutionPreset resolution;
	ChiakiVideoFPSPreset fps;
	unsigned int bitrate;   // kbps, 0 = valor por defecto de la resolución
	bool h264;              // forzar H.264 (por defecto H.265 en PS5)
	bool fullscreen;
	const char *hw_decoder; // "vaapi", "none"... NULL = automático
	const char *login_pin;  // código de inicio de sesión de la consola, si tiene
} RdStreamSettings;

void rd_stream_settings_default(RdStreamSettings *s);

/** Bloquea hasta que termina la sesión. Devuelve 0 si se cerró sin error. */
int rd_stream_run(ChiakiLog *log, const RdHost *host, const RdStreamSettings *settings);

#endif
