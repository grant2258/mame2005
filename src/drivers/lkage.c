/***************************************************************************

Legend of Kage
(C)1985 Taito
CPU: Z80 (x2), MC68705
Sound: YM2203 (x2)

Phil Stroffolino
pjstroff@hotmail.com

TODO:
- Note that all the bootlegs are derived from a different version of the
  original which hasn't been found yet.
- SOUND: lots of unknown writes to the YM2203 I/O ports
- lkage is verfied to be an original set, but it seems to work regardless of what
  the mcu does. Moreover, the mcu returns a checksum which is different from the
  one I think the game expects (89, while the game seems to expect 5d). But the
  game works anyway, it never gives the usual Taito "BAD HW" message.
- sprite and tilemap placement is most certainly wrong

Take the following observations with a grain of salt (might not be true):
- attract mode is bogus (observe the behavior of the player)
- the second stage isn't supposed to have (red) Samurai, only Ninja.
- The final stage is almost impossible in MAME! On the arcade, I could make my
  way to the top fairly easily, but in MAME I have to use invulnerability.

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "cpu/z80/z80.h"
#include "cpu/m6805/m6805.h"
#include "sound/2203intf.h"


extern unsigned char *lkage_scroll, *lkage_vreg;
WRITE8_HANDLER( lkage_videoram_w );
VIDEO_START( lkage );
VIDEO_UPDATE( lkage );

READ8_HANDLER( lkage_68705_portA_r );
WRITE8_HANDLER( lkage_68705_portA_w );
READ8_HANDLER( lkage_68705_portB_r );
WRITE8_HANDLER( lkage_68705_portB_w );
READ8_HANDLER( lkage_68705_portC_r );
WRITE8_HANDLER( lkage_68705_portC_w );
WRITE8_HANDLER( lkage_68705_ddrA_w );
WRITE8_HANDLER( lkage_68705_ddrB_w );
WRITE8_HANDLER( lkage_68705_ddrC_w );
WRITE8_HANDLER( lkage_mcu_w );
READ8_HANDLER( lkage_mcu_r );
READ8_HANDLER( lkage_mcu_status_r );


static int sound_nmi_enable,pending_nmi;

static void nmi_callback(int param)
{
	if (sound_nmi_enable) cpunum_set_input_line(1,INPUT_LINE_NMI,PULSE_LINE);
	else pending_nmi = 1;
}

static WRITE8_HANDLER( lkage_sound_command_w )
{
	soundlatch_w(offset,data);
	timer_set(TIME_NOW,data,nmi_callback);
}

static WRITE8_HANDLER( lkage_sh_nmi_disable_w )
{
	sound_nmi_enable = 0;
}

static WRITE8_HANDLER( lkage_sh_nmi_enable_w )
{
	sound_nmi_enable = 1;
	if (pending_nmi)
	{ /* probably wrong but commands may go lost otherwise */
		cpunum_set_input_line(1,INPUT_LINE_NMI,PULSE_LINE);
		pending_nmi = 0;
	}
}



static ADDRESS_MAP_START( readmem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0xdfff) AM_READ(MRA8_ROM)
	AM_RANGE(0xe000, 0xe7ff) AM_READ(MRA8_RAM)
	AM_RANGE(0xe800, 0xefff) AM_READ(paletteram_r)
	AM_RANGE(0xf000, 0xf003) AM_READ(MRA8_RAM)
	AM_RANGE(0xf062, 0xf062) AM_READ(lkage_mcu_r)
	AM_RANGE(0xf080, 0xf080) AM_READ(input_port_0_r) /* DSW1 */
	AM_RANGE(0xf081, 0xf081) AM_READ(input_port_1_r) /* DSW2 (coinage) */
	AM_RANGE(0xf082, 0xf082) AM_READ(input_port_2_r) /* DSW3 */
	AM_RANGE(0xf083, 0xf083) AM_READ(input_port_3_r)	/* start buttons, insert coin, tilt */
	AM_RANGE(0xf084, 0xf084) AM_READ(input_port_4_r)	/* P1 controls */
	AM_RANGE(0xf086, 0xf086) AM_READ(input_port_5_r)	/* P2 controls */
	AM_RANGE(0xf087, 0xf087) AM_READ(lkage_mcu_status_r)
