/*************************************************************************

	Atari Basketball hardware

*************************************************************************/

#include "sound/discrete.h"

/* Discrete Sound Input Nodes */
#define BSKTBALL_NOTE_DATA		NODE_01
#define BSKTBALL_CROWD_DATA		NODE_02
#define BSKTBALL_NOISE_EN		NODE_03
#define BSKTBALL_BOUNCE_EN		NODE_04


/*----------- defined in machine/bsktball.c -----------*/

WRITE8_HANDLER( bsktball_nmion_w );
INTERRUPT_GEN( bsktball_interrupt );
WRITE8_HANDLER( bsktball_ld1_w );
WRITE8_HANDLER( bsktball_ld2_w );
READ8_HANDLER( bsktball_in0_r );
WRITE8_HANDLER( bsktball_led1_w );
WRITE8_HANDLER( bsktball_led2_w );


/*----------- defined in sndhrdw/bsktball.c -----------*/

WRITE8_HANDLER( bsktball_bounce_w );
WRITE8_HANDLER( bsktball_note_w );
WRITE8_HANDLER( bsktball_noise_reset_w );

extern struct discrete_sound_block bsktball_discrete_interface[];


/*----------- defined in vidhrdw/bsktball.c -----------*/

extern unsigned char *bsktball_motion;
VIDEO_UPDATE( bsktball );
