/* Sega System 32 */

/*

there are still MANY issues with the system 32 driver, mostly video related

-- bitmap enable is not understood
 rad mobile, rad rally 'Winners Don't Use Drugs'
 rad rally title screen

-- background pen / solid tilemap not understood
 rad rally course select
 *many* other places

-- clipping windows not fully understood for tilemaps or sprites

-- flip bits for tilemaps unknown (svf uses a kludge)

-- priorities are wrong (needs full priority buffer rendering)

-- many more

-- protection is also a serious problem in several games

-- readd code for jpark cabinet (it was never right anyway)

-- which io ports should be considered custom?...

-- move alien3 / jpark to custom io

-- readd random number generator...

*/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "machine/eeprom.h"
#include "machine/random.h"
#include "sound/2612intf.h"
#include "sound/rf5c68.h"

//#define S32_LOG printf
#define S32_LOG logerror

static data16_t* segas32_io_ram;
data16_t* segas32_workram;
static data16_t* segas32_shared_ram;

data16_t* segas32_protram;

/* clean up */
data16_t sys32_tilebank_external;
data16_t sys32_displayenable;
data16_t *system32_mixerregs[2];		// mixer registers
int system32_mixerShift;

/* used by multi32.c? */
int analogRead[8];
int analogSwitch=0;

int system32_temp_kludge;
data16_t *sys32_spriteram16;

static read16_handler custom_io_r;
static write16_handler custom_io_w;

/* hmm */
extern int system32_screen_mode;
extern int system32_screen_old_mode;
extern int system32_allow_high_resolution;


/******************************************************************************
 ******************************************************************************
  Prototypes
 ******************************************************************************
 ******************************************************************************/

static WRITE16_HANDLER(segas32_irq_ack_w);

/* vidhrdw/system32.c */
extern VIDEO_START(system32);
extern VIDEO_UPDATE(system32);
extern WRITE16_HANDLER( sys32_videoram_w );
extern READ16_HANDLER ( sys32_videoram_r );
extern WRITE16_HANDLER ( sys32_spriteram_w );

/* machine/segas32.c */
extern READ16_HANDLER( arabfgt_protboard_r );
extern READ16_HANDLER( arf_wakeup_protection_r );
extern WRITE16_HANDLER( arabfgt_protboard_w );

extern READ16_HANDLER( brival_protection_r );
extern WRITE16_HANDLER( brival_protboard_w );

#if 0
extern READ16_HANDLER( ga2_sprite_protection_r );
extern READ16_HANDLER( ga2_wakeup_protection_r );
#endif

extern void decrypt_ga2_protrom(void);
extern READ16_HANDLER( ga2_dpram_r );
extern WRITE16_HANDLER( ga2_dpram_w );


extern WRITE16_HANDLER(sonic_level_load_protection);

/******************************************************************************
 ******************************************************************************
  Misc Old functions (clean up!)
 ******************************************************************************
 ******************************************************************************/

extern int sys32_brightness[2][3];
data16_t *scrambled_paletteram16[2];

void system32_set_colour (int offset)
{
	int data;
	int r,g,b;
	int r2,g2,b2;
	UINT16 r_bright, g_bright, b_bright;

	data = paletteram16[offset];

	r = (data >> 0) & 0x0f;
	g = (data >> 4) & 0x0f;
	b = (data >> 8) & 0x0f;

	r2 = (data >> 13) & 0x1;
	g2 = (data >> 13) & 0x1;
	b2 = (data >> 13) & 0x1;

	r = (r << 4) | (r2 << 3);
	g = (g << 4) | (g2 << 3);
	b = (b << 4) | (b2 << 3);

	// there might be better ways of doing this ... but for now its functional ;-)
	r_bright = sys32_brightness[0][0]; r_bright &= 0x3f;
	g_bright = sys32_brightness[0][1]; g_bright &= 0x3f;
	b_bright = sys32_brightness[0][2]; b_bright &= 0x3f;

	if ((r_bright & 0x20)) { r = (r * (r_bright&0x1f))>>5; } else { r = r+(((0xf8-r) * (r_bright&0x1f))>>5); }
	if ((g_bright & 0x20)) { g = (g * (g_bright&0x1f))>>5; } else { g = g+(((0xf8-g) * (g_bright&0x1f))>>5); }
	if ((b_bright & 0x20)) { b = (b * (b_bright&0x1f))>>5; } else { b = b+(((0xf8-b) * (b_bright&0x1f))>>5); }

	palette_set_color(offset,r,g,b);
}

static WRITE16_HANDLER( system32_paletteram16_xBBBBBGGGGGRRRRR_scrambled_word_w )
{
	int r,g,b;
	int r2,g2,b2;

	COMBINE_DATA(&scrambled_paletteram16[0][offset]); // it expects to read back the same values?

	/* rearrange the data to normal format ... */

	r = (data >>1) & 0xf;
	g = (data >>6) & 0xf;
	b = (data >>11) & 0xf;

	r2 = (data >>0) & 0x1;
	g2 = (data >>5) & 0x1;
	b2 = (data >> 10) & 0x1;

	data = (data & 0x8000) | r | g<<4 | b << 8 | r2 << 12 | g2 << 13 | b2 << 14;

	COMBINE_DATA(&paletteram16[offset]);

	system32_set_colour(offset);
}

static WRITE16_HANDLER( system32_paletteram16_xBGRBBBBGGGGRRRR_word_w )
{
	COMBINE_DATA(&paletteram16[offset]);

	// some games use 8-bit writes to some palette regions
	// (especially for the text layer palettes)

	system32_set_colour(offset);
}

/******************************************************************************
 ******************************************************************************
  Game Specific Custom I/O Handlers
 ******************************************************************************
 ******************************************************************************/

/* Alien3 - todo convert to custom_io */
static data16_t sys32_gun_p1_x_c00050_data;
static data16_t sys32_gun_p1_y_c00052_data;
static data16_t sys32_gun_p2_x_c00054_data;
static data16_t sys32_gun_p2_y_c00056_data;

static WRITE16_HANDLER ( sys32_gun_p1_x_c00050_w ) { sys32_gun_p1_x_c00050_data = readinputport(7); }
static WRITE16_HANDLER ( sys32_gun_p1_y_c00052_w ) { sys32_gun_p1_y_c00052_data = readinputport(8); sys32_gun_p1_y_c00052_data = (sys32_gun_p1_y_c00052_data+1)&0xff; }
static WRITE16_HANDLER ( sys32_gun_p2_x_c00054_w ) { sys32_gun_p2_x_c00054_data = readinputport(9); }
static WRITE16_HANDLER ( sys32_gun_p2_y_c00056_w ) { sys32_gun_p2_y_c00056_data = readinputport(10); sys32_gun_p2_y_c00056_data = (sys32_gun_p2_y_c00056_data+1)&0xff; }

static READ16_HANDLER ( sys32_gun_p1_x_c00050_r ) { int retdata; retdata = sys32_gun_p1_x_c00050_data & 0x80; sys32_gun_p1_x_c00050_data <<= 1; return retdata; }
static READ16_HANDLER ( sys32_gun_p1_y_c00052_r ) { int retdata; retdata = sys32_gun_p1_y_c00052_data & 0x80; sys32_gun_p1_y_c00052_data <<= 1; return retdata; }
static READ16_HANDLER ( sys32_gun_p2_x_c00054_r ) { int retdata; retdata = sys32_gun_p2_x_c00054_data & 0x80; sys32_gun_p2_x_c00054_data <<= 1; return retdata; }
static READ16_HANDLER ( sys32_gun_p2_y_c00056_r ) { int retdata; retdata = sys32_gun_p2_y_c00056_data & 0x80; sys32_gun_p2_y_c00056_data <<= 1; return retdata; }


/* Burning Rival */
static READ16_HANDLER( brival_custom_io_r )
{
	UINT16 ret_data;

	ret_data = segas32_io_ram[offset];

	switch (offset)
	{
		case 0x62 /2:
			return readinputportbytag("EXTRABUTTONS");
	}

	S32_LOG("unmapped segas32_io_r %02x, %04x, %04x\n",offset*2,segas32_io_ram[offset], mem_mask);
	return ret_data;
}

/* Driving Games */

static READ16_HANDLER( driving_custom_io_r )
{

	UINT16 ret_data;

	ret_data = segas32_io_ram[offset];

	switch (offset)
	{
		case 0x50 /2:
			ret_data = analogRead[0] & 0x80;
			analogRead[0] <<= 1;
			return ret_data;

		case 0x52 /2:
			ret_data = analogRead[1] & 0x80;
			analogRead[1] <<= 1;
			return ret_data;

		case 0x54 /2:
			ret_data = analogRead[0] & 0x80;
			analogRead[2] <<= 1;
			return ret_data;
	}

	S32_LOG("unmapped segas32_io_r %02x, %04x, %04x\n",offset*2,segas32_io_ram[offset], mem_mask);
	return ret_data;
}

WRITE16_HANDLER( driving_custom_io_w )
{
	COMBINE_DATA( &segas32_io_ram[offset] );
//	S32_LOG("custom offset%04x\n",offset);

	switch (offset)
	{
		case 0x50 /2:
			analogRead[0] = readinputportbytag("STEER");
			return;
			break;

		case 0x52 /2:
			analogRead[1] = readinputportbytag("ACCEL");
			return;
			break;

		case 0x54 /2:
			analogRead[2] = readinputportbytag("BRAKE");
			return;
			break;
	}

	S32_LOG("unmapped segas32_io_w %02x, %04x, %04x\n",offset*2,data, mem_mask);
}

/* SegaSonic Arcade - this doesn't work too well */
static UINT8 last[6];

#define FIX_SONIC_DELTA \
	if (delta >=  0x80) \
		delta -= 0x100; \
	if (delta <= -0x80) \
		delta += 0x100; \


static READ16_HANDLER( sonic_custom_io_r )
{

	UINT16 ret_data;
	int delta = 0;

	ret_data = segas32_io_ram[offset];

	switch (offset)
	{
		case 0x40 /2:
			delta = (int)readinputportbytag("TRACKX1") - (int)last[0];
			FIX_SONIC_DELTA
			return delta;

		case 0x44 /2:
			delta = (int)readinputportbytag("TRACKY1") - (int)last[1];
			FIX_SONIC_DELTA
			return delta;

		case 0x48 /2:
			delta = (int)readinputportbytag("TRACKX2") - (int)last[2];
			FIX_SONIC_DELTA
			return delta;

		case 0x4c /2:
			delta = (int)readinputportbytag("TRACKY2") - (int)last[3];
			FIX_SONIC_DELTA
			return delta;

		case 0x50 /2:
			delta = (int)readinputportbytag("TRACKX3") - (int)last[4];
			FIX_SONIC_DELTA
			return delta;

		case 0x54 /2:
			delta = (int)readinputportbytag("TRACKY3") - (int)last[5];
			FIX_SONIC_DELTA
			return delta;

	}

	S32_LOG("unmapped segas32_io_r %02x, %04x, %04x\n",offset*2,segas32_io_ram[offset], mem_mask);
	return ret_data;
}

WRITE16_HANDLER( sonic_custom_io_w )
{
	COMBINE_DATA( &segas32_io_ram[offset] );
//	S32_LOG("custom offset%04x\n",offset);

	switch (offset)
	{
		case 0x40 /2:
			last[0] = readinputportbytag("TRACKX1");
			last[1] = readinputportbytag("TRACKY1");
			return;
			break;

		case 0x48 /2:
			last[2] = readinputportbytag("TRACKX2");
			last[3] = readinputportbytag("TRACKY2");
			return;
			break;

		case 0x50 /2:
			last[4] = readinputportbytag("TRACKX3");
			last[5] = readinputportbytag("TRACKY3");
			return;
			break;
	}

	S32_LOG("unmapped segas32_io_w %02x, %04x, %04x\n",offset*2,data, mem_mask);
}


/* Spiderman and Arabian Fight */
static READ16_HANDLER( spidey_custom_io_r )
{
	UINT16 ret_data;

	ret_data = segas32_io_ram[offset];

	switch (offset)
	{
		case 0x60 /2:
			return readinputportbytag("PLAYER3");

		case 0x62 /2:
			return readinputportbytag("PLAYER4");

		case 0x64 /2:
			return readinputportbytag("START34");
	}

	S32_LOG("unmapped segas32_io_r %02x, %04x, %04x\n",offset*2,segas32_io_ram[offset], mem_mask);
	return ret_data;
}

/******************************************************************************
 ******************************************************************************
  I/O Handlers
 ******************************************************************************
 ******************************************************************************/

static READ16_HANDLER( segas32_io_r )
{
	UINT16 ret_data;

	ret_data = segas32_io_ram[offset];

	switch (offset)
	{
		case 0x00 /2:
			return readinputportbytag("PLAYER1");

		case 0x02 /2:
			return readinputportbytag("PLAYER2");

		case 0x08 /2:
			return readinputportbytag("START12");

		case 0x0a /2:
			return (EEPROM_read_bit() << 7) | readinputportbytag("SERVICE34");
	}

	if (custom_io_r)
		return custom_io_r(offset, mem_mask);

	S32_LOG("unmapped segas32_io_r %02x, %04x, %04x\n",offset*2,segas32_io_ram[offset], mem_mask);
	return ret_data;
}

static WRITE16_HANDLER( segas32_io_w )
{
	COMBINE_DATA( &segas32_io_ram[offset] );

	switch (offset)
	{
		case 0x06 /2: /* Eeprom lives here */
			if(ACCESSING_LSB)
			{
				EEPROM_write_bit(data & 0x80);
				EEPROM_set_cs_line((data & 0x20) ? CLEAR_LINE : ASSERT_LINE);
				EEPROM_set_clock_line((data & 0x40) ? ASSERT_LINE : CLEAR_LINE);
				return;
			}
			break;

		case 0x0e /2: /* external tilebank (on system32) */
			COMBINE_DATA(&sys32_tilebank_external);
			return;
			break;

		case 0x1c /2: /* display enable */
			COMBINE_DATA(&sys32_displayenable);
			return;
			break;

	}

	if (custom_io_w)
	{
		custom_io_w(offset, data, mem_mask);
		return;
	}

	S32_LOG("unmapped segas32_io_w %02x, %04x, %04x\n",offset*2,data, mem_mask);
}


/******************************************************************************
 ******************************************************************************
  Memory Layout
 ******************************************************************************
 ******************************************************************************

 things we don't understand or game speciifc bits not here for now..

 ******************************************************************************/

