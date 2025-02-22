/********************************************************************

 Driver for common Hyperstone based games

 by bits from Angelo Salese, David Haywood,
    Pierpaolo Prazzoli and Tomasz Slanina

 Games Supported:

	Vamp 1/2				(c) 1999 Danbi & F2 System
	Mission Craft			(c) 2000 Sun
	Coolmini				(c) 199? Semicom
	Super Lup Lup Puzzle	(c) 1999 Omega System

 Games Needed:

	Vamp 1/2 (World version)

*********************************************************************/

#include "driver.h"
#include "machine/eeprom.h"
#include "sound/2151intf.h"
#include "sound/okim6295.h"

static data16_t *tiles, *wram;
static int flip_bit, flipscreen = 0;
static int palshift;

static WRITE16_HANDLER( vamphalf_palette_w )
{
	int r,g,b;
	COMBINE_DATA(&paletteram16[offset]);

	r = ((paletteram16[offset] & 0x7c00) >> 10);
	g = ((paletteram16[offset] & 0x03e0) >> 5);
	b = ((paletteram16[offset] & 0x001f) >> 0);

	palette_set_color(offset,r<<3,g<<3,b<<3);
}

static READ16_HANDLER( oki_r )
{
	if(offset)
		return OKIM6295_status_0_r(0);
	else
		return 0;
}

static WRITE16_HANDLER( oki_w )
{
	if(offset)
		OKIM6295_data_0_w(0, data);
}

static READ16_HANDLER( ym2151_status_r )
{
	if(offset)
		return YM2151_status_port_0_r(0);
	else
		return 0;
}

static WRITE16_HANDLER( ym2151_data_w )
{
	if(offset)
		YM2151_data_port_0_w(0, data);
}

static WRITE16_HANDLER( ym2151_register_w )
{
	if(offset)
		YM2151_register_port_0_w(0,data);
}

static READ16_HANDLER( eeprom_r )
{
	if(offset)
		return EEPROM_read_bit();
	else
		return 0;
}

static WRITE16_HANDLER( eeprom_w )
{
	if(offset)
	{
		EEPROM_write_bit(data & 0x01);
		EEPROM_set_cs_line((data & 0x04) ? CLEAR_LINE : ASSERT_LINE );
		EEPROM_set_clock_line((data & 0x02) ? ASSERT_LINE : CLEAR_LINE );
	}
}

static WRITE16_HANDLER( flipscreen_w )
{
	if(offset)
	{
		if(data & flip_bit)
			flipscreen = 1;
		else
			flipscreen = 0;
	}
}

static ADDRESS_MAP_START( common_map, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x001fffff) AM_RAM AM_BASE(&wram)
	AM_RANGE(0x40000000, 0x4003ffff) AM_RAM AM_BASE(&tiles)
	AM_RANGE(0x80000000, 0x8000ffff) AM_READ(MRA16_RAM) AM_WRITE(vamphalf_palette_w) AM_BASE(&paletteram16)
	AM_RANGE(0xfff00000, 0xffffffff) AM_ROM AM_REGION(REGION_USER1,0)
ADDRESS_MAP_END

