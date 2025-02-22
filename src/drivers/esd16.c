/***************************************************************************

						  -= ESD 16 Bit Games =-

					driver by	Luca Elia (l.elia@tin.it)


Main  CPU	:	M68000
Video Chips	:	2 x ACTEL A40MX04 (84 Pin Square Socketed)

Sound CPU	:	Z80
Sound Chips	:	M6295 (AD-65)  +  YM3812 (U6612)  +  YM3014 (U6614)

---------------------------------------------------------------------------
Year + Game			PCB				Notes
---------------------------------------------------------------------------
98	Multi Champ		ESD 11-09-98
00  Head Panic      ESD 08-26-1999 (with Fuuki)
---------------------------------------------------------------------------

Head Panic
- Maybe the sprite code can be merged again, haven't checked yet.
- Nude / Bikini pics don't show in-game, even when set in test mode?

---------------------------------------------------------------------------

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "machine/eeprom.h"
#include "sound/okim6295.h"
#include "sound/3812intf.h"

/* Variables defined in vidhrdw: */

extern data16_t *esd16_vram_0, *esd16_scroll_0;
extern data16_t *esd16_vram_1, *esd16_scroll_1;
extern struct tilemap *esdtilemap_1_16x16;

/* Functions defined in vidhrdw: */

WRITE16_HANDLER( esd16_vram_0_w );
WRITE16_HANDLER( esd16_vram_1_w );

VIDEO_START( esd16 );
VIDEO_UPDATE( esd16 );
VIDEO_UPDATE( hedpanic );

data16_t *head_layersize;
data16_t* headpanic_platform_x;
data16_t* headpanic_platform_y;



/***************************************************************************


							Memory Maps - Main CPU


***************************************************************************/

WRITE16_HANDLER( esd16_spriteram_w ) {	COMBINE_DATA(&spriteram16[offset]);	}

WRITE16_HANDLER( esd16_flip_screen_w )
{
	if (ACCESSING_LSB && !(data & 0x7e))
	{
		flip_screen_set( data & 0x80 );
		//               data & 0x01 ?? always 1
	}
	else	logerror("CPU #0 - PC %06X: unknown flip screen bits: %02X\n",activecpu_get_pc(),data);
}

WRITE16_HANDLER( esd16_sound_command_w )
{
	if (ACCESSING_LSB)
	{
		soundlatch_w(0,data & 0xff);
		cpunum_set_input_line(1,0,ASSERT_LINE);		// Generate an IRQ
		cpu_spinuntil_time(TIME_IN_USEC(50));	// Allow the other CPU to reply
	}
}

/*
 Lines starting with an empty comment in the following MemoryReadAddress
 arrays are there for debug (e.g. the game does not read from those ranges
 AFAIK)
*/

static ADDRESS_MAP_START( multchmp_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x07ffff) AM_READ(MRA16_ROM				)	// ROM
	AM_RANGE(0x100000, 0x10ffff) AM_READ(MRA16_RAM				)	// RAM
	AM_RANGE(0x200000, 0x2005ff) AM_READ(MRA16_RAM				)	// Palette
/**/AM_RANGE(0x300000, 0x3007ff) AM_READ(MRA16_RAM				)	// Sprites
/**/AM_RANGE(0x400000, 0x403fff) AM_READ(MRA16_RAM				)	// Layers
/**/AM_RANGE(0x420000, 0x423fff) AM_READ(MRA16_RAM				)	//
/**/AM_RANGE(0x500000, 0x500003) AM_READ(MRA16_RAM				)	// Scroll
/**/AM_RANGE(0x500004, 0x500007) AM_READ(MRA16_RAM				)	//
/**/AM_RANGE(0x500008, 0x50000b) AM_READ(MRA16_RAM				)	//
/**/AM_RANGE(0x50000c, 0x50000f) AM_READ(MRA16_RAM				)	//
	AM_RANGE(0x600002, 0x600003) AM_READ(input_port_0_word_r	)	// Inputs
	AM_RANGE(0x600004, 0x600005) AM_READ(input_port_1_word_r	)	//
	AM_RANGE(0x600006, 0x600007) AM_READ(input_port_2_word_r	)	//
	AM_RANGE(0x700008, 0x70000b) AM_READ(MRA16_NOP				)	// ? Only read once
ADDRESS_MAP_END

