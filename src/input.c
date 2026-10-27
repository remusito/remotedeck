#include "input.h"

#include <string.h>

// Combinación para salir del streaming: L1 + R1 + Options + Share (Start + View en el Deck).
#define RD_QUIT_COMBO (CHIAKI_CONTROLLER_BUTTON_L1 | CHIAKI_CONTROLLER_BUTTON_R1 \
		| CHIAKI_CONTROLLER_BUTTON_OPTIONS | CHIAKI_CONTROLLER_BUTTON_SHARE)

static void open_pad(RdInput *in, int device_index)
{
	if(in->pad || !SDL_IsGameController(device_index))
		return;
	in->pad = SDL_GameControllerOpen(device_index);
	if(!in->pad)
		return;
	in->pad_instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in->pad));
	chiaki_controller_state_set_idle(&in->pad_state);
#if SDL_VERSION_ATLEAST(2, 0, 14)
	if(SDL_GameControllerHasSensor(in->pad, SDL_SENSOR_GYRO))
		SDL_GameControllerSetSensorEnabled(in->pad, SDL_SENSOR_GYRO, SDL_TRUE);
	if(SDL_GameControllerHasSensor(in->pad, SDL_SENSOR_ACCEL))
		SDL_GameControllerSetSensorEnabled(in->pad, SDL_SENSOR_ACCEL, SDL_TRUE);
#endif
	SDL_Log("Mando conectado: %s", SDL_GameControllerName(in->pad));
}

static void close_pad(RdInput *in)
{
	if(!in->pad)
		return;
	SDL_GameControllerClose(in->pad);
	in->pad = NULL;
	in->pad_instance = -1;
	chiaki_controller_state_set_idle(&in->pad_state);
}

void rd_input_init(RdInput *in, bool ps5)
{
	memset(in, 0, sizeof(*in));
	in->pad_instance = -1;
	chiaki_controller_state_set_idle(&in->pad_state);
	chiaki_controller_state_set_idle(&in->key_state);
	chiaki_orientation_tracker_init(&in->orient);
	chiaki_accel_new_zero_set_inactive(&in->accel_zero, false);
	chiaki_accel_new_zero_set_inactive(&in->real_accel, true);
	// Resolución del touchpad del DualShock 4 / DualSense
	in->touch_max_x = 1919.0f;
	in->touch_max_y = ps5 ? 1079.0f : 942.0f;

	for(int i = 0; i < SDL_NumJoysticks(); i++)
		open_pad(in, i);
}

void rd_input_fini(RdInput *in)
{
	close_pad(in);
}

static uint32_t map_button(SDL_GameControllerButton b)
{
	// SDL usa nombres posicionales: A = botón inferior, B = derecho, etc.
	switch(b)
	{
		case SDL_CONTROLLER_BUTTON_A: return CHIAKI_CONTROLLER_BUTTON_CROSS;
		case SDL_CONTROLLER_BUTTON_B: return CHIAKI_CONTROLLER_BUTTON_MOON;
		case SDL_CONTROLLER_BUTTON_X: return CHIAKI_CONTROLLER_BUTTON_BOX;
		case SDL_CONTROLLER_BUTTON_Y: return CHIAKI_CONTROLLER_BUTTON_PYRAMID;
		case SDL_CONTROLLER_BUTTON_DPAD_UP: return CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return CHIAKI_CONTROLLER_BUTTON_L1;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return CHIAKI_CONTROLLER_BUTTON_R1;
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: return CHIAKI_CONTROLLER_BUTTON_L3;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return CHIAKI_CONTROLLER_BUTTON_R3;
		case SDL_CONTROLLER_BUTTON_START: return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
		case SDL_CONTROLLER_BUTTON_BACK: return CHIAKI_CONTROLLER_BUTTON_SHARE;
		case SDL_CONTROLLER_BUTTON_GUIDE: return CHIAKI_CONTROLLER_BUTTON_PS;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLER_BUTTON_TOUCHPAD: return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
		// Botones traseros del Steam Deck: L4 = PS, R4 = clic de touchpad.
		// (El botón Steam lo captura Steam, así que el PS necesita otro sitio.)
		case SDL_CONTROLLER_BUTTON_PADDLE1: return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
		case SDL_CONTROLLER_BUTTON_PADDLE2: return CHIAKI_CONTROLLER_BUTTON_PS;
#endif
		default: return 0;
	}
}