static ADDRESS_MAP_START( vamphalf_io, ADDRESS_SPACE_IO, 16 )
	AM_RANGE(0x0c0, 0x0c3) AM_READWRITE(oki_r, oki_w)
	AM_RANGE(0x140, 0x143) AM_WRITE(ym2151_register_w)
	AM_RANGE(0x144, 0x147) AM_READWRITE(ym2151_status_r, ym2151_data_w)
	AM_RANGE(0x1c0, 0x1c3) AM_READ(eeprom_r)
	AM_RANGE(0x240, 0x243) AM_WRITE(flipscreen_w)
	AM_RANGE(0x600, 0x603) AM_READ(input_port_1_word_r)
	AM_RANGE(0x604, 0x607) AM_READ(input_port_0_word_r)
	AM_RANGE(0x608, 0x60b) AM_WRITE(eeprom_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( misncrft_io, ADDRESS_SPACE_IO, 16 )
	AM_RANGE(0x100, 0x103) AM_WRITE(flipscreen_w)
	AM_RANGE(0x200, 0x203) AM_READ(input_port_0_word_r)
	AM_RANGE(0x240, 0x243) AM_READ(input_port_1_word_r)
	AM_RANGE(0x3c0, 0x3c3) AM_WRITE(eeprom_w)
	AM_RANGE(0x580, 0x583) AM_READ(eeprom_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START( coolmini_io, ADDRESS_SPACE_IO, 16 )
	AM_RANGE(0x200, 0x203) AM_WRITE(flipscreen_w)
	AM_RANGE(0x300, 0x303) AM_READ(input_port_1_word_r)
	AM_RANGE(0x304, 0x307) AM_READ(input_port_0_word_r)
	AM_RANGE(0x308, 0x30b) AM_WRITE(eeprom_w)
	AM_RANGE(0x4c0, 0x4c3) AM_READWRITE(oki_r, oki_w)
	AM_RANGE(0x540, 0x543) AM_WRITE(ym2151_register_w)
	AM_RANGE(0x544, 0x547) AM_READWRITE(ym2151_status_r, ym2151_data_w)
	AM_RANGE(0x7c0, 0x7c3) AM_READ(eeprom_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START( suplup_io, ADDRESS_SPACE_IO, 16 )
	AM_RANGE(0x020, 0x023) AM_WRITE(eeprom_w)
	AM_RANGE(0x040, 0x043) AM_READ(input_port_0_word_r)
	AM_RANGE(0x060, 0x063) AM_READ(input_port_1_word_r)
	AM_RANGE(0x080, 0x083) AM_READWRITE(oki_r, oki_w)
	AM_RANGE(0x0c0, 0x0c3) AM_WRITE(ym2151_register_w)
	AM_RANGE(0x0c4, 0x0c7) AM_READWRITE(ym2151_status_r, ym2151_data_w)
	AM_RANGE(0x100, 0x103) AM_READ(eeprom_r)
ADDRESS_MAP_END

/*
Sprite list:

Offset+0
-------- xxxxxxxx Y offs
-------x -------- Don't draw the sprite
x------- -------- Flip X
-x------ -------- Flip Y

Offset+1
xxxxxxxx xxxxxxxx Sprite number

Offset+2
-------- -xxxxxxx Color
or
-xxxxxxx -------- Color

Offset+3
-------x xxxxxxxx X offs
*/

static void draw_sprites(struct mame_bitmap *bitmap)
{
	const struct GfxElement *gfx = Machine->gfx[0];
	UINT32 cnt;
	int block, offs;
	int code,color,x,y,fx,fy;
	struct rectangle clip;

	clip.min_x = Machine->visible_area.min_x;
	clip.max_x = Machine->visible_area.max_x;

	for (block=0; block<0x8000; block+=0x800)
	{
		if(flipscreen)
		{
			clip.min_y = 256 - (16-(block/0x800))*16;
			clip.max_y = 256 - ((16-(block/0x800))*16)+15;
		}
		else
		{
			clip.min_y = (16-(block/0x800))*16;
			clip.max_y = ((16-(block/0x800))*16)+15;
		}

		for (cnt=0; cnt<0x800; cnt+=8)
		{
			offs = (block + cnt) / 2;
			if(tiles[offs] & 0x0100) continue;

			code  = tiles[offs+1] & 0xffff;
			color = (tiles[offs+2] >> (palshift)) & 0x7f;

			x = tiles[offs+3] & 0x01ff;
			y = 256 - (tiles[offs] & 0x00ff);

			fx = tiles[offs] & 0x8000;
			fy = tiles[offs] & 0x4000;

			if(flipscreen)
			{
				fx = !fx;
				fy = !fy;

				x = 366 - x;
				y = 256 - y;
			}

			drawgfx(bitmap,gfx,code,color,fx,fy,x,y,&clip,TRANSPARENCY_PEN,0);
		}
	}
}


VIDEO_UPDATE( common )
{
	fillbitmap(bitmap,Machine->pens[0],cliprect);
	draw_sprites(bitmap);
}


INPUT_PORTS_START( common )
	PORT_START
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    ) PORT_PLAYER(1)
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  ) PORT_PLAYER(1)
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  ) PORT_PLAYER(1)
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x0040, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1)
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1)
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    ) PORT_PLAYER(2)
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  ) PORT_PLAYER(2)
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  ) PORT_PLAYER(2)
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2)
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(2)
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2)

	PORT_START
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_SERVICE )
	PORT_SERVICE_NO_TOGGLE( 0x0010, IP_ACTIVE_LOW )
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x0040, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xff00, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END