static ADDRESS_MAP_START( segas32_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	ADDRESS_MAP_FLAGS( AMEF_UNMAP(1) )
	AM_RANGE(0x000000, 0x0fffff) AM_READ(MRA16_ROM) // Low Rom (0)
	AM_RANGE(0x100000, 0x1fffff) AM_READ(MRA16_BANK1) // Data Rom (1)
	AM_RANGE(0x200000, 0x23ffff) AM_READ(MRA16_RAM) // work RAM

	AM_RANGE(0x300000, 0x31ffff) AM_READ(sys32_videoram_r) // Tile Ram
	AM_RANGE(0x400000, 0x41ffff) AM_READ(MRA16_RAM) // sprite RAM
	AM_RANGE(0x600000, 0x607fff) AM_READ(MRA16_RAM)
	AM_RANGE(0x608000, 0x60ffff) AM_READ(MRA16_RAM)
	AM_RANGE(0x610000, 0x6100ff) AM_READ(MRA16_RAM) // mixer registers

	AM_RANGE(0x700000, 0x701fff) AM_READ(MRA16_RAM)	// shared RAM
	AM_RANGE(0xc00000, 0xc0007f) AM_READ(segas32_io_r)
	AM_RANGE(0xf00000, 0xffffff) AM_READ(MRA16_BANK2) // High Rom (0)
ADDRESS_MAP_END

static ADDRESS_MAP_START( segas32_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	ADDRESS_MAP_FLAGS( AMEF_UNMAP(1) )
	AM_RANGE(0x000000, 0x0fffff) AM_WRITE(MWA16_ROM)
	AM_RANGE(0x100000, 0x1fffff) AM_WRITE(MWA16_ROM)
	AM_RANGE(0x200000, 0x23ffff) AM_WRITE(MWA16_RAM) AM_BASE(&segas32_workram)

	AM_RANGE(0x300000, 0x31ffff) AM_WRITE(sys32_videoram_w)
	AM_RANGE(0x400000, 0x41ffff) AM_WRITE(sys32_spriteram_w) AM_BASE(&sys32_spriteram16) // Sprites
	AM_RANGE(0x600000, 0x607fff) AM_WRITE(system32_paletteram16_xBBBBBGGGGGRRRRR_scrambled_word_w) AM_BASE(&scrambled_paletteram16[0])	// magic data-line-scrambled mirror of palette RAM * we need to shuffle data written then?
	AM_RANGE(0x608000, 0x60ffff) AM_WRITE(system32_paletteram16_xBGRBBBBGGGGRRRR_word_w) AM_BASE(&paletteram16) // Palettes
	AM_RANGE(0x610000, 0x6100ff) AM_WRITE(MWA16_RAM) AM_BASE(&system32_mixerregs[0]) // mixer chip registers

	AM_RANGE(0x700000, 0x701fff) AM_WRITE(MWA16_RAM) AM_BASE(&segas32_shared_ram) // Shared ram with the z80
	AM_RANGE(0xc00000, 0xc0007f) AM_WRITE(segas32_io_w) AM_BASE(&segas32_io_ram)
	AM_RANGE(0xd00006, 0xd00007) AM_WRITE(segas32_irq_ack_w)
	AM_RANGE(0xf00000, 0xffffff) AM_WRITE(MWA16_ROM)
ADDRESS_MAP_END

/****************************************************
 GA2 protection board (move to machine/segas32.c?)
****************************************************/

data8_t *ga2_dpram;

static ADDRESS_MAP_START( ga2_v25_map, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x00000, 0x0ffff) AM_ROM
	AM_RANGE(0x10000, 0x1ffff) AM_RAM AM_BASE(&ga2_dpram)
	AM_RANGE(0xf0000, 0xfffff) AM_ROM
ADDRESS_MAP_END



/******************************************************************************
 ******************************************************************************
  Sound Hardware (cleanup!)
 ******************************************************************************
 ******************************************************************************/

static UINT8 *sys32_SoundMemBank;
static int s32_f1_prot;			// port f1 is used to protect the sound program on some games
static int s32_blo, s32_bhi;		// bank high and low values

static READ8_HANDLER( system32_bank_r )
{
	return sys32_SoundMemBank[offset];
}

// the Z80's work RAM is fully shared with the V60 or V70 and battery backed up.
static READ8_HANDLER( sys32_shared_snd_r )
{
	data8_t *RAM = (data8_t *)segas32_shared_ram;

	return RAM[offset];
}

static WRITE8_HANDLER( sys32_shared_snd_w )
{
	data8_t *RAM = (data8_t *)segas32_shared_ram;

	RAM[offset] = data;
}

// some games require that port f1 be a magic echo-back latch.
// thankfully, it's not required to do any math or anything on the values.
static READ8_HANDLER( sys32_sound_prot_r )
{
	return s32_f1_prot;
}

static WRITE8_HANDLER( sys32_sound_prot_w )
{
	s32_f1_prot = data;
}

static ADDRESS_MAP_START( sound_readmem_32, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x9fff) AM_READ(MRA8_ROM)
	AM_RANGE(0xa000, 0xbfff) AM_READ(system32_bank_r)
	AM_RANGE(0xd000, 0xdfff) AM_READ(RF5C68_r)
	AM_RANGE(0xe000, 0xffff) AM_READ(sys32_shared_snd_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START( sound_writemem_32, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x9fff) AM_WRITE(MWA8_ROM)
	AM_RANGE(0xc000, 0xc008) AM_WRITE(RF5C68_reg_w)
	AM_RANGE(0xd000, 0xdfff) AM_WRITE(RF5C68_w)
	AM_RANGE(0xe000, 0xffff) AM_WRITE(sys32_shared_snd_w)
ADDRESS_MAP_END

static void s32_recomp_bank(void)
{
	unsigned char *RAM = memory_region(REGION_CPU2);
	int Bank=0;
	static char remapbhi[8] =
	{
		0, 1, 5, 3, 4, 2, 6, 4
	};

	switch (s32_blo & 0xc0)
	{
		case 0x00:	// normal case
			Bank = (((remapbhi[s32_bhi]<<6) + (s32_blo)) << 13);
			break;
		case 0x40:
		case 0xc0:
			// SegaSonic (prototype) needs this alternate interpretation.
//			Bank = (((remapbhi[s32_bhi]<<5) + (s32_blo&0x3f)) << 13);
			// all other s32 games work with this, notably Super Visual Football
			// and Alien3: The Gun
			Bank = (((remapbhi[s32_bhi]<<6) + (s32_blo&0x3f)) << 13);
			break;
	}

	sys32_SoundMemBank = &RAM[Bank+0x100000];
}

static WRITE8_HANDLER( sys32_soundbank_lo_w )
{
	s32_blo = data;
	s32_recomp_bank();
}

static WRITE8_HANDLER( sys32_soundbank_hi_w )
{
	s32_bhi = data;
	s32_recomp_bank();
}

static ADDRESS_MAP_START( sound_readport_32, ADDRESS_SPACE_IO, 8 )
	AM_RANGE(0x80, 0x80) AM_READ(YM3438_status_port_0_A_r)
	AM_RANGE(0x90, 0x90) AM_READ(YM3438_status_port_1_A_r)
	AM_RANGE(0xf1, 0xf1) AM_READ(sys32_sound_prot_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START( sound_writeport_32, ADDRESS_SPACE_IO, 8 )
	AM_RANGE(0x80, 0x80) AM_WRITE(YM3438_control_port_0_A_w)
	AM_RANGE(0x81, 0x81) AM_WRITE(YM3438_data_port_0_A_w)
	AM_RANGE(0x82, 0x82) AM_WRITE(YM3438_control_port_0_B_w)
	AM_RANGE(0x83, 0x83) AM_WRITE(YM3438_data_port_0_B_w)
	AM_RANGE(0x90, 0x90) AM_WRITE(YM3438_control_port_1_A_w)
	AM_RANGE(0x91, 0x91) AM_WRITE(YM3438_data_port_1_A_w)
	AM_RANGE(0x92, 0x92) AM_WRITE(YM3438_control_port_1_B_w)
	AM_RANGE(0x93, 0x93) AM_WRITE(YM3438_data_port_1_B_w)
	AM_RANGE(0xa0, 0xa0) AM_WRITE(sys32_soundbank_lo_w)
	AM_RANGE(0xb0, 0xb0) AM_WRITE(sys32_soundbank_hi_w)
	AM_RANGE(0xc1, 0xc1) AM_WRITE(MWA8_NOP)
	AM_RANGE(0xf1, 0xf1) AM_WRITE(sys32_sound_prot_w)
ADDRESS_MAP_END


static void irq_handler(int irq)
{
	cpunum_set_input_line( 1, 0 , irq ? ASSERT_LINE : CLEAR_LINE );
}

struct YM3438interface sys32_ym3438_interface =
{
	irq_handler
};


/******************************************************************************
 ******************************************************************************
  NVRAM / Eeprom  (reads / writes are handled in io_r and io_w)
 ******************************************************************************
 ******************************************************************************/

static int system32_use_default_eeprom;
enum { EEPROM_SYS32_0=0, EEPROM_ALIEN3, EEPROM_RADM, EEPROM_RADR };

/* alien 3 with the gun calibrated, it doesn't prompt you if its not */
unsigned char alien3_default_eeprom[128] = {
	0x33, 0x53, 0x41, 0x32, 0x00, 0x35, 0x00, 0x00, 0x8B, 0xE8, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00,
	0x01, 0x01, 0x03, 0x00, 0x01, 0x08, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x8B, 0xE8, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x03, 0x00,
	0x01, 0x08, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* put radmobile in 'upright' mode since we don't emulate the motor */
unsigned char radm_default_eeprom[128] = {
	0x45, 0x53, 0x41, 0x47, 0x83, 0x01, 0x00, 0x01, 0x03, 0x93, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xFF, 0x01, 0x01, 0x01, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x60, 0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x45, 0x53, 0x41, 0x47, 0x83, 0x01, 0x00, 0x01, 0x03, 0x93, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xFF, 0x01, 0x01, 0x01, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x60, 0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};

/* set rad rally to 1 coin 1 credit otherwise it'll boot into freeplay by default which isn't ideal ;-) */
unsigned char radr_default_eeprom[128] = {
	0x45, 0x53, 0x41, 0x47, 0x00, 0x03, 0x00, 0x01, 0x04, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xFF, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x75, 0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
	0x45, 0x53, 0x41, 0x47, 0x00, 0x03, 0x00, 0x01, 0x04, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0xFF, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x75, 0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00
};

static NVRAM_HANDLER( segas32 )
{
	if (read_or_write)
		EEPROM_save(file);
	else {
		EEPROM_init(&eeprom_interface_93C46);

		if (file)
			EEPROM_load(file);
		else
		{
			if (system32_use_default_eeprom == EEPROM_ALIEN3)
				EEPROM_set_data(alien3_default_eeprom,0x80);

			if (system32_use_default_eeprom == EEPROM_RADM)
				EEPROM_set_data(radm_default_eeprom,0x80);

			if (system32_use_default_eeprom == EEPROM_RADR)
				EEPROM_set_data(radr_default_eeprom,0x80);
		}
	}
}

/******************************************************************************
 ******************************************************************************
  Interrupts
 ******************************************************************************
 ******************************************************************************/

static unsigned char irq_status;

static int irq_callback(int irqline)
{
	int i;
	for(i=7; i>=0; i--)
		if(irq_status & (1 << i)) {
			return i;
		}
	return 0;
}

static WRITE16_HANDLER(segas32_irq_ack_w)
{
	if(ACCESSING_MSB) {
		irq_status &= data >> 8;
		if(!irq_status)
			cpunum_set_input_line(0, 0, CLEAR_LINE);
	}
}

static void irq_init(void)
{
	irq_status = 0;
	cpunum_set_input_line(0, 0, CLEAR_LINE);
	cpu_set_irq_callback(0, irq_callback);
}

static void irq_raise(int level)
{
	irq_status |= (1 << level);
	cpunum_set_input_line(0, 0, ASSERT_LINE);
}

static INTERRUPT_GEN( segas32_interrupt )
{
	if(cpu_getiloops())
		irq_raise(1);
	else
		irq_raise(0);
}


/******************************************************************************
 ******************************************************************************
  Initialization
 ******************************************************************************
 ******************************************************************************/

static MACHINE_INIT( segas32 )
{
	cpu_setbank(1, memory_region(REGION_USER1)); // data rom
	cpu_setbank(2, memory_region(REGION_CPU1)); // main rom
	irq_init();

	system32_screen_mode = 0;
	system32_screen_old_mode = 1;
}

/******************************************************************************
 ******************************************************************************
  Graphics Decode
 ******************************************************************************
 ******************************************************************************/

static struct GfxLayout segas32_bgcharlayout =
{
	16,16,
	RGN_FRAC(1,1),
	4,
	{ 0,1,2,3 },
	{ 0, 4, 16, 20, 8, 12, 24, 28,
	   32, 36, 48, 52, 40, 44, 56, 60  },
	{ 0*64, 1*64, 2*64, 3*64, 4*64, 5*64, 6*64, 7*64,
	  8*64, 9*64,10*64,11*64,12*64,13*64,14*64,15*64 },
	16*64
};

static struct GfxLayout segas32_fgcharlayout =
{
	8,8,
	RGN_FRAC(1,1),
	4,
	{ 0,1,2,3 },
	{ 0, 4, 8, 12, 16, 20, 24, 28 },
	{ 0*32, 1*32, 2*32, 3*32, 4*32, 5*32, 6*32, 7*32 },
	16*16
};


static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &segas32_bgcharlayout,   0x00, 0x3ff  },
	{ REGION_GFX3, 0, &segas32_fgcharlayout,   0x00, 0x3ff  },
	{ -1 } /* end of array */
};

/******************************************************************************
 ******************************************************************************
  Machine Driver
 ******************************************************************************
 ******************************************************************************

  the actual V60 has a clock speed of 16mhz, however the V60 core does not
  have accurate timings so we must divide this by around 10

 ******************************************************************************
 ******************************************************************************/

static MACHINE_DRIVER_START( segas32 )

	/* basic machine hardware */
						
					  
	MDRV_CPU_ADD(V60, 16107950)
	MDRV_CPU_PROGRAM_MAP(segas32_readmem,segas32_writemem)
	MDRV_CPU_VBLANK_INT(segas32_interrupt,2)
                                   
	MDRV_CPU_ADD_TAG("sound", Z80, 8053975)	// verified on real PCB
	MDRV_CPU_PROGRAM_MAP(sound_readmem_32, sound_writemem_32)
	MDRV_CPU_IO_MAP(sound_readport_32, sound_writeport_32)

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(100)

	MDRV_MACHINE_INIT(segas32)
	MDRV_NVRAM_HANDLER(segas32)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_NEEDS_6BITS_PER_GUN | VIDEO_UPDATE_AFTER_VBLANK | VIDEO_RGB_DIRECT | VIDEO_HAS_SHADOWS )
	MDRV_SCREEN_SIZE(40*8, 28*8)
	MDRV_VISIBLE_AREA(0*8, 40*8-1, 0*8, 28*8-1)
	MDRV_GFXDECODE(gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(0x4000)

	MDRV_VIDEO_START(system32)
	MDRV_VIDEO_UPDATE(system32)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_STEREO("left", "right")

	MDRV_SOUND_ADD(YM3438, 8053975)
	MDRV_SOUND_CONFIG(sys32_ym3438_interface)
	MDRV_SOUND_ROUTE(0, "left", 0.40)
	MDRV_SOUND_ROUTE(1, "right", 0.40)

	MDRV_SOUND_ADD(YM3438, 8053975)
	MDRV_SOUND_ROUTE(0, "left", 0.40)
	MDRV_SOUND_ROUTE(1, "right", 0.40)

	MDRV_SOUND_ADD(RF5C68, 12500000) /* pitch matches real board, real speed is 8 MHz */
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "left", 0.55)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "right", 0.55)
MACHINE_DRIVER_END

/* System 32 can change modes at run time, games which use a higher mode need to start up
  in a higher mode or Mame can't switch */
static MACHINE_DRIVER_START( segas32_hi )
	MDRV_IMPORT_FROM( segas32 )

	MDRV_SCREEN_SIZE(52*8, 28*8)
	MDRV_VISIBLE_AREA(0*8, 52*8-1, 0*8, 28*8-1)
MACHINE_DRIVER_END


/* ga2 has a v25 on a protection board */
static MACHINE_DRIVER_START( segas32_ga2 )
	MDRV_IMPORT_FROM( segas32 )
	MDRV_CPU_ADD(V20, 10000000) // 10.000 MHz OSC
	MDRV_CPU_PROGRAM_MAP(ga2_v25_map,0)
MACHINE_DRIVER_END

/******************************************************************************
 ******************************************************************************
  Input Ports
 ******************************************************************************
 ******************************************************************************/

#define SYSTEM32_PLAYER_INPUTS(_n_, _b1_, _b2_, _b3_, _b4_) \
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_##_b1_         ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_##_b2_         ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_##_b3_         ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_##_b4_         ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(_n_) \
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  ) PORT_PLAYER(_n_)