static uint32_t map_key(SDL_Keycode k)
{
	switch(k)
	{
		case SDLK_RETURN: return CHIAKI_CONTROLLER_BUTTON_CROSS;
		case SDLK_BACKSPACE: return CHIAKI_CONTROLLER_BUTTON_MOON;
		case SDLK_LEFTBRACKET: return CHIAKI_CONTROLLER_BUTTON_BOX;
		case SDLK_RIGHTBRACKET: return CHIAKI_CONTROLLER_BUTTON_PYRAMID;
		case SDLK_UP: return CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
		case SDLK_DOWN: return CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
		case SDLK_LEFT: return CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
		case SDLK_RIGHT: return CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
		case SDLK_q: return CHIAKI_CONTROLLER_BUTTON_L1;
		case SDLK_e: return CHIAKI_CONTROLLER_BUTTON_R1;
		case SDLK_o: return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
		case SDLK_F1: return CHIAKI_CONTROLLER_BUTTON_PS;
		case SDLK_F2: return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
		case SDLK_F3: return CHIAKI_CONTROLLER_BUTTON_SHARE;
		default: return 0;
	}
}

static int finger_slot(RdInput *in, SDL_TouchID touch, SDL_FingerID finger, bool create)
{
	for(int i = 0; i < RD_MAX_FINGERS; i++)
		if(in->fingers[i].active && in->fingers[i].touch_id == touch && in->fingers[i].finger_id == finger)
			return i;
	if(!create)
		return -1;
	for(int i = 0; i < RD_MAX_FINGERS; i++)
		if(!in->fingers[i].active)
			return i;
	return -1;
}

static float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Coordenadas normalizadas (0..1) -> coordenadas de touchpad PS.
static bool handle_touch(RdInput *in, Uint32 type, SDL_TouchID touch, SDL_FingerID finger, float nx, float ny)
{
	uint16_t x = (uint16_t)(clamp01(nx) * in->touch_max_x);
	uint16_t y = (uint16_t)(clamp01(ny) * in->touch_max_y);
	ChiakiControllerState *s = &in->pad_state;

	if(type == SDL_FINGERDOWN)
	{
		int slot = finger_slot(in, touch, finger, true);
		if(slot < 0)
			return false;
		int8_t id = chiaki_controller_state_start_touch(s, x, y);
		if(id < 0)
			return false;
		in->fingers[slot] = (RdFinger){ true, touch, finger, id };
		return true;
	}
	int slot = finger_slot(in, touch, finger, false);
	if(slot < 0)
		return false;
	if(type == SDL_FINGERMOTION)
		chiaki_controller_state_set_touch_pos(s, (uint8_t)in->fingers[slot].chiaki_id, x, y);
	else
	{
		chiaki_controller_state_stop_touch(s, (uint8_t)in->fingers[slot].chiaki_id);
		in->fingers[slot].active = false;
	}
	return true;
}

#if SDL_VERSION_ATLEAST(2, 0, 14)
static bool handle_sensor(RdInput *in, const SDL_ControllerSensorEvent *ev)
{
	ChiakiControllerState *s = &in->pad_state;
	uint32_t ts_us = (uint32_t)(ev->timestamp * 1000);
	switch(ev->sensor)
	{
		case SDL_SENSOR_ACCEL:
		{
			float ax = ev->data[0] / SDL_STANDARD_GRAVITY;
			float ay = ev->data[1] / SDL_STANDARD_GRAVITY;
			float az = ev->data[2] / SDL_STANDARD_GRAVITY;
			chiaki_accel_new_zero_set_active(&in->real_accel, ax, ay, az, true);
			chiaki_orientation_tracker_update(&in->orient, s->gyro_x, s->gyro_y, s->gyro_z,
					ax, ay, az, &in->accel_zero, false, ts_us);
			break;
		}
		case SDL_SENSOR_GYRO:
			chiaki_orientation_tracker_update(&in->orient, ev->data[0], ev->data[1], ev->data[2],
					s->accel_x, s->accel_y, s->accel_z, &in->accel_zero, true, ts_us);
			break;
		default:
			return false;
	}
	chiaki_orientation_tracker_apply_to_controller_state(&in->orient, s);
	return true;
}
#endif

