/***************************************************************************

  Vapor Trail (World version)  (c) 1989 Data East Corporation
  Vapor Trail (USA version)    (c) 1989 Data East USA
  Kuhga (Japanese version)     (c) 1989 Data East Corporation

  Emulation by Bryan McPhail, mish@tendril.co.uk

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "cpu/h6280/h6280.h"
#include "sound/2203intf.h"
#include "sound/2151intf.h"
#include "sound/okim6295.h"

VIDEO_START( vaportra );
VIDEO_UPDATE( vaportra );

WRITE16_HANDLER( vaportra_pf1_data_w );
WRITE16_HANDLER( vaportra_pf2_data_w );
WRITE16_HANDLER( vaportra_pf3_data_w );
WRITE16_HANDLER( vaportra_pf4_data_w );

WRITE16_HANDLER( vaportra_control_0_w );
WRITE16_HANDLER( vaportra_control_1_w );
WRITE16_HANDLER( vaportra_control_2_w );
WRITE16_HANDLER( vaportra_palette_24bit_rg_w );
WRITE16_HANDLER( vaportra_palette_24bit_b_w );

extern data16_t *vaportra_pf1_data,*vaportra_pf2_data,*vaportra_pf3_data,*vaportra_pf4_data;

/******************************************************************************/

static READ16_HANDLER( vaportra_pf1_data_r ) { return vaportra_pf1_data[offset]; }
static READ16_HANDLER( vaportra_pf2_data_r ) { return vaportra_pf2_data[offset]; }
static READ16_HANDLER( vaportra_pf3_data_r ) { return vaportra_pf3_data[offset]; }
static READ16_HANDLER( vaportra_pf4_data_r ) { return vaportra_pf4_data[offset]; }

static WRITE16_HANDLER( vaportra_sound_w )
{
	soundlatch_w(0,data & 0xff);
	cpunum_set_input_line(1,0,PULSE_LINE);
}

static READ16_HANDLER( vaportra_control_r )
{
	switch (offset<<1)
	{
		case 4: /* Dip Switches */
			return (readinputport(4) + (readinputport(3) << 8));
		case 2: /* Credits */
			return readinputport(2);
		case 0: /* Player 1 & Player 2 joysticks & fire buttons */
			return (readinputport(0) + (readinputport(1) << 8));
	}

	logerror("Unknown control read at %d\n",offset);
	return ~0;
}

/******************************************************************************/