static struct GfxLayout sprites_layout =
{
	16,16,
	RGN_FRAC(1,1),
	8,
	{ 0,1,2,3,4,5,6,7 },
	{ 0,8,16,24, 32,40,48,56, 64,72,80,88 ,96,104,112,120 },
	{ 0*128, 1*128, 2*128, 3*128, 4*128, 5*128, 6*128, 7*128, 8*128,9*128,10*128,11*128,12*128,13*128,14*128,15*128 },
	16*128,
};

static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &sprites_layout, 0, 0x80 },
	{ -1 } /* end of array */
};

static INTERRUPT_GEN( common_interrupts )
{
	if(cpu_getiloops())
	{
		cpunum_set_input_line(0, 4, PULSE_LINE);
	}
	else
	{
		cpunum_set_input_line(0, 7, PULSE_LINE);
	}
}

data8_t suplup_default_nvram[128] = {
	0xE8, 0xFE, 0xFF, 0xFF, 0x10, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x96, 0x2D, 0xB4, 0x80, 0x00,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xAA
};

data8_t misncrft_default_nvram[128] = {
	0x67, 0xBE, 0x00, 0x01, 0x80, 0xFE, 0x04, 0x10, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xAA
};

void nvram_handler_93C46_vamphalf(mame_file *file,int read_or_write)
{
	if (read_or_write)
		EEPROM_save(file);
	else
	{
		EEPROM_init(&eeprom_interface_93C46);
		if (file)
		{
			EEPROM_load(file);
		}
		else
		{
			if (!strcmp(Machine->gamedrv->name,"suplup")) EEPROM_set_data(suplup_default_nvram,128);
			if (!strcmp(Machine->gamedrv->name,"misncrft")) EEPROM_set_data(misncrft_default_nvram,128);

		}
	}
}

static ADDRESS_MAP_START( qs1000_prg_map, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE( 0x0000, 0xffff) AM_ROM
ADDRESS_MAP_END

static ADDRESS_MAP_START( qs1000_data_map, ADDRESS_SPACE_DATA, 8 )
	AM_RANGE( 0x0000, 0x007f) AM_RAM	// RAM?  wavetable registers?  not sure.
ADDRESS_MAP_END

static MACHINE_DRIVER_START( common )
	MDRV_CPU_ADD_TAG("main", E116T, 50000000)	/* 50 MHz */
	MDRV_CPU_PROGRAM_MAP(common_map,0)
	MDRV_CPU_VBLANK_INT(common_interrupts, 2)

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)

	MDRV_NVRAM_HANDLER(93C46_vamphalf)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(512, 512)
	MDRV_VISIBLE_AREA(31, 350, 16, 255)

	MDRV_PALETTE_LENGTH(0x8000)
	MDRV_GFXDECODE(gfxdecodeinfo)

	MDRV_VIDEO_UPDATE(common)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( sound_ym_oki )
	MDRV_SPEAKER_STANDARD_STEREO("left", "right")

	MDRV_SOUND_ADD(YM2151, 14318180/4)
	MDRV_SOUND_ROUTE(0, "left", 1.0)
	MDRV_SOUND_ROUTE(1, "right", 1.0)

	MDRV_SOUND_ADD(OKIM6295, 1789772.5 )
	MDRV_SOUND_CONFIG(okim6295_interface_region_1_pin7high)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "left", 1.0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "right", 1.0)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( sound_qs1000 )
	MDRV_CPU_ADD(I8052, 24000000/4)	/* 6 MHz? */
	MDRV_CPU_PROGRAM_MAP( qs1000_prg_map, 0 )
	MDRV_CPU_DATA_MAP( qs1000_data_map, 0 )