static ADDRESS_MAP_START( multchmp_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x07ffff) AM_WRITE(MWA16_ROM						)	// ROM
	AM_RANGE(0x100000, 0x10ffff) AM_WRITE(MWA16_RAM						)	// RAM
	AM_RANGE(0x200000, 0x2005ff) AM_WRITE(paletteram16_xRRRRRGGGGGBBBBB_word_w) AM_BASE(&paletteram16	)	// Palette
	AM_RANGE(0x300000, 0x3007ff) AM_WRITE(MWA16_RAM) AM_BASE(&spriteram16) AM_SIZE(&spriteram_size	)	// Sprites
	AM_RANGE(0x300800, 0x300807) AM_WRITE(esd16_spriteram_w				)	// Sprites (Mirrored)
	AM_RANGE(0x400000, 0x403fff) AM_WRITE(esd16_vram_0_w) AM_BASE(&esd16_vram_0	)	// Layers
	AM_RANGE(0x420000, 0x423fff) AM_WRITE(esd16_vram_1_w) AM_BASE(&esd16_vram_1	)	// Scroll
	AM_RANGE(0x500000, 0x500003) AM_WRITE(MWA16_RAM) AM_BASE(&esd16_scroll_0	)	//
	AM_RANGE(0x500004, 0x500007) AM_WRITE(MWA16_RAM) AM_BASE(&esd16_scroll_1	)	//
	AM_RANGE(0x500008, 0x50000b) AM_WRITE(MWA16_RAM						)	// ? 0
	AM_RANGE(0x50000c, 0x50000f) AM_WRITE(MWA16_RAM						)	// ? 0
	AM_RANGE(0x600000, 0x600001) AM_WRITE(MWA16_NOP						)	// IRQ Ack
	AM_RANGE(0x600008, 0x600009) AM_WRITE(esd16_flip_screen_w			)	// Flip Screen + ?
	AM_RANGE(0x60000a, 0x60000b) AM_WRITE(MWA16_NOP						)	// ? 2
	AM_RANGE(0x60000c, 0x60000d) AM_WRITE(esd16_sound_command_w			)	// To Sound CPU
ADDRESS_MAP_END

WRITE16_HANDLER(hedpanic_platform_w)
{
	int offsets = headpanic_platform_x[0]+0x40* headpanic_platform_y[0];

	esd16_vram_1[offsets] = data;

	tilemap_mark_tile_dirty(esdtilemap_1_16x16,offsets);
}


static READ16_HANDLER( esd_eeprom_r )
{
	if (ACCESSING_MSB)
	{
		return ((EEPROM_read_bit() & 0x01) << 15);
	}

//	logerror("(0x%06x) unk EEPROM read: %04x\n", activecpu_get_pc(), mem_mask);
	return 0;
}

static WRITE16_HANDLER( esd_eeprom_w )
{
	if (ACCESSING_MSB)
	{
		// data line
		EEPROM_write_bit((data & 0x0400) >> 6);

		// clock line asserted.
		EEPROM_set_clock_line((data & 0x0200) ? ASSERT_LINE : CLEAR_LINE );

		// reset line asserted: reset.
		EEPROM_set_cs_line((data & 0x0100) ? CLEAR_LINE : ASSERT_LINE );
	}

//	logerror("(0x%06x) Unk EEPROM write: %04x %04x\n", activecpu_get_pc(), data, mem_mask);
}


static ADDRESS_MAP_START( hedpanic_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x07ffff) AM_READ(MRA16_ROM				)	// ROM
	AM_RANGE(0x100000, 0x10ffff) AM_READ(MRA16_RAM)
	AM_RANGE(0x800000, 0x800fff) AM_READ(MRA16_RAM)
	AM_RANGE(0xc00002, 0xc00003) AM_READ(input_port_0_word_r	)	// Inputs
	AM_RANGE(0xc00004, 0xc00005) AM_READ(input_port_1_word_r	)	//
	AM_RANGE(0xc00006, 0xc00007) AM_READ(esd_eeprom_r	)
ADDRESS_MAP_END