static ADDRESS_MAP_START( vaportra_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x07ffff) AM_READ(MRA16_ROM)
	AM_RANGE(0x100000, 0x10000f) AM_READ(vaportra_control_r)

	AM_RANGE(0x200000, 0x201fff) AM_READ(vaportra_pf2_data_r)
	AM_RANGE(0x202000, 0x203fff) AM_READ(vaportra_pf4_data_r)
	AM_RANGE(0x280000, 0x281fff) AM_READ(vaportra_pf1_data_r)
	AM_RANGE(0x282000, 0x283fff) AM_READ(vaportra_pf3_data_r)

	AM_RANGE(0x300000, 0x300fff) AM_READ(MRA16_RAM)
	AM_RANGE(0x304000, 0x304fff) AM_READ(MRA16_RAM)
	AM_RANGE(0x308000, 0x308001) AM_READ(MRA16_NOP)
	AM_RANGE(0xff8000, 0xff87ff) AM_READ(MRA16_RAM)
	AM_RANGE(0xffc000, 0xffffff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( vaportra_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x07ffff) AM_WRITE(MWA16_ROM)
	AM_RANGE(0x100000, 0x100003) AM_WRITE(vaportra_control_2_w)
	AM_RANGE(0x100006, 0x100007) AM_WRITE(vaportra_sound_w)

	AM_RANGE(0x200000, 0x201fff) AM_WRITE(vaportra_pf2_data_w) AM_BASE(&vaportra_pf2_data)
	AM_RANGE(0x202000, 0x203fff) AM_WRITE(vaportra_pf4_data_w) AM_BASE(&vaportra_pf4_data)
	AM_RANGE(0x240000, 0x24000f) AM_WRITE(vaportra_control_0_w)

	AM_RANGE(0x280000, 0x281fff) AM_WRITE(vaportra_pf1_data_w) AM_BASE(&vaportra_pf1_data)
	AM_RANGE(0x282000, 0x283fff) AM_WRITE(vaportra_pf3_data_w) AM_BASE(&vaportra_pf3_data)
	AM_RANGE(0x2c0000, 0x2c000f) AM_WRITE(vaportra_control_1_w)

	AM_RANGE(0x300000, 0x3009ff) AM_WRITE(vaportra_palette_24bit_rg_w) AM_BASE(&paletteram16)
	AM_RANGE(0x304000, 0x3049ff) AM_WRITE(vaportra_palette_24bit_b_w) AM_BASE(&paletteram16_2)
	AM_RANGE(0x308000, 0x308001) AM_WRITE(MWA16_NOP)
	AM_RANGE(0x30c000, 0x30c001) AM_WRITE(buffer_spriteram16_w)
	AM_RANGE(0x318000, 0x3187ff) AM_MIRROR(0xce0000) AM_WRITE(MWA16_RAM) AM_BASE(&spriteram16) AM_SIZE(&spriteram_size)
	
	AM_RANGE(0xffc000, 0xffffff) AM_WRITE(MWA16_RAM)
ADDRESS_MAP_END

/******************************************************************************/

static WRITE8_HANDLER( YM2151_w )
{
	switch (offset) {
	case 0:
		YM2151_register_port_0_w(0,data);
		break;
	case 1:
		YM2151_data_port_0_w(0,data);
		break;
	}
}

static WRITE8_HANDLER( YM2203_w )
{
	switch (offset) {
	case 0:
		YM2203_control_port_0_w(0,data);
		break;
	case 1:
		YM2203_write_port_0_w(0,data);
		break;
	}
}

static ADDRESS_MAP_START( sound_readmem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x000000, 0x00ffff) AM_READ(MRA8_ROM)
	AM_RANGE(0x100000, 0x100001) AM_READ(YM2203_status_port_0_r)
	AM_RANGE(0x110000, 0x110001) AM_READ(YM2151_status_port_0_r)
	AM_RANGE(0x120000, 0x120001) AM_READ(OKIM6295_status_0_r)
	AM_RANGE(0x130000, 0x130001) AM_READ(OKIM6295_status_1_r)
	AM_RANGE(0x140000, 0x140001) AM_READ(soundlatch_r)
	AM_RANGE(0x1f0000, 0x1f1fff) AM_READ(MRA8_BANK8)
ADDRESS_MAP_END

static ADDRESS_MAP_START( sound_writemem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x000000, 0x00ffff) AM_WRITE(MWA8_ROM)
	AM_RANGE(0x100000, 0x100001) AM_WRITE(YM2203_w)
	AM_RANGE(0x110000, 0x110001) AM_WRITE(YM2151_w)
	AM_RANGE(0x120000, 0x120001) AM_WRITE(OKIM6295_data_0_w)
	AM_RANGE(0x130000, 0x130001) AM_WRITE(OKIM6295_data_1_w)
	AM_RANGE(0x1f0000, 0x1f1fff) AM_WRITE(MWA8_BANK8)
	AM_RANGE(0x1fec00, 0x1fec01) AM_WRITE(H6280_timer_w)
	AM_RANGE(0x1ff402, 0x1ff403) AM_WRITE(H6280_irq_status_w)
ADDRESS_MAP_END

/******************************************************************************/

INPUT_PORTS_START( vaportra )
	PORT_START	/* Player 1 controls */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON2 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START1 )

	PORT_START	/* Player 2 controls */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START2 )

	PORT_START	/* Credits */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_VBLANK )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	/* Dip switch bank 1 */
	PORT_DIPNAME( 0x07, 0x07, DEF_STR( Coin_A ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x07, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x06, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x05, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_6C ) )
	PORT_DIPNAME( 0x38, 0x38, DEF_STR( Coin_B ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x38, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x30, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x28, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x20, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x18, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x10, DEF_STR( 1C_6C ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_SERVICE( 0x80, IP_ACTIVE_LOW )

	PORT_START	/* Dip switch bank 2 */
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0x00, "2" )
	PORT_DIPSETTING(    0x03, "3" )
	PORT_DIPSETTING(    0x02, "4" )
	PORT_DIPSETTING(    0x01, "5" )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x20, "150k, 300k and 600k" )
	PORT_DIPSETTING(    0x30, "200k and 600k" )
	PORT_DIPSETTING(    0x10, "300k only" )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Allow_Continue ) )
	PORT_DIPSETTING(    0x00, DEF_STR( No ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Yes ) )
  	PORT_DIPNAME( 0x80, 0x00, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END

/******************************************************************************/

static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 chars */
	4096,
	4,		/* 4 bits per pixel  */
	{ 8, 0,  0x40000*8+8, 0x40000*8 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*16, 1*16, 2*16, 3*16, 4*16, 5*16, 6*16, 7*16 },
	16*8	/* every char takes 8 consecutive bytes */
};