MACHINE_DRIVER_END

static MACHINE_DRIVER_START( vamphalf )
	MDRV_IMPORT_FROM(common)
	MDRV_CPU_MODIFY("main")
	MDRV_CPU_IO_MAP(vamphalf_io,0)

	MDRV_IMPORT_FROM(sound_ym_oki)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( misncrft )
	MDRV_IMPORT_FROM(common)
	MDRV_CPU_REPLACE("main", GMS30C2116, 50000000)	/* 50 MHz */
	MDRV_CPU_IO_MAP(misncrft_io,0)

	MDRV_IMPORT_FROM(sound_qs1000)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( coolmini )
	MDRV_IMPORT_FROM(common)
	MDRV_CPU_MODIFY("main")
	MDRV_CPU_IO_MAP(coolmini_io,0)

	MDRV_IMPORT_FROM(sound_ym_oki)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( suplup )
	MDRV_IMPORT_FROM(common)
	MDRV_CPU_MODIFY("main")
	MDRV_CPU_IO_MAP(suplup_io,0)

	MDRV_IMPORT_FROM(sound_ym_oki)
MACHINE_DRIVER_END

/*

Vamp 1/2 (Semi Vamp)
Danbi, 1999

Official page here...
http://f2.co.kr/eng/product/intro1-17.asp


PCB Layout
----------
             KA12    VROM1.

             BS901   AD-65    ROML01.   ROMU01.
                              ROML00.   ROMU00.
                 62256
                 62256

T2316162A  E1-16T  PROM1.          QL2003-XPL84C

                 62256
                 62256       62256
                             62256
    93C46.IC3                62256
                             62256
    50.000MHz  QL2003-XPL84C
B1 B2 B3                     28.000MHz



Notes
-----
B1 B2 B3:      Push buttons for SERV, RESET, TEST
T2316162A:     Main program RAM
E1-16T:        Hyperstone E1-16T CPU
QL2003-XPL84C: QuickLogic PLCC84 PLD
AD-65:         Compatible to OKI M6295
KA12:          Compatible to Y3012 or Y3014
BS901          Compatible to YM2151
PROM1:         Main program
VROM1:         OKI samples
ROML* / U*:    Graphics, device is MX29F1610ML (surface mounted SOP44 MASK ROM)

*/

ROM_START( vamphalf )
	ROM_REGION16_BE( 0x100000, REGION_USER1, ROMREGION_ERASE00 ) /* Hyperstone CPU Code */
	/* 0 - 0x80000 empty */
	ROM_LOAD( "prom1", 0x80000, 0x80000, CRC(f05e8e96) SHA1(c860e65c811cbda2dc70300437430fb4239d3e2d) )

	ROM_REGION( 0x800000, REGION_GFX1, ROMREGION_DISPOSE ) /* 16x16x8 Sprites */
	ROM_LOAD32_WORD( "roml00", 0x000000, 0x200000, CRC(cc075484) SHA1(6496d94740457cbfdac3d918dce2e52957341616) )
	ROM_LOAD32_WORD( "romu00", 0x000002, 0x200000, CRC(711c8e20) SHA1(1ef7f500d6f5790f5ae4a8b58f96ee9343ef8d92) )
	ROM_LOAD32_WORD( "roml01", 0x400000, 0x200000, CRC(626c9925) SHA1(c90c72372d145165a8d3588def12e15544c6223b) )
	ROM_LOAD32_WORD( "romu01", 0x400002, 0x200000, CRC(d5be3363) SHA1(dbdd0586909064e015f190087f338f37bbf205d2) )

	ROM_REGION( 0x40000, REGION_SOUND1, 0 ) /* Oki Samples */
	ROM_LOAD( "vrom1", 0x00000, 0x40000, CRC(ee9e371e) SHA1(3ead5333121a77d76e4e40a0e0bf0dbc75f261eb) )