bool rd_input_handle_event(RdInput *in, const SDL_Event *ev)
{
	ChiakiControllerState *s = &in->pad_state;
	switch(ev->type)
	{
		case SDL_CONTROLLERDEVICEADDED:
			open_pad(in, ev->cdevice.which);
			return false;
		case SDL_CONTROLLERDEVICEREMOVED:
			if(in->pad && ev->cdevice.which == in->pad_instance)
			{
				close_pad(in);
				for(int i = 0; i < SDL_NumJoysticks(); i++)
					open_pad(in, i);
				return true;
			}
			return false;
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
		{
			if(ev->cbutton.which != in->pad_instance)
				return false;
			uint32_t b = map_button((SDL_GameControllerButton)ev->cbutton.button);
			if(!b)
				return false;
			if(ev->type == SDL_CONTROLLERBUTTONDOWN)
				s->buttons |= b;
			else
				s->buttons &= ~b;
			if((s->buttons & RD_QUIT_COMBO) == RD_QUIT_COMBO)
				in->quit_requested = true;
			return true;
		}
		case SDL_CONTROLLERAXISMOTION:
			if(ev->caxis.which != in->pad_instance)
				return false;
			switch(ev->caxis.axis)
			{
				case SDL_CONTROLLER_AXIS_LEFTX: s->left_x = ev->caxis.value; break;
				case SDL_CONTROLLER_AXIS_LEFTY: s->left_y = ev->caxis.value; break;
				case SDL_CONTROLLER_AXIS_RIGHTX: s->right_x = ev->caxis.value; break;
				case SDL_CONTROLLER_AXIS_RIGHTY: s->right_y = ev->caxis.value; break;
				case SDL_CONTROLLER_AXIS_TRIGGERLEFT: s->l2_state = (uint8_t)(ev->caxis.value >> 7); break;
				case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: s->r2_state = (uint8_t)(ev->caxis.value >> 7); break;
				default: return false;
			}
			return true;
#if SDL_VERSION_ATLEAST(2, 0, 14)
		case SDL_CONTROLLERSENSORUPDATE:
			if(ev->csensor.which != in->pad_instance)
				return false;
			return handle_sensor(in, &ev->csensor);
		case SDL_CONTROLLERTOUCHPADDOWN:
		case SDL_CONTROLLERTOUCHPADMOTION:
		case SDL_CONTROLLERTOUCHPADUP:
		{
			Uint32 type = ev->type == SDL_CONTROLLERTOUCHPADDOWN ? SDL_FINGERDOWN
				: ev->type == SDL_CONTROLLERTOUCHPADMOTION ? SDL_FINGERMOTION : SDL_FINGERUP;
			// touch_id negativo para no chocar con los de la pantalla táctil
			return handle_touch(in, type, -1 - ev->ctouchpad.touchpad, ev->ctouchpad.finger,
					ev->ctouchpad.x, ev->ctouchpad.y);
		}
#endif
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		case SDL_FINGERUP:
			// La pantalla táctil del Deck hace de touchpad del DualSense.
			return handle_touch(in, ev->type, ev->tfinger.touchId, ev->tfinger.fingerId,
					ev->tfinger.x, ev->tfinger.y);
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		{
			if(ev->key.repeat)
				return false;
			bool down = ev->type == SDL_KEYDOWN;
			SDL_Keycode k = ev->key.keysym.sym;
			if(down && k == SDLK_ESCAPE && (ev->key.keysym.mod & KMOD_SHIFT))
			{
				in->quit_requested = true;
				return false;
			}
			if(k == SDLK_z || k == SDLK_c)
			{
				uint8_t *t = k == SDLK_z ? &in->key_state.l2_state : &in->key_state.r2_state;
				*t = down ? 0xff : 0;
				return true;
			}
			uint32_t b = map_key(k);
			if(!b)
				return false;
			if(down)
				in->key_state.buttons |= b;
			else
				in->key_state.buttons &= ~b;
			return true;
		}
		default:
			return false;
	}
}

void rd_input_get_state(RdInput *in, ChiakiControllerState *out)
{
	chiaki_controller_state_or(out, &in->pad_state, &in->key_state);
}

void rd_input_rumble(RdInput *in, uint8_t left, uint8_t right)
{
	if(!in->pad)
		return;
	// La consola manda actualizaciones continuas; un valor 0 detiene la vibración.
	SDL_GameControllerRumble(in->pad, (Uint16)(left * 257), (Uint16)(right * 257), 5000);
}