static struct GfxLayout seallayout =
{
	16,16,
	4096,
	4,
	{ 0x80000*8+8, 0x80000*8, 8, 0 },
	{ 32*8+0, 32*8+1, 32*8+2, 32*8+3, 32*8+4, 32*8+5, 32*8+6, 32*8+7,
		0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*16, 1*16, 2*16, 3*16, 4*16, 5*16, 6*16, 7*16,
			8*16, 9*16, 10*16, 11*16, 12*16, 13*16, 14*16, 15*16 },
	64*8
};

static struct GfxLayout seallayout3 =
{
	16,16,
	4096,
	4,
	{ 8, 0, 0x40000*8+8, 0x40000*8 },
	{ 32*8+0, 32*8+1, 32*8+2, 32*8+3, 32*8+4, 32*8+5, 32*8+6, 32*8+7,
		0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*16, 1*16, 2*16, 3*16, 4*16, 5*16, 6*16, 7*16,
			8*16, 9*16, 10*16, 11*16, 12*16, 13*16, 14*16, 15*16 },
	64*8
};

static struct GfxLayout seallayout2 =
{
	16,16,
	4096*2,
	4,
	{ 8, 0, 0x80000*8+8, 0x80000*8 },
	{ 32*8+0, 32*8+1, 32*8+2, 32*8+3, 32*8+4, 32*8+5, 32*8+6, 32*8+7,
		0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*16, 1*16, 2*16, 3*16, 4*16, 5*16, 6*16, 7*16,
			8*16, 9*16, 10*16, 11*16, 12*16, 13*16, 14*16, 15*16 },
	64*8
};

static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x000000, &charlayout,    0, 16 },	/* Characters 8x8 */
	{ REGION_GFX2, 0x000000, &seallayout,  768, 16 },	/* Tiles 16x16 */
	{ REGION_GFX1, 0x000000, &seallayout3, 512, 16 },	/* Tiles 16x16 */
	{ REGION_GFX2, 0x040000, &seallayout, 1024, 16 },	/* Tiles 16x16 */
	{ REGION_GFX3, 0x000000, &seallayout2, 256, 16 },	/* Sprites 16x16 */
	{ -1 } /* end of array */
};

/******************************************************************************/

static void sound_irq(int state)
{
	cpunum_set_input_line(1,1,state); /* IRQ 2 */
}

static struct YM2151interface ym2151_interface =
{
	sound_irq
};



