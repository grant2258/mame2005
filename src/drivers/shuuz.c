/***************************************************************************

	Atari Shuuz hardware

	driver by Aaron Giles

	Games supported:
		* Shuuz (1990) [2 sets]

	Known bugs:
		* none at this time

****************************************************************************

	Memory map (TBA)

***************************************************************************/


#include "driver.h"
#include "machine/atarigen.h"
#include "shuuz.h"
#include "sound/okim6295.h"



/*************************************
 *
 *	Interrupt handling
 *
 *************************************/

static void update_interrupts(void)
{
	int newstate = 0;

	if (atarigen_scanline_int_state)
		newstate = 4;

	if (newstate)
		cpunum_set_input_line(0, newstate, ASSERT_LINE);
	else
		cpunum_set_input_line(0, 7, CLEAR_LINE);
}



/*************************************
 *
 *	Initialization
 *
 *************************************/

static MACHINE_INIT( shuuz )
{
	atarigen_eeprom_reset();
	atarivc_reset(atarivc_eof_data, 1);
	atarigen_interrupt_reset(update_interrupts);
}


static WRITE16_HANDLER( latch_w )
{
}



/*************************************
 *
 *	LETA I/O
 *
 *************************************/

static READ16_HANDLER( leta_r )
{
	/* trackball -- rotated 45 degrees? */
	static int cur[2];
	int which = offset & 1;

	/* when reading the even ports, do a real analog port update */
	if (which == 0)
	{
		int dx = (INT8)readinputport(2);
		int dy = (INT8)readinputport(3);

		cur[0] = dx + dy;
		cur[1] = dx - dy;
	}

	/* clip the result to -0x3f to +0x3f to remove directional ambiguities */
	return cur[which];
}



/*************************************
 *
 *	MSM5295 I/O
 *
 *************************************/

static READ16_HANDLER( adpcm_r )
{
	return OKIM6295_status_0_r(offset) | 0xff00;
}


static WRITE16_HANDLER( adpcm_w )
{
	if (ACCESSING_LSB)
		OKIM6295_data_0_w(offset, data & 0xff);
}



/*************************************
 *
 *	Additional I/O
 *
 *************************************/

static READ16_HANDLER( special_port0_r )
{
	int result = readinputport(0);

	if ((result & 0x0800) && atarigen_get_hblank())
		result &= ~0x0800;

	return result;
}



/*************************************
 *
 *	Main CPU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( main_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x03ffff) AM_READ(MRA16_ROM)
	AM_RANGE(0x100000, 0x100fff) AM_READ(atarigen_eeprom_r)
	AM_RANGE(0x103000, 0x103003) AM_READ(leta_r)
	AM_RANGE(0x105000, 0x105001) AM_READ(special_port0_r)
	AM_RANGE(0x105002, 0x105003) AM_READ(input_port_1_word_r)
	AM_RANGE(0x106000, 0x106001) AM_READ(adpcm_r)
	AM_RANGE(0x107000, 0x107007) AM_READ(MRA16_NOP)
	AM_RANGE(0x3e0000, 0x3e087f) AM_READ(MRA16_RAM)
	AM_RANGE(0x3effc0, 0x3effff) AM_READ(atarivc_r)
	AM_RANGE(0x3f4000, 0x3fffff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END


static ADDRESS_MAP_START( main_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x03ffff) AM_WRITE(MWA16_ROM)
	AM_RANGE(0x100000, 0x100fff) AM_WRITE(atarigen_eeprom_w) AM_BASE(&atarigen_eeprom) AM_SIZE(&atarigen_eeprom_size)
	AM_RANGE(0x101000, 0x101fff) AM_WRITE(atarigen_eeprom_enable_w)
	AM_RANGE(0x102000, 0x102001) AM_WRITE(watchdog_reset16_w)
	AM_RANGE(0x105000, 0x105001) AM_WRITE(latch_w)
	AM_RANGE(0x106000, 0x106001) AM_WRITE(adpcm_w)
	AM_RANGE(0x107000, 0x107007) AM_WRITE(MWA16_NOP)
	AM_RANGE(0x3e0000, 0x3e087f) AM_WRITE(atarigen_666_paletteram_w) AM_BASE(&paletteram16)
	AM_RANGE(0x3effc0, 0x3effff) AM_WRITE(atarivc_w) AM_BASE(&atarivc_data)
	AM_RANGE(0x3f4000, 0x3f5eff) AM_WRITE(atarigen_playfield_latched_msb_w) AM_BASE(&atarigen_playfield)
	AM_RANGE(0x3f5f00, 0x3f5f7f) AM_WRITE(MWA16_RAM) AM_BASE(&atarivc_eof_data)
	AM_RANGE(0x3f5f80, 0x3f5fff) AM_WRITE(atarimo_0_slipram_w) AM_BASE(&atarimo_0_slipram)
	AM_RANGE(0x3f6000, 0x3f7fff) AM_WRITE(atarigen_playfield_upper_w) AM_BASE(&atarigen_playfield_upper)
	AM_RANGE(0x3f8000, 0x3fcfff) AM_WRITE(MWA16_RAM)
	AM_RANGE(0x3fd000, 0x3fd3ff) AM_WRITE(atarimo_0_spriteram_w) AM_BASE(&atarimo_0_spriteram)
	AM_RANGE(0x3fd400, 0x3fffff) AM_WRITE(MWA16_RAM)
ADDRESS_MAP_END



/*************************************
 *
 *	Port definitions
 *
 *************************************/

