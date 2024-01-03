#include <stdio.h>

#include "libretro.h"
#include "osdepend.h"
#include "input.h"

#define controls 32
#define joycodes 4 * controls
#define keycodes RETROK_LAST
extern const struct OSCodeInfo retroKeys[];

int retroKeyState[keycodes+joycodes]; 
int retroJsState[joycodes];
const struct OSCodeInfo *osd_get_code_list(void)
{
    return retroKeys;
}



INT32 osd_get_code_value(os_code_t code)
{
  if (code < RETROK_LAST)  
    return retroKeyState[code];
  else
    return retroJsState[code - RETROK_LAST];
}



int osd_readkey_unicode(int flush)
{
    // TODO
    return 0;
}

/******************************************************************************

	Keymapping

******************************************************************************/

// Unassigned keycodes
//	KEYCODE_OPENBRACE, KEYCODE_CLOSEBRACE, KEYCODE_BACKSLASH2, KEYCODE_STOP, KEYCODE_LWIN, KEYCODE_RWIN, KEYCODE_DEL_PAD, KEYCODE_PAUSE,

// The format for each systems key constants is RETROK_$(TAG) and KEYCODE_$(TAG)
// EMIT1(TAG): The tag value is the same between libretro and MAME
// EMIT2(RTAG, MTAG): The tag value is different between the two
// EXITX(TAG): MAME has no equivalent key.

#define EMIT2(RETRO, KEY) {(char*)#RETRO, RETROK_##RETRO, KEYCODE_##KEY}
#define EMIT1(KEY) {(char*)#KEY, RETROK_##KEY, KEYCODE_##KEY}
#define EMITX(KEY) {(char*)#KEY, RETROK_##KEY, CODE_OTHER_DIGITAL}