static MACHINE_DRIVER_START( vaportra )

	/* basic machine hardware */
	MDRV_CPU_ADD(M68000,12000000) /* Custom chip 59 */
	MDRV_CPU_PROGRAM_MAP(vaportra_readmem,vaportra_writemem)
	MDRV_CPU_VBLANK_INT(irq6_line_hold,1)/* VBL */

	MDRV_CPU_ADD(H6280, 32220000/8) /* Custom chip 45; Audio section crystal is 32.220 MHz */
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_PROGRAM_MAP(sound_readmem,sound_writemem)

	MDRV_FRAMES_PER_SECOND(58)
	MDRV_VBLANK_DURATION(529)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_NEEDS_6BITS_PER_GUN | VIDEO_BUFFERS_SPRITERAM)
	MDRV_SCREEN_SIZE(32*8, 32*8)
	MDRV_VISIBLE_AREA(0*8, 32*8-1, 1*8, 31*8-1)
	MDRV_GFXDECODE(gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(1280)

	MDRV_VIDEO_START(vaportra)
	MDRV_VIDEO_UPDATE(vaportra)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(YM2203, 32220000/8)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.60)

	MDRV_SOUND_ADD(YM2151, 32220000/9)
	MDRV_SOUND_CONFIG(ym2151_interface)
	MDRV_SOUND_ROUTE(0, "mono", 0.60)
	MDRV_SOUND_ROUTE(1, "mono", 0.60)

	MDRV_SOUND_ADD(OKIM6295, 32220000/32)
	MDRV_SOUND_CONFIG(okim6295_interface_region_1_pin7high)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.75)

	MDRV_SOUND_ADD(OKIM6295, 32220000/16)
	MDRV_SOUND_CONFIG(okim6295_interface_region_2_pin7high)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.60)
MACHINE_DRIVER_END

/******************************************************************************/

ROM_START( vaportra )
	ROM_REGION( 0x80000, REGION_CPU1, 0 ) /* 68000 code */
  	ROM_LOAD16_BYTE( "fl_02-1.bin", 0x00000, 0x20000, CRC(9ae36095) SHA1(c8d11a6033a44277a267915b4ca471c43acd1143) )
  	ROM_LOAD16_BYTE( "fl_00-1.bin", 0x00001, 0x20000, CRC(c08cc048) SHA1(b28f95856817b8a8cb6cc588d48e95196cbf52fd) )
	ROM_LOAD16_BYTE( "fl_03.bin",   0x40000, 0x20000, CRC(80bd2844) SHA1(3fcaa409c7134388fa9458df8e8aaecc93f085e6) )
 	ROM_LOAD16_BYTE( "fl_01.bin",   0x40001, 0x20000, CRC(9474b085) SHA1(5510309ddab5fbf1dbb0a7b1e424a5dff5ec263d) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 )	/* Sound CPU */
	ROM_LOAD( "fj04",    0x00000, 0x10000, CRC(e9aedf9b) SHA1(f7bcf8f666015140aaad8ee5cf619636934b7066) )

	ROM_REGION( 0x080000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "vtmaa00.bin",   0x000000, 0x80000, CRC(0330e13b) SHA1(dce70667ea738295332556752d1305c5e941b383) ) /* chars & tiles */

	ROM_REGION( 0x100000, REGION_GFX2, ROMREGION_DISPOSE )
  	ROM_LOAD( "vtmaa01.bin",   0x000000, 0x80000, CRC(c217a31b) SHA1(e259d48190d6890781fb0338e17e14822876babb) ) /* tiles 2 */
	ROM_LOAD( "vtmaa02.bin",   0x080000, 0x80000, CRC(091ff98e) SHA1(814dc08c055bad5368955a4b1fe6a706b58adc02) ) /* tiles 3 */

	ROM_REGION( 0x100000, REGION_GFX3, ROMREGION_DISPOSE )
  	ROM_LOAD( "vtmaa03.bin",   0x000000, 0x80000, CRC(1a30bf81) SHA1(00e6c713e12133a99d64ca80638c9cbc8e26b2c8) ) /* sprites */
  	ROM_LOAD( "vtmaa04.bin",   0x080000, 0x80000, CRC(b713e9cc) SHA1(af33943d75d2ee3a7385f624537008dca9e1d5d8) )

	ROM_REGION( 0x20000, REGION_SOUND1, 0 )	/* ADPCM samples */
	ROM_LOAD( "fj06",    0x00000, 0x20000, CRC(6e98a235) SHA1(374564b4e494d03cd1330c06e321b9452c22a075) )

	ROM_REGION( 0x20000, REGION_SOUND2, 0 )	/* ADPCM samples */
	ROM_LOAD( "fj05",    0x00000, 0x20000, CRC(39cda2b5) SHA1(f5c5a305025d451ab48f84cd63e36a3bbdefda96) )