static ADDRESS_MAP_START( hedpanic_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x07ffff) AM_WRITE(MWA16_ROM						)	// ROM
	AM_RANGE(0x100000, 0x10ffff) AM_WRITE(MWA16_RAM)
	AM_RANGE(0x800000, 0x800fff) AM_WRITE(paletteram16_xRRRRRGGGGGBBBBB_word_w) AM_BASE(&paletteram16)
	AM_RANGE(0x900000, 0x9007ff) AM_WRITE(MWA16_RAM) AM_BASE(&spriteram16) AM_SIZE(&spriteram_size	)	// Sprites
	AM_RANGE(0x900800, 0x900807) AM_WRITE(esd16_spriteram_w				)	// Sprites (Mirrored)
	AM_RANGE(0xa00000, 0xa03fff) AM_WRITE(esd16_vram_0_w) AM_BASE(&esd16_vram_0	)	// Layers
	AM_RANGE(0xa20000, 0xa23fff) AM_WRITE(esd16_vram_1_w) AM_BASE(&esd16_vram_1	)	//
	AM_RANGE(0xa24000, 0xa27fff) AM_WRITE(esd16_vram_1_w) AM_BASE(&esd16_vram_1	)	// mirror?
	AM_RANGE(0xb00000, 0xb00003) AM_WRITE(MWA16_RAM) AM_BASE(&esd16_scroll_0	)	// Scroll
	AM_RANGE(0xb00004, 0xb00007) AM_WRITE(MWA16_RAM) AM_BASE(&esd16_scroll_1	)	//
	AM_RANGE(0xb00008, 0xb00009) AM_WRITE(MWA16_RAM) AM_BASE(&headpanic_platform_x)
	AM_RANGE(0xb0000a, 0xb0000b) AM_WRITE(MWA16_RAM) AM_BASE(&headpanic_platform_y)
//	AM_RANGE(0xb0000c, 0xb0000d) AM_WRITE(MWA16_RAM) AM_BASE(&head_unknown1) // ??
	AM_RANGE(0xb0000e, 0xb0000f) AM_WRITE(MWA16_RAM) AM_BASE(&head_layersize) // ??
//	AM_RANGE(0xc00000, 0xc00001) AM_WRITE(MWA16_RAM) AM_BASE(&head_unknown3	)	// IRQ Ack
//	AM_RANGE(0xc00008, 0xc00009) AM_WRITE(MWA16_RAM) AM_BASE(&head_unknown5		)	// Flip Screen + ? // not checked
//	AM_RANGE(0xc0000a, 0xc0000b) AM_WRITE(MWA16_RAM) AM_BASE(&head_unknown4	)	// ? 2 not checked
	AM_RANGE(0xc0000c, 0xc0000d) AM_WRITE(esd16_sound_command_w			)	// To Sound CPU // ok
	AM_RANGE(0xc0000e, 0xc0000f) AM_WRITE(esd_eeprom_w)
	AM_RANGE(0xd00008, 0xd00009) AM_WRITE(hedpanic_platform_w)
ADDRESS_MAP_END


/***************************************************************************


							Memory Maps - Sound CPU


***************************************************************************/

static WRITE8_HANDLER( esd16_sound_rombank_w )
{
	int bank = data & 0xf;
	if (data != bank)	logerror("CPU #1 - PC %04X: unknown bank bits: %02X\n",activecpu_get_pc(),data);
	if (bank >= 3)	bank += 1;
	cpu_setbank(1, memory_region(REGION_CPU2) + 0x4000 * bank);
}

static ADDRESS_MAP_START( multchmp_sound_readmem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x7fff) AM_READ(MRA8_ROM		)	// ROM
	AM_RANGE(0x8000, 0xbfff) AM_READ(MRA8_BANK1		)	// Banked ROM
	AM_RANGE(0xf800, 0xffff) AM_READ(MRA8_RAM		)	// RAM
ADDRESS_MAP_END

static ADDRESS_MAP_START( multchmp_sound_writemem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x7fff) AM_WRITE(MWA8_ROM		)	// ROM
	AM_RANGE(0x8000, 0xbfff) AM_WRITE(MWA8_ROM		)	// Banked ROM
	AM_RANGE(0xf800, 0xffff) AM_WRITE(MWA8_RAM		)	// RAM
ADDRESS_MAP_END

READ8_HANDLER( esd16_sound_command_r )
{
	/* Clear IRQ only after reading the command, or some get lost */
	cpunum_set_input_line(1,0,CLEAR_LINE);
	return soundlatch_r(0);
}