ROM_END

/*

Mission Craft
Sun, 2000

PCB Layout
----------

SUN2000
|---------------------------------------------|
|       |------|  SND-ROM1     ROMH00  ROMH01 |
|       |QDSP  |                              |
|       |QS1001|                              |
|DA1311A|------|  SND-ROM2                    |
|       /------\                              |
|       |QDSP  |               ROML00  ROML01 |
|       |QS1000|                              |
|  24MHz\------/                              |
|                                 |---------| |
|                                 | ACTEL   | |
|J               62256            |A40MX04-F| |
|A  *  PRG-ROM2  62256            |PL84     | |
|M   PAL                          |         | |
|M                    62256 62256 |---------| |
|A                    62256 62256             |
|             |-------|           |---------| |
|             |GMS    |           | ACTEL   | |
|  93C46      |30C2116|           |A40MX04-F| |
|             |       | 62256     |PL84     | |
|  HY5118164C |-------| 62256     |         | |
|                                 |---------| |
|SW2                                          |
|SW1                                          |
|   50MHz                              28MHz  |
|---------------------------------------------|
Notes:
      GMS30C2116 - based on Hyperstone technology, clock running at 50.000MHz
      QS1001A    - Wavetable audio chip, 1M ROM, manufactured by AdMOS (Now LG Semi.), SOP32
      QS1000     - Wavetable audio chip manufactured by AdMOS (Now LG Semi.), QFP100
                   provides Creative Waveblaster functionality and General Midi functions
      SW1        - Used to enter test mode
      SW2        - PCB Reset
      *          - Empty socket for additional program ROM

*/