ROM_END

ROM_START( vaportru )
	ROM_REGION( 0x80000, REGION_CPU1, 0 ) /* 68000 code */
  	ROM_LOAD16_BYTE( "fj02",   0x00000, 0x20000, CRC(a2affb73) SHA1(0d49397cc9891047a0b92e92e2e3d0e7fcaf8db9) )
  	ROM_LOAD16_BYTE( "fj00",   0x00001, 0x20000, CRC(ef05e07b) SHA1(0e505709fa251e6b30f019c0c28ee9ba2b29a50a) )
	ROM_LOAD16_BYTE( "fj03",   0x40000, 0x20000, CRC(44893379) SHA1(da1340bc1821a552c317cb9a7c1ba69eb080b055) )
 	ROM_LOAD16_BYTE( "fj01",   0x40001, 0x20000, CRC(97fbc107) SHA1(b2899eb4347c0471397b83051e46c94dff3526f5) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 )	/* Sound CPU */
	ROM_LOAD( "fj04",    0x00000, 0x10000, CRC(e9aedf9b) SHA1(f7bcf8f666015140aaad8ee5cf619636934b7066) )

	ROM_REGION( 0x080000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "vtmaa00.bin",   0x000000, 0x80000, CRC(0330e13b) SHA1(dce70667ea738295332556752d1305c5e941b383) ) /* chars & tiles */

	ROM_REGION( 0x100000, REGION_GFX2, ROMREGION_DISPOSE )
  	ROM_LOAD( "vtmaa01.bin",   0x000000, 0x80000, CRC(c217a31b) SHA1(e259d48190d6890781fb0338e17e14822876babb) ) /* tiles 2 */
	ROM_LOAD( "vtmaa02.bin",   0x080000, 0x80000, CRC(091ff98e) SHA1(814dc08c055bad5368955a4b1fe6a706b58adc02) ) /* tiles 3 */

	ROM_REGION( 0x100000, REGION_GFX3, ROMREGION_DISPOSE )
  	ROM_LOAD( "vtmaa03.bin",   0x000000, 0x80000, CRC(1a30bf81) SHA1(00e6c713e12133a99d64ca80638c9cbc8e26b2c8) ) /* sprites */
  	ROM_LOAD( "vtmaa04.bin",   0x080000, 0x80000, CRC(b713e9cc) SHA1(af33943d75d2ee3a7385f624537008dca9e1d5d8) )

	ROM_REGION( 0x20000, REGION_SOUND1, 0 )	/* ADPCM samples */
	ROM_LOAD( "fj06",    0x00000, 0x20000, CRC(6e98a235) SHA1(374564b4e494d03cd1330c06e321b9452c22a075) )

	ROM_REGION( 0x20000, REGION_SOUND2, 0 )	/* ADPCM samples */
	ROM_LOAD( "fj05",    0x00000, 0x20000, CRC(39cda2b5) SHA1(f5c5a305025d451ab48f84cd63e36a3bbdefda96) )
ROM_END