//	AM_RANGE(0xf0a3, 0xf0a3) AM_READ(MRA8_NOP) /* unknown */
	AM_RANGE(0xf0c0, 0xf0c5) AM_READ(MRA8_RAM)
	AM_RANGE(0xf100, 0xf15f) AM_READ(MRA8_RAM)
	AM_RANGE(0xf400, 0xffff) AM_READ(MRA8_RAM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( writemem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0xdfff) AM_WRITE(MWA8_ROM)
	AM_RANGE(0xe000, 0xe7ff) AM_WRITE(MWA8_RAM)
	AM_RANGE(0xe800, 0xefff) AM_WRITE(MWA8_RAM) AM_BASE(&paletteram)
//	paletteram_xxxxRRRRGGGGBBBB_w, &paletteram },
	AM_RANGE(0xf000, 0xf003) AM_WRITE(MWA8_RAM) AM_BASE(&lkage_vreg) /* video registers */
	AM_RANGE(0xf060, 0xf060) AM_WRITE(lkage_sound_command_w)
	AM_RANGE(0xf061, 0xf061) AM_WRITE(MWA8_NOP) /* unknown */
	AM_RANGE(0xf062, 0xf062) AM_WRITE(lkage_mcu_w)
//	AM_RANGE(0xf063, 0xf063) AM_WRITE(MWA8_NOP) /* unknown */
//	AM_RANGE(0xf0a2, 0xf0a2) AM_WRITE(MWA8_NOP) /* unknown */
//	AM_RANGE(0xf0a3, 0xf0a3) AM_WRITE(MWA8_NOP) /* unknown */
	AM_RANGE(0xf0c0, 0xf0c5) AM_WRITE(MWA8_RAM) AM_BASE(&lkage_scroll) /* scrolling */
//	AM_RANGE(0xf0e1, 0xf0e1) AM_WRITE(MWA8_NOP) /* unknown */
	AM_RANGE(0xf100, 0xf15f) AM_WRITE(MWA8_RAM) AM_BASE(&spriteram) /* spriteram */
	AM_RANGE(0xf400, 0xffff) AM_WRITE(lkage_videoram_w) AM_BASE(&videoram) /* videoram */
ADDRESS_MAP_END

static READ8_HANDLER( port_fetch_r )
{
	return memory_region(REGION_USER1)[offset];
}

static ADDRESS_MAP_START( readport, ADDRESS_SPACE_IO, 8 )
	AM_RANGE(0x4000, 0x7fff) AM_READ(port_fetch_r)
ADDRESS_MAP_END


static ADDRESS_MAP_START( m68705_readmem, ADDRESS_SPACE_PROGRAM, 8 )
	ADDRESS_MAP_FLAGS( AMEF_ABITS(11) )
	AM_RANGE(0x0000, 0x0000) AM_READ(lkage_68705_portA_r)
	AM_RANGE(0x0001, 0x0001) AM_READ(lkage_68705_portB_r)
	AM_RANGE(0x0002, 0x0002) AM_READ(lkage_68705_portC_r)
	AM_RANGE(0x0010, 0x007f) AM_READ(MRA8_RAM)
	AM_RANGE(0x0080, 0x07ff) AM_READ(MRA8_ROM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( m68705_writemem, ADDRESS_SPACE_PROGRAM, 8 )
	ADDRESS_MAP_FLAGS( AMEF_ABITS(11) )
	AM_RANGE(0x0000, 0x0000) AM_WRITE(lkage_68705_portA_w)
	AM_RANGE(0x0001, 0x0001) AM_WRITE(lkage_68705_portB_w)
	AM_RANGE(0x0002, 0x0002) AM_WRITE(lkage_68705_portC_w)
	AM_RANGE(0x0004, 0x0004) AM_WRITE(lkage_68705_ddrA_w)
	AM_RANGE(0x0005, 0x0005) AM_WRITE(lkage_68705_ddrB_w)
	AM_RANGE(0x0006, 0x0006) AM_WRITE(lkage_68705_ddrC_w)
	AM_RANGE(0x0010, 0x007f) AM_WRITE(MWA8_RAM)
	AM_RANGE(0x0080, 0x07ff) AM_WRITE(MWA8_ROM)
ADDRESS_MAP_END



/***************************************************************************/

/* sound section is almost identical to Bubble Bobble, YM2203 instead of YM3526 */

static ADDRESS_MAP_START( readmem_sound, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x7fff) AM_READ(MRA8_ROM)
	AM_RANGE(0x8000, 0x87ff) AM_READ(MRA8_RAM)
	AM_RANGE(0x9000, 0x9000) AM_READ(YM2203_status_port_0_r)
	AM_RANGE(0xa000, 0xa000) AM_READ(YM2203_status_port_1_r)
	AM_RANGE(0xb000, 0xb000) AM_READ(soundlatch_r)
	AM_RANGE(0xb001, 0xb001) AM_READ(MRA8_NOP)	/* ??? */
	AM_RANGE(0xe000, 0xefff) AM_READ(MRA8_ROM)	/* space for diagnostic ROM? */
ADDRESS_MAP_END

static ADDRESS_MAP_START( writemem_sound, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x7fff) AM_WRITE(MWA8_ROM)
	AM_RANGE(0x8000, 0x87ff) AM_WRITE(MWA8_RAM)
	AM_RANGE(0x9000, 0x9000) AM_WRITE(YM2203_control_port_0_w)
	AM_RANGE(0x9001, 0x9001) AM_WRITE(YM2203_write_port_0_w)
	AM_RANGE(0xa000, 0xa000) AM_WRITE(YM2203_control_port_1_w)
	AM_RANGE(0xa001, 0xa001) AM_WRITE(YM2203_write_port_1_w)
	AM_RANGE(0xb000, 0xb000) AM_WRITE(MWA8_NOP)	/* ??? */
	AM_RANGE(0xb001, 0xb001) AM_WRITE(lkage_sh_nmi_enable_w)
	AM_RANGE(0xb002, 0xb002) AM_WRITE(lkage_sh_nmi_disable_w)
	AM_RANGE(0xe000, 0xefff) AM_WRITE(MWA8_ROM)	/* space for diagnostic ROM? */
ADDRESS_MAP_END

/***************************************************************************/

INPUT_PORTS_START( lkage )
	PORT_START_TAG("DSW1")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x03, "10000" ) /* unconfirmed */
	PORT_DIPSETTING(    0x02, "15000" ) /* unconfirmed */
	PORT_DIPSETTING(    0x01, "20000" ) /* unconfirmed */
	PORT_DIPSETTING(    0x00, "24000" ) /* unconfirmed */
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Free_Play ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x18, 0x18, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x18, "3" )
	PORT_DIPSETTING(    0x10, "4" )
	PORT_DIPSETTING(    0x08, "5" )
	PORT_DIPSETTING(	0x00, "255 (Cheat)")
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x00, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Cocktail ) )

	PORT_START_TAG("DSW2")
	PORT_DIPNAME( 0x0f, 0x00, DEF_STR( Coin_A ) )
	PORT_DIPSETTING(    0x0f, DEF_STR( 9C_1C ) )
	PORT_DIPSETTING(    0x0e, DEF_STR( 8C_1C ) )
	PORT_DIPSETTING(    0x0d, DEF_STR( 7C_1C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 6C_1C ) )
	PORT_DIPSETTING(    0x0b, DEF_STR( 5C_1C ) )
	PORT_DIPSETTING(    0x0a, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x09, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x05, DEF_STR( 1C_6C ) )
	PORT_DIPSETTING(    0x06, DEF_STR( 1C_7C ) )
	PORT_DIPSETTING(    0x07, DEF_STR( 1C_8C ) )
	PORT_DIPNAME( 0xf0, 0x00, DEF_STR( Coin_B ) )
	PORT_DIPSETTING(    0xf0, DEF_STR( 9C_1C ) )
	PORT_DIPSETTING(    0xe0, DEF_STR( 8C_1C ) )
	PORT_DIPSETTING(    0xd0, DEF_STR( 7C_1C ) )
	PORT_DIPSETTING(    0xc0, DEF_STR( 6C_1C ) )
	PORT_DIPSETTING(    0xb0, DEF_STR( 5C_1C ) )
	PORT_DIPSETTING(    0xa0, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x90, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x80, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x10, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x20, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x30, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x40, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x50, DEF_STR( 1C_6C ) )
	PORT_DIPSETTING(    0x60, DEF_STR( 1C_7C ) )
	PORT_DIPSETTING(    0x70, DEF_STR( 1C_8C ) )

	PORT_START_TAG("DSW3")
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x03, "Easiest" ) /* unconfirmed */
	PORT_DIPSETTING(    0x02, DEF_STR( Easy ) )    /* unconfirmed */
	PORT_DIPSETTING(    0x01, DEF_STR( Normal ) )  /* unconfirmed */
	PORT_DIPSETTING(    0x00, DEF_STR( Hard ) )    /* unconfirmed */
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, "Coinage Display" )
	PORT_DIPSETTING(    0x10, "Coins/Credits" )
	PORT_DIPSETTING(    0x00, "Insert Coin" )
	PORT_DIPNAME( 0x20, 0x20, "Year Display" )
	PORT_DIPSETTING(    0x00, "1985" )
	PORT_DIPSETTING(    0x20, "MCMLXXXIV" )
	PORT_DIPNAME( 0x40, 0x40, "Invulnerability (Cheat)")
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, "Coin Slots" )
	PORT_DIPSETTING(    0x80, "A and B" )
	PORT_DIPSETTING(    0x00, "A only" )

	PORT_START_TAG("SERVICE")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_COIN3 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_TILT )
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_COIN1 )
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_COIN2 )
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN )

	PORT_START_TAG("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("IN2")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_COCKTAIL
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END



static struct GfxLayout tile_layout =
{
	8,8,
	RGN_FRAC(1,4),
	4,
	{ RGN_FRAC(1,4),RGN_FRAC(0,4),RGN_FRAC(3,4),RGN_FRAC(2,4) },
	{ 7, 6, 5, 4, 3, 2, 1, 0 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8
};

static struct GfxLayout sprite_layout =
{
	16,16,
	RGN_FRAC(1,4),
	4,
	{ RGN_FRAC(1,4),RGN_FRAC(0,4),RGN_FRAC(3,4),RGN_FRAC(2,4) },
	{ 7, 6, 5, 4, 3, 2, 1, 0,
			64+7, 64+6, 64+5, 64+4, 64+3, 64+2, 64+1, 64+0 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8,
			128+0*8, 128+1*8, 128+2*8, 128+3*8, 128+4*8, 128+5*8, 128+6*8, 128+7*8 },
	32*8
};


static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &tile_layout,  128, 3 },
	{ REGION_GFX1, 0x0000, &sprite_layout,  0, 8 },
	{ -1 }
};