static ADDRESS_MAP_START( multchmp_sound_readport, ADDRESS_SPACE_IO, 8 )
	AM_RANGE(0x02, 0x02) AM_READ(OKIM6295_status_0_r		)	// M6295
	AM_RANGE(0x03, 0x03) AM_READ(esd16_sound_command_r		)	// From Main CPU
	AM_RANGE(0x06, 0x06) AM_READ(MRA8_NOP					)	// ? At the start
ADDRESS_MAP_END

static ADDRESS_MAP_START( multchmp_sound_writeport, ADDRESS_SPACE_IO, 8 )
	AM_RANGE(0x00, 0x00) AM_WRITE(YM3812_control_port_0_w	)	// YM3812
	AM_RANGE(0x01, 0x01) AM_WRITE(YM3812_write_port_0_w		)
	AM_RANGE(0x02, 0x02) AM_WRITE(OKIM6295_data_0_w			)	// M6295
	AM_RANGE(0x04, 0x04) AM_WRITE(MWA8_NOP					)	// ? $00, $30
	AM_RANGE(0x05, 0x05) AM_WRITE(esd16_sound_rombank_w 	)	// ROM Bank
	AM_RANGE(0x06, 0x06) AM_WRITE(MWA8_NOP					)	// ? 1 (End of NMI routine)
ADDRESS_MAP_END


/***************************************************************************


								Input Ports


***************************************************************************/