ROM_START( kuhga )
	ROM_REGION( 0x80000, REGION_CPU1, 0 ) /* 68000 code */
  	ROM_LOAD16_BYTE( "fp02-3.bin", 0x00000, 0x20000, CRC(d0705ef4) SHA1(781efbf36d9dda543895e0a59cd4d72667439a93) )
  	ROM_LOAD16_BYTE( "fp00-3.bin", 0x00001, 0x20000, CRC(1da92e48) SHA1(6507bd9bbc31ee03e38b82cc135aebf090902761) )
	ROM_LOAD16_BYTE( "fp03.bin",   0x40000, 0x20000, CRC(ea0da0f1) SHA1(ca40e694cb0aa0c13672c14fd4a389bc6d26cbc6) )
 	ROM_LOAD16_BYTE( "fp01.bin",   0x40001, 0x20000, CRC(e3ecbe86) SHA1(382e959111ec37ad94da8fd6dcefe2d2aab346b6) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 )	/* Sound CPU */
	ROM_LOAD( "fj04",    0x00000, 0x10000, CRC(e9aedf9b) SHA1(f7bcf8f666015140aaad8ee5cf619636934b7066) )

	ROM_REGION( 0x080000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "vtmaa00.bin",   0x000000, 0x80000, CRC(0330e13b) SHA1(dce70667ea738295332556752d1305c5e941b383) ) /* chars & tiles */

	ROM_REGION( 0x100000, REGION_GFX2, ROMREGION_DISPOSE )
  	ROM_LOAD( "vtmaa01.bin",   0x000000, 0x80000, CRC(c217a31b) SHA1(e259d48190d6890781fb0338e17e14822876babb) ) /* tiles 2 */
	ROM_LOAD( "vtmaa02.bin",   0x080000, 0x80000, CRC(091ff98e) SHA1(814dc08c055bad5368955a4b1fe6a706b58adc02) ) /* tiles 3 */

	ROM_REGION( 0x100000, REGION_GFX3, ROMREGION_DISPOSE )
  	ROM_LOAD( "vtmaa03.bin",   0x000000, 0x80000, CRC(1a30bf81) SHA1(00e6c713e12133a99d64ca80638c9cbc8e26b2c8) ) /* sprites */
  	ROM_LOAD( "vtmaa04.bin",   0x080000, 0x80000, CRC(b713e9cc) SHA1(af33943d75d2ee3a7385f624537008dca9e1d5d8) )

	ROM_REGION( 0x20000, REGION_SOUND1, 0 )	/* ADPCM samples */
	ROM_LOAD( "fj06",    0x00000, 0x20000, CRC(6e98a235) SHA1(374564b4e494d03cd1330c06e321b9452c22a075) )

	ROM_REGION( 0x20000, REGION_SOUND2, 0 )	/* ADPCM samples */
	ROM_LOAD( "fj05",    0x00000, 0x20000, CRC(39cda2b5) SHA1(f5c5a305025d451ab48f84cd63e36a3bbdefda96) )
ROM_END

/******************************************************************************/

static DRIVER_INIT( vaportra )
{
	unsigned char *RAM = memory_region(REGION_CPU1);
	int i;

	for (i=0x00000; i<0x80000; i++)
		RAM[i]=(RAM[i] & 0x7e) | ((RAM[i] & 0x01) << 7) | ((RAM[i] & 0x80) >> 7);
}

/******************************************************************************/

GAME( 1989, vaportra, 0,        vaportra, vaportra, vaportra, ROT270, "Data East Corporation", "Vapor Trail - Hyper Offence Formation (World revision 1)" )
GAME( 1989, vaportru, vaportra, vaportra, vaportra, vaportra, ROT270, "Data East USA", "Vapor Trail - Hyper Offence Formation (US)" )
GAME( 1989, kuhga,    vaportra, vaportra, vaportra, vaportra, ROT270, "Data East Corporation", "Kuhga - Operation Code 'Vapor Trail' (Japan revision 3)" )