INPUT_PORTS_START( shuuz )
	PORT_START
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x07fc, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_VBLANK )
	PORT_BIT( 0xf000, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x07fc, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_SERVICE( 0x0800, IP_ACTIVE_LOW )
	PORT_BIT( 0xf000, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START
    PORT_BIT( 0xff, 0, IPT_TRACKBALL_X ) PORT_SENSITIVITY(50) PORT_KEYDELTA(30) PORT_PLAYER(1)
	PORT_BIT( 0xff00, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START
    PORT_BIT( 0xff, 0, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(50) PORT_KEYDELTA(30) PORT_REVERSE PORT_PLAYER(1)
	PORT_BIT( 0xff00, IP_ACTIVE_LOW, IPT_UNUSED )
INPUT_PORTS_END


INPUT_PORTS_START( shuuz2 )
	PORT_START
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x00fc, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT(  0x0100, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Step Debug SW") PORT_CODE(KEYCODE_S)
	PORT_BIT( 0x0600, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_VBLANK )
	PORT_BIT(  0x1000, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Playfield Debug SW") PORT_CODE(KEYCODE_Y)
	PORT_BIT(  0x2000, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Reset Debug SW") PORT_CODE(KEYCODE_E)
	PORT_BIT(  0x4000, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Crosshair Debug SW") PORT_CODE(KEYCODE_C)
	PORT_BIT(  0x8000, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Freeze Debug SW") PORT_CODE(KEYCODE_F)

	PORT_START
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x00fc, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT(  0x0100, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Replay Debug SW") PORT_CODE(KEYCODE_R)
	PORT_BIT( 0x0600, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_SERVICE( 0x0800, IP_ACTIVE_LOW )
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1)

	PORT_START
    PORT_BIT( 0xff, 0, IPT_TRACKBALL_X ) PORT_SENSITIVITY(50) PORT_KEYDELTA(30) PORT_PLAYER(1)
	PORT_BIT( 0xff00, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START
    PORT_BIT( 0xff, 0, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(50) PORT_KEYDELTA(30) PORT_REVERSE PORT_PLAYER(1)
	PORT_BIT( 0xff00, IP_ACTIVE_LOW, IPT_UNUSED )
INPUT_PORTS_END



/*************************************
 *
 *	Graphics definitions
 *
 *************************************/

static struct GfxLayout pfmolayout =
{
	8,8,
	RGN_FRAC(1,2),
	4,
	{ 0, 4, 0+RGN_FRAC(1,2), 4+RGN_FRAC(1,2) },
	{ 0, 1, 2, 3, 8, 9, 10, 11 },
	{ 0*8, 2*8, 4*8, 6*8, 8*8, 10*8, 12*8, 14*8 },
	16*8
};


static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &pfmolayout,  256, 16 },		/* sprites & playfield */
	{ REGION_GFX2, 0, &pfmolayout,    0, 16 },		/* sprites & playfield */
	{ -1 }
};



/*************************************
 *
 *	Machine driver
 *
 *************************************/

static MACHINE_DRIVER_START( shuuz )

	/* basic machine hardware */
	MDRV_CPU_ADD(M68000, ATARI_CLOCK_14MHz/2)
	MDRV_CPU_PROGRAM_MAP(main_readmem,main_writemem)

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_REAL_60HZ_VBLANK_DURATION)
	
	MDRV_MACHINE_INIT(shuuz)
	MDRV_NVRAM_HANDLER(atarigen)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_NEEDS_6BITS_PER_GUN | VIDEO_UPDATE_BEFORE_VBLANK)
	MDRV_SCREEN_SIZE(42*8, 30*8)
	MDRV_VISIBLE_AREA(0*8, 42*8-1, 0*8, 30*8-1)
	MDRV_GFXDECODE(gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(1024)

	MDRV_VIDEO_START(shuuz)
	MDRV_VIDEO_UPDATE(shuuz)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(OKIM6295, ATARI_CLOCK_14MHz/16)
	MDRV_SOUND_CONFIG(okim6295_interface_region_1_pin7high)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END



/*************************************
 *
 *	ROM definition(s)
 *
 *************************************/

ROM_START( shuuz )
	ROM_REGION( 0x40000, REGION_CPU1, 0 )	/* 4*64k for 68000 code */
	ROM_LOAD16_BYTE( "4010.23p",     0x00000, 0x20000, CRC(1c2459f8) SHA1(4b8daf196e3ba17cf958a3c1af4e4dacfb79b9e7) )
	ROM_LOAD16_BYTE( "4011.13p",     0x00001, 0x20000, CRC(6db53a85) SHA1(7f9b3ea78fa65221931bfdab1aa5f1913ffed753) )

	ROM_REGION( 0x080000, REGION_GFX1, ROMREGION_DISPOSE | ROMREGION_INVERT )
	ROM_LOAD( "2030.43x", 0x000000, 0x20000, CRC(8ecf1ed8) SHA1(47143f1eaf43027c5301eb6009d8a56a98328894) )
	ROM_LOAD( "2032.20x", 0x020000, 0x20000, CRC(5af184e6) SHA1(630969466c606d1f51da81911fb365a4cac4685c) )
	ROM_LOAD( "2031.87x", 0x040000, 0x20000, CRC(72e9db63) SHA1(be13830b38c2603bbd6b875abdc1675788a60b24) )
	ROM_LOAD( "2033.65x", 0x060000, 0x20000, CRC(8f552498) SHA1(7fd323f3b30747a8645d7a9676fdf8f973b6632a) )

	ROM_REGION( 0x100000, REGION_GFX2, ROMREGION_DISPOSE | ROMREGION_INVERT )
	ROM_LOAD( "1020.43u", 0x000000, 0x20000, CRC(d21ad039) SHA1(5389745eff6690c1890f98a9630869b1084fb2f3) )
	ROM_LOAD( "1022.20u", 0x020000, 0x20000, CRC(0c10bc90) SHA1(11272757ecad42a4fae49046bd1b01d5ff7f7d4f) )
	ROM_LOAD( "1024.43m", 0x040000, 0x20000, CRC(adb09347) SHA1(5294dfb3d4aa83525795ca03c2f328ab9a666baf) )
	ROM_LOAD( "1026.20m", 0x060000, 0x20000, CRC(9b20e13d) SHA1(726b6fb548c0906a5baa90b9698f99a6af9ecc36) )
	ROM_LOAD( "1021.87u", 0x080000, 0x20000, CRC(8388910c) SHA1(62c6b1885bed042ef72fb62464923a33f9b464f1) )
	ROM_LOAD( "1023.65u", 0x0a0000, 0x20000, CRC(71353112) SHA1(0aab14379e1b562b81cdd52eb209e264a12232c4) )
	ROM_LOAD( "1025.87m", 0x0c0000, 0x20000, CRC(f7b20a64) SHA1(667c539fa809d3ae4a1c127e2044dd3a4e533266) )
	ROM_LOAD( "1027.65m", 0x0e0000, 0x20000, CRC(55d54952) SHA1(73e1a388ea48bab567bde8958ee228432ebfbf67) )

	ROM_REGION( 0x40000, REGION_SOUND1, 0 )	/* ADPCM data */
	ROM_LOAD( "1040.75b", 0x00000, 0x20000, CRC(0896702b) SHA1(d826bb4812d393889584c7c656c317fd5745a05f) )
	ROM_LOAD( "1041.65b", 0x20000, 0x20000, CRC(b3b07ce9) SHA1(f1128a143b72867c16b9803b0beb0188420cbfb5) )
ROM_END


ROM_START( shuuz2 )
	ROM_REGION( 0x40000, REGION_CPU1, 0 )	/* 4*64k for 68000 code */
	ROM_LOAD16_BYTE( "23p.rom",     0x00000, 0x20000, CRC(98aec4e7) SHA1(8cbe6e7835ecf0ef74a2de723ef970a63d3bddd1) )
	ROM_LOAD16_BYTE( "13p.rom",     0x00001, 0x20000, CRC(dd9d5d5c) SHA1(0bde6be55532c232b1d27824c2ce61f33501cbb0) )

	ROM_REGION( 0x080000, REGION_GFX1, ROMREGION_DISPOSE | ROMREGION_INVERT )
	ROM_LOAD( "2030.43x", 0x000000, 0x20000, CRC(8ecf1ed8) SHA1(47143f1eaf43027c5301eb6009d8a56a98328894) )
	ROM_LOAD( "2032.20x", 0x020000, 0x20000, CRC(5af184e6) SHA1(630969466c606d1f51da81911fb365a4cac4685c) )
	ROM_LOAD( "2031.87x", 0x040000, 0x20000, CRC(72e9db63) SHA1(be13830b38c2603bbd6b875abdc1675788a60b24) )
	ROM_LOAD( "2033.65x", 0x060000, 0x20000, CRC(8f552498) SHA1(7fd323f3b30747a8645d7a9676fdf8f973b6632a) )

	ROM_REGION( 0x100000, REGION_GFX2, ROMREGION_DISPOSE | ROMREGION_INVERT )
	ROM_LOAD( "1020.43u", 0x000000, 0x20000, CRC(d21ad039) SHA1(5389745eff6690c1890f98a9630869b1084fb2f3) )
	ROM_LOAD( "1022.20u", 0x020000, 0x20000, CRC(0c10bc90) SHA1(11272757ecad42a4fae49046bd1b01d5ff7f7d4f) )
	ROM_LOAD( "1024.43m", 0x040000, 0x20000, CRC(adb09347) SHA1(5294dfb3d4aa83525795ca03c2f328ab9a666baf) )
	ROM_LOAD( "1026.20m", 0x060000, 0x20000, CRC(9b20e13d) SHA1(726b6fb548c0906a5baa90b9698f99a6af9ecc36) )
	ROM_LOAD( "1021.87u", 0x080000, 0x20000, CRC(8388910c) SHA1(62c6b1885bed042ef72fb62464923a33f9b464f1) )
	ROM_LOAD( "1023.65u", 0x0a0000, 0x20000, CRC(71353112) SHA1(0aab14379e1b562b81cdd52eb209e264a12232c4) )
	ROM_LOAD( "1025.87m", 0x0c0000, 0x20000, CRC(f7b20a64) SHA1(667c539fa809d3ae4a1c127e2044dd3a4e533266) )
	ROM_LOAD( "1027.65m", 0x0e0000, 0x20000, CRC(55d54952) SHA1(73e1a388ea48bab567bde8958ee228432ebfbf67) )

	ROM_REGION( 0x40000, REGION_SOUND1, 0 )	/* ADPCM data */
	ROM_LOAD( "1040.75b", 0x00000, 0x20000, CRC(0896702b) SHA1(d826bb4812d393889584c7c656c317fd5745a05f) )
	ROM_LOAD( "1041.65b", 0x20000, 0x20000, CRC(b3b07ce9) SHA1(f1128a143b72867c16b9803b0beb0188420cbfb5) )
ROM_END



/*************************************
 *
 *	Driver init
 *
 *************************************/

static DRIVER_INIT( shuuz )
{
	atarigen_eeprom_default = NULL;
}



/*************************************
 *
 *	Game driver(s)
 *
 *************************************/

GAME( 1990, shuuz,  0,     shuuz, shuuz,  shuuz, ROT0, "Atari Games", "Shuuz (version 8.0)" )
GAME( 1990, shuuz2, shuuz, shuuz, shuuz2, shuuz, ROT0, "Atari Games", "Shuuz (version 7.1)" )