ROM_START( misncrft )
	ROM_REGION16_BE( 0x100000, REGION_USER1, ROMREGION_ERASE00 ) /* Hyperstone CPU Code */
	/* 0 - 0x80000 empty */
	ROM_LOAD( "prg-rom2.bin", 0x80000, 0x80000, CRC(059ae8c1) SHA1(2c72fcf560166cb17cd8ad665beae302832d551c) )

	ROM_REGION( 0x400000, REGION_CPU2, 0 )	/* i8052 code */
	ROM_LOAD( "snd-rom2.us1", 0x00000, 0x20000, CRC(8821e5b9) SHA1(4b8df97bc61b48aa16ed411614fcd7ed939cac33) )

	ROM_REGION( 0x800000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD32_WORD( "roml00", 0x000000, 0x200000, CRC(748c5ae5) SHA1(28005f655920e18c82eccf05c0c449dac16ee36e) )
	ROM_LOAD32_WORD( "romh00", 0x000002, 0x200000, CRC(f34ae697) SHA1(2282e3ef2d100f3eea0167b25b66b35a64ddb0f8) )
	ROM_LOAD32_WORD( "roml01", 0x400000, 0x200000, CRC(e37ece7b) SHA1(744361bb73905bc0184e6938be640d3eda4b758d) )
	ROM_LOAD32_WORD( "romh01", 0x400002, 0x200000, CRC(71fe4bc3) SHA1(08110b02707e835bf428d343d5112b153441e255) )

	ROM_REGION( 0x80000, REGION_SOUND1, 0 )
	ROM_LOAD( "snd-rom1.u15", 0x00000, 0x80000, CRC(fb381da9) SHA1(2b1a5447ed856ab92e44d000f27a04d981e3ac52) )

	ROM_REGION( 0x80000, REGION_SOUND2, 0 )
	ROM_LOAD( "qs1001a.u17", 0x00000, 0x80000, CRC(d13c6407) SHA1(57b14f97c7d4f9b5d9745d3571a0b7115fbe3176) )
ROM_END


ROM_START( coolmini )
	ROM_REGION16_BE( 0x100000, REGION_USER1, ROMREGION_ERASE00 ) /* Hyperstone CPU Code */
	ROM_LOAD( "cm-rom1.040", 0x00000, 0x80000, CRC(9688fa98) SHA1(d5ebeb1407980072f689c3b3a5161263c7082e9a) )
	ROM_LOAD( "cm-rom2.040", 0x80000, 0x80000, CRC(9d588fef) SHA1(7b6b0ba074c7fa0aecda2b55f411557b015522b6) )

	/* 8 flash roms missing */
	ROM_REGION( 0x800000, REGION_GFX1, ROMREGION_DISPOSE )
	ROM_LOAD( "coolmini.gfx1", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx2", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx3", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx4", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx5", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx6", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx7", 0x000000, 0x100000, NO_DUMP )
	ROM_LOAD( "coolmini.gfx8", 0x000000, 0x100000, NO_DUMP )

	ROM_REGION( 0x40000, REGION_SOUND1, 0 ) /* Oki Samples */
	ROM_LOAD( "cm-vrom1.020", 0x00000, 0x40000, CRC(fcc28081) SHA1(44031df0ee28ca49df12bcb73c83299fac205e21) )
ROM_END

/*

Super Lup Lup Puzzle
?, ?

PCB Layout
----------

F-E1-16-001
|----------------------------------------------|
|       M6295       VROM1    N341256           |
|  YM3016                                      |
|       YMXXXX    |---------|N341256           |
|                 |Quicklogi|                  |
|                 |c        |N341256           |
|J                |QL2003-  |                  |
|A        N341256 |XPL84C   |N341256           |
|M                |---------|                  |
|M        N341256 |---------|N341256           |
|A                |Quicklogi|                  |
|         N341256 |c        |N341256           |
|                 |QL2003-  |                  |
|         N341256 |XPL84C   |N341256           |
|                 |---------|    ROML00  ROMU00|
|93C46            GM71C18163 N341256           |
|PAL          E1-16T             ROML01  ROMU01|
|TEST  ROM1                                    |
|SERV                                          |
|RESET ROM2   50MHz                 14.31818MHz|
|----------------------------------------------|
Notes:
      E1-16T clock : 50.000MHz
      M6295 clock  : 1.7897725MHz (14.31818/8). Sample Rate = 1789772.5 / 132
      YMXXXX clock : 3.579545MHz (14.31818/4). Chip is either YM2151 or YM3812, actually stamped 'KA51'
      VSync        : 60Hz
      N341256      : NKK N341256SJ-15 32K x8 SRAM (SOJ28)
      GM71C18163   : LG Semi GM71C18163 1M x16 EDO DRAM (SOJ44)

      ROMs:
           ROML00/01, ROMU00/01 - Macronix MX29F1610MC-12 SOP44 16MBit FlashROM
           VROM1                - Macronix MX27C2000 2MBit DIP32 EPROM
           ROM1/2               - ST M27C4001 4MBit DIP32 EPROM
*/


ROM_START( suplup )
	ROM_REGION16_BE( 0x100000, REGION_USER1, ROMREGION_ERASE00 ) /* Hyperstone CPU Code */
	ROM_LOAD( "rom1.bin", 0x00000, 0x80000, CRC(61fb2dbe) SHA1(21cb8f571b2479de6779b877b656d1ffe5b3516f) )
	ROM_LOAD( "rom2.bin", 0x80000, 0x80000, CRC(0c176c57) SHA1(f103a1afc528c01cbc18639273ab797fb9afacb1) )

	ROM_REGION( 0x800000, REGION_GFX1, ROMREGION_DISPOSE ) /* 16x16x8 Sprites */
	ROM_LOAD32_WORD( "roml00.bin", 0x000000, 0x200000, CRC(7848e183) SHA1(1db8f0ea8f73f42824423d382b37b4d75fa3e54c) )
	ROM_LOAD32_WORD( "romu00.bin", 0x000002, 0x200000, CRC(13e3ab7f) SHA1(d5b6b15ca5aef2e2788d2b81e0418062f42bf2f2) )
	ROM_LOAD32_WORD( "roml01.bin", 0x400000, 0x200000, CRC(15769f55) SHA1(2c13e8da2682ccc7878218aaebe3c3c67d163fd2) )
	ROM_LOAD32_WORD( "romu01.bin", 0x400002, 0x200000, CRC(6687bc6f) SHA1(cf842dfb2bcdfda0acc0859985bdba91d4a80434) )

	ROM_REGION( 0x40000, REGION_SOUND1, 0 ) /* Oki Samples */
	ROM_LOAD( "vrom1.bin", 0x00000, 0x40000, CRC(34a56987) SHA1(4d8983648a7f0acf43ff4c9c8aa6c8640ee2bbfe) )
ROM_END

static int irq_active(void)
{
	UINT32 FCR = activecpu_get_reg(27);
	if( !(FCR&(1<<29)) ) // int 2 (irq 4)
		return 1;
	else
		return 0;
}

static READ16_HANDLER( vamphalf_speedup_r )
{
	if(activecpu_get_pc() == 0x82de)
	{
		if(irq_active())
			cpu_spinuntil_int();
		else
			activecpu_eat_cycles(50);
	}

	return wram[(0x4a6d0/2)+offset];
}

static READ16_HANDLER( misncrft_speedup_r )
{
	if(activecpu_get_pc() == 0xecc8)
	{
		if(irq_active())
			cpu_spinuntil_int();
		else
			activecpu_eat_cycles(50);
	}

	return wram[(0x72eb4/2)+offset];
}

static READ16_HANDLER( coolmini_speedup_r )
{
	if(activecpu_get_pc() == 0x75f7a)
	{
		if(irq_active())
			cpu_spinuntil_int();
		else
			activecpu_eat_cycles(50);
	}

	return wram[(0xd2e80/2)+offset];
}

static READ16_HANDLER( suplup_speedup_r )
{
	if(activecpu_get_pc() == 0xaf18a )
	{
		if(irq_active())
			cpu_spinuntil_int();
		else
			activecpu_eat_cycles(50);
	}

	return wram[(0x11605c/2)+offset];

}

DRIVER_INIT( vamphalf )
{
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x0004a6d0, 0x0004a6d3, 0, 0, vamphalf_speedup_r );

	palshift = 0;
	flip_bit = 0x80;
}