INPUT_PORTS_START( alien3 )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)	// trigger
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)	// button
	PORT_BIT( 0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE2 )
	PORT_BIT( 0xe0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00060 - port 4
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00062 - port 5
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00064 - port 6
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00050 - port 7  - player 1 lightgun X axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_X ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START	// 0xc00052 - port 8  - player 1 lightgun Y axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_Y ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START	// 0xc00054 - port 9  - player 2 lightgun X axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_X ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(2)

	PORT_START	// 0xc00056 - port 10 - player 2 lightgun Y axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_Y ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(2)
INPUT_PORTS_END

INPUT_PORTS_START( brival )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	SYSTEM32_PLAYER_INPUTS(1, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	SYSTEM32_PLAYER_INPUTS(2, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("EXTRABUTTONS")	// 0xc00062 - port 5
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON6 ) PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_BUTTON6 ) PORT_PLAYER(1)
INPUT_PORTS_END

INPUT_PORTS_START( f1en )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("Gear Up")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("Gear Down")
	PORT_BIT( 0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("START12")	// 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0xe0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("STEER")	// port 4
	PORT_BIT( 0xff, 0x80, IPT_PADDLE ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START_TAG("ACCEL")	// port 6
	PORT_BIT( 0xff, 0, IPT_PEDAL ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START_TAG("BRAKE")	// port 8
	PORT_BIT( 0xff, 0, IPT_PEDAL2 ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)
INPUT_PORTS_END



INPUT_PORTS_START( ga2 )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	SYSTEM32_PLAYER_INPUTS(1, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	SYSTEM32_PLAYER_INPUTS(2, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )


	PORT_START_TAG("PLAYER3")	// 0xc00060 - port 4
	SYSTEM32_PLAYER_INPUTS(3, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("PLAYER4")	// 0xc00060 - port 4
	SYSTEM32_PLAYER_INPUTS(4, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("START34")	// 0xc00064 - port 6
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_START3 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_START4 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END

INPUT_PORTS_START( ga2u )

	PORT_INCLUDE( ga2 )

	PORT_MODIFY("START12")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN4 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )

INPUT_PORTS_END


INPUT_PORTS_START( holo )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	SYSTEM32_PLAYER_INPUTS(1, BUTTON1, BUTTON2, UNKNOWN, UNKNOWN)

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	SYSTEM32_PLAYER_INPUTS(2, BUTTON1, BUTTON2, UNKNOWN, UNKNOWN)

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END

INPUT_PORTS_START( jpark )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0xfe, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0xfe, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00060 - port 4
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00062 - port 5
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00064 - port 6
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	// 0xc00050 - port 7  - player 1 lightgun X axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_X ) PORT_MINMAX(0x40,0xc0) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START	// 0xc00052 - port 8  - player 1 lightgun Y axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_Y ) PORT_MINMAX(0x39,0xbf) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START	// 0xc00054 - port 9  - player 2 lightgun X axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_X ) PORT_MINMAX(0x40,0xc0) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(2)

	PORT_START	// 0xc00056 - port 10 - player 2 lightgun Y axis
	PORT_BIT( 0xff, 0x80, IPT_LIGHTGUN_Y ) PORT_MINMAX(0x39,0xbf) PORT_SENSITIVITY(35) PORT_KEYDELTA(15) PORT_PLAYER(2)
INPUT_PORTS_END


INPUT_PORTS_START( radm )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("Light")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("Wiper")
	PORT_BIT( 0xf8, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0xe0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("STEER")
	PORT_BIT( 0xff, 0x80, IPT_PADDLE ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START_TAG("ACCEL")
	PORT_BIT( 0xff, 0, IPT_PEDAL ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START_TAG("BRAKE")
	PORT_BIT( 0xff, 0, IPT_PEDAL2 ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)
INPUT_PORTS_END

INPUT_PORTS_START( radr )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("Gear Change") PORT_TOGGLE
	PORT_BIT( 0xfe, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0xe0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("STEER")
	PORT_BIT( 0xff, 0x80, IPT_PADDLE ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START_TAG("ACCEL")
	PORT_BIT( 0xff, 0, IPT_PEDAL ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)

	PORT_START_TAG("BRAKE")
	PORT_BIT( 0xff, 0, IPT_PEDAL2 ) PORT_MINMAX(0,0xff) PORT_SENSITIVITY(30) PORT_KEYDELTA(10) PORT_PLAYER(1)
INPUT_PORTS_END

INPUT_PORTS_START( sonic )
	PORT_START_TAG("SERVICE34")	// 0xc0000a - port 0
	PORT_BIT( 0x7f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1")	// 0xc00000 - port 1
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(3)
	PORT_BIT( 0xf8, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START_TAG("PLAYER2")	// 0xc00002 - port 2
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0xfe, IP_ACTIVE_HIGH, IPT_UNKNOWN )	// active_high makes player3 control labels visible in service mode

	PORT_START_TAG("START12") // 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START3 )

	PORT_START_TAG("TRACKX1") // 0xc00040 - port 7  - player 1 trackball X axis
	PORT_BIT( 0xff, 0, IPT_TRACKBALL_X ) PORT_SENSITIVITY(100) PORT_KEYDELTA(15) PORT_REVERSE PORT_PLAYER(1)

	PORT_START_TAG("TRACKY1") // 0xc00044 - port 8  - player 1 trackball Y axis
	PORT_BIT( 0xff, 0, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(15) PORT_PLAYER(1)

	PORT_START_TAG("TRACKX2") // 0xc00048 - port 9  - player 2 trackball X axis
	PORT_BIT( 0xff, 0, IPT_TRACKBALL_X ) PORT_SENSITIVITY(100) PORT_KEYDELTA(15) PORT_REVERSE PORT_PLAYER(2)

	PORT_START_TAG("TRACKY2") // 0xc0004c - port 10 - player 2 trackball Y axis
	PORT_BIT( 0xff, 0, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(15) PORT_PLAYER(2)

	PORT_START_TAG("TRACKX3") // 0xc00050 - port 11 - player 3 trackball X axis
	PORT_BIT( 0xff, 0, IPT_TRACKBALL_X ) PORT_SENSITIVITY(100) PORT_KEYDELTA(15) PORT_REVERSE PORT_PLAYER(3)

	PORT_START_TAG("TRACKY3") // 0xc00054 - port 12 - player 3 trackball Y axis
	PORT_BIT( 0xff, 0, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(15) PORT_PLAYER(3)
INPUT_PORTS_END


INPUT_PORTS_START( spidey )
	PORT_START_TAG("SERVICE34")
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1") // 0xc00000 - port 1
	SYSTEM32_PLAYER_INPUTS(1, BUTTON1, BUTTON2, UNKNOWN, UNKNOWN)

	PORT_START_TAG("PLAYER2") // 0xc00002 - port 2
	SYSTEM32_PLAYER_INPUTS(2, BUTTON1, BUTTON2, UNKNOWN, UNKNOWN)

	PORT_START_TAG("START12")	// 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )


	PORT_START_TAG("PLAYER3")	// 0xc00060 - port 4
	SYSTEM32_PLAYER_INPUTS(3, BUTTON1, BUTTON2, UNKNOWN, UNKNOWN)

	PORT_START_TAG("PLAYER4")	// 0xc00062 - port 5
	SYSTEM32_PLAYER_INPUTS(4, BUTTON1, BUTTON2, UNKNOWN, UNKNOWN)

	PORT_START_TAG("START34")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_START3 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_START4 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END

INPUT_PORTS_START( spideyu )
	PORT_INCLUDE( spidey )

	PORT_MODIFY("START12")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN4 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END

INPUT_PORTS_START( svf )
	PORT_START_TAG("SERVICE34")
	PORT_BIT( 0x0f, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_SERVICE3 )	// PSW1
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_SERVICE4 )	// PSW2
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_SPECIAL )	// EEPROM data

	PORT_START_TAG("PLAYER1") // 0xc00000 - port 1
	SYSTEM32_PLAYER_INPUTS(1, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("PLAYER2") // 0xc00002 - port 2
	SYSTEM32_PLAYER_INPUTS(2, BUTTON1, BUTTON2, BUTTON3, UNKNOWN)

	PORT_START_TAG("START12")	// 0xc00008 - port 3
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_SERVICE_NO_TOGGLE( 0x02, IP_ACTIVE_LOW )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNKNOWN )
INPUT_PORTS_END

/******************************************************************************
 ******************************************************************************
  Rom Loading
 ******************************************************************************
 ******************************************************************************/


/*****************************************
 *****************************************
  Air Rescue
 *****************************************
 *****************************************
  requires 2 linked system32 pcbs
  requires additional maths DSP to be
  emulated
 ****************************************/


ROM_START( arescue )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14540.13", 0x000000, 0x20000, CRC(c2b4e5d0) SHA1(69f8ddded5095df9012663d0ded61b78f1692a8d) )
	ROM_RELOAD     (                  0x020000, 0x20000 )
	ROM_RELOAD     (                  0x040000, 0x20000 )
	ROM_RELOAD     (                  0x060000, 0x20000 )
	ROM_LOAD16_WORD( "epr14539.6",   0x080000, 0x20000, CRC(1a1b5532) SHA1(f3651470222036703b7ecedb6e91e4cdb3d20df7) )
	ROM_RELOAD     (                  0x0a0000, 0x20000 )
	ROM_RELOAD     (                  0x0c0000, 0x20000 )
	ROM_RELOAD     (                  0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr14508.7",    0x000001, 0x080000, CRC(6702c14d) SHA1(dc9324f16a3e3238f5ccdade9451d6823a50b563) )
	ROM_LOAD16_BYTE( "epr14509.14",   0x000000, 0x080000, CRC(daa5a356) SHA1(ca87242c59de5ab5f9406635bee758a855fe20bc) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD("epr14513.35", 0x000000, 0x40000, CRC(f9a884cd) SHA1(73010fff5e0257355e08e78838c74af86ed364ce) )
	ROM_RELOAD(             0x100000, 0x40000             )
	ROM_LOAD("mpr14512.31", 0x180000, 0x80000, CRC(9da48051) SHA1(2d41148d089a75362ed0fde577eca919213ac666) )
	ROM_LOAD("mpr14510.22", 0x280000, 0x80000, CRC(5ea6d52d) SHA1(d424082468940bb91ab424ac7812839792ed4e88) )
	ROM_LOAD("mpr14511.26", 0x380000, 0x80000, CRC(074c53cc) SHA1(9c89843bbe8058123c25b7f8f86de754ddbca2bb) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "mpr14496.25", 0x000003, 0x080000, CRC(737da16c) SHA1(52247d9bc2924e90d040bef484a541b1f4a9026f) )
	ROM_LOAD32_BYTE( "mpr14497.29", 0x000001, 0x080000, CRC(ebd7ed17) SHA1(2307dc28501965432d2ff55a21698efdce014401) )
	ROM_LOAD32_BYTE( "mpr14498.34", 0x000002, 0x080000, CRC(d4a764bd) SHA1(8434a9225ed1e01e8b1cfe169268e42cd3ce6ee3) )
	ROM_LOAD32_BYTE( "mpr14499.38", 0x000000, 0x080000, CRC(fadc4b2b) SHA1(01c02a4dfad1ab19bac8b81b61d37fdc035bc5c5) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr14500.24", 0x800007, 0x100000, CRC(0a064e9b) SHA1(264761f4aacaeeac9426528caf180404cd7f6e18) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14501.28", 0x800006, 0x100000, CRC(4662bb41) SHA1(80774e680468e9ba9c5dd5eeaa4791fa3b3722fd) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14502.33", 0x800005, 0x100000, CRC(988555a9) SHA1(355e44319fd51358329cc7cd226e4c4725e045cb) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14503.37", 0x800004, 0x100000, CRC(90556aca) SHA1(24df62af55048db66d50c7034c5460330d231bf5) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14504.23", 0x800003, 0x100000, CRC(46dd038d) SHA1(9530a52e2e7388437c20ebcb19bf84c8b3b5086b) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14505.27", 0x800002, 0x100000, CRC(be142c1f) SHA1(224631e00c2458c39c6a2ef7978c2b1131fb4da2) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14506.32", 0x800001, 0x100000, CRC(5dd8fb6b) SHA1(7d21cacb2c9dba5db2547b6d8e89397e0424ee8e) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14507.36", 0x800000, 0x100000, CRC(db3f59ec) SHA1(96dcb3827354773fc2911c62260a27e90dcbe96a) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x20000, REGION_USER2, 0 ) /* NEC uPD77P25 DSP Internal ROM */
	ROM_LOAD("d7725.01", 0x000000, 0x002800, CRC(a7ec5644) SHA1(e9b05c70b639ee289e557dfd9a6c724b36338e2b) )

ROM_END


/*****************************************
 *****************************************
  Alien 3 - The Gun
 *****************************************
 *****************************************/


ROM_START( alien3 )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "15943.bin", 0x000000, 0x040000, CRC(ac4591aa) SHA1(677155a3ebdac6602525e06adb25d287eaf9e089) )
	ROM_RELOAD     (               0x040000, 0x40000 )
	ROM_LOAD( "15942.bin", 0x080000, 0x040000, CRC(a1e1d0ec) SHA1(10d8d2235a67a4ba475fe98124c6a4a5311592b5) )
	ROM_RELOAD     (               0x0c0000, 0x40000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "15855.bin", 0x000000, 0x080000, CRC(a6fadabe) SHA1(328bbb54651eef197ba13f1bd9228f3f4de7ee5e) )
	ROM_LOAD16_BYTE( "15854.bin", 0x000001, 0x080000, CRC(d1aec392) SHA1(f48804fe0151e83ad45e912b55db8ae8ddebd2ad) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "15859.bin", 0x00000, 0x40000, CRC(91b55bd0) SHA1(23b85a006a91c2a5eb1cee14172fd0d8b7732518) )
	ROM_RELOAD(            0x100000, 0x40000             )
	ROM_LOAD( "15858.bin", 0x180000, 0x100000, CRC(2eb64c10) SHA1(b2dbe86b82e889f4a9850cf4aa6596a139c1c3d6) )
	ROM_LOAD( "15856.bin", 0x280000, 0x100000, CRC(a5ef4f1f) SHA1(e8da7a995955e80872a25bd75465c590b649cfab) )
	ROM_LOAD( "15857.bin", 0x380000, 0x100000, CRC(915c56df) SHA1(7031f937c826af17caf7ec8cbb6155d0a55bd38a) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "15863.bin", 0x000000, 0x200000, CRC(9d36b645) SHA1(2977047780b615b64c3b4aec78fef0643d40490e) )
	ROM_LOAD16_BYTE( "15862.bin", 0x000001, 0x200000, CRC(9e277d25) SHA1(9f191484a42391268306a8d2d95c340ce8b2d6cd) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "15864.bin", 0x000000, 0x200000, CRC(58207157) SHA1(d1b0c7edac8b89b1322398d4cd3a976a88bc0b56) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15866.bin", 0x000002, 0x200000, CRC(9c53732c) SHA1(9aa5103cc10b4927c16e0cf102b64a15dd038756) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15868.bin", 0x000004, 0x200000, CRC(62d556e8) SHA1(d70cab0881784a3d4dd06d0c99587ca6054c2dc4) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15870.bin", 0x000006, 0x200000, CRC(d31c0400) SHA1(44c1b2e5236d894d31ff72552a7ad50270dd2fad) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15865.bin", 0x800000, 0x200000, CRC(dd64f87b) SHA1(cfa96c5f2b1221706552f5cef4aa7c61ebe21e39) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15867.bin", 0x800002, 0x200000, CRC(8cf9cb11) SHA1(a77399fccee3f258a5716721edd69a33f94f8daf) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15869.bin", 0x800004, 0x200000, CRC(dd4b137f) SHA1(7316dce32d35bf468defae5e6ed86910a37a2457) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15871.bin", 0x800006, 0x200000, CRC(58eb10ae) SHA1(23f2a72dc7b2d7b5c8a979952f81608296805745) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END


/*****************************************
 *****************************************
  Arabian Fight
 *****************************************
 *****************************************/


ROM_START( arabfgt )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "mp14608.8",  0x000000, 0x20000, CRC(cd5efba9) SHA1(a7daf8e95d31359753c984c447e93d40f43a179d) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_RELOAD     (               0x080000, 0x20000 )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "ep14592.18", 0x000000, 0x40000, CRC(f7dff316) SHA1(338690a1404dde6e7e66067f23605a247c7d0f5b) )
	ROM_RELOAD( 0x080000, 0x40000)
	ROM_LOAD16_BYTE( "ep14591.9",  0x000001, 0x40000, CRC(bbd940fb) SHA1(99c17aba890935eaf7ea468492da03103288eb1b) )
	ROM_RELOAD( 0x080001, 0x40000)

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU + banks */
	ROM_LOAD( "ep14596.36", 0x000000, 0x20000, CRC(bd01faec) SHA1(c909dcb8ef2672c4b0060d911d295e445ca311eb) )
	ROM_RELOAD(             0x100000, 0x20000             )
	ROM_LOAD( "mp14595f.35", 0x180000, 0x100000, CRC(5173d1af) SHA1(dccda644488d0c561c8ff7fa9619bd9504d8d9c6) )
	ROM_LOAD( "mp14593f.24", 0x280000, 0x100000, CRC(aa037047) SHA1(5cb1cfb235bbbf875d2b07ac4a9130ba13d47e57) )
	ROM_LOAD( "mp14594f.34", 0x380000, 0x100000, CRC(01777645) SHA1(7bcbe7687bd80b94bd3b2b3099cdd036bf7e0cd3) )

	ROM_REGION( 0x100000, REGION_CPU3, 0 ) /* Protection CPU (simulated, not emulated) */
	ROM_LOAD( "14468-01.3", 0x00000, 0x10000, CRC(c3c591e4) SHA1(53e48066e85b61d0c456618d14334a509b354cb3) )
	ROM_RELOAD(             0xf0000, 0x10000             )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mp14599f.14", 0x000000, 0x200000, CRC(94f1cf10) SHA1(34ec86487bcb6726c025149c319f00a854eb7a1d) )
	ROM_LOAD16_BYTE( "mp14598f.5",  0x000001, 0x200000, CRC(010656f3) SHA1(31619c022cba4f250ce174f186d3e34444f60faf) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mp14600f.32", 0x000000, 0x200000, CRC(e860988a) SHA1(328581877c0890519c854f75f0976b0e9c4560f8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14602.30",  0x000002, 0x200000, CRC(64524e4d) SHA1(86246185ab5ab638a73991c9e3aeb07c6d51be4f) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14604.28",  0x000004, 0x200000, CRC(5f8d5167) SHA1(1b08495e5a4cc2530c2895e47abd0e0b75496c68) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14606.26",  0x000006, 0x200000, CRC(7047f437) SHA1(e806a1cd73c96b33e8edc64e41d99bf7798103e0) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14601f.31", 0x800000, 0x200000, CRC(a2f3bb32) SHA1(1a60975dead5faf08ad4e9a96a00f98664d5e5ec) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14603.29",  0x800002, 0x200000, CRC(f6ce494b) SHA1(b3117e34913e855c035ebe37fbfbe0f7466f94f0) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14605.27",  0x800004, 0x200000, CRC(aaf52697) SHA1(b502a37ae68fc08b60cdf0e2b744898b3474d3b9) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14607.25",  0x800006, 0x200000, CRC(b70b0735) SHA1(9ef2da6f710bc5c2c7ee30dc144409a61dbe6646) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END


ROM_START( arabfgtj )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "ep14597.8",  0x000000, 0x20000, CRC(7a6fe222) SHA1(f730e9d44ad20dbaa59f6311a077c889e0aec8e4) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_RELOAD     (               0x080000, 0x20000 )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "ep14592.18", 0x000000, 0x40000, CRC(f7dff316) SHA1(338690a1404dde6e7e66067f23605a247c7d0f5b) )
	ROM_RELOAD( 0x080000, 0x40000)
	ROM_LOAD16_BYTE( "ep14591.9",  0x000001, 0x40000, CRC(bbd940fb) SHA1(99c17aba890935eaf7ea468492da03103288eb1b) )
	ROM_RELOAD( 0x080001, 0x40000)

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU + banks */
	ROM_LOAD( "ep14596.36", 0x000000, 0x20000, CRC(bd01faec) SHA1(c909dcb8ef2672c4b0060d911d295e445ca311eb) )
	ROM_RELOAD(             0x100000, 0x20000             )
	ROM_LOAD( "mp14595f.35", 0x180000, 0x100000, CRC(5173d1af) SHA1(dccda644488d0c561c8ff7fa9619bd9504d8d9c6) )
	ROM_LOAD( "mp14593f.24", 0x280000, 0x100000, CRC(aa037047) SHA1(5cb1cfb235bbbf875d2b07ac4a9130ba13d47e57) )
	ROM_LOAD( "mp14594f.34", 0x380000, 0x100000, CRC(01777645) SHA1(7bcbe7687bd80b94bd3b2b3099cdd036bf7e0cd3) )

	ROM_REGION( 0x100000, REGION_CPU3, 0 ) /* Protection CPU */
	ROM_LOAD( "14468-01.3", 0x00000, 0x10000, CRC(c3c591e4) SHA1(53e48066e85b61d0c456618d14334a509b354cb3) )
	ROM_RELOAD(             0xf0000, 0x10000             )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mp14599f.14", 0x000000, 0x200000, CRC(94f1cf10) SHA1(34ec86487bcb6726c025149c319f00a854eb7a1d) )
	ROM_LOAD16_BYTE( "mp14598f.5",  0x000001, 0x200000, CRC(010656f3) SHA1(31619c022cba4f250ce174f186d3e34444f60faf) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mp14600f.32", 0x000000, 0x200000, CRC(e860988a) SHA1(328581877c0890519c854f75f0976b0e9c4560f8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14602.30",  0x000002, 0x200000, CRC(64524e4d) SHA1(86246185ab5ab638a73991c9e3aeb07c6d51be4f) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14604.28",  0x000004, 0x200000, CRC(5f8d5167) SHA1(1b08495e5a4cc2530c2895e47abd0e0b75496c68) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14606.26",  0x000006, 0x200000, CRC(7047f437) SHA1(e806a1cd73c96b33e8edc64e41d99bf7798103e0) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14601f.31", 0x800000, 0x200000, CRC(a2f3bb32) SHA1(1a60975dead5faf08ad4e9a96a00f98664d5e5ec) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14603.29",  0x800002, 0x200000, CRC(f6ce494b) SHA1(b3117e34913e855c035ebe37fbfbe0f7466f94f0) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14605.27",  0x800004, 0x200000, CRC(aaf52697) SHA1(b502a37ae68fc08b60cdf0e2b744898b3474d3b9) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp14607.25",  0x800006, 0x200000, CRC(b70b0735) SHA1(9ef2da6f710bc5c2c7ee30dc144409a61dbe6646) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  Burning Rival
 *****************************************
 *****************************************/

ROM_START( brival )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "ep15720.8", 0x000000, 0x020000, CRC(0d182d78) SHA1(53e9e5898869ea4a354dc3e9a78d8b8e9a7274c9) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_RELOAD     (               0x080000, 0x20000 )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "ep15723.18", 0x000000, 0x080000, CRC(4ff40d39) SHA1(b33a656f976ec7a1a2268e7b9a81d5b84f3d9ca3) )
	ROM_LOAD16_BYTE( "ep15724.9",  0x000001, 0x080000, CRC(3ff8a052) SHA1(f484a8e15a022f9ff290e662ab27f96f9f0ad24e) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "ep15725.36", 0x00000, 0x20000, CRC(ea1407d7) SHA1(68b571341f032278e87a38739ba8084b7a6062d3) )
	ROM_RELOAD(             0x100000, 0x20000             )
	ROM_LOAD( "mp15627.35", 0x180000, 0x100000, CRC(8a8388c5) SHA1(7ee03feb975cc576a3d8651fd41976ca87d60894) )
	ROM_LOAD( "mp15625.24", 0x280000, 0x100000, CRC(3ce82932) SHA1(f2107bc2591f46a51c9f0d706933b1ae69db91f9) )
	ROM_LOAD( "mp15626.34", 0x380000, 0x100000, CRC(83306d1e) SHA1(feb08902b51c0013d9417832cdf198e36cdfc28c) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mp14599f.14", 0x000000, 0x200000, CRC(1de17e83) SHA1(04ee14b863f93b42a5bd1b6da71cff54ef11d4b7) )
	ROM_LOAD16_BYTE( "mp14598f.5",  0x000001, 0x200000, CRC(cafb0de9) SHA1(94c6bfc7a4081dee373e9466a7b6f80889696087) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mp15637.32", 0x000000, 0x200000, CRC(f39844c0) SHA1(c48dc8cccdd9d3756cf99a983c6a89ed43fcda22) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15635.30", 0x000002, 0x200000, CRC(263cf6d1) SHA1(7accd214502fd050edc0901c9929d6069dae4d00) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15633.28", 0x000004, 0x200000, CRC(44e9a88b) SHA1(57a930b9c3b83c889df54de60c90f847c2dcb614) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15631.26", 0x000006, 0x200000, CRC(e93cf9c9) SHA1(17786cd3ccaef613216db724e923861841c52b45) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15636.31", 0x800000, 0x200000, CRC(079ff77f) SHA1(bdd41acef58c39ba58cf85d307229622877dbdf9) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15634.29", 0x800002, 0x200000, CRC(1edc14cd) SHA1(80a281c904560b364fe9f2b8987b7a254220a29f) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15632.27", 0x800004, 0x200000, CRC(796215f2) SHA1(d7b393781dbba59c9b1cd600d27e6d91e36ea771) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp15630.25", 0x800006, 0x200000, CRC(8dabb501) SHA1(c5af2187d00e0b9732a82441f9758b303fecbb2c) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  Dark Edge
 *****************************************
 *****************************************
  this is protected and doesn't work
 ****************************************/


ROM_START( darkedge )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "ep15246.8", 0x000000, 0x80000, CRC(c0bdceeb) SHA1(9cf670cf9a8691f259c75c1d9c6cb14e8a70bb72) )
	ROM_RELOAD     (               0x080000, 0x80000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	/* none */

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr15243.36", 0x00000, 0x20000, CRC(08ca5f11) SHA1(c2c48d2f02770941a93794f82cb407d6264904d2) )
	ROM_RELOAD(              0x100000, 0x20000             )
	ROM_LOAD( "mpr15242.35", 0x180000, 0x100000, CRC(ffb7d917) SHA1(bfeae1a2bd7250edb695b7034f6b1f851f6fd48a) )
	ROM_LOAD( "mpr15240.24", 0x280000, 0x100000, CRC(867d59e8) SHA1(fb1c0d26dbb1bde9d8bc86419cd911b8e37bf923) )
	ROM_LOAD( "mpr15241.34", 0x380000, 0x100000, CRC(8eccc4fe) SHA1(119724b9b6d2b51ad4f065ebf74d200960090e68) )

	ROM_REGION( 0x100000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr15248", 0x000000, 0x080000, CRC(185b308b) SHA1(a49c1b752b3c4355560e0cd712fb9a096140e37b) )
	ROM_LOAD16_BYTE( "mpr15247", 0x000001, 0x080000, CRC(be21548c) SHA1(2e315aadc2a0b781c3ee3fe71c75eb1f43514eff) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr15249.32", 0x000000, 0x200000, CRC(2b4371a8) SHA1(47f448bfbc068f2d0cdedd81bcd280823d5758a3) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15251.30", 0x000002, 0x200000, CRC(efe2d689) SHA1(af22153ea3afdde3732f881087c642170f91d745) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15253.28", 0x000004, 0x200000, CRC(8356ed01) SHA1(a28747813807361c7d0c722a94e194caea8bfab6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15255.26", 0x000006, 0x200000, CRC(ff04a5b0) SHA1(d4548f9da014ba5249c2f75d654a2a88c095aaf8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15250.31", 0x800000, 0x200000, CRC(c5cab71a) SHA1(111c69c40a39c3fceef38f5876e1dcf7ac2fbee2) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15252.29", 0x800002, 0x200000, CRC(f8885fea) SHA1(ef944df5f6fd64813734056ad2a150f518c75459) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15254.27", 0x800004, 0x200000, CRC(7765424b) SHA1(7cd4c275f6333beeea62dd65a769e11650c68923) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15256.25", 0x800006, 0x200000, CRC(44c36b62) SHA1(4c7f2cc4347ef2126dcbf43e8dce8500e52b5f8e) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( darkedgj )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr15244.8", 0x000000, 0x80000, CRC(0db138cb) SHA1(79ccb754e0d816b395b536a6d9c5a6e93168a913) )
	ROM_RELOAD     (               0x080000, 0x80000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	/* none */

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr15243.36", 0x00000, 0x20000, CRC(08ca5f11) SHA1(c2c48d2f02770941a93794f82cb407d6264904d2) )
	ROM_RELOAD(              0x100000, 0x20000             )
	ROM_LOAD( "mpr15242.35", 0x180000, 0x100000, CRC(ffb7d917) SHA1(bfeae1a2bd7250edb695b7034f6b1f851f6fd48a) )
	ROM_LOAD( "mpr15240.24", 0x280000, 0x100000, CRC(867d59e8) SHA1(fb1c0d26dbb1bde9d8bc86419cd911b8e37bf923) )
	ROM_LOAD( "mpr15241.34", 0x380000, 0x100000, CRC(8eccc4fe) SHA1(119724b9b6d2b51ad4f065ebf74d200960090e68) )

	ROM_REGION( 0x100000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr15248", 0x000000, 0x080000, CRC(185b308b) SHA1(a49c1b752b3c4355560e0cd712fb9a096140e37b) )
	ROM_LOAD16_BYTE( "mpr15247", 0x000001, 0x080000, CRC(be21548c) SHA1(2e315aadc2a0b781c3ee3fe71c75eb1f43514eff) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr15249.32", 0x000000, 0x200000, CRC(2b4371a8) SHA1(47f448bfbc068f2d0cdedd81bcd280823d5758a3) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15251.30", 0x000002, 0x200000, CRC(efe2d689) SHA1(af22153ea3afdde3732f881087c642170f91d745) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15253.28", 0x000004, 0x200000, CRC(8356ed01) SHA1(a28747813807361c7d0c722a94e194caea8bfab6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15255.26", 0x000006, 0x200000, CRC(ff04a5b0) SHA1(d4548f9da014ba5249c2f75d654a2a88c095aaf8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15250.31", 0x800000, 0x200000, CRC(c5cab71a) SHA1(111c69c40a39c3fceef38f5876e1dcf7ac2fbee2) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15252.29", 0x800002, 0x200000, CRC(f8885fea) SHA1(ef944df5f6fd64813734056ad2a150f518c75459) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15254.27", 0x800004, 0x200000, CRC(7765424b) SHA1(7cd4c275f6333beeea62dd65a769e11650c68923) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15256.25", 0x800006, 0x200000, CRC(44c36b62) SHA1(4c7f2cc4347ef2126dcbf43e8dce8500e52b5f8e) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  Dragon Ball Z, VRVS
 *****************************************
 *****************************************
  this is protected and doesn't work
 ****************************************/


ROM_START( dbzvrvs )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "16543",   0x000000, 0x80000, CRC(7b9bc6f5) SHA1(556fd8471bf471e41fc6a50471c2be1bd6b98697) )
	ROM_LOAD16_WORD( "16542.a", 0x080000, 0x80000, CRC(6449ab22) SHA1(03e6cdacf77f2ff80dd6798094deac5486f2c840) )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	/* none */

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "16541", 0x00000, 0x40000, CRC(1d61d836) SHA1(c6b1b54d41d2650abeaf69a31aa76c4462531880) )
	ROM_RELOAD(        0x100000, 0x40000             )
	ROM_LOAD( "16540", 0x180000, 0x100000, CRC(b6f9bb43) SHA1(823f29a2fc4b9315e8c58616dbd095d45d366c8b) )
	ROM_LOAD( "16539", 0x280000, 0x100000, CRC(38c26418) SHA1(2442933e13c83209e904c1dec677aeda91b75290) )
	ROM_LOAD( "16538", 0x380000, 0x100000, CRC(4d402c31) SHA1(2df160fd7e70f3d7b52fef2a2082e68966fd1535) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "16544", 0x000000, 0x100000, CRC(f6c93dfc) SHA1(a006cedb7d0151ccc8d22e6588b1c39e099da182) )
	ROM_LOAD16_BYTE( "16545", 0x000001, 0x100000, CRC(51748bac) SHA1(b1cae16b62a8d29117c0adb140eb09c1092f6c37) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "16546", 0x000000, 0x200000, CRC(96f4be31) SHA1(ce3281630180d91de7850e9b1062382817fe0b1d) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16548", 0x000002, 0x200000, CRC(00377f59) SHA1(cf0f808d7730f334c5ac80d3171fa457be9ac88e) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16550", 0x000004, 0x200000, CRC(168e8966) SHA1(a18ec30f1358b09bcde6d8d2dbe0a82bea3bdae9) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16553", 0x000006, 0x200000, CRC(c0a43009) SHA1(e4f73768de512046b3e25c4238da811dcc2dde0b) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16547", 0x800000, 0x200000, CRC(50d328ed) SHA1(c4795299f5d7c9f3a847d684d8cde7012d4486f0) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16549", 0x800002, 0x200000, CRC(a5802e9f) SHA1(4cec3ed85a21aaf99b73013795721f212019e619) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16551", 0x800004, 0x200000, CRC(dede05fc) SHA1(51e092579e2b81fb68a9cc54165f80026fe71796) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "16552", 0x800006, 0x200000, CRC(a31dae31) SHA1(2da2c391f29b5fdb87e3f95d9dabd50370fafa5a) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  F1 Exhaust Note
 *****************************************
 *****************************************

  to display the title screen this will
  require 2 linked system32 boards to be
  emulated

 *****************************************/

ROM_START( f1en )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "ep14452a.006", 0x000000, 0x20000, CRC(b5b4a9d9) SHA1(6699c15dc1155c3cee33a06d320acbff0ab5ad11) )
	ROM_RELOAD     (                  0x020000, 0x20000 )
	ROM_RELOAD     (                  0x040000, 0x20000 )
	ROM_RELOAD     (                  0x060000, 0x20000 )
	ROM_RELOAD     (                  0x080000, 0x20000 )
	ROM_RELOAD     (                  0x0a0000, 0x20000 )
	ROM_RELOAD     (                  0x0c0000, 0x20000 )
	ROM_RELOAD     (                  0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr14445.014",  0x000000, 0x040000, CRC(d06261ab) SHA1(6e1c4ce4e49a142fd5b1ecac98145960d7afd567) )
	ROM_RELOAD( 0x080000, 0x040000)
	ROM_LOAD16_BYTE( "epr14444.007",  0x000001, 0x040000, CRC(07724354) SHA1(9d7f64a80553c4ae0e9cf716478fd5c4b8277470) )
	ROM_RELOAD( 0x080001, 0x040000)

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr14449.035", 0x00000, 0x20000, CRC(2d29699c) SHA1(cae02e5533a0edd3b3b4a54a1a43321285e06416) )
	ROM_RELOAD(               0x100000, 0x20000             )
	ROM_LOAD( "epr14448.031", 0x180000, 0x080000, CRC(87ca1e8d) SHA1(739274171c13983a60d061176095645419dade49) )
	ROM_LOAD( "epr14446.022", 0x280000, 0x080000, CRC(646ec2cb) SHA1(67e453f128ae227e22c68f55d0d3f5831fbeb2f9) )
	ROM_LOAD( "epr14447.026", 0x380000, 0x080000, CRC(db1cfcbd) SHA1(c76eb2ced5571a548ad00709097dd272747127a2) )

	ROM_REGION( 0x100000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "mpr14362", 0x000000, 0x040000, CRC(fb1c4e79) SHA1(38ee23763b9e5bb62bbc54cab95041415404f0c4) )
	ROM_LOAD32_BYTE( "mpr14361", 0x000002, 0x040000, CRC(e3204bda) SHA1(34157e80edd6d685bd5a5e23b1e0130a5f3d138a) )
	ROM_LOAD32_BYTE( "mpr14360", 0x000001, 0x040000, CRC(c5e8da79) SHA1(662a6c146fe3d0b8763d845379c06d0ee6ced1ed) )
	ROM_LOAD32_BYTE( "mpr14359", 0x000003, 0x040000, CRC(70305c68) SHA1(7a6a1bf7381eba8cc1c3897497b32ca63316972a) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr14370", 0x800000, 0x080000, CRC(fda78289) SHA1(3740affdcc738c50d07ff3e5b592bdf8a8b6be15) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14369", 0x800001, 0x080000, CRC(7765116d) SHA1(9493148aa84adc90143cf638265d4c55bfb43990) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14368", 0x800002, 0x080000, CRC(5744a30e) SHA1(98544fb234a8e93716e951d5414a490845e213c5) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14367", 0x800003, 0x080000, CRC(77bb9003) SHA1(6370fdeab4967976840d752577cd860b9ce8efca) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14366", 0x800004, 0x080000, CRC(21078e83) SHA1(f35f643c28aad3bf18cb9906b114c4f49b7b4cd1) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14365", 0x800005, 0x080000, CRC(36913790) SHA1(4a447cffb44b023fe1441277db1e411d4cd119eb) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14364", 0x800006, 0x080000, CRC(0fa12ecd) SHA1(6a34c7718edffbeddded8786e11cac181b485ebd) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14363", 0x800007, 0x080000, CRC(f3427a56) SHA1(6a99d7432dfff35470ddcca5cfde36689a77e706) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  F1 Super Lap
 *****************************************
 *****************************************

  Requires 2 linked system 32 boards to
  function

  protected

 *****************************************/

ROM_START( f1lap )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "15598", 0x000000, 0x020000, CRC(9feab7cd) SHA1(2a14c0df39e7bdae12a34679fabc6abb7618e27d) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD( "15599", 0x080000, 0x020000, CRC(5c5ac112) SHA1(2c071946e33f0700a832c7aad36f639acd35f555) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "15596", 0x000000, 0x040000, CRC(20e92909) SHA1(b974c79e11bfbd1cee61f9041cf79971fd96db3a) )
	ROM_LOAD16_BYTE( "15597", 0x000001, 0x040000, CRC(cd1ccddb) SHA1(ff0371a8010141d1ab81b5eba555ae7c64e5da37) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "15592", 0x00000, 0x20000, CRC(7c055cc8) SHA1(169beb83dfae86dd408aa92b3c214b8f607825fc) )
	ROM_RELOAD(        0x100000, 0x20000             )
	ROM_LOAD( "15593", 0x180000, 0x100000, CRC(e7300441) SHA1(33c264f0e6326689ba75026932c0932868e83b25) )
	ROM_LOAD( "15595", 0x280000, 0x100000, CRC(3fbdad9a) SHA1(573ea2242f79c7d3b6bf0e6745f6b07a621834ac) )
	ROM_LOAD( "15594", 0x380000, 0x100000, CRC(7f4ca3bb) SHA1(dc53a1857d619e574acb4c0587a6ba844df2d283) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "15608", 0x000000, 0x200000, CRC(64462c69) SHA1(9501e83c52e3e16f73b94cef975b5a31b2ee5476) )
	ROM_LOAD16_BYTE( "15609", 0x000001, 0x200000, CRC(d586e455) SHA1(aea190d31c590216eb19766ba749b1e9b710bdce) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "15600", 0x000000, 0x200000, CRC(d2698d23) SHA1(996fbcc1d0814e6f14fa7e4870ece077ecda54e6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15602", 0x000002, 0x200000, CRC(1674764d) SHA1(bc39757a5d25df1a088f874ca2442854eb551e48) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15604", 0x000004, 0x200000, CRC(1552bbb9) SHA1(77edd3f9d8dec87fa0445d264309e6164eba9313) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15606", 0x000006, 0x200000, CRC(2b4f5265) SHA1(48b4ccdedb52fbf80661ff380e5a273201fc0a12) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15601", 0x800000, 0x200000, CRC(31a8f40a) SHA1(62798346750dea87e43c8a8b01c33bf886bb50f4) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15603", 0x800002, 0x200000, CRC(3805ecbc) SHA1(54d29250441160f282c70adfd515adb21d2cda33) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15605", 0x800004, 0x200000, CRC(cbdbf35e) SHA1(a1c0900ac3210e72f5848561a6c4a77c804782c6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "15607", 0x800006, 0x200000, CRC(6c8817c9) SHA1(f5d493ed4237caf5042e95373bf9abd1fd16f873) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x20000, REGION_USER2, 0 ) /*  comms board  */
	ROM_LOAD( "15612", 0x00000, 0x20000, CRC(9d204617) SHA1(8db57121065f5d1ac52fcfb88459bdbdc30e645b) )
ROM_END

/*****************************************
 *****************************************
  Golden Axe RODA
 *****************************************
 *****************************************/



ROM_START( ga2 )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14961.b", 0x000000, 0x20000, CRC(d9cd8885) SHA1(dc9d1f01770bd23ba5959e300badbc5093a149bc) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD16_WORD( "epr14958.b", 0x080000, 0x20000, CRC(0be324a3) SHA1(5e5f457548906453eaa8d326c353b47353eab73d) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x200000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr15148.b", 0x000000, 0x40000, CRC(c477a9fd) SHA1(a9d60f801c12fd067e5ad1801a92c84edd13bd08) )
	ROM_RELOAD( 0x80000, 0x40000)
	ROM_LOAD16_BYTE( "epr15147.b", 0x000001, 0x40000, CRC(1bb676ea) SHA1(125ffd13204f48be23e20b281c42c2307888c40b) )
	ROM_RELOAD( 0x80001, 0x40000)

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU + banks */
	ROM_LOAD("epr14945", 0x000000, 0x010000, CRC(4781d4cb) SHA1(bd1b774b3cd0c3e0290c55e426f66d6820d21d0f) )
	ROM_RELOAD(          0x100000, 0x010000)
	ROM_LOAD("mpr14944", 0x180000, 0x100000, CRC(fd4d4b86) SHA1(e14b9cd6004bf9ecd902e37b433b828241361b46) )
	ROM_LOAD("mpr14942", 0x280000, 0x100000, CRC(a89b0e90) SHA1(e14c62418eb7f9a2deb2a6dcf635bedc1c73c253) )
	ROM_LOAD("mpr14943", 0x380000, 0x100000, CRC(24d40333) SHA1(38faf8f3eac317a163e93bd2247fe98189b13d2d) )

	ROM_REGION( 0x100000, REGION_CPU3, 0 ) /* Protection CPU */
	ROM_LOAD( "epr14468", 0x00000, 0x10000, CRC(77634daa) SHA1(339169d164b9ed7dc3787b084d33effdc8e9efc1) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr14948", 0x000000, 0x200000, CRC(75050d4a) SHA1(51d6bc9935abcf30af438e69c2cf4e09f57a803f) )
	ROM_LOAD16_BYTE( "mpr14947", 0x000001, 0x200000, CRC(b53e62f4) SHA1(5aa0f198e6eb070b77b0d180d30c0228a9bc691e) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr14949", 0x000000, 0x200000, CRC(152c716c) SHA1(448d16ea036b66e886119c00af543dfa5e53fd84) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14951", 0x000002, 0x200000, CRC(fdb1a534) SHA1(3126b595bf69bf9952fedf8f9c6743eb10489dc6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14953", 0x000004, 0x200000, CRC(33bd1c15) SHA1(4e16562e3357d4db54b20543073e8f1fd6f74b1f) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14955", 0x000006, 0x200000, CRC(e42684aa) SHA1(12e0f18a11edb46f09e2e8c5c4ba14170d0cf00d) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14950", 0x800000, 0x200000, CRC(15fd0026) SHA1(e918984bd60ad63172fe273b31cc9019100228c8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14952", 0x800002, 0x200000, CRC(96f96613) SHA1(4c9808866032dab0401de322c28242e8a8775457) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14954", 0x800004, 0x200000, CRC(39b2ac9e) SHA1(74f4c81d85ab9b4c5e8ae4b4d2c6dff766c482ca) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14956", 0x800006, 0x200000, CRC(298fca50) SHA1(16e09b19cc7be3dfc8e82b45348e6d1cf2ed5621) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( ga2u )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14960.b", 0x000000, 0x20000, CRC(87182fea) SHA1(bb669ea7091f1ea34589a565490effa934ca44a3) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD16_WORD( "epr14957.b", 0x080000, 0x20000, CRC(ab787cf4) SHA1(7e19bb3e5d587b5009efc9f9fa52aecaef0eedc4) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x200000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr15146.b", 0x000000, 0x40000, CRC(7293d5c3) SHA1(535a8b4b4a05546b321cee8de6733edfc1f71589) )
	ROM_RELOAD( 0x80000, 0x40000)
	ROM_LOAD16_BYTE( "epr15145.b", 0x000001, 0x40000, CRC(0da61782) SHA1(f0302d747e5d55663095bb38732af423104c33ea) )
	ROM_RELOAD( 0x80001, 0x40000)

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU + banks */
	ROM_LOAD("epr14945", 0x000000, 0x010000, CRC(4781d4cb) SHA1(bd1b774b3cd0c3e0290c55e426f66d6820d21d0f) )
	ROM_RELOAD(          0x100000, 0x010000)
	ROM_LOAD("mpr14944", 0x180000, 0x100000, CRC(fd4d4b86) SHA1(e14b9cd6004bf9ecd902e37b433b828241361b46) )
	ROM_LOAD("mpr14942", 0x280000, 0x100000, CRC(a89b0e90) SHA1(e14c62418eb7f9a2deb2a6dcf635bedc1c73c253) )
	ROM_LOAD("mpr14943", 0x380000, 0x100000, CRC(24d40333) SHA1(38faf8f3eac317a163e93bd2247fe98189b13d2d) )

	ROM_REGION( 0x100000, REGION_CPU3, 0 ) /* Protection CPU */
	ROM_LOAD( "epr14468", 0x00000, 0x10000, CRC(77634daa) SHA1(339169d164b9ed7dc3787b084d33effdc8e9efc1) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr14948", 0x000000, 0x200000, CRC(75050d4a) SHA1(51d6bc9935abcf30af438e69c2cf4e09f57a803f) )
	ROM_LOAD16_BYTE( "mpr14947", 0x000001, 0x200000, CRC(b53e62f4) SHA1(5aa0f198e6eb070b77b0d180d30c0228a9bc691e) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr14949", 0x000000, 0x200000, CRC(152c716c) SHA1(448d16ea036b66e886119c00af543dfa5e53fd84) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14951", 0x000002, 0x200000, CRC(fdb1a534) SHA1(3126b595bf69bf9952fedf8f9c6743eb10489dc6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14953", 0x000004, 0x200000, CRC(33bd1c15) SHA1(4e16562e3357d4db54b20543073e8f1fd6f74b1f) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14955", 0x000006, 0x200000, CRC(e42684aa) SHA1(12e0f18a11edb46f09e2e8c5c4ba14170d0cf00d) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14950", 0x800000, 0x200000, CRC(15fd0026) SHA1(e918984bd60ad63172fe273b31cc9019100228c8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14952", 0x800002, 0x200000, CRC(96f96613) SHA1(4c9808866032dab0401de322c28242e8a8775457) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14954", 0x800004, 0x200000, CRC(39b2ac9e) SHA1(74f4c81d85ab9b4c5e8ae4b4d2c6dff766c482ca) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14956", 0x800006, 0x200000, CRC(298fca50) SHA1(16e09b19cc7be3dfc8e82b45348e6d1cf2ed5621) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( ga2j )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14956.17", 0x000000, 0x20000, CRC(f1929177) SHA1(7dc39c40eff9fb46c2e51d1e83478cd6970e3951) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD16_WORD( "epr14946.8", 0x080000, 0x20000, CRC(eacafe94) SHA1(d41a7e1ee2df9e053b559be0a1a6d2ae520fd3e4) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x200000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr14941.18", 0x000000, 0x40000, CRC(0ffb8203) SHA1(b27dce634d203af8abb6ddfb656d4c48eb54af01) )
	ROM_RELOAD( 0x80000, 0x40000)
	ROM_LOAD16_BYTE( "epr14940.9", 0x000001, 0x40000, CRC(3b5b3084) SHA1(ea17f6b7fd413fe3808f822cec84c993c9b75aa2) )
	ROM_RELOAD( 0x80001, 0x40000)

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU + banks */
	ROM_LOAD("epr14945", 0x000000, 0x010000, CRC(4781d4cb) SHA1(bd1b774b3cd0c3e0290c55e426f66d6820d21d0f) )
	ROM_RELOAD(          0x100000, 0x010000)
	ROM_LOAD("mpr14944", 0x180000, 0x100000, CRC(fd4d4b86) SHA1(e14b9cd6004bf9ecd902e37b433b828241361b46) )
	ROM_LOAD("mpr14942", 0x280000, 0x100000, CRC(a89b0e90) SHA1(e14c62418eb7f9a2deb2a6dcf635bedc1c73c253) )
	ROM_LOAD("mpr14943", 0x380000, 0x100000, CRC(24d40333) SHA1(38faf8f3eac317a163e93bd2247fe98189b13d2d) )

	ROM_REGION( 0x100000, REGION_CPU3, 0 ) /* Protection CPU */
	ROM_LOAD( "epr1446802.3", 0x00000, 0x10000, CRC(77634daa) SHA1(339169d164b9ed7dc3787b084d33effdc8e9efc1) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr14948", 0x000000, 0x200000, CRC(75050d4a) SHA1(51d6bc9935abcf30af438e69c2cf4e09f57a803f) )
	ROM_LOAD16_BYTE( "mpr14947", 0x000001, 0x200000, CRC(b53e62f4) SHA1(5aa0f198e6eb070b77b0d180d30c0228a9bc691e) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr14949", 0x000000, 0x200000, CRC(152c716c) SHA1(448d16ea036b66e886119c00af543dfa5e53fd84) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14951", 0x000002, 0x200000, CRC(fdb1a534) SHA1(3126b595bf69bf9952fedf8f9c6743eb10489dc6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14953", 0x000004, 0x200000, CRC(33bd1c15) SHA1(4e16562e3357d4db54b20543073e8f1fd6f74b1f) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14955", 0x000006, 0x200000, CRC(e42684aa) SHA1(12e0f18a11edb46f09e2e8c5c4ba14170d0cf00d) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14950", 0x800000, 0x200000, CRC(15fd0026) SHA1(e918984bd60ad63172fe273b31cc9019100228c8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14952", 0x800002, 0x200000, CRC(96f96613) SHA1(4c9808866032dab0401de322c28242e8a8775457) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14954", 0x800004, 0x200000, CRC(39b2ac9e) SHA1(74f4c81d85ab9b4c5e8ae4b4d2c6dff766c482ca) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr14956", 0x800006, 0x200000, CRC(298fca50) SHA1(16e09b19cc7be3dfc8e82b45348e6d1cf2ed5621) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END


/*****************************************
 *****************************************
  Holloseum
 *****************************************
 *****************************************/

ROM_START( holo )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14977.a", 0x000000, 0x020000, CRC(e0d7e288) SHA1(3126041ba73f21fac0207bf5c63230c61180f564) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD16_WORD( "epr14976.a", 0x080000, 0x020000, CRC(e56f13be) SHA1(3d9e7add8feaa35c4c2e8bda84ae251087bd5e40) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr15011", 0x000000, 0x020000, CRC(b9f59f59) SHA1(f8c91fa877cf53153bec3d7850eab38227cc18ba) )
	ROM_LOAD16_BYTE( "epr15010", 0x000001, 0x020000, CRC(0c09c57b) SHA1(028a9fe1c625be218ba90906308d25d69d4de4c4) )

        ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr14965", 0x00000, 0x20000, CRC(3a918cfe) SHA1(f43ecbc9e774873e868bc921321541b308ea1a3c) )
	ROM_RELOAD(           0x100000, 0x020000             )
	ROM_LOAD( "mpr14964", 0x180000, 0x100000, CRC(7ff581d5) SHA1(ab81bd70937319e4edc8924bdb493d5ef1ec096a) )
	ROM_LOAD( "mpr14962", 0x280000, 0x100000, CRC(6b2e694e) SHA1(7874bdfd534231c7756e0e0d9fc7a3d5bdba74d3) )
	ROM_LOAD( "mpr14963", 0x380000, 0x100000, CRC(0974a60e) SHA1(87d770edcee9c9e8f37d36ab28c5aa5d685ea849) )

	ROM_REGION( 0x000100, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	/* game doesn't use bg tilemaps */

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr14973", 0x800000, 0x100000, CRC(b3c3ff6b) SHA1(94e8dbfae37a5b122ee3d471aad1f758e4a39b9e) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14972", 0x800001, 0x100000, CRC(0c161374) SHA1(413ab45deb687ecdbdc06ae98aa32ad8a0d80e0c) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14971", 0x800002, 0x100000, CRC(dfcf6fdf) SHA1(417291b54010be20dd6738a70d372b580615a8bb) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14970", 0x800003, 0x100000, CRC(cae3a745) SHA1(b6cc1f4abb460cda4714967e880928dc727ecf0a) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14969", 0x800004, 0x100000, CRC(c06b7c15) SHA1(8b97a899e6eacf798b9f55af8df95e12ccacadec) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14968", 0x800005, 0x100000, CRC(f413894a) SHA1(d65f57b1e55199e901c7c2f701589c46eeab739a) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14967", 0x800006, 0x100000, CRC(5377fce0) SHA1(baf8f82ab851b24202938fc7213d72324b9b92c0) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr14966", 0x800007, 0x100000, CRC(dffba2e9) SHA1(b97e47e78abb8302bc2c87681643382203bd76eb) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  Jurassic Park
 *****************************************
 *****************************************/

ROM_START( jpark )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "ep16402a.8", 0x000000, 0x080000, CRC(c70db239) SHA1(fd79dfd1ce194fcc8ccb58117bc845cdfe9943b1) )
	ROM_RELOAD     (               0x080000, 0x80000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "ep16395.18", 0x000000, 0x080000, CRC(ac5a01d6) SHA1(df6bffdf5723cb8790a9c1c0ab271989a758bdd8) )
	ROM_LOAD16_BYTE( "ep16394.9",  0x000001, 0x080000, CRC(c08c3a8a) SHA1(923cf256d863656336401fa75103b42298cb3822) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "ep16399.36", 0x00000, 0x40000, CRC(b09b2fe3) SHA1(bf8d646bab65fcc4ece8c2bd9a3df389e5860ed6) )
	ROM_RELOAD(             0x100000, 0x40000             )
	ROM_LOAD( "mp16398.35", 0x180000, 0x100000, CRC(fa710ca6) SHA1(1fd625070eef5f99d7be07606aeeff9282e32532) )
	ROM_LOAD( "mp16396.24", 0x280000, 0x100000, CRC(f69a2dc4) SHA1(3f02b10976852916c58e852f3161a857784fe36b) )
	ROM_LOAD( "mp16397.34", 0x380000, 0x100000, CRC(6e96e0be) SHA1(422b783b72127b80a23043b2dd1c04f5772f436e) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mp16404.14", 0x000000, 0x200000, CRC(11283807) SHA1(99e465c3fc31e640740b8257a349e203f026754a) )
	ROM_LOAD16_BYTE( "mp16403.5",  0x000001, 0x200000, CRC(02530a9b) SHA1(b43e1b47f74c801bfc599cbe893fb8dc13453dd0) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mp16405.32", 0x000000, 0x200000, CRC(b425f182) SHA1(66c6bd29dd3450db816b895c4c9c5208a66aae67) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16407.30", 0x000002, 0x200000, CRC(bc49ffd9) SHA1(a50ba7ddccfdfd7638c4041978b39c1559afbbb4) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16409.28", 0x000004, 0x200000, CRC(fe73660d) SHA1(ec1a3ea5303d2ccb9e327da18476969953626e1c) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16411.26", 0x000006, 0x200000, CRC(71cabbc5) SHA1(9760f57ef43eb251488dadd37711d5682d902434) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16406.31", 0x800000, 0x200000, CRC(b9ed73d6) SHA1(0dd22e7a21e95d84fc91acd742c737f96529f515) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16408.29", 0x800002, 0x200000, CRC(7b2f476b) SHA1(da99a9911982ba8aaef8c9aecc2977c9fd6da094) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16410.27", 0x800004, 0x200000, CRC(49c8f952) SHA1(f26b818711910b10bf520e5f849a1478a6b1d6e6) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16412.25", 0x800006, 0x200000, CRC(105dc26e) SHA1(fd2ef8c9fe1a78b4f9cc891a6fbd060184e58a1f) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x10000, REGION_CPU3, 0 ) /* unused */
	ROM_LOAD( "ep13908.xx", 0x00000, 0x8000, CRC(6228c1d2) SHA1(bd37fe775534fb94c9af80546948ce5f9c47bbf5) ) /* cabinet movement */
ROM_END

/*****************************************
 *****************************************
  Rad Mobile
 *****************************************
 *****************************************/

ROM_START( radm )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr13693.21", 0x000000, 0x20000, CRC(3f09a211) SHA1(e0e011d7069745e9bf0395bc1375d0f8b9c46dab) )
	ROM_RELOAD     (                 0x020000, 0x20000 )
	ROM_RELOAD     (                 0x040000, 0x20000 )
	ROM_RELOAD     (                 0x060000, 0x20000 )
	ROM_RELOAD     (                 0x080000, 0x20000 )
	ROM_RELOAD     (                 0x0a0000, 0x20000 )
	ROM_RELOAD     (                 0x0c0000, 0x20000 )
	ROM_RELOAD     (                 0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr13525.bin", 0x000000, 0x80000, CRC(62ad83a0) SHA1(b537176ebca15d91db04d5d7ab36aa967d41288e) )
	ROM_LOAD16_BYTE( "epr13526.bin", 0x000001, 0x80000, CRC(59ea372a) SHA1(e7a5d59586652c59c23e07e0a99ecc740fb6144d) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr13527.bin", 0x00000, 0x20000, CRC(a2e3fbbe) SHA1(2787bbef696ab3f2b7855ac991867837d3de54cd) )
	ROM_RELOAD(               0x100000, 0x020000             )
	ROM_LOAD( "epr13523.bin", 0x180000, 0x080000, CRC(d5563697) SHA1(eb3fd3dbfea383ac1bb5d2e1552723994cb4693d) )
	ROM_LOAD( "epr13523.bin", 0x280000, 0x080000, CRC(d5563697) SHA1(eb3fd3dbfea383ac1bb5d2e1552723994cb4693d) )
	ROM_LOAD( "epr13699.bin", 0x380000, 0x080000, CRC(33fd2913) SHA1(60b664559b4989446b1c7d875432e53a36fe27df) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "mpr13519.bin", 0x000000, 0x080000, CRC(bedc9534) SHA1(7b3f7a47b6c0ca6707dc3c1167f3564d43adb32f) )
	ROM_LOAD32_BYTE( "mpr13520.bin", 0x000002, 0x080000, CRC(3532e91a) SHA1(669c8d27b4b48e1ab9d6d30b0994f5a4e5169118) )
	ROM_LOAD32_BYTE( "mpr13521.bin", 0x000001, 0x080000, CRC(e9bca903) SHA1(18a73c830b9755262a1c525e3ad5ae084117b64d) )
	ROM_LOAD32_BYTE( "mpr13522.bin", 0x000003, 0x080000, CRC(25e04648) SHA1(617e794e8f7aa2a435bac917b8968699fe88dafb) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr13511.bin", 0x800000, 0x100000, CRC(f8f15b11) SHA1(da6c2b8c3a94c4c263583f046823eaea818aff7c) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13512.bin", 0x800001, 0x100000, CRC(d0be34a6) SHA1(b42a63e30f0f7a94de8a825ca93cf8efdb7a7648) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13513.bin", 0x800002, 0x100000, CRC(feef1982) SHA1(bdf906317079a12c48ef4fca5bef0d437e9bf050) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13514.bin", 0x800003, 0x100000, CRC(d0f9ebd1) SHA1(510ebd3d7a52bcab2debea61591770d1dff172a1) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13515.bin", 0x800004, 0x100000, CRC(77bf2387) SHA1(7215dde5618e238edbe16b3007ede790785fe25f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13516.bin", 0x800005, 0x100000, CRC(8c4bc62d) SHA1(3206f623ec0b7558413d063404103b183f26b488) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13517.bin", 0x800006, 0x100000, CRC(1d7d84a7) SHA1(954cfccfc7250a5bead2eeba42e655d5ac82955f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13518.bin", 0x800007, 0x100000, CRC(9ea4b15d) SHA1(7dcfd6d42bb945beca8344cf92e7bd53903a824b) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x8000, REGION_USER2, 0 ) /* unused (cabinet motor?) */
	ROM_LOAD( "epr13686.bin", 0x00000, 0x8000, CRC(317a2857) SHA1(e0788dc7a7d214d9c4d26b24e44c1a0dc9ae477c) ) /* cabinet movement */
ROM_END

ROM_START( radmu )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr13690.bin", 0x000000, 0x20000, CRC(21637dec) SHA1(b9921effb10a72f3bdca4d540149c7f46662b716) )
	ROM_RELOAD     (                 0x020000, 0x20000 )
	ROM_RELOAD     (                 0x040000, 0x20000 )
	ROM_RELOAD     (                 0x060000, 0x20000 )
	ROM_RELOAD     (                 0x080000, 0x20000 )
	ROM_RELOAD     (                 0x0a0000, 0x20000 )
	ROM_RELOAD     (                 0x0c0000, 0x20000 )
	ROM_RELOAD     (                 0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr13525.bin", 0x000000, 0x80000, CRC(62ad83a0) SHA1(b537176ebca15d91db04d5d7ab36aa967d41288e) )
	ROM_LOAD16_BYTE( "epr13526.bin", 0x000001, 0x80000, CRC(59ea372a) SHA1(e7a5d59586652c59c23e07e0a99ecc740fb6144d) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr13527.bin", 0x00000, 0x20000, CRC(a2e3fbbe) SHA1(2787bbef696ab3f2b7855ac991867837d3de54cd) )
	ROM_RELOAD(               0x100000, 0x020000             )
	ROM_LOAD( "epr13523.bin", 0x180000, 0x080000, CRC(d5563697) SHA1(eb3fd3dbfea383ac1bb5d2e1552723994cb4693d) )
	ROM_LOAD( "epr13523.bin", 0x280000, 0x080000, CRC(d5563697) SHA1(eb3fd3dbfea383ac1bb5d2e1552723994cb4693d) )
	ROM_LOAD( "epr13699.bin", 0x380000, 0x080000, CRC(33fd2913) SHA1(60b664559b4989446b1c7d875432e53a36fe27df) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "mpr13519.bin", 0x000000, 0x080000, CRC(bedc9534) SHA1(7b3f7a47b6c0ca6707dc3c1167f3564d43adb32f) )
	ROM_LOAD32_BYTE( "mpr13520.bin", 0x000002, 0x080000, CRC(3532e91a) SHA1(669c8d27b4b48e1ab9d6d30b0994f5a4e5169118) )
	ROM_LOAD32_BYTE( "mpr13521.bin", 0x000001, 0x080000, CRC(e9bca903) SHA1(18a73c830b9755262a1c525e3ad5ae084117b64d) )
	ROM_LOAD32_BYTE( "mpr13522.bin", 0x000003, 0x080000, CRC(25e04648) SHA1(617e794e8f7aa2a435bac917b8968699fe88dafb) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr13511.bin", 0x800000, 0x100000, CRC(f8f15b11) SHA1(da6c2b8c3a94c4c263583f046823eaea818aff7c) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13512.bin", 0x800001, 0x100000, CRC(d0be34a6) SHA1(b42a63e30f0f7a94de8a825ca93cf8efdb7a7648) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13513.bin", 0x800002, 0x100000, CRC(feef1982) SHA1(bdf906317079a12c48ef4fca5bef0d437e9bf050) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13514.bin", 0x800003, 0x100000, CRC(d0f9ebd1) SHA1(510ebd3d7a52bcab2debea61591770d1dff172a1) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13515.bin", 0x800004, 0x100000, CRC(77bf2387) SHA1(7215dde5618e238edbe16b3007ede790785fe25f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13516.bin", 0x800005, 0x100000, CRC(8c4bc62d) SHA1(3206f623ec0b7558413d063404103b183f26b488) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13517.bin", 0x800006, 0x100000, CRC(1d7d84a7) SHA1(954cfccfc7250a5bead2eeba42e655d5ac82955f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13518.bin", 0x800007, 0x100000, CRC(9ea4b15d) SHA1(7dcfd6d42bb945beca8344cf92e7bd53903a824b) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x8000, REGION_USER2, 0 ) /* unused (cabinet motor?) */
	ROM_LOAD( "epr13686.bin", 0x00000, 0x8000, CRC(317a2857) SHA1(e0788dc7a7d214d9c4d26b24e44c1a0dc9ae477c) ) /* cabinet movement */
ROM_END


/*****************************************
 *****************************************
  Rad Rally
 *****************************************
 *****************************************/

ROM_START( radr )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14241.06", 0x000000, 0x20000, CRC(59a5f63d) SHA1(325a26a09475ddc828de71e71a1d3043f3959cec) )
	ROM_RELOAD     (                0x020000, 0x20000 )
	ROM_RELOAD     (                0x040000, 0x20000 )
	ROM_RELOAD     (                0x060000, 0x20000 )
	ROM_RELOAD     (                0x080000, 0x20000 )
	ROM_RELOAD     (                0x0a0000, 0x20000 )
	ROM_RELOAD     (                0x0c0000, 0x20000 )
	ROM_RELOAD     (                0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr14106.14", 0x000000, 0x80000, CRC(e73c63bf) SHA1(30fb68eaa7d02a232c873bd7751cac7d0fa08e44) )
	ROM_LOAD16_BYTE( "epr14107.07", 0x000001, 0x80000, CRC(832f797a) SHA1(b0c16ef7bd8d37f592975052ba9da3da70a2fc79) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr14108.35", 0x00000, 0x20000, CRC(38a99b4d) SHA1(b6455e6b29bfef41c5e0ebe3a8064889b7e5f5fd) )
	ROM_RELOAD(              0x100000, 0x20000             )
	ROM_LOAD( "epr14109.31", 0x180000, 0x080000, CRC(b42e5833) SHA1(da94ce7c1d7a581a1aa6b79b323c67a919918808) )
	ROM_LOAD( "epr14110.26", 0x280000, 0x080000, CRC(b495e7dc) SHA1(b4143fcee10e0649378fdb1e3f5a0a2c585414ec) )
	ROM_LOAD( "epr14237.22", 0x380000, 0x080000, CRC(0a4b4b29) SHA1(98447a587f903ba03e17d6a145b7c8bfddf25c4d) )

	ROM_REGION( 0x100000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "epr14102.38", 0x000000, 0x040000, CRC(5626e80f) SHA1(9844817295a8cd8a9b09da6681b0c1fbfe82618e) )
	ROM_LOAD32_BYTE( "epr14103.34", 0x000002, 0x040000, CRC(08c7e804) SHA1(cf45b1934edc43cb3a0ed72159949cb0dd00d701) )
	ROM_LOAD32_BYTE( "epr14104.29", 0x000001, 0x040000, CRC(b0173646) SHA1(1ba4edc033e0e4f5a1e02987e9f6b8b1650b46d7) )
	ROM_LOAD32_BYTE( "epr14105.25", 0x000003, 0x040000, CRC(614843b6) SHA1(d4f2cd3b024f7152d6e89237f0da06adea2efe57) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr13511.36", 0x800000, 0x100000, CRC(f8f15b11) SHA1(da6c2b8c3a94c4c263583f046823eaea818aff7c) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13512.32", 0x800001, 0x100000, CRC(d0be34a6) SHA1(b42a63e30f0f7a94de8a825ca93cf8efdb7a7648) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13513.27", 0x800002, 0x100000, CRC(feef1982) SHA1(bdf906317079a12c48ef4fca5bef0d437e9bf050) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13514.23", 0x800003, 0x100000, CRC(d0f9ebd1) SHA1(510ebd3d7a52bcab2debea61591770d1dff172a1) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13515.37", 0x800004, 0x100000, CRC(77bf2387) SHA1(7215dde5618e238edbe16b3007ede790785fe25f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13516.33", 0x800005, 0x100000, CRC(8c4bc62d) SHA1(3206f623ec0b7558413d063404103b183f26b488) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13517.28", 0x800006, 0x100000, CRC(1d7d84a7) SHA1(954cfccfc7250a5bead2eeba42e655d5ac82955f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13518.24", 0x800007, 0x100000, CRC(9ea4b15d) SHA1(7dcfd6d42bb945beca8344cf92e7bd53903a824b) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x8000, REGION_USER2, 0 ) /* unused */
	ROM_LOAD( "epr14084.17", 0x00000, 0x8000, CRC(f14ed074) SHA1(e1bb23eac85e3236046527c5c7688f6f23d43aef) ) /* cabinet link */
ROM_END

ROM_START( radru )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "epr14240.bin", 0x000000, 0x20000, CRC(8473e7ab) SHA1(fbd883dc804d7de6ce239b68c6e6ae3a54e9e03c) )
	ROM_RELOAD     (                0x020000, 0x20000 )
	ROM_RELOAD     (                0x040000, 0x20000 )
	ROM_RELOAD     (                0x060000, 0x20000 )
	ROM_RELOAD     (                0x080000, 0x20000 )
	ROM_RELOAD     (                0x0a0000, 0x20000 )
	ROM_RELOAD     (                0x0c0000, 0x20000 )
	ROM_RELOAD     (                0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr14106.14", 0x000000, 0x80000, CRC(e73c63bf) SHA1(30fb68eaa7d02a232c873bd7751cac7d0fa08e44) )
	ROM_LOAD16_BYTE( "epr14107.07", 0x000001, 0x80000, CRC(832f797a) SHA1(b0c16ef7bd8d37f592975052ba9da3da70a2fc79) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr14108.35", 0x00000, 0x20000, CRC(38a99b4d) SHA1(b6455e6b29bfef41c5e0ebe3a8064889b7e5f5fd) )
	ROM_RELOAD(              0x100000, 0x20000             )
	ROM_LOAD( "epr14109.31", 0x180000, 0x080000, CRC(b42e5833) SHA1(da94ce7c1d7a581a1aa6b79b323c67a919918808) )
	ROM_LOAD( "epr14110.26", 0x280000, 0x080000, CRC(b495e7dc) SHA1(b4143fcee10e0649378fdb1e3f5a0a2c585414ec) )
	ROM_LOAD( "epr14237.22", 0x380000, 0x080000, CRC(0a4b4b29) SHA1(98447a587f903ba03e17d6a145b7c8bfddf25c4d) )

	ROM_REGION( 0x100000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "epr14102.38", 0x000000, 0x040000, CRC(5626e80f) SHA1(9844817295a8cd8a9b09da6681b0c1fbfe82618e) )
	ROM_LOAD32_BYTE( "epr14103.34", 0x000002, 0x040000, CRC(08c7e804) SHA1(cf45b1934edc43cb3a0ed72159949cb0dd00d701) )
	ROM_LOAD32_BYTE( "epr14104.29", 0x000001, 0x040000, CRC(b0173646) SHA1(1ba4edc033e0e4f5a1e02987e9f6b8b1650b46d7) )
	ROM_LOAD32_BYTE( "epr14105.25", 0x000003, 0x040000, CRC(614843b6) SHA1(d4f2cd3b024f7152d6e89237f0da06adea2efe57) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr13511.36", 0x800000, 0x100000, CRC(f8f15b11) SHA1(da6c2b8c3a94c4c263583f046823eaea818aff7c) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13512.32", 0x800001, 0x100000, CRC(d0be34a6) SHA1(b42a63e30f0f7a94de8a825ca93cf8efdb7a7648) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13513.27", 0x800002, 0x100000, CRC(feef1982) SHA1(bdf906317079a12c48ef4fca5bef0d437e9bf050) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13514.23", 0x800003, 0x100000, CRC(d0f9ebd1) SHA1(510ebd3d7a52bcab2debea61591770d1dff172a1) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13515.37", 0x800004, 0x100000, CRC(77bf2387) SHA1(7215dde5618e238edbe16b3007ede790785fe25f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13516.33", 0x800005, 0x100000, CRC(8c4bc62d) SHA1(3206f623ec0b7558413d063404103b183f26b488) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13517.28", 0x800006, 0x100000, CRC(1d7d84a7) SHA1(954cfccfc7250a5bead2eeba42e655d5ac82955f) , ROM_SKIP(7) )
	ROMX_LOAD( "mpr13518.24", 0x800007, 0x100000, CRC(9ea4b15d) SHA1(7dcfd6d42bb945beca8344cf92e7bd53903a824b) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */

	ROM_REGION( 0x8000, REGION_USER2, 0 ) /* unused */
	ROM_LOAD( "epr14084.17", 0x00000, 0x8000, CRC(f14ed074) SHA1(e1bb23eac85e3236046527c5c7688f6f23d43aef) ) /* cabinet link */
ROM_END

/*****************************************
 *****************************************
  Slipstream
 *****************************************
 *****************************************
  doesn't work, bad dump?
 *****************************************/


ROM_START( slipstrm )
	ROM_REGION( 0x200000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD16_WORD( "slipstrm.u6", 0x000000, 0x80000, BAD_DUMP CRC(7d066307) SHA1(d87e04167263b435b77830db02ed58651ccc020c) )
	ROM_RELOAD     (               0x080000, 0x80000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "slipstrm.u14",0x000000, 0x80000, BAD_DUMP CRC(c3ff6309) SHA1(dcc857736fe0f15aa7909c3ee88a7e239c8f0228) )
	ROM_LOAD16_BYTE( "slipstrm.u7", 0x000001, 0x80000, BAD_DUMP CRC(0e605c81) SHA1(47c64195cab9a07b234d5a375d26168e53ffaa17) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "slipstrm.u35", 0x00000, 0x20000, BAD_DUMP CRC(0fee2278) SHA1(7533a03c3fc46d65dfdd07bddf1e6e0bbc368752) )
	ROM_RELOAD(              0x100000, 0x20000             )
	ROM_LOAD( "slipstrm.u31", 0x180000, 0x080000, BAD_DUMP CRC(ae7be5f2) SHA1(ba089355e64864435bcc3b0c208e4bce1ea66295) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "slipstrm.u38", 0x000000, 0x080000, CRC(3cbb2d0b) SHA1(b94006347b72cd60a889b0e279f62f677cedfd2e) )
	ROM_LOAD32_BYTE( "slipstrm.u34", 0x000002, 0x080000, CRC(4167be55) SHA1(96b34d311b318c00c3fad917e341589a70ba0a15) )
	ROM_LOAD32_BYTE( "slipstrm.u29", 0x000001, 0x080000, CRC(52c4bb85) SHA1(4fbee1072a19c75c25b5fd269acc75640923d69c) )
	ROM_LOAD32_BYTE( "slipstrm.u25", 0x000003, 0x080000, CRC(4948604a) SHA1(d5a1b9781fef7976a59a0af9b755a04fcacf9381) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "slipstrm.u36", 0x800000, 0x80000, CRC(cffe9e0d) SHA1(5272d54ff142de927a9abd61f3646e963c7d22c4) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u32", 0x800001, 0x80000, CRC(4ebd1383) SHA1(ce35f4d15e7904bfde55e58cdde925cba8002763) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u27", 0x800002, 0x80000, CRC(b3cf4fe2) SHA1(e13199522e1e3e8b9cfe72cc29b33f25dad542ef) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u23", 0x800003, 0x80000, CRC(c6345391) SHA1(155758097911ffca0c5c0b2a24a8033339dcfcbb) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u37", 0x800004, 0x80000, CRC(2de4288e) SHA1(8e794f79f506293edb7609187a7908516ce76849) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u33", 0x800005, 0x80000, CRC(6cfb74fb) SHA1(b74c886959910cd069427418525b23300a9b7b18) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u28", 0x800006, 0x80000, CRC(53234bf4) SHA1(1eca538dcb86e44c31310ab1ab42a2b66b69c8fe) , ROM_SKIP(7) )
	ROMX_LOAD( "slipstrm.u24", 0x800007, 0x80000, CRC(22c129cf) SHA1(0f64680511a357038f6a556253c13fbb5417dd1a) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  SegaSonic Arcade
 *****************************************
 *****************************************/

ROM_START( sonic )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "epr-c-87.17", 0x000000, 0x020000, CRC(25e3c27e) SHA1(8f173cd5c7c817dcccdcad9be5781cfaa081d73e) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD( "epr-c-86.8", 0x080000, 0x020000, CRC(efe9524c) SHA1(8020e734704a8f989919ee5ad92f70035de717f0) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr-c-81.18", 0x000000, 0x040000, CRC(65b06c25) SHA1(9f524012a7adbc71737f90fc556f0ce9adc2bcf8) )
	ROM_LOAD16_BYTE( "epr-c-80.9",  0x000001, 0x040000, CRC(2db66fd2) SHA1(54582c0d5977649a38fc3a2c0fe4d7b1959abc76) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr15785.36", 0x00000, 0x40000, CRC(0fe7422e) SHA1(b7eaf4736ba155965317bb4ef3b33fc122635151) )
	ROM_RELOAD(              0x100000, 0x40000             )
	ROM_LOAD( "mpr15784.35", 0x180000, 0x100000, CRC(42f06714) SHA1(30e45bb2d9b492f0c1acc4fbe1e5869f0559300b) )
	ROM_LOAD( "mpr15782.33", 0x280000, 0x100000, CRC(cf56b5a0) SHA1(5786228aab120c3361524ba93b418b24fd5b8ffb) ) // (this is the only rom unchanged from the prototype)
	ROM_LOAD( "mpr15783.34", 0x380000, 0x100000, CRC(e4220eea) SHA1(a546c8bfc24e0695cf79c49e1a867d2595a1ed7f) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr15789.14", 0x000000, 0x100000, CRC(4378f12b) SHA1(826e0550a3c5f2b6e59c6531ac03658a4f826651) )
	ROM_LOAD16_BYTE( "mpr15788.5",  0x000001, 0x100000, CRC(a6ed5d7a) SHA1(d30f26b452d380e7657e044e144f7dbbc4dc13e5) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr15790.32", 0x000000, 0x200000, CRC(c69d51b1) SHA1(7644fb64457855f9ed87ca25ddc28c21bcb61fd9) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15792.30", 0x000002, 0x200000, CRC(1006bb67) SHA1(38c752e634aa94b1a23c09c4dba6388b7d0358af) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15794.28", 0x000004, 0x200000, CRC(8672b480) SHA1(61659e3856cdff0b2bca190a7e60c81b86ea2089) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15796.26", 0x000006, 0x200000, CRC(95b8011c) SHA1(ebc56ae49a76d04de60b0f81506769219a9885a7) , ROM_SKIP(6)|ROM_GROUPWORD )

	// NOTE: these last 4 are in fact 16 megabit ROMs,
	// but they were dumped as 8 because the top half
	// is "FF" in all of them.
	ROMX_LOAD( "mpr15791.31", 0x800000, 0x100000, CRC(42217066) SHA1(46d14c632da1bed02bc0a637e34ab9cbf356c5de) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15793.29", 0x800002, 0x100000, CRC(75bafe55) SHA1(ad33dae062c4bdf8d17d3f6f7c333aa2e7da260e) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15795.27", 0x800004, 0x100000, CRC(7f3dad30) SHA1(84be1c31df35e1c7fef77e83d6d135378789a1ef) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr15797.25", 0x800006, 0x100000, CRC(013c6cab) SHA1(eb9b77d28815d2e225b0882706084a52b11c48ea) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( sonicp )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "sonpg0.bin", 0x000000, 0x020000, CRC(da05dcbb) SHA1(c2ced1f3aee92b0e531d5cd7611d4811f2ae95e7) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD( "sonpg1.bin", 0x080000, 0x020000, CRC(c57dc5c5) SHA1(5741bdd52ee7181d883129885838b36f4af8a04c) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "sonpd0.bin", 0x000000, 0x040000, CRC(a7da7546) SHA1(0a10573b21cd38d58380698bc18b0256dbb24044) )
	ROM_LOAD16_BYTE( "sonpd1.bin", 0x000001, 0x040000, CRC(c30e4c70) SHA1(897b6f62921694fe3c63677908f76eaf38b7b92f) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "sonsnd0.bin", 0x00000, 0x40000, CRC(569c8d4b) SHA1(9f1f6da6adbea043cc5ad853806fcb7bf683c832) )
	ROM_RELOAD(              0x100000, 0x40000             )
	ROM_LOAD( "sonsnd1.bin", 0x180000, 0x100000, CRC(f4fa5a21) SHA1(14a364ba7744ff0b44423d8d6bab990fe534ff29) )
	ROM_LOAD( "sonsnd3.bin", 0x280000, 0x100000, CRC(cf56b5a0) SHA1(5786228aab120c3361524ba93b418b24fd5b8ffb) )
	ROM_LOAD( "sonsnd2.bin", 0x380000, 0x100000, CRC(e1bd45a5) SHA1(b411757853d61588e5223b48b5124cc00b3d65dd) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "sonscl0.bin", 0x000000, 0x080000, CRC(445e31b9) SHA1(5678dfda74a09b5ac673448b222d11df4ca23aff) )
	ROM_LOAD32_BYTE( "sonscl1.bin", 0x000002, 0x080000, CRC(3d234181) SHA1(2e8c14ad36be76f5f5fc6a3ee152f1abc8bf0ddd) )
	ROM_LOAD32_BYTE( "sonscl2.bin", 0x000001, 0x080000, CRC(a5de28b2) SHA1(49a16ac10cf01b5b8802b8b015a2e403086c206a) )
	ROM_LOAD32_BYTE( "sonscl3.bin", 0x000003, 0x080000, CRC(7ce7554b) SHA1(8def3acae6baafbe9e350f18e245a9a833df5cc4) )

	ROM_REGION( 0x1000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "sonobj0.bin", 0x800000, 0x100000, CRC(ceea18e3) SHA1(f902a7e2f8e126fd7a7862c55de32ce6352a7716) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj1.bin", 0x800001, 0x100000, CRC(6bbc226b) SHA1(5ef4256b6a93891daf1349def6db3bc428e5f4f3) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj2.bin", 0x800002, 0x100000, CRC(fcd5ef0e) SHA1(e3e50d4838ac3cce41d69ee6cd31981fbe422a4b) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj3.bin", 0x800003, 0x100000, CRC(b99b42ab) SHA1(60d91dc4e8e0adc62809cd2e71833c658124fbfc) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj4.bin", 0x800004, 0x100000, CRC(c7ec1456) SHA1(d866b9dff546bd6feb43e317328ac0a2324303b9) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj5.bin", 0x800005, 0x100000, CRC(bd5da27f) SHA1(ab3043190a32b555513a29a70e01547daf698cf8) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj6.bin", 0x800006, 0x100000, CRC(313c92d1) SHA1(a5134750667502811fd755cc0974a744cdb785e1) , ROM_SKIP(7) )
	ROMX_LOAD( "sonobj7.bin", 0x800007, 0x100000, CRC(3784c507) SHA1(8ea58c52b09b84643218e26f1ec1fa0ea864346e) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  Spiderman : The Videogame
 *****************************************
 *****************************************/


ROM_START( spidey )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "14307", 0x000000, 0x020000, CRC(d900219c) SHA1(d59654db1fc0ec4d5f8cda9000ab4bd3bb36cdfc) )
	ROM_RELOAD (       0x020000, 0x020000 )
	ROM_RELOAD (       0x040000, 0x020000 )
	ROM_RELOAD (       0x060000, 0x020000 )
	ROM_LOAD( "14306", 0x080000, 0x020000, CRC(64379dc6) SHA1(7efc7175351186c54f141161a395e63b1cc7e7a5) )
	ROM_RELOAD (       0x0a0000, 0x020000 )
	ROM_RELOAD (       0x0c0000, 0x020000 )
	ROM_RELOAD (       0x0e0000, 0x020000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "14281", 0x000000, 0x020000, CRC(8a746c42) SHA1(fa3729ec3aa4b3c59322408146ce2cfbf5a11b98) )
	ROM_LOAD16_BYTE( "14280", 0x000001, 0x020000, CRC(3c8148f7) SHA1(072b7982bb95e7a9ab77844b59020146c262488d) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "14285", 0x00000, 0x40000, CRC(25aefad6) SHA1(10153f4e773a0f55378f869eb1d85156e85f893f) )
	ROM_RELOAD(        0x100000, 0x40000             )
	ROM_LOAD( "14284", 0x180000, 0x080000, CRC(760542d4) SHA1(dcac73869c02fefd328bd6bdbcbdb3b68b0647da) )
	ROM_LOAD( "14282", 0x280000, 0x080000, CRC(ea20979e) SHA1(9b70ef055da8c7c56da54b7edef2379678e7c50f) )
	ROM_LOAD( "14283", 0x380000, 0x080000, CRC(c863a91c) SHA1(afdc76bbb9b207cfcb47d437248a757d03212f4e) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "14291", 0x000000, 0x100000, CRC(490f95a1) SHA1(f220788670b76164ac414ed9b16a422f719be267) )
	ROM_LOAD32_BYTE( "14290", 0x000002, 0x100000, CRC(a144162d) SHA1(d43f12dd9f690cdfcebb6c7b515ff7dc7dcaa377) )
	ROM_LOAD32_BYTE( "14289", 0x000001, 0x100000, CRC(38570582) SHA1(a9d810a02a1f5a6849c79d65fbebff21a4b82b59) )
	ROM_LOAD32_BYTE( "14288", 0x000003, 0x100000, CRC(3188b636) SHA1(bc0adeeca5040caa563ee1e0eded9c323ca23446) )

	ROM_REGION( 0x0800000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "14299", 0x000000, 0x100000, CRC(ce59231b) SHA1(bcb1f11b74935694d0617ec8df66db2cc57b6219) , ROM_SKIP(7) )
	ROMX_LOAD( "14298", 0x000001, 0x100000, CRC(2745c84c) SHA1(5a0528c921cba7a1047d3a2ece79925103d719a1) , ROM_SKIP(7) )
	ROMX_LOAD( "14297", 0x000002, 0x100000, CRC(29cb9450) SHA1(7dc38d23a2f0cee2f4edde05c1a6f0dc83f331db) , ROM_SKIP(7) )
	ROMX_LOAD( "14296", 0x000003, 0x100000, CRC(9d8cbd31) SHA1(55a9f9ec9029157da033e69664b58e694a28db47) , ROM_SKIP(7) )
	ROMX_LOAD( "14295", 0x000004, 0x100000, CRC(29591f50) SHA1(1ac4ceaf74892e30f210ad77024eb441c5e4a959) , ROM_SKIP(7) )
	ROMX_LOAD( "14294", 0x000005, 0x100000, CRC(fa86b794) SHA1(7b6907e5734fbf2fba7bcc213a8551fec5e9f3d5) , ROM_SKIP(7) )
	ROMX_LOAD( "14293", 0x000006, 0x100000, CRC(52899269) SHA1(ff809ff88701109e0ca79e785a61402d97335cec) , ROM_SKIP(7) )
	ROMX_LOAD( "14292", 0x000007, 0x100000, CRC(41f71443) SHA1(351d40d6159cb5b792519bce5d16490965800cfb) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( spideyu )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "14303", 0x000000, 0x020000, CRC(7f1bd28f) SHA1(cff57e66d09682baf44aace99d698ad305f6a3d5) )
	ROM_RELOAD (       0x020000, 0x020000 )
	ROM_RELOAD (       0x040000, 0x020000 )
	ROM_RELOAD (       0x060000, 0x020000 )
	ROM_LOAD( "14302", 0x080000, 0x020000, CRC(d954c40a) SHA1(436c81779274861de79dc6ce2c0fcc65bfd52098) )
	ROM_RELOAD (       0x0a0000, 0x020000 )
	ROM_RELOAD (       0x0c0000, 0x020000 )
	ROM_RELOAD (       0x0e0000, 0x020000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "14281", 0x000000, 0x020000, CRC(8a746c42) SHA1(fa3729ec3aa4b3c59322408146ce2cfbf5a11b98) )
	ROM_LOAD16_BYTE( "14280", 0x000001, 0x020000, CRC(3c8148f7) SHA1(072b7982bb95e7a9ab77844b59020146c262488d) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "14285", 0x00000, 0x40000, CRC(25aefad6) SHA1(10153f4e773a0f55378f869eb1d85156e85f893f) )
	ROM_RELOAD(        0x100000, 0x40000             )
	ROM_LOAD( "14284", 0x180000, 0x080000, CRC(760542d4) SHA1(dcac73869c02fefd328bd6bdbcbdb3b68b0647da) )
	ROM_LOAD( "14282", 0x280000, 0x080000, CRC(ea20979e) SHA1(9b70ef055da8c7c56da54b7edef2379678e7c50f) )
	ROM_LOAD( "14283", 0x380000, 0x080000, CRC(c863a91c) SHA1(afdc76bbb9b207cfcb47d437248a757d03212f4e) )

	ROM_REGION( 0x400000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD32_BYTE( "14291", 0x000000, 0x100000, CRC(490f95a1) SHA1(f220788670b76164ac414ed9b16a422f719be267) )
	ROM_LOAD32_BYTE( "14290", 0x000002, 0x100000, CRC(a144162d) SHA1(d43f12dd9f690cdfcebb6c7b515ff7dc7dcaa377) )
	ROM_LOAD32_BYTE( "14289", 0x000001, 0x100000, CRC(38570582) SHA1(a9d810a02a1f5a6849c79d65fbebff21a4b82b59) )
	ROM_LOAD32_BYTE( "14288", 0x000003, 0x100000, CRC(3188b636) SHA1(bc0adeeca5040caa563ee1e0eded9c323ca23446) )

	ROM_REGION( 0x0800000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "14299", 0x000000, 0x100000, CRC(ce59231b) SHA1(bcb1f11b74935694d0617ec8df66db2cc57b6219) , ROM_SKIP(7) )
	ROMX_LOAD( "14298", 0x000001, 0x100000, CRC(2745c84c) SHA1(5a0528c921cba7a1047d3a2ece79925103d719a1) , ROM_SKIP(7) )
	ROMX_LOAD( "14297", 0x000002, 0x100000, CRC(29cb9450) SHA1(7dc38d23a2f0cee2f4edde05c1a6f0dc83f331db) , ROM_SKIP(7) )
	ROMX_LOAD( "14296", 0x000003, 0x100000, CRC(9d8cbd31) SHA1(55a9f9ec9029157da033e69664b58e694a28db47) , ROM_SKIP(7) )
	ROMX_LOAD( "14295", 0x000004, 0x100000, CRC(29591f50) SHA1(1ac4ceaf74892e30f210ad77024eb441c5e4a959) , ROM_SKIP(7) )
	ROMX_LOAD( "14294", 0x000005, 0x100000, CRC(fa86b794) SHA1(7b6907e5734fbf2fba7bcc213a8551fec5e9f3d5) , ROM_SKIP(7) )
	ROMX_LOAD( "14293", 0x000006, 0x100000, CRC(52899269) SHA1(ff809ff88701109e0ca79e785a61402d97335cec) , ROM_SKIP(7) )
	ROMX_LOAD( "14292", 0x000007, 0x100000, CRC(41f71443) SHA1(351d40d6159cb5b792519bce5d16490965800cfb) , ROM_SKIP(7) )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

/*****************************************
 *****************************************
  Super Visual Football /
    Super Visual Soccer   /
      The JLeague '94
 *****************************************
 *****************************************

 Jleague 94 is protected, the others are
 not

 ****************************************/

ROM_START( svf )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "ep16872a.17", 0x000000, 0x020000, CRC(1f383b00) SHA1(c3af01743de5ff09ada19879902842efdbceb595) )
	ROM_RELOAD     (               0x020000, 0x20000 )
	ROM_RELOAD     (               0x040000, 0x20000 )
	ROM_RELOAD     (               0x060000, 0x20000 )
	ROM_LOAD( "ep16871a.8", 0x080000, 0x020000, CRC(f7061bd7) SHA1(b46f4f2ecda8f521c0a91f2f2c2445b72cbc2874) )
	ROM_RELOAD     (               0x0a0000, 0x20000 )
	ROM_RELOAD     (               0x0c0000, 0x20000 )
	ROM_RELOAD     (               0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr16865.18", 0x000000, 0x080000, CRC(9198ca9f) SHA1(0f6271ce8a07e4ab7fdce38964055510f2ebfd4e) )
	ROM_LOAD16_BYTE( "epr16864.9", 0x000001, 0x080000, CRC(201a940e) SHA1(e19d76141844dbdedee0698ea50edbb898ab55e9) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr16866.36", 0x00000, 0x20000, CRC(74431350) SHA1(d3208b595423b5b0f25ee90db213112a09906f8f) )
	ROM_RELOAD(           0x100000, 0x20000             )
	ROM_LOAD( "mpr16779.35", 0x180000, 0x100000, CRC(7055e859) SHA1(cde27fa4aaf0ee54063ee68794e9a6075581fff5) )
	ROM_LOAD( "mpr16777.24", 0x280000, 0x100000, CRC(14b5d5df) SHA1(1b0b0a31294b1bbc16d2046b374d584a1b00a78c) )
	ROM_LOAD( "mpr16778.34", 0x380000, 0x100000, CRC(feedaecf) SHA1(25c14ccb85c467dc0c8e85b61f8f86f4396c0cc7) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr16784.14", 0x000000, 0x100000, CRC(4608efe2) SHA1(9b41aa28f50af770e854ef9fdff1a55da7b7b131) )
	ROM_LOAD16_BYTE( "mpr16783.5", 0x000001, 0x100000, CRC(042eabe7) SHA1(a11df5c21d85f0c96dbdcaf57be37a79658ad648) )

	ROM_REGION( 0x2000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr16785.32", 0x000000, 0x200000, CRC(51f775ce) SHA1(125b40bf47304d37b92e81df5081c81d9af6c8a2) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16787.30", 0x000002, 0x200000, CRC(dee7a204) SHA1(29acff4d5dd68609ac46853860788206d18262ab) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16789.28", 0x000004, 0x200000, CRC(6b6c8ad3) SHA1(97b0078c851845c31dcf0fe4b2a88393dcdf8988) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16791.26", 0x000006, 0x200000, CRC(4f7236da) SHA1(d1c29f6aa82d60a626217f1430bc8a76ef012007) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16860.31", 0x800000, 0x200000, CRC(578a7325) SHA1(75a066841fa24952d8ed5ac2d988609295f437a8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16861.29", 0x800002, 0x200000, CRC(d79c3f73) SHA1(e4360efb0964a92cfad8c458a5568709fcc81339) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16862.27", 0x800004, 0x200000, CRC(00793354) SHA1(3b37a89c5100d5f92a3567fc8d2003bc9d6fe0cd) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16863.25", 0x800006, 0x200000, CRC(42338226) SHA1(106636408d5648fb95fbaee06074c57f6a535a82) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( svs )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "ep16883a.17", 0x000000, 0x020000, CRC(e1c0c3ce) SHA1(12dd8d9d1a2c2c7bf1ab652a6a6f947384d79577) )
	ROM_RELOAD	   (			   0x020000, 0x20000 )
	ROM_RELOAD	   (			   0x040000, 0x20000 )
	ROM_RELOAD	   (			   0x060000, 0x20000 )
	ROM_LOAD( "ep16882a.8", 0x080000, 0x020000, CRC(1161bbbe) SHA1(3cfeed9ea947eed79aeb5674d54de45d15fb6e1f) )
	ROM_RELOAD	   (			   0x0a0000, 0x20000 )
	ROM_RELOAD	   (			   0x0c0000, 0x20000 )
	ROM_RELOAD	   (			   0x0e0000, 0x20000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr16865.18", 0x000000, 0x080000, CRC(9198ca9f) SHA1(0f6271ce8a07e4ab7fdce38964055510f2ebfd4e) )
	ROM_LOAD16_BYTE( "epr16864.9", 0x000001, 0x080000, CRC(201a940e) SHA1(e19d76141844dbdedee0698ea50edbb898ab55e9) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr16868.36", 0x00000, 0x40000, CRC(47aa4ec7) SHA1(baea18aaac0314f769f1e36fdbe8aedf62862544) ) /* same as jleague but with a different part number */
	ROM_RELOAD(           0x100000, 0x20000             )
	ROM_LOAD( "mpr16779.35", 0x180000, 0x100000, CRC(7055e859) SHA1(cde27fa4aaf0ee54063ee68794e9a6075581fff5) )
	ROM_LOAD( "mpr16777.24", 0x280000, 0x100000, CRC(14b5d5df) SHA1(1b0b0a31294b1bbc16d2046b374d584a1b00a78c) )
	ROM_LOAD( "mpr16778.34", 0x380000, 0x100000, CRC(feedaecf) SHA1(25c14ccb85c467dc0c8e85b61f8f86f4396c0cc7) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr16784.14", 0x000000, 0x100000, CRC(4608efe2) SHA1(9b41aa28f50af770e854ef9fdff1a55da7b7b131) )
	ROM_LOAD16_BYTE( "mpr16783.5", 0x000001, 0x100000, CRC(042eabe7) SHA1(a11df5c21d85f0c96dbdcaf57be37a79658ad648) )

	ROM_REGION( 0x2000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr16785.32", 0x000000, 0x200000, CRC(51f775ce) SHA1(125b40bf47304d37b92e81df5081c81d9af6c8a2) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16787.30", 0x000002, 0x200000, CRC(dee7a204) SHA1(29acff4d5dd68609ac46853860788206d18262ab) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16789.28", 0x000004, 0x200000, CRC(6b6c8ad3) SHA1(97b0078c851845c31dcf0fe4b2a88393dcdf8988) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16791.26", 0x000006, 0x200000, CRC(4f7236da) SHA1(d1c29f6aa82d60a626217f1430bc8a76ef012007) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16860.31", 0x800000, 0x200000, CRC(578a7325) SHA1(75a066841fa24952d8ed5ac2d988609295f437a8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16861.29", 0x800002, 0x200000, CRC(d79c3f73) SHA1(e4360efb0964a92cfad8c458a5568709fcc81339) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16862.27", 0x800004, 0x200000, CRC(00793354) SHA1(3b37a89c5100d5f92a3567fc8d2003bc9d6fe0cd) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16863.25", 0x800006, 0x200000, CRC(42338226) SHA1(106636408d5648fb95fbaee06074c57f6a535a82) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END

ROM_START( jleague )
	ROM_REGION( 0x100000, REGION_CPU1, 0 ) /* v60 code */
	ROM_LOAD( "epr16782.17",0x000000, 0x020000, CRC(f0278944) SHA1(49e3842231ee5abdd6205b598309153d6b4ddc02) )
	ROM_RELOAD     (        0x020000, 0x020000 )
	ROM_RELOAD     (        0x040000, 0x020000 )
	ROM_RELOAD     (        0x060000, 0x020000 )
	ROM_LOAD( "epr16781.8", 0x080000, 0x020000, CRC(7df9529b) SHA1(de3633f4941ff3877c4cb8b53e080eccea19f22e) )
	ROM_RELOAD     (        0x0a0000, 0x020000 )
	ROM_RELOAD     (        0x0c0000, 0x020000 )
	ROM_RELOAD     (        0x0e0000, 0x020000 )

	ROM_REGION( 0x100000, REGION_USER1, 0 ) /* v60 data */
	ROM_LOAD16_BYTE( "epr16776.18", 0x000000, 0x080000, CRC(e8694626) SHA1(d4318a9a6b1cc5c719bff9c25b7398dd2ea1e18b) )
	ROM_LOAD16_BYTE( "epr16775.9",  0x000001, 0x080000, CRC(e81e2c3d) SHA1(2900710f1dec6cf71875c82a56584ba45ed3a545) )

	ROM_REGION( 0x480000, REGION_CPU2, 0 ) /* sound CPU */
	ROM_LOAD( "epr16780.36", 0x00000, 0x40000, CRC(47aa4ec7) SHA1(baea18aaac0314f769f1e36fdbe8aedf62862544) ) /* can probably be cut */
	ROM_RELOAD(           0x100000, 0x40000             )
	ROM_LOAD( "mpr16779.35", 0x180000, 0x100000, CRC(7055e859) SHA1(cde27fa4aaf0ee54063ee68794e9a6075581fff5) )
	ROM_LOAD( "mpr16777.24", 0x280000, 0x100000, CRC(14b5d5df) SHA1(1b0b0a31294b1bbc16d2046b374d584a1b00a78c) )
	ROM_LOAD( "mpr16778.34", 0x380000, 0x100000, CRC(feedaecf) SHA1(25c14ccb85c467dc0c8e85b61f8f86f4396c0cc7) )

	ROM_REGION( 0x200000, REGION_GFX1, ROMREGION_DISPOSE ) /* tiles */
	ROM_LOAD16_BYTE( "mpr16784.14", 0x000000, 0x100000, CRC(4608efe2) SHA1(9b41aa28f50af770e854ef9fdff1a55da7b7b131) )
	ROM_LOAD16_BYTE( "mpr16783.5", 0x000001, 0x100000, CRC(042eabe7) SHA1(a11df5c21d85f0c96dbdcaf57be37a79658ad648) )

	ROM_REGION( 0x2000000, REGION_GFX2, 0 ) /* sprites */
	ROMX_LOAD( "mpr16785.32", 0x000000, 0x200000, CRC(51f775ce) SHA1(125b40bf47304d37b92e81df5081c81d9af6c8a2) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16787.30", 0x000002, 0x200000, CRC(dee7a204) SHA1(29acff4d5dd68609ac46853860788206d18262ab) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16789.28", 0x000004, 0x200000, CRC(6b6c8ad3) SHA1(97b0078c851845c31dcf0fe4b2a88393dcdf8988) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mpr16791.26", 0x000006, 0x200000, CRC(4f7236da) SHA1(d1c29f6aa82d60a626217f1430bc8a76ef012007) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16786.31",  0x800000, 0x200000, CRC(d08a2d32) SHA1(baac63cbacbe38e057684174040384a7152eb523) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16788.29",  0x800002, 0x200000, CRC(cd9d3605) SHA1(7c4402be1a1ddde6766cfdd5fe7e2a088c4a59e8) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16790.27",  0x800004, 0x200000, CRC(2ea75746) SHA1(c91e5d678917886cc23fbef7a577c5a70665c7b2) , ROM_SKIP(6)|ROM_GROUPWORD )
	ROMX_LOAD( "mp16792.25",  0x800006, 0x200000, CRC(9f416072) SHA1(18107cf64f918888aa5a54432f8e9137910101b8) , ROM_SKIP(6)|ROM_GROUPWORD )

	ROM_REGION( 0x20000, REGION_GFX3, 0 ) /* FG tiles */
	/* populated at runtime */
ROM_END


/******************************************************************************
 ******************************************************************************
  Driver Initialization
 ******************************************************************************
 ******************************************************************************/

static struct
{
	const char *s_name;
	read16_handler custom_io_r;
	write16_handler custom_io_w;
	int default_eeprom;
	int mixershift;
	int system32_allow_high_resolution;
	int system32_temp_kludge;
} segas32_config_table[] =
{
	/* shortname,  custom IO Read       , Custom IO Write      , Default EEprom    , Mixershift, Allow Hi-Res? , Kludge? */
	{ "arescue",   NULL                 , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 0 },
	{ "alien3",    NULL                 , NULL                 , EEPROM_ALIEN3     , 4         , 0             , 0 },
	{ "arabfgt",   spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 0 },
	{ "arabfgtj",  spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 0 },
	{ "brival",    brival_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 5         , 1             , 0 },
	{ "darkedge",  brival_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 5         , 1             , 0 },
	{ "darkedgj",  brival_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 5         , 1             , 0 },
	{ "dbzvrvs",   brival_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 5         , 1             , 0 },
	{ "f1en",      driving_custom_io_r  , driving_custom_io_w  , EEPROM_SYS32_0    , 5         , 0             , 0 },
	{ "f1lap",     driving_custom_io_r  , driving_custom_io_w  , EEPROM_SYS32_0    , 5         , 0             , 0 },
	{ "ga2",       spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 3         , 0             , 0 },
	{ "ga2u",      spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 3         , 0             , 0 },
	{ "ga2j",      spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 3         , 0             , 0 },
	{ "holo",      NULL                 , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 1 },
	{ "jpark",     NULL                 , NULL                 , EEPROM_SYS32_0    , 6         , 0             , 0 },
	{ "radm",      driving_custom_io_r  , driving_custom_io_w  , EEPROM_RADM       , 5         , 0             , 0 },
	{ "radmu",     driving_custom_io_r  , driving_custom_io_w  , EEPROM_RADM       , 5         , 0             , 0 },
	{ "radr",      driving_custom_io_r  , driving_custom_io_w  , EEPROM_RADR       , 5         , 1             , 0 },
	{ "radru",     driving_custom_io_r  , driving_custom_io_w  , EEPROM_RADR       , 5         , 1             , 0 },
	{ "slpstrm",   driving_custom_io_r  , driving_custom_io_w  , EEPROM_SYS32_0    , 5         , 0             , 0 },
	{ "sonic",     sonic_custom_io_r    , sonic_custom_io_w    , EEPROM_SYS32_0    , 5         , 1             , 0 },
	{ "sonicp",    sonic_custom_io_r    , sonic_custom_io_w    , EEPROM_SYS32_0    , 5         , 1             , 0 },
	{ "spidey",    spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 3         , 0             , 0 },
	{ "spideyu",   spidey_custom_io_r   , NULL                 , EEPROM_SYS32_0    , 3         , 0             , 0 },
	{ "svf",       NULL                 , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 0 },
	{ "svs",       NULL                 , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 0 },
	{ "jleague",   NULL                 , NULL                 , EEPROM_SYS32_0    , 4         , 0             , 0 },

	{ NULL, NULL, NULL }
};

static void segas32_set_config( void )
{
	int n_game;

	/* reset the custom handlers and other pointers */
	custom_io_r = NULL;
	custom_io_w = NULL;
	system32_mixerShift = 0;

	n_game = 0;
	while( segas32_config_table[ n_game ].s_name != NULL )
	{
		if( strcmp( Machine->gamedrv->name, segas32_config_table[ n_game ].s_name ) == 0 )
		{
			custom_io_r = segas32_config_table[ n_game ].custom_io_r;
			custom_io_w = segas32_config_table[ n_game ].custom_io_w;
			system32_use_default_eeprom = segas32_config_table[ n_game ].default_eeprom;
			system32_mixerShift = segas32_config_table[ n_game ].mixershift;
			system32_allow_high_resolution =  segas32_config_table[ n_game ].system32_allow_high_resolution;
			system32_temp_kludge =  segas32_config_table[ n_game ].system32_temp_kludge;
			break;
		}
		n_game++;
	}
}


DRIVER_INIT ( segas32 )
{
	segas32_set_config();

}

/* Alien3 custom io, move to custom io function.. */
DRIVER_INIT ( alien3 )
{
	init_segas32();

	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00050, 0xc00051, 0, 0, sys32_gun_p1_x_c00050_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00052, 0xc00053, 0, 0, sys32_gun_p1_y_c00052_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00054, 0xc00055, 0, 0, sys32_gun_p2_x_c00054_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00056, 0xc00057, 0, 0, sys32_gun_p2_y_c00056_r);

	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00050, 0xc00051, 0, 0, sys32_gun_p1_x_c00050_w);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00052, 0xc00053, 0, 0, sys32_gun_p1_y_c00052_w);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00054, 0xc00055, 0, 0, sys32_gun_p2_x_c00054_w);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00056, 0xc00057, 0, 0, sys32_gun_p2_y_c00056_w);
}

/* Arabian Fight has additional protection handlers */
DRIVER_INIT ( arabfgt )
{
	init_segas32();

	/* install protection handlers */
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00000, 0xa000ff, 0, 0, arabfgt_protboard_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00100, 0xa0011f, 0, 0, arf_wakeup_protection_r);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00000, 0xa00fff, 0, 0, arabfgt_protboard_w);
}

/* Burning Rival has additional protection handlers */
DRIVER_INIT ( brival )
{
	init_segas32();

	/* install protection handlers */
	segas32_protram = auto_malloc (0x1000);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x20ba00, 0x20ba07, 0, 0, brival_protection_r);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00000, 0xa00fff, 0, 0, brival_protboard_w);
}

DRIVER_INIT ( ga2 )
{
	init_segas32();

	/* install protection handlers */
	/* simulation */
//	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00000, 0xa0001f, 0, 0, ga2_sprite_protection_r); /* main sprite colours */
//	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00100, 0xa0015f, 0, 0, ga2_wakeup_protection_r);

	decrypt_ga2_protrom();
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xa00000, 0xa00fff, 0, 0, ga2_dpram_w);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM,  0xa00000, 0xa00fff, 0, 0, ga2_dpram_r);
}

DRIVER_INIT ( sonic )
{
	init_segas32();

	/* install protection handlers */
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0x20E5C4, 0x20E5C5, 0, 0, sonic_level_load_protection);
}

DRIVER_INIT ( jpark )
{
	/* Temp. Patch until we emulate the 'Drive Board', thanks to Malice */
	data16_t *pROM = (data16_t *)memory_region(REGION_CPU1);
	pROM[0xC15A8/2] = 0xCD70;
	pROM[0xC15AA/2] = 0xD8CD;

	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00050, 0xc00051, 0, 0, sys32_gun_p1_x_c00050_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00052, 0xc00053, 0, 0, sys32_gun_p1_y_c00052_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00054, 0xc00055, 0, 0, sys32_gun_p2_x_c00054_r);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00056, 0xc00057, 0, 0, sys32_gun_p2_y_c00056_r);

	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00050, 0xc00051, 0, 0, sys32_gun_p1_x_c00050_w);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00052, 0xc00053, 0, 0, sys32_gun_p1_y_c00052_w);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00054, 0xc00055, 0, 0, sys32_gun_p2_x_c00054_w);
	memory_install_write16_handler(0, ADDRESS_SPACE_PROGRAM, 0xc00056, 0xc00057, 0, 0, sys32_gun_p2_y_c00056_w);


	init_segas32();
}

/* ??? */
static READ16_HANDLER ( arescue_unknown_r )
{
	logerror("unk_r? %06x %06x\n", activecpu_get_pc(), 0x800000+offset*2);
	return 0x0001;
}

static DRIVER_INIT ( arescue )
{
	init_segas32();
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x800000, 0x8fffff, 0, 0, arescue_unknown_r); // protection? communication?
}

static READ16_HANDLER( f1en_unknown_read )
{
	return 0;
}

static DRIVER_INIT ( f1en )
{
	init_segas32();
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x810000, 0x810003, 0, 0, f1en_unknown_read);
	memory_install_read16_handler(0, ADDRESS_SPACE_PROGRAM, 0x818000, 0x818003, 0, 0, f1en_unknown_read);
}


/******************************************************************************
 ******************************************************************************
  Game Definitions
 ******************************************************************************
 ******************************************************************************/

GAMEX(1992, arescue,  0,        segas32,    f1en,    arescue,   ROT0, "Sega"  , "Air Rescue", GAME_NOT_WORKING | GAME_IMPERFECT_GRAPHICS )
GAMEX(1993, alien3,   0,        segas32,    alien3,  alien3,    ROT0, "Sega"  , "Alien�: The Gun", GAME_IMPERFECT_GRAPHICS )
GAMEX(1992, arabfgt,  0,        segas32,    spidey,  arabfgt,   ROT0, "Sega"  , "Arabian Fight (US)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1992, arabfgtj, arabfgt,  segas32,    spidey,  arabfgt,   ROT0, "Sega"  , "Arabian Fight (Japan)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1992, brival,   0,        segas32_hi, brival,  brival,    ROT0, "Sega"  , "Burning Rival (Japan)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1992, darkedge, 0,        segas32_hi, brival,  segas32,   ROT0, "Sega"  , "Dark Edge (World)", GAME_NOT_WORKING | GAME_UNEMULATED_PROTECTION )
GAMEX(1992, darkedgj, darkedge, segas32_hi, brival,  segas32,   ROT0, "Sega"  , "Dark Edge (Japan)", GAME_NOT_WORKING | GAME_UNEMULATED_PROTECTION )
GAMEX(1994, dbzvrvs,  0,        segas32_hi, svf,	 segas32,   ROT0, "Sega / Banpresto", "Dragon Ball Z V.R.V.S.", GAME_NOT_WORKING | GAME_UNEMULATED_PROTECTION)
GAMEX(1991, f1en,     0,        segas32,    f1en,    f1en,      ROT0, "Sega"  , "F1 Exhaust Note", GAME_IMPERFECT_GRAPHICS )
GAMEX(1993, f1lap,    0,        segas32,    f1en,    segas32,   ROT0, "Sega"  , "F1 Super Lap", GAME_NOT_WORKING )
GAMEX(1992, ga2,      0,        segas32_ga2,ga2,     ga2,       ROT0, "Sega"  , "Golden Axe: The Revenge of Death Adder (World)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1992, ga2u,     ga2,      segas32_ga2,ga2u,    ga2,       ROT0, "Sega"  , "Golden Axe: The Revenge of Death Adder (US)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1992, ga2j,     ga2,      segas32_ga2,ga2,     ga2,       ROT0, "Sega"  , "Golden Axe: The Revenge of Death Adder (Japan)", GAME_IMPERFECT_GRAPHICS )
GAME( 1992, holo,     0,        segas32,    holo,    segas32,   ROT0, "Sega"  , "Holosseum (US)" )
GAMEX(1994, jpark,    0,        segas32,    jpark,   jpark,     ROT0, "Sega"  , "Jurassic Park", GAME_IMPERFECT_GRAPHICS )
GAMEX(1991, radm,     0,        segas32,    radm,    segas32,   ROT0, "Sega"  , "Rad Mobile (World)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1991, radmu,    radm,     segas32,    radm,    segas32,   ROT0, "Sega"  , "Rad Mobile (US)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1991, radr,     0,        segas32_hi, radr,    segas32,   ROT0, "Sega"  , "Rad Rally (World)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1991, radru,    radr,     segas32_hi, radr,    segas32,   ROT0, "Sega"  , "Rad Rally (US)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1995, slipstrm, 0,        segas32_hi, f1en,	 segas32,   ROT0, "Capcom", "Slipstream (bad dump?)", GAME_NOT_WORKING )
GAMEX(1992, sonic,    0,        segas32_hi, sonic,   sonic,     ROT0, "Sega"  , "SegaSonic The Hedgehog (Japan, rev. C)", GAME_NOT_WORKING | GAME_UNEMULATED_PROTECTION )
GAMEX(1992, sonicp,   sonic,    segas32_hi, sonic,   segas32,   ROT0, "Sega"  , "SegaSonic The Hedgehog (Japan, prototype)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1991, spidey,   0,        segas32,    spidey,  segas32,   ROT0, "Sega"  , "Spider-Man: The Videogame (World)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1991, spideyu,  spidey,   segas32,    spideyu, segas32,   ROT0, "Sega"  , "Spider-Man: The Videogame (US)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1994, svf,      0,        segas32,    svf,     segas32,   ROT0, "Sega"  , "Super Visual Football: European Sega Cup", GAME_IMPERFECT_GRAPHICS )
GAMEX(1994, svs,	  svf,		segas32,    svf,	 segas32,   ROT0, "Sega"  , "Super Visual Soccer: Sega Cup (US)", GAME_IMPERFECT_GRAPHICS )
GAMEX(1994, jleague,  svf,      segas32,    svf,     segas32,   ROT0, "Sega"  , "The J.League 1994 (Japan)", GAME_NOT_WORKING | GAME_UNEMULATED_PROTECTION )