static void irqhandler(int irq)
{
	cpunum_set_input_line(1,0,irq ? ASSERT_LINE : CLEAR_LINE);
}

static struct YM2203interface ym2203_interface =
{
	0,
	0,
	0,
	0,
	irqhandler
};



static MACHINE_DRIVER_START( lkage )

	/* basic machine hardware */
	MDRV_CPU_ADD(Z80,6000000)
	MDRV_CPU_FLAGS(CPU_16BIT_PORT)	/* ??? */
	MDRV_CPU_PROGRAM_MAP(readmem,writemem)
	MDRV_CPU_IO_MAP(readport,0)
	MDRV_CPU_VBLANK_INT(irq0_line_hold,1)

	MDRV_CPU_ADD(Z80, 6000000)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)	/* ??? */
	MDRV_CPU_PROGRAM_MAP(readmem_sound,writemem_sound)
								/* IRQs are triggered by the YM2203 */
	MDRV_CPU_ADD(M68705,4000000/2)	/* ??? */
	MDRV_CPU_PROGRAM_MAP(m68705_readmem,m68705_writemem)

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_REAL_60HZ_VBLANK_DURATION)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(32*8, 32*8)
	MDRV_VISIBLE_AREA(0*8, 32*8-1, 2*8, 30*8-1)
	MDRV_GFXDECODE(gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(176)
		/*
			there are actually 1024 colors in paletteram, however, we use a 100% correct
			reduced "virtual palette" to achieve some optimizations in the video driver.
		*/

	MDRV_VIDEO_START(lkage)
	MDRV_VIDEO_UPDATE(lkage)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(YM2203, 4000000)
	MDRV_SOUND_CONFIG(ym2203_interface)
	MDRV_SOUND_ROUTE(0, "mono", 0.15)
	MDRV_SOUND_ROUTE(1, "mono", 0.15)
	MDRV_SOUND_ROUTE(2, "mono", 0.15)
	MDRV_SOUND_ROUTE(3, "mono", 0.40)

	MDRV_SOUND_ADD(YM2203, 4000000)
	MDRV_SOUND_ROUTE(0, "mono", 0.15)
	MDRV_SOUND_ROUTE(1, "mono", 0.15)
	MDRV_SOUND_ROUTE(2, "mono", 0.15)
	MDRV_SOUND_ROUTE(3, "mono", 0.40)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( lkageb )

	/* basic machine hardware */
	MDRV_CPU_ADD(Z80,6000000)
	MDRV_CPU_FLAGS(CPU_16BIT_PORT)	/* ??? */
	MDRV_CPU_PROGRAM_MAP(readmem,writemem)
	MDRV_CPU_IO_MAP(readport,0)
	MDRV_CPU_VBLANK_INT(irq0_line_hold,1)

	MDRV_CPU_ADD(Z80, 6000000)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)	/* ??? */
	MDRV_CPU_PROGRAM_MAP(readmem_sound,writemem_sound)
								/* IRQs are triggered by the YM2203 */
	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_REAL_60HZ_VBLANK_DURATION)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(32*8, 32*8)
	MDRV_VISIBLE_AREA(0*8, 32*8-1, 2*8, 30*8-1)
	MDRV_GFXDECODE(gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(176)
		/*
			there are actually 1024 colors in paletteram, however, we use a 100% correct
			reduced "virtual palette" to achieve some optimizations in the video driver.
		*/

	MDRV_VIDEO_START(lkage)
	MDRV_VIDEO_UPDATE(lkage)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(YM2203, 4000000)
	MDRV_SOUND_CONFIG(ym2203_interface)
	MDRV_SOUND_ROUTE(0, "mono", 0.15)
	MDRV_SOUND_ROUTE(1, "mono", 0.15)
	MDRV_SOUND_ROUTE(2, "mono", 0.15)
	MDRV_SOUND_ROUTE(3, "mono", 0.40)

	MDRV_SOUND_ADD(YM2203, 4000000)
	MDRV_SOUND_ROUTE(0, "mono", 0.15)
	MDRV_SOUND_ROUTE(1, "mono", 0.15)
	MDRV_SOUND_ROUTE(2, "mono", 0.15)
	MDRV_SOUND_ROUTE(3, "mono", 0.40)
MACHINE_DRIVER_END



ROM_START( lkage )
	ROM_REGION( 0x14000, REGION_CPU1, 0 ) /* Z80 code (main CPU) */
	ROM_LOAD( "a54-01-1.37", 0x0000, 0x8000, CRC(973da9c5) SHA1(ad3b5d6a329b784e47be563c6f8dc628f32ba0a5) )
	ROM_LOAD( "a54-02-1.38", 0x8000, 0x8000, CRC(27b509da) SHA1(c623950bd7dd2b5699ca948e3731455964106b89) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 ) /* Z80 code (sound CPU) */
	ROM_LOAD( "a54-04.54",   0x0000, 0x8000, CRC(541faf9a) SHA1(b142ff3bd198f700697ec06ea92db3109ab5818e) )

	ROM_REGION( 0x10000, REGION_CPU3, 0 ) /* 68705 MCU code */
	ROM_LOAD( "a54-09.53",   0x0000, 0x0800, CRC(0e8b8846) SHA1(a4a105462b0127229bb7edfadd2e581c7e40f1cc) )

	ROM_REGION( 0x4000, REGION_USER1, 0 ) /* data */
	ROM_LOAD( "a54-03.51",   0x0000, 0x4000, CRC(493e76d8) SHA1(13c6160edd94ba2801fd89bb33bcae3a1e3454ff) )

	ROM_REGION( 0x10000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "a54-05-1.84", 0x0000, 0x4000, CRC(0033c06a) SHA1(89964503fc338817c6511fd15942741996b7037a) )
	ROM_LOAD( "a54-06-1.85", 0x4000, 0x4000, CRC(9f04d9ad) SHA1(3b9a4d30348fd02e5c8ae94655548bd4a02dd65d) )
	ROM_LOAD( "a54-07-1.86", 0x8000, 0x4000, CRC(b20561a4) SHA1(0d6d83dfae79ea133e37704ca47426b4c978fb36) )
	ROM_LOAD( "a54-08-1.87", 0xc000, 0x4000, CRC(3ff3b230) SHA1(ffcd964efb0af32b5d7a70305dfda615ea95acbe) )

	ROM_REGION( 0x0200, REGION_PROMS, 0 )
	ROM_LOAD( "a54-10.2",    0x0000, 0x0200, CRC(17dfbd14) SHA1(f8f0b6dfedd4ba108dad43ccc7697ef4ab9cbf86) )	/* unknown */
ROM_END

ROM_START( lkageb )
	ROM_REGION( 0x10000, REGION_CPU1, 0 ) /* Z80 code (main CPU) */
	ROM_LOAD( "ic37_1",      0x0000, 0x8000, CRC(05694f7b) SHA1(08a3796d6cf04d64db52ed8208a51084c420e10a) )
	ROM_LOAD( "ic38_2",      0x8000, 0x8000, CRC(22efe29e) SHA1(f7a29d54081ca7509e822ad8823ec977bccc4a40) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 ) /* Z80 code (sound CPU) */
	ROM_LOAD( "a54-04.54",   0x0000, 0x8000, CRC(541faf9a) SHA1(b142ff3bd198f700697ec06ea92db3109ab5818e) )

	ROM_REGION( 0x10000, REGION_CPU3, 0 ) /* 68705 MCU code */
	ROM_LOAD( "mcu",   0x0000, 0x0800, NO_DUMP )

	ROM_REGION( 0x4000, REGION_USER1, 0 ) /* data */
	ROM_LOAD( "a54-03.51",   0x0000, 0x4000, CRC(493e76d8) SHA1(13c6160edd94ba2801fd89bb33bcae3a1e3454ff) )

	ROM_REGION( 0x10000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "ic93_5",      0x0000, 0x4000, CRC(76753e52) SHA1(13f61969d59b055a5ab40237148e091d7cabe190) )
	ROM_LOAD( "ic94_6",      0x4000, 0x4000, CRC(f33c015c) SHA1(756326daab255d3a36d97e51ee141b9f7157f12e) )
	ROM_LOAD( "ic95_7",      0x8000, 0x4000, CRC(0e02c2e8) SHA1(1d8a817ba66cf26a4fe51ae00874c0fe6e7cebe3) )
	ROM_LOAD( "ic96_8",      0xc000, 0x4000, CRC(4ef5f073) SHA1(dfd234542b28cff74692a1c381772da01e8bb4a7) )

	ROM_REGION( 0x0200, REGION_PROMS, 0 )
	ROM_LOAD( "a54-10.2",    0x0000, 0x0200, CRC(17dfbd14) SHA1(f8f0b6dfedd4ba108dad43ccc7697ef4ab9cbf86) )	/* unknown */
ROM_END

ROM_START( lkageb2 )
	ROM_REGION( 0x10000, REGION_CPU1, 0 ) /* Z80 code (main CPU) */
	ROM_LOAD( "lok.a",       0x0000, 0x8000, CRC(866df793) SHA1(44a9a773d7bbfc5f9d53f56682438ef8b23ecbd6) )
	ROM_LOAD( "lok.b",       0x8000, 0x8000, CRC(fba9400f) SHA1(fedcb9b717feaeec31afda098f0ac2744df6c7be) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 ) /* Z80 code (sound CPU) */
	ROM_LOAD( "a54-04.54",   0x0000, 0x8000, CRC(541faf9a) SHA1(b142ff3bd198f700697ec06ea92db3109ab5818e) )

	ROM_REGION( 0x4000, REGION_USER1, 0 ) /* data */
	ROM_LOAD( "a54-03.51",   0x0000, 0x4000, CRC(493e76d8) SHA1(13c6160edd94ba2801fd89bb33bcae3a1e3454ff) )

	ROM_REGION( 0x10000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "ic93_5",      0x0000, 0x4000, CRC(76753e52) SHA1(13f61969d59b055a5ab40237148e091d7cabe190) )
	ROM_LOAD( "ic94_6",      0x4000, 0x4000, CRC(f33c015c) SHA1(756326daab255d3a36d97e51ee141b9f7157f12e) )
	ROM_LOAD( "ic95_7",      0x8000, 0x4000, CRC(0e02c2e8) SHA1(1d8a817ba66cf26a4fe51ae00874c0fe6e7cebe3) )
	ROM_LOAD( "ic96_8",      0xc000, 0x4000, CRC(4ef5f073) SHA1(dfd234542b28cff74692a1c381772da01e8bb4a7) )

	ROM_REGION( 0x0200, REGION_PROMS, 0 )
	ROM_LOAD( "a54-10.2",    0x0000, 0x0200, CRC(17dfbd14) SHA1(f8f0b6dfedd4ba108dad43ccc7697ef4ab9cbf86) )	/* unknown */
ROM_END

ROM_START( lkageb3 )
	ROM_REGION( 0x10000, REGION_CPU1, 0 ) /* Z80 code (main CPU) */
	ROM_LOAD( "z1.bin",      0x0000, 0x8000, CRC(60cac488) SHA1(b61df14159f37143b1faed22d77fc7be31602022) )
	ROM_LOAD( "z2.bin",      0x8000, 0x8000, CRC(22c95f17) SHA1(8ca438d508a36918778651adf599cf45a7c4a5d7) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 ) /* Z80 code (sound CPU) */
	ROM_LOAD( "a54-04.54",   0x0000, 0x8000, CRC(541faf9a) SHA1(b142ff3bd198f700697ec06ea92db3109ab5818e) )

	ROM_REGION( 0x4000, REGION_USER1, 0 ) /* data */
	ROM_LOAD( "a54-03.51",   0x0000, 0x4000, CRC(493e76d8) SHA1(13c6160edd94ba2801fd89bb33bcae3a1e3454ff) )

	ROM_REGION( 0x10000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "ic93_5",      0x0000, 0x4000, CRC(76753e52) SHA1(13f61969d59b055a5ab40237148e091d7cabe190) )
	ROM_LOAD( "ic94_6",      0x4000, 0x4000, CRC(f33c015c) SHA1(756326daab255d3a36d97e51ee141b9f7157f12e) )
	ROM_LOAD( "ic95_7",      0x8000, 0x4000, CRC(0e02c2e8) SHA1(1d8a817ba66cf26a4fe51ae00874c0fe6e7cebe3) )
	ROM_LOAD( "ic96_8",      0xc000, 0x4000, CRC(4ef5f073) SHA1(dfd234542b28cff74692a1c381772da01e8bb4a7) )

	ROM_REGION( 0x0200, REGION_PROMS, 0 )
	ROM_LOAD( "a54-10.2",    0x0000, 0x0200, CRC(17dfbd14) SHA1(f8f0b6dfedd4ba108dad43ccc7697ef4ab9cbf86) )	/* unknown */
ROM_END

static unsigned char mcu_val;

/*Note:This probably uses another MCU dump,which is undumped.*/

static READ8_HANDLER( fake_mcu_r )
{
	switch(mcu_val)
	{
		/*These are for the attract mode*/
		case 0x01: return (mcu_val-1);
		case 0x90: return (mcu_val+0x43);
		/*Gameplay Protection,checked in this order at a start of a play*/
		case 0xa6: return (mcu_val+0x27);
		case 0x34: return (mcu_val+0x7f);
		case 0x48: return (mcu_val+0xb7);

		default:   return (mcu_val);
	}
}

static WRITE8_HANDLER( fake_mcu_w )
{
	//if(data != 1 && data != 0xa6 && data != 0x34 && data != 0x48)
	//	usrintf_showmessage("PC = %04x %02x",activecpu_get_pc(),data);

	mcu_val = data;
}

static READ8_HANDLER( fake_status_r )
{
	static int res = 3;// cpu data/mcu ready status

	return res;
}

DRIVER_INIT( lkageb )
{
	memory_install_read8_handler(0, ADDRESS_SPACE_PROGRAM, 0xf062, 0xf062, 0, 0, fake_mcu_r);
	memory_install_read8_handler(0, ADDRESS_SPACE_PROGRAM, 0xf087, 0xf087, 0, 0, fake_status_r);
	memory_install_write8_handler(0, ADDRESS_SPACE_PROGRAM, 0xf062, 0xf062, 0, 0, fake_mcu_w );
}


GAME( 1984, lkage,   0,     lkage,  lkage, 0,       ROT0, "Taito Corporation", "The Legend of Kage" )
GAME( 1984, lkageb,  lkage, lkageb, lkage, lkageb,  ROT0, "bootleg", "The Legend of Kage (bootleg set 1)" )
GAME( 1984, lkageb2, lkage, lkageb, lkage, 0,       ROT0, "bootleg", "The Legend of Kage (bootleg set 2)" )
GAME( 1984, lkageb3, lkage, lkageb, lkage, 0,       ROT0, "bootleg", "The Legend of Kage (bootleg set 3)" )
