/*************************************************************************

	Atari Canyon Bomber hardware

*************************************************************************/

#include "sound/discrete.h"

/* Discrete Sound Input Nodes */
#define CANYON_MOTOR1_DATA		NODE_01
#define CANYON_MOTOR2_DATA		NODE_02
#define CANYON_EXPLODE_DATA		NODE_03
#define CANYON_WHISTLE1_EN		NODE_04
#define CANYON_WHISTLE2_EN		NODE_05
#define CANYON_ATTRACT1_EN		NODE_06
#define CANYON_ATTRACT2_EN		NODE_07


/*----------- defined in sndhrdw/canyon.c -----------*/

WRITE8_HANDLER( canyon_motor_w );
WRITE8_HANDLER( canyon_explode_w );
WRITE8_HANDLER( canyon_attract_w );
WRITE8_HANDLER( canyon_whistle_w );

extern struct discrete_sound_block canyon_discrete_interface[];


/*----------- defined in vidhrdw/canyon.c -----------*/

VIDEO_UPDATE( canyon );
