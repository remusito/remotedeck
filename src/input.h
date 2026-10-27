// Traducción de mando/teclado/pantalla táctil (SDL) a estado de mando DualSense.
#ifndef RD_INPUT_H
#define RD_INPUT_H

#include <chiaki/controller.h>
#include <chiaki/orientation.h>

#include <SDL.h>

#include <stdbool.h>

#define RD_MAX_FINGERS 4

typedef struct rd_finger_t
{
	bool active;
	SDL_TouchID touch_id;
	SDL_FingerID finger_id;
	int8_t chiaki_id;
} RdFinger;

typedef struct rd_input_t
{
	SDL_GameController *pad;
	SDL_JoystickID pad_instance;
	ChiakiControllerState pad_state;
	ChiakiControllerState key_state;
	ChiakiOrientationTracker orient;
	ChiakiAccelNewZero accel_zero;
	ChiakiAccelNewZero real_accel;
	RdFinger fingers[RD_MAX_FINGERS];
	float touch_max_x, touch_max_y;
	bool quit_requested;
} RdInput;

void rd_input_init(RdInput *in, bool ps5);
void rd_input_fini(RdInput *in);

/** Procesa un evento SDL. Devuelve true si el estado del mando ha cambiado. */
bool rd_input_handle_event(RdInput *in, const SDL_Event *ev);

/** Estado combinado (mando + teclado) listo para enviar a la consola. */
void rd_input_get_state(RdInput *in, ChiakiControllerState *out);

void rd_input_rumble(RdInput *in, uint8_t left, uint8_t right);

#endif