#define EMIT_RETRO_PAD(INDEX) \
  {"J" #INDEX "Hat Left",   ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_LEFT   + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX "Hat Right",  ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_RIGHT  + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX "Hat Up",     ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_UP     + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX "Hat Down",   ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_DOWN   + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX "B",          ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_B      + RETROK_LAST, JOYCODE_##INDEX##_BUTTON1}, \
  {"J" #INDEX "A",          ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_A      + RETROK_LAST, JOYCODE_##INDEX##_BUTTON2}, \
  {"J" #INDEX "Y",          ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_Y      + RETROK_LAST, JOYCODE_##INDEX##_BUTTON3}, \
  {"J" #INDEX "X",          ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_X      + RETROK_LAST, JOYCODE_##INDEX##_BUTTON4}, \
  {"J" #INDEX "L",          ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_L      + RETROK_LAST, JOYCODE_##INDEX##_BUTTON5}, \
  {"J" #INDEX "R",          ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_R      + RETROK_LAST, JOYCODE_##INDEX##_BUTTON6}, \
  {"J" #INDEX "L2",         ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_L2     + RETROK_LAST, JOYCODE_##INDEX##_BUTTON7}, \
  {"J" #INDEX "R2",         ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_R2     + RETROK_LAST, JOYCODE_##INDEX##_BUTTON8}, \
  {"J" #INDEX "L3",         ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_L3     + RETROK_LAST, JOYCODE_##INDEX##_BUTTON9}, \
  {"J" #INDEX "R3",         ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_R3     + RETROK_LAST, JOYCODE_##INDEX##_BUTTON10}, \
  {"J" #INDEX " Start",     ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_START  + RETROK_LAST, JOYCODE_##INDEX##_START}, \
  {"J" #INDEX "Select",     ((INDEX - 1) * controls) + RETRO_DEVICE_ID_JOYPAD_SELECT + RETROK_LAST, JOYCODE_##INDEX##_SELECT}, \
  {"J" #INDEX " AXIS 0 X-", ((INDEX - 1) * controls) + 16                            + RETROK_LAST, JOYCODE_##INDEX##_LEFT}, \
  {"J" #INDEX " AXIS 0 X+", ((INDEX - 1) * controls) + 17                            + RETROK_LAST, JOYCODE_##INDEX##_RIGHT}, \
  {"J" #INDEX " AXIS 1 Y-", ((INDEX - 1) * controls) + 18                            + RETROK_LAST, JOYCODE_##INDEX##_UP}, \
  {"J" #INDEX " AXIS 1 Y+", ((INDEX - 1) * controls) + 19                            + RETROK_LAST, JOYCODE_##INDEX##_DOWN}, \
  {"J" #INDEX " AXIS 2 X-", ((INDEX - 1) * controls) + 20                            + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX " AXIS 2 X+", ((INDEX - 1) * controls) + 21                            + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX " AXIS 3 Y-", ((INDEX - 1) * controls) + 22                            + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX " AXIS 3 Y+", ((INDEX - 1) * controls) + 23                            + RETROK_LAST, CODE_OTHER_DIGITAL}, \
  {"J" #INDEX " X 0 AXIS" , ((INDEX - 1) * controls) + 24                            + RETROK_LAST, JOYCODE_##INDEX##_ANALOG_X}, \
  {"J" #INDEX " Y 1 AXIS" , ((INDEX - 1) * controls) + 25                            + RETROK_LAST, JOYCODE_##INDEX##_ANALOG_Y}, \
  {"J" #INDEX " X 2 AXIS" , ((INDEX - 1) * controls) + 26                            + RETROK_LAST, JOYCODE_##INDEX##_ANALOG_Z}, \
  {"J" #INDEX " Y 3 AXIS" , ((INDEX - 1) * controls) + 27                            + RETROK_LAST, CODE_OTHER_ANALOG_ABSOLUTE}, \ 
  {"J" #INDEX " L AXIS"   , ((INDEX - 1) * controls) + 28                            + RETROK_LAST, JOYCODE_##INDEX##_ANALOG_PEDAL1}, \
  {"J" #INDEX " R AXIS"   , ((INDEX - 1) * controls) + 29                            + RETROK_LAST, JOYCODE_##INDEX##_ANALOG_PEDAL2}, \ 
  {"GUN" #INDEX " X"      , ((INDEX - 1) * controls) + 30                            + RETROK_LAST, GUNCODE_##INDEX##_ANALOG_X}, \
  {"GUN" #INDEX " Y"      , ((INDEX - 1) * controls) + 31                            + RETROK_LAST, GUNCODE_##INDEX##_ANALOG_Y} 


const struct OSCodeInfo retroKeys[] =
{

    EMIT1(BACKSPACE),
    EMIT1(TAB),

    EMITX(CLEAR),
    
    EMIT1(BACKSPACE),
    EMIT1(TAB),
    EMITX(CLEAR),

    EMIT2(RETURN, ENTER),

    EMITX(PAUSE),
    EMIT2(ESCAPE, ESC),
    EMIT1(SPACE),
    EMITX(EXCLAIM),
    EMIT2(QUOTEDBL, TILDE),
    EMITX(HASH),
    EMITX(DOLLAR),
    EMITX(AMPERSAND),
    EMIT1(QUOTE),
    EMITX(LEFTPAREN),
    EMITX(RIGHTPAREN),
    EMIT1(ASTERISK),
    EMIT2(PLUS, EQUALS),
    EMIT1(COMMA),
    EMIT1(MINUS),
    EMITX(PERIOD),
    EMIT1(SLASH),
    
    EMIT1(0), EMIT1(1), EMIT1(2), EMIT1(3), EMIT1(4), EMIT1(5), EMIT1(6), EMIT1(7), EMIT1(8), EMIT1(9),
    
    EMIT1(COLON),
    EMITX(SEMICOLON),
    EMITX(LESS),
    EMITX(EQUALS),
    EMITX(GREATER),
    EMITX(QUESTION),
    EMITX(AT),
    EMITX(LEFTBRACKET),
    EMIT1(BACKSLASH),
    EMITX(RIGHTBRACKET),
    EMITX(CARET),
    EMITX(UNDERSCORE),
    EMITX(BACKQUOTE),
    
    EMIT2(a, A), EMIT2(b, B), EMIT2(c, C), EMIT2(d, D), EMIT2(e, E), EMIT2(f, F),
    EMIT2(g, G), EMIT2(h, H), EMIT2(i, I), EMIT2(j, J), EMIT2(k, K), EMIT2(l, L),
    EMIT2(m, M), EMIT2(n, N), EMIT2(o, O), EMIT2(p, P), EMIT2(q, Q), EMIT2(r, R),
    EMIT2(s, S), EMIT2(t, T), EMIT2(u, U), EMIT2(v, V), EMIT2(w, W), EMIT2(x, X),
    EMIT2(y, Y), EMIT2(z, Z),
    
    EMIT2(DELETE, DEL),

    EMIT2(KP0, 0_PAD), EMIT2(KP1, 1_PAD), EMIT2(KP2, 2_PAD), EMIT2(KP3, 3_PAD),
    EMIT2(KP4, 4_PAD), EMIT2(KP5, 5_PAD), EMIT2(KP6, 6_PAD), EMIT2(KP7, 7_PAD),
    EMIT2(KP8, 8_PAD), EMIT2(KP9, 9_PAD),
    
    EMITX(KP_PERIOD),
    EMIT2(KP_DIVIDE, SLASH_PAD),
    EMITX(KP_MULTIPLY),
    EMIT2(KP_MINUS, MINUS_PAD),
    EMIT2(KP_PLUS, PLUS_PAD),
    EMIT2(KP_ENTER, ENTER_PAD),
    EMITX(KP_EQUALS),

    EMIT1(UP), EMIT1(DOWN), EMIT1(RIGHT), EMIT1(LEFT),

    EMIT1(INSERT), EMIT1(HOME), EMIT1(END), EMIT2(PAGEUP, PGUP), EMIT2(PAGEDOWN, PGDN),

    EMIT1(F1), EMIT1(F2), EMIT1(F3), EMIT1(F4), EMIT1(F5), EMIT1(F6),
    EMIT1(F7), EMIT1(F8), EMIT1(F9), EMIT1(F10), EMIT1(F11), EMIT1(F12),
    EMITX(F13), EMITX(F14), EMITX(F15),

    EMIT1(NUMLOCK),
    EMIT1(CAPSLOCK),
    EMIT2(SCROLLOCK, SCRLOCK),
    EMIT1(RSHIFT), EMIT1(LSHIFT), EMIT2(RCTRL, RCONTROL), EMIT2(LCTRL, LCONTROL), EMIT1(RALT), EMIT1(LALT),
    EMITX(RMETA), EMITX(LMETA), EMITX(LSUPER), EMITX(RSUPER),
    
    EMITX(MODE),
    EMITX(COMPOSE),

    EMITX(HELP),
    EMIT2(PRINT, PRTSCR),
    EMITX(SYSREQ),
    EMITX(BREAK),
    EMIT1(MENU),
    EMITX(POWER),
    EMITX(EURO),
    EMITX(UNDO),

    EMIT_RETRO_PAD(1),
    EMIT_RETRO_PAD(2),
    EMIT_RETRO_PAD(3),
    EMIT_RETRO_PAD(4),
    {0, 0, 0}
};


int convert_analog_scale(int input, int trigger_deadzone)
{
  static const int TRIGGER_MAX = 0x8000;
  int neg_test=0;
  float scale;
  int ;

  trigger_deadzone = (32678 /100) * trigger_deadzone;

  if (input < 0) { input =abs(input); neg_test=1; }
  scale = ((float)TRIGGER_MAX/(float)(TRIGGER_MAX - trigger_deadzone));

  if ( input > 0 && input > trigger_deadzone )
  {
    // Re-scale analog range
    float scaled = (input - trigger_deadzone)*scale;
    input = (int)round(scaled);

    if (input > +32767) 
    {
     input = +32767;
    }
    input = input / 327.67;
  }
  else
  {
    // do not return 0 analog  
    input = 0;
  }

  if (neg_test) input =-abs(input);
  if (input == 0 && !neg_test) return 3;
  else if (input == 0 && neg_test) return -3;
  else  return (int) input * 655.35;
}

// These calibration functions should never actually be used (as long as needs_calibration returns 0 anyway).
int osd_joystick_needs_calibration(void) { return 0; }
void osd_joystick_start_calibration(void){ }
const char *osd_joystick_calibrate_next(void) { return 0; }
void osd_joystick_calibrate(void) { }
void osd_joystick_end_calibration(void) { }

void osd_customize_inputport_list(struct InputPortDefinition *defaults){ }