INPUT_PORTS_START( multchmp )
	PORT_START_TAG("IN0")	// $600002.w
	PORT_BIT(  0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1)
	PORT_BIT(  0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1)
	PORT_BIT(  0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1)
	PORT_BIT(  0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)
	PORT_BIT(  0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT(  0x0020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT(  0x0040, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0x0080, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_BIT(  0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2)
	PORT_BIT(  0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2)
	PORT_BIT(  0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2)
	PORT_BIT(  0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2)
	PORT_BIT(  0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT(  0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT(  0x4000, IP_ACTIVE_LOW, IPT_UNKNOWN )	// Resets the test mode
	PORT_BIT(  0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("IN1")	// $600005.b
	PORT_BIT(  0x0001, IP_ACTIVE_LOW, IPT_COIN1   )
	PORT_BIT(  0x0002, IP_ACTIVE_LOW, IPT_COIN2   )
	PORT_BIT(  0x0004, IP_ACTIVE_LOW, IPT_START1  )
	PORT_BIT(  0x0008, IP_ACTIVE_LOW, IPT_START2  )
	PORT_BIT(  0x0010, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0x0020, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0x0040, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0x0080, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0xff00, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("IN2")	// $600006.w
	PORT_SERVICE( 0x0001, IP_ACTIVE_LOW )
	PORT_DIPNAME( 0x0002, 0x0002, "Coinage Type" )	// Not Supported
	PORT_DIPSETTING(      0x0002, "1" )
//	PORT_DIPSETTING(      0x0000, "2" )
	PORT_DIPNAME( 0x0004, 0x0000, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(      0x0004, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0008, 0x0008, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(      0x0008, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x0030, 0x0030, DEF_STR( Coin_A ) )
	PORT_DIPSETTING(      0x0010, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(      0x0030, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( 2C_3C ) )
	PORT_DIPSETTING(      0x0020, DEF_STR( 1C_2C ) )
	PORT_DIPNAME( 0x00c0, 0x00c0, DEF_STR( Coin_B ) )
	PORT_DIPSETTING(      0x0040, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(      0x00c0, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( 2C_3C ) )
	PORT_DIPSETTING(      0x0080, DEF_STR( 1C_2C ) )

//	PORT_DIPNAME( 0x0300, 0x0300, DEF_STR( Difficulty" ) )	CRASH CPP??
	PORT_DIPNAME( 0x0300, 0x0300, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(      0x0200, DEF_STR( Easy ) )
	PORT_DIPSETTING(      0x0300, DEF_STR( Normal ) )
	PORT_DIPSETTING(      0x0100, DEF_STR( Hard ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( Hardest ) )
	PORT_DIPNAME( 0x0c00, 0x0c00, DEF_STR( Lives ) )
	PORT_DIPSETTING(      0x0000, "2" )
	PORT_DIPSETTING(      0x0c00, "3" )
	PORT_DIPSETTING(      0x0800, "4" )
	PORT_DIPSETTING(      0x0400, "5" )
	PORT_DIPNAME( 0x1000, 0x1000, "Selectable Games" )
	PORT_DIPSETTING(      0x1000, "3" )
	PORT_DIPSETTING(      0x0000, "4" )
	PORT_DIPNAME( 0x2000, 0x2000, DEF_STR( Free_Play ) )
	PORT_DIPSETTING(      0x2000, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x4000, 0x4000, "Unknown 2-6" )	// unused
	PORT_DIPSETTING(      0x4000, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
	PORT_DIPNAME( 0x8000, 0x8000, "Unknown 2-7" )	// unused
	PORT_DIPSETTING(      0x8000, DEF_STR( Off ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( On ) )
INPUT_PORTS_END


INPUT_PORTS_START( hedpanic )
	PORT_START_TAG("IN0")	// $600002.w
	PORT_BIT(  0x0001, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1)
	PORT_BIT(  0x0002, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1)
	PORT_BIT(  0x0004, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1)
	PORT_BIT(  0x0008, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)
	PORT_BIT(  0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT(  0x0020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT(  0x0040, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0x0080, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_BIT(  0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2)
	PORT_BIT(  0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2)
	PORT_BIT(  0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2)
	PORT_BIT(  0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2)
	PORT_BIT(  0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT(  0x2000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT(  0x4000, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(  0x8000, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("IN1")	// $600005.b
	PORT_BIT(  0x0001, IP_ACTIVE_LOW, IPT_COIN1   )
	PORT_BIT(  0x0002, IP_ACTIVE_LOW, IPT_COIN2   )
	PORT_BIT(  0x0004, IP_ACTIVE_LOW, IPT_START1  )
	PORT_BIT(  0x0008, IP_ACTIVE_LOW, IPT_START2  )
	PORT_BIT(  0x0010, IP_ACTIVE_LOW, IPT_SERVICE1  )
	PORT_BIT(  0x0020, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_SERVICE_NO_TOGGLE( 0x0040, IP_ACTIVE_LOW)
	PORT_BIT(  0xff00, IP_ACTIVE_LOW, IPT_UNKNOWN )

INPUT_PORTS_END

/***************************************************************************


							Graphics Layouts


***************************************************************************/

/* 16x16x5, made of four 8x8 tiles */
static struct GfxLayout layout_16x16x5 =
{
	16,16,
	RGN_FRAC(1,5),
	5,
	{ RGN_FRAC(4,5),RGN_FRAC(3,5),RGN_FRAC(2,5),RGN_FRAC(1,5), RGN_FRAC(0,5) },
	{ STEP8(0+7,-1), STEP8(8*16+7,-1) },
	{ STEP16(0,8) },
	16*16
};

/* 8x8x8 */
static struct GfxLayout layout_8x8x8 =
{
	8,8,
	RGN_FRAC(1,4),
	8,
	{ STEP8(0,1) },
	{ RGN_FRAC(3,4)+0*8,RGN_FRAC(2,4)+0*8,RGN_FRAC(1,4)+0*8,RGN_FRAC(0,4)+0*8,
	  RGN_FRAC(3,4)+1*8,RGN_FRAC(2,4)+1*8,RGN_FRAC(1,4)+1*8,RGN_FRAC(0,4)+1*8 },
	{ STEP8(0,2*8) },
	8*8*2,
};

static struct GfxDecodeInfo esd16_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &layout_16x16x5, 0x200, 8 }, // [0] Sprites
	{ REGION_GFX2, 0, &layout_8x8x8,   0x000, 2 }, // [1] Layers
	{ REGION_GFX1, 0, &layout_16x16x5, 0x200, 8 }, // [0] Sprites
	{ -1 }
};

static struct GfxLayout hedpanic_layout_8x8x8 =
{
	8,8,
	RGN_FRAC(1,1),
	8,
	{ 0,1,2,3,4,5,6,7 },
	{ 0*8,2*8,1*8,3*8,4*8,6*8,5*8,7*8 },
	{ 0*64,1*64,2*64,3*64,4*64,5*64,6*64,7*64 },
	64*8,
};

static struct GfxLayout hedpanic_layout_16x16x8 =
{
	16,16,
	RGN_FRAC(1,1),
	8,
	{ 0,1,2,3,4,5,6,7 },
	{ 0*8,2*8,1*8,3*8,4*8,6*8,5*8,7*8,
	  64*8+0*8,64*8+2*8,64*8+1*8,64*8+3*8,64*8+4*8,64*8+6*8,64*8+5*8,64*8+7*8 },
	{ 0*64,1*64,2*64,3*64,4*64,5*64,6*64,7*64,
	  128*8+0*64,128*8+1*64,128*8+2*64,128*8+3*64,128*8+4*64,128*8+5*64,128*8+6*64,128*8+7*64
	},
	256*8,
};


static struct GfxLayout hedpanic_sprite_16x16x5 =
{
	16,16,
	RGN_FRAC(1,3),
	5,
	{   RGN_FRAC(2,3), RGN_FRAC(0,3), RGN_FRAC(0,3)+8, RGN_FRAC(1,3),RGN_FRAC(1,3)+8 },
	{ 7,6,5,4,3,2,1,0, 256+7,256+6,256+5,256+4,256+3,256+2,256+1,256+0 },
	{ 0*16,1*16,2*16,3*16,4*16,5*16,6*16,7*16,8*16,9*16,10*16,11*16,12*16,13*16,14*16,15*16 },
	16*32,
};


static struct GfxDecodeInfo hedpanic_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &hedpanic_sprite_16x16x5, 0x200, 8 }, // [0] Sprites
	{ REGION_GFX2, 0, &hedpanic_layout_8x8x8,   0x000, 2 }, // [1] Layers
	{ REGION_GFX2, 0, &hedpanic_layout_16x16x8, 0x000, 2 }, // [1] Layers
	{ -1 }
};


/***************************************************************************


								Machine Drivers


***************************************************************************/

static MACHINE_DRIVER_START( multchmp )

	/* basic machine hardware */
	MDRV_CPU_ADD_TAG("main",M68000, 16000000)
	MDRV_CPU_PROGRAM_MAP(multchmp_readmem,multchmp_writemem)
	MDRV_CPU_VBLANK_INT(irq6_line_hold,1)

	MDRV_CPU_ADD(Z80, 4000000)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)	/* ? */
	MDRV_CPU_PROGRAM_MAP(multchmp_sound_readmem,multchmp_sound_writemem)
	MDRV_CPU_IO_MAP(multchmp_sound_readport,multchmp_sound_writeport)
	MDRV_CPU_VBLANK_INT(nmi_line_pulse,32)	/* IRQ By Main CPU */

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(0x140, 0x100)
	MDRV_VISIBLE_AREA(0, 0x140-1, 0+8, 0x100-8-1)
	MDRV_GFXDECODE(esd16_gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(768)

	MDRV_VIDEO_START(esd16)
	MDRV_VIDEO_UPDATE(esd16)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(YM3812, 4000000)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.40)

	MDRV_SOUND_ADD(OKIM6295, 1056000)
	MDRV_SOUND_CONFIG(okim6295_interface_region_1_pin7high) // clock frequency & pin 7 not verified
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.80)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( hedpanic )

	/* basic machine hardware */
	MDRV_IMPORT_FROM(multchmp)

	MDRV_CPU_MODIFY("main")
	MDRV_CPU_PROGRAM_MAP(hedpanic_readmem,hedpanic_writemem)

	MDRV_NVRAM_HANDLER(93C46)

	MDRV_PALETTE_LENGTH(0x1000/2)

	MDRV_GFXDECODE(hedpanic_gfxdecodeinfo)
	MDRV_VIDEO_UPDATE(hedpanic)

MACHINE_DRIVER_END

/***************************************************************************


								ROMs Loading


***************************************************************************/

/***************************************************************************

								Multi Champ

(C) ESD 1998
PCB No. ESD 11-09-98    (Probably the manufacture date)
CPU: MC68HC000FN16 (68000, 68 pin square socketed)
SND: Z80, U6612 (Probably YM3812), AD-65 (OKI 6295), U6614 (YM3014)
OSC: 16.000MHz, 14.000MHz
RAM: 4 x 62256, 9 x 6116
DIPS: 2 x 8 position
Dip info is in Japanese! I will scan and make it available on my site for translation.

Other Chips: 2 x ACTEL A40MX04 (84 pin square socketed)
8 PAL's (not dumped)

ROMS:

MULTCHMP.U02  \   Main Program     MX27C2000
MULTCHMP.U03  /                    MX27C2000
MULTCHMP.U06   -- Sound Program    27C010
MULTCHMP.U10   -- ADPCM Samples ?  27C010
MULTCHMP.U27 -\                    27C4001
MULTCHMP.U28   \                   27C4001
MULTCHMP.U29    |                  27C4001
MULTCHMP.U30    |                  27C4001
MULTCHMP.U31    |                  27C4001
MULTCHMP.U32    |                  27C4001
MULTCHMP.U33    +- GFX             27C4001
MULTCHMP.U34    |                  27C4001
MULTCHMP.U35    |                  MX27C2000
MULTCHMP.U36    |                  MX27C2000
MULTCHMP.U37    |                  MX27C2000
MULTCHMP.U38   /                   MX27C2000
MULTCHMP.U39 -/                    MX27C2000

***************************************************************************/

ROM_START( multchmp )
	ROM_REGION( 0x080000, REGION_CPU1, 0 )		/* 68000 Code */
	ROM_LOAD16_BYTE( "multchmp.u02", 0x000000, 0x040000, CRC(7da8c0df) SHA1(763a3240554a02d8a9a0b13b6bfcd384825a6c57) )
	ROM_LOAD16_BYTE( "multchmp.u03", 0x000001, 0x040000, CRC(5dc62799) SHA1(ff7882985efc20309c3f901a622f1beffa0c47be) )

	ROM_REGION( 0x24000, REGION_CPU2, 0 )		/* Z80 Code */
	ROM_LOAD( "multchmp.u06", 0x00000, 0x0c000, CRC(7c178bd7) SHA1(8754d3c70d9b2bf369a5ce0cce4cc0696ed22750) )
	ROM_CONTINUE(             0x10000, 0x14000             )

	ROM_REGION( 0x140000, REGION_GFX1, ROMREGION_DISPOSE )	/* Sprites, 16x16x5 */
	ROM_LOAD( "multchmp.u36", 0x000000, 0x040000, CRC(d8f06fa8) SHA1(f76912f93f99578529612a7f01d82ac7229a8e41) )
	ROM_LOAD( "multchmp.u37", 0x040000, 0x040000, CRC(b1ae7f08) SHA1(37dd9d4cef8b9e1d09d7b46a9794fb2b777c9a01) )
	ROM_LOAD( "multchmp.u38", 0x080000, 0x040000, CRC(88e252e8) SHA1(07d898379798c6be42b636762b0af61b9111a480) )
	ROM_LOAD( "multchmp.u39", 0x0c0000, 0x040000, CRC(51f01067) SHA1(d5ebbc7d358b63724d2f24da8b2ce4a202be37a5) )
	ROM_LOAD( "multchmp.u35", 0x100000, 0x040000, CRC(9d1590a6) SHA1(35f634dbf0df06ec62359c7bae43c7f5d14b0ab2) )

	ROM_REGION( 0x400000, REGION_GFX2, ROMREGION_DISPOSE )	/* Layers, 16x16x8 */
	ROM_LOAD( "multchmp.u27", 0x000000, 0x080000, CRC(dc42704e) SHA1(58a04a47ffc6d6ae0e4d49e466b1c58b37ad741a) )
	ROM_LOAD( "multchmp.u28", 0x080000, 0x080000, CRC(449991fa) SHA1(fd93e420a04cb8bea5421aa9cbe079bd3e7d4924) )
	ROM_LOAD( "multchmp.u33", 0x100000, 0x080000, CRC(e4c0ec96) SHA1(74152108e4d05f4aff9d38919f212fcb8c87cef3) )
	ROM_LOAD( "multchmp.u34", 0x180000, 0x080000, CRC(bffaaccc) SHA1(d9ab248e2c7c639666e3717cfc5d8c8468a1bde2) )
	ROM_LOAD( "multchmp.u29", 0x200000, 0x080000, CRC(01bd1399) SHA1(b717ccffe0af92a42a0879736d34d3ad71840233) )
	ROM_LOAD( "multchmp.u30", 0x280000, 0x080000, CRC(c6b4cc18) SHA1(d9097b85584272cfe4989a40d622ef1feeee6775) )
	ROM_LOAD( "multchmp.u31", 0x300000, 0x080000, CRC(b1e4e9e3) SHA1(1a7393e9073b028b4170393b3788ad8cb86c0c78) )
	ROM_LOAD( "multchmp.u32", 0x380000, 0x080000, CRC(f05cb5b4) SHA1(1b33e60942238e39d61ae59e9317b99e83595ab1) )

	ROM_REGION( 0x20000, REGION_SOUND1, ROMREGION_SOUNDONLY )	/* Samples */
	ROM_LOAD( "multchmp.u10", 0x00000, 0x20000, CRC(6e741fcd) SHA1(742e0952916c00f67dd9f8d01e721a9a538d2fc4) )
ROM_END

/***************************************************************************

PCB Layout (Head Panic)
----------

ESD 08-26-1999

  3014  3812 6116 6295   ESD4
           ESD3  Z80                   *
       6116                 6116  ESD6 *
       6116                 6116  ESD7 *
                 ESD_CRTC99            *
                 (LARGE QFP)      ESD5 *
         ESD1                          *
         ESD2
     68000                        6116
                MCM6206   ACTEL   6116
   93C46        MCM6206   A40MX04 6116
 16MHz          MCM6206           6116
 14MHz          MCM6206  ESD8 %  ESD9


Notes:
      HSync: 15.625kHz
      VSync: 60Hz
      MCM6206 is 32kx8 SRAM
      6116 is 8kx8 SRAM
      * : Board has positions for 6x standard 32 pin EPROMs but only position ESD5 is populated
          with an EPROM. In between the unpopulated positions are 2x smt pads. These are populated
          with 2x 16M SOP44 smt Mask ROMs.
      % : ROMs ESD8 and ESD9 are also 16M SOP44 smt Mask ROMs, though these are dedicated smt
          locations (i.e. no option for EPROMs at this location)

***************************************************************************/


ROM_START( hedpanic )
	ROM_REGION( 0x080000, REGION_CPU1, 0 )		/* 68000 Code */
	ROM_LOAD16_BYTE( "esd2", 0x000000, 0x040000, CRC(8cccc691) SHA1(d6a5dd6c21a67638b9023182f77780282b9b04e5) )
	ROM_LOAD16_BYTE( "esd1", 0x000001, 0x040000, CRC(d8574925) SHA1(bd4990778b90a49aa6b10f8cf6709ce2424f546a) )

	ROM_REGION( 0x84000, REGION_CPU2, 0 )		/* Z80 Code */
	ROM_LOAD( "esd3", 0x00000, 0x0c000, CRC(c668d443) SHA1(fa66a5dc5cb10e6ccc3fbdd7790091d912767001) ) // 0x040000 of data repeated 2x
	ROM_CONTINUE(             0x10000, 0x74000             )

	ROM_REGION( 0x600000, REGION_GFX1, ROMREGION_DISPOSE )	/* Sprites, 16x16x5 */
	ROM_LOAD( "esd6", 0x200000, 0x200000, CRC(5858372c) SHA1(dc96112587df681d53cf7449bd39477919978325) )
	ROM_LOAD( "esd7", 0x000000, 0x200000, CRC(055d525f) SHA1(85ad474691f96e47311a1904015d1c92d3b2d607) )
	/* expand this to take up 0x200000 bytes too so we can decode it */
	ROM_LOAD16_BYTE( "esd5", 0x400000, 0x080000, CRC(bd785921) SHA1(c8bcb38d5aa6f5a27f0dedf7efd1d6737d59b4ca) )
	ROM_FILL( 0x500000, 0x100000, 0 )

	ROM_REGION( 0x400000, REGION_GFX2, ROMREGION_DISPOSE )	/* Layers, 16x16x8 */
	ROM_LOAD16_BYTE( "esd8", 0x000000, 0x200000, CRC(23aceb4f) SHA1(35d9ebc33b9e1515e47750cfcdfc0bf8bf44b71d) )
	ROM_LOAD16_BYTE( "esd9", 0x000001, 0x200000, CRC(76b46cd2) SHA1(679cbf50ae5935e8848868081ecef4ec66424f6c) )

	ROM_REGION( 0x80000, REGION_SOUND1, ROMREGION_SOUNDONLY )	/* Samples */
	ROM_LOAD( "esd4", 0x000000, 0x080000, CRC(5692fe92) SHA1(4423039cb437ab36d198b212ef394bf1704be404) ) // 0x020000 of data repeated 4x
ROM_END

/***************************************************************************


								Game Drivers


***************************************************************************/

GAME( 1998, multchmp, 0, multchmp, multchmp, 0, ROT0, "ESD", "Multi Champ (Korea)" )
GAME( 2000, hedpanic, 0, hedpanic, hedpanic, 0, ROT0, "ESD / Fuuki", "Head Panic (Korea?)" )