DRIVER_INIT( misncrft )
{
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x00072eb4, 0x00072eb7, 0, 0, misncrft_speedup_r );

	palshift = 0;
	flip_bit = 1;
}

DRIVER_INIT( coolmini )
{
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x000d2e80, 0x000d2e83, 0, 0, coolmini_speedup_r );

	palshift = 0;
	flip_bit = 1;
}

DRIVER_INIT( suplup )
{
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x0011605c, 0x0011605f, 0, 0, suplup_speedup_r );

	palshift = 8;
	/* no flipscreen */
}

GAME( 1999, suplup,   0, suplup,   common, suplup,   ROT0,  "Omega System",      "Super Lup Lup Puzzle / Zhuan Zhuan Puzzle (version 4.0 / 990518)" ) // also has 'Puzzle Bang Bang' title but it can't be selected
GAME( 1999, vamphalf, 0, vamphalf, common, vamphalf, ROT0,  "Danbi & F2 System", "Vamp 1/2 (Korea)" )
GAMEX(2000, misncrft, 0, misncrft, common, misncrft, ROT90, "Sun",               "Mission Craft (version 2.4)",	GAME_NO_SOUND )
GAMEX(19??, coolmini, 0, coolmini, common, coolmini, ROT0,  "Semicom",           "Cool Mini",					GAME_NOT_WORKING )
