/***************************************************************************

AmeriDarts      (c) 1989 Ameri Corporation
Cool Pool       (c) 1992 Catalina
9 Ball Shootout (c) 1993 E-Scape/Bundra

preliminary driver by Nicola Salmoria and Aaron Giles


The main cpu is a 34010; it is encrypted in 9 Ball Shootout.

The second CPU in AmeriDarts is a 32015, whose built-in ROM hasn't been read.

The second CPU in Cool Pool and 9 Ball Shootout is a 320C26; the code is
the same in the two games.

Cool Pool:
- There are handlers for the two external ints, which aren't hooked up yet.
  INT2 is probably connected to the "IOP" (32026).

- rom test starts at 0xffe021a0. It also contains functions to print text on
  the screen (0xffe04230), so should be good a good start for the gfx emulation.

- The checksum test routine is wrong, e.g. when it says to be testing 4U/8U it
  is actually reading 4U/8U/3U/7U, when testing 3U/7U it actually reads
  2U/6U/1U/5U. The placement cannot therefore be exactly determined by the
  check passing.

9 Ball Shootout:
- U110 and U111 both have checksum 00000000 so the ROM test cannot tell if they
  are swapped.

TODO:
- emulate "thrash protection" (NVRAM write protection)
- emulate "IOP" (I/O Processor?) see check at 0xffe02c90 (coolpool)

***************************************************************************/

#include "driver.h"
#include "cpu/tms34010/tms34010.h"
#include "cpu/tms34010/34010ops.h"
#include "cpu/tms32025/tms32025.h"
#include "vidhrdw/tlc34076.h"
#include "sound/dac.h"


static data16_t *code_rom;

static data16_t *ram_base;

static data16_t dpyadr;
static int dpyadrscan;

VIDEO_START( coolpool )
{
	return 0;
}

void coolpool_to_shiftreg(unsigned int address, UINT16* shiftreg)
{
//logerror("%08x:coolpool_to_shiftreg(%08x)\n", activecpu_get_pc(), address);
	memcpy(shiftreg, &ram_base[TOWORD(address)], TOBYTE(0x1000));
}

void coolpool_from_shiftreg(unsigned int address, UINT16* shiftreg)
{
//logerror("%08x:coolpool_from_shiftreg(%08x)\n", activecpu_get_pc(), address);
	memcpy(&ram_base[TOWORD(address)], shiftreg, TOBYTE(0x1000));
}

READ16_HANDLER( coolpool_gfxrom_r )
{
	data8_t *rom = memory_region(REGION_GFX1);

	return rom[2*offset] | (rom[2*offset+1] << 8);
}

WRITE16_HANDLER( coolpool_34010_io_register_w )
{
	if (offset == REG_DPYADR || offset == REG_DPYTAP)
		force_partial_update(cpu_getscanline());
	tms34010_io_register_w(offset, data, mem_mask);

	/* track writes to the DPYADR register which takes effect on the following scanline */
	if (offset == REG_DPYADR)
	{
		dpyadr = ~data & 0xfffc;
		dpyadrscan = cpu_getscanline() + 1;
		logerror("dpyadr = %04X on scan %d\n", dpyadr, dpyadrscan);
	}
}


void coolpool_dpyint_callback(int scanline)
{
//	if (scanline < Machine->visible_area.max_y)
//		force_partial_update(scanline - 1);
}


VIDEO_UPDATE( amerdart )
{
	data16_t *base = &ram_base[TOWORD(0x800)];
	int x, y;

	for (y = cliprect->min_y; y <= cliprect->max_y; y++)
	{
		UINT8 scanline[320];
		for (x = cliprect->min_x; x <= cliprect->max_x; x += 2)
		{
			data16_t pixels = base[x / 4];

			scanline[x+0] = (pixels >> 0) & 15;
			scanline[x+1] = (pixels >> 4) & 15;
			scanline[x+2] = (pixels >> 8) & 15;
			scanline[x+3] = (pixels >> 12) & 15;
		}

		draw_scanline8(bitmap, cliprect->min_x, y, cliprect->max_x - cliprect->min_x + 1, scanline, NULL, -1);
		base += TOWORD(0x800);
	}
}

INTERRUPT_GEN( coolpool_vblank_start )
{
	/* dpyadr is latched from dpystrt at the beginning of VBLANK every frame */
	cpuintrf_push_context(0);

	/* due to timing issues, we sometimes set the DPYADR just before we get here;
	   in order to avoid trouncing that value, we look for the last scanline */
	if (dpyadrscan < Machine->visible_area.max_y)
		dpyadr = ~tms34010_io_register_r(REG_DPYSTRT, 0) & 0xfffc;
	dpyadrscan = 0;

	cpuintrf_pop_context();
}

VIDEO_UPDATE( coolpool )
{
	data16_t dpytap, dudate, dumask;
	int x, y, offset;

	/* if we're blank, just blank the screen */
	if (tms34010_io_display_blanked(0))
	{
		fillbitmap(bitmap, get_black_pen(), cliprect);
		return;
	}

	/* fetch current scanline advance and column offset values */
	cpuintrf_push_context(0);
	dudate = (tms34010_io_register_r(REG_DPYCTL, 0) & 0x03fc) << 4;
	dumask = dudate - 1;
	dpytap = tms34010_io_register_r(REG_DPYTAP, 0) & 0x3fff & dumask;
	cpuintrf_pop_context();

	/* compute the offset */
	offset = (dpyadr << 4) + dpytap;
/*
{
	static int temp;
	if (code_pressed(KEYCODE_J) && temp > 0) temp -= 2;
	else if (code_pressed(KEYCODE_K)) temp += 2;
	offset = temp;
}*/

	/* adjust for when DPYADR was written */
	if (cliprect->min_y > dpyadrscan)
		offset += (cliprect->min_y - dpyadrscan) * dudate;
//printf("DPYADR = %04X DPYTAP = %04X (%d-%d)\n", dpyadr, dpytap, cliprect->min_y, cliprect->max_y);

	/* render the visible section */
	for (y = cliprect->min_y; y <= cliprect->max_y; y++)
	{
		UINT8 scanline[320];
		for (x = cliprect->min_x; x <= cliprect->max_x; x += 2)
		{
			data16_t pixels = ram_base[(offset & ~dumask & TOWORD(0x1fffff)) | ((offset + x/2) & dumask)];

			scanline[x+0] = (pixels >> 0) & 0xff;
			scanline[x+1] = (pixels >> 8) & 0xff;
		}

		draw_scanline8(bitmap, cliprect->min_x, y, cliprect->max_x - cliprect->min_x + 1, scanline, NULL, -1);
		offset += dudate;
	}
}



static data16_t input_data;

MACHINE_INIT( coolpool )
{
	tlc34076_reset(6);
}

WRITE16_HANDLER( amerdart_input_w )
{
	logerror("%08X:IOP write = %04X\n", activecpu_get_pc(), data);
	COMBINE_DATA(&input_data);
	cpunum_set_input_line(0, 1, ASSERT_LINE);
}

READ16_HANDLER( amerdart_input_r )
{
	logerror("%08X:IOP read\n", activecpu_get_pc());
	cpunum_set_input_line(0, 1, CLEAR_LINE);

	switch (input_data)
	{
		case 0x19:
			return 0x6c00;

		case 0x500:
			return readinputport(0);

		default:
			return input_data;
	}
	return 0;
}


static WRITE16_HANDLER( coolpool_misc_w )
{
	logerror("%08x:IOP_reset_w %04x\n",activecpu_get_pc(),data);

	coin_counter_w(0,~data & 0x0001);
	coin_counter_w(1,~data & 0x0002);

	cpunum_set_input_line(1, INPUT_LINE_RESET, (data & 0x0400) ? ASSERT_LINE : CLEAR_LINE);
}

static int cmd_pending,iop_cmd,iop_answer;

static WRITE16_HANDLER( coolpool_iop_w )
{
	logerror("%08x:IOP write %04x\n",activecpu_get_pc(),data);
	iop_cmd = data;
	cmd_pending = 1;
	cpunum_set_input_line(1, 0, HOLD_LINE);	/* ???  I have no idea who should generate this! */
										/* the DSP polls the status bit so it isn't strictly */
										/* necessary to also have an IRQ */
}

static READ16_HANDLER( coolpool_iop_r )
{
//	logerror("%08x:IOP read %04x\n",activecpu_get_pc(),iop_answer);
	cpunum_set_input_line(0, 1, CLEAR_LINE);

	return iop_answer;
}

static READ16_HANDLER( dsp_cmd_r )
{
	cmd_pending = 0;
//	logerror("%08x:IOP cmd_r %04x\n",activecpu_get_pc(),iop_cmd);
	return iop_cmd;
}

static WRITE16_HANDLER( dsp_answer_w )
{
	logerror("%08x:IOP answer %04x\n",activecpu_get_pc(),data);
//usrintf_showmessage("IOP answer %04x",data);
	iop_answer = data;
	cpunum_set_input_line(0, 1, ASSERT_LINE);
}

static READ16_HANDLER( dsp_bio_line_r )
{
	if (cmd_pending) return CLEAR_LINE;
	else return ASSERT_LINE;
}

static READ16_HANDLER( dsp_hold_line_r )
{
	return CLEAR_LINE;	/* ??? */
}


static int romaddr;

static READ16_HANDLER( dsp_rom_r )
{
	data8_t *rom = memory_region(REGION_USER2);
	return rom[romaddr & (memory_region_length(REGION_USER2)-1)];
}

static WRITE16_HANDLER( dsp_romaddr_w )
{
	switch (offset)
	{
		case 0:
			romaddr = (romaddr & 0xffff00) | (data >> 8); break;
		case 1:
			romaddr = (romaddr & 0x0000ff) | (data << 8); break;
	}
}


WRITE16_HANDLER( dsp_dac_w )
{
	DAC_signed_data_16_w(0,(data << 4) ^ 0x8000);
}



/*************************************
 *
 *	Memory maps
 *
 *************************************/

static ADDRESS_MAP_START( amerdart_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x000fffff) AM_READ(MRA16_RAM)
	AM_RANGE(0x05000000, 0x0500000f) AM_READ(amerdart_input_r)	// IOP
	AM_RANGE(0x06000000, 0x06007fff) AM_READ(MRA16_RAM)
	AM_RANGE(0xc0000000, 0xc00001ff) AM_READ(tms34010_io_register_r)
	AM_RANGE(0xffb00000, 0xffffffff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( amerdart_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x000fffff) AM_WRITE(MWA16_RAM) AM_BASE(&ram_base)
//	AM_RANGE(0x04000000, 0x0400000f) AM_WRITE(???_w)	DSP reset, coin counters
	AM_RANGE(0x05000000, 0x0500000f) AM_WRITE(amerdart_input_w)	// IOP
	AM_RANGE(0x06000000, 0x06007fff) AM_WRITE(MWA16_RAM)	// NVRAM
	AM_RANGE(0xc0000000, 0xc00001ff) AM_WRITE(tms34010_io_register_w)
	AM_RANGE(0xffb00000, 0xffffffff) AM_WRITE(MWA16_ROM) AM_BASE(&code_rom)
ADDRESS_MAP_END

#if 0
static ADDRESS_MAP_START( io_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x0000, 0x011f) AM_READ(MRA16_RAM)	/* 90h words internal RAM */
	AM_RANGE(0x8000, 0xffff) AM_READ(MRA16_ROM)	/* 800h words. The real DSPs ROM is at */
									/* address 0 */
									/* View it at 8000h in the debugger */
ADDRESS_MAP_END

static ADDRESS_MAP_START( io_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x0000, 0x011f) AM_WRITE(MWA16_RAM)
	AM_RANGE(0x8000, 0xffff) AM_WRITE(MWA16_ROM)
ADDRESS_MAP_END
#endif


static ADDRESS_MAP_START( coolpool_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x001fffff) AM_READ(MRA16_RAM)
	AM_RANGE(0x01000000, 0x010000ff) AM_READ(tlc34076_lsb_r)	// IMSG176P-40
	AM_RANGE(0x02000000, 0x0200000f) AM_READ(coolpool_iop_r)	// "IOP"
//	{ 0x02000010, 0x0200001f, 				// "IOP"
	AM_RANGE(0x03000000, 0x03ffffff) AM_READ(coolpool_gfxrom_r)
	AM_RANGE(0x06000000, 0x06007fff) AM_READ(MRA16_RAM)	// NVRAM
	AM_RANGE(0xc0000000, 0xc00001ff) AM_READ(tms34010_io_register_r)
	AM_RANGE(0xffe00000, 0xffffffff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( coolpool_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x001fffff) AM_WRITE(MWA16_RAM) AM_BASE(&ram_base)	// video + work ram
	AM_RANGE(0x01000000, 0x010000ff) AM_WRITE(tlc34076_lsb_w)	// IMSG176P-40
	AM_RANGE(0x02000000, 0x0200000f) AM_WRITE(coolpool_iop_w)	// "IOP"
	AM_RANGE(0x03000000, 0x0300000f) AM_WRITE(coolpool_misc_w)	// IOP reset + other stuff
	AM_RANGE(0x06000000, 0x06007fff) AM_WRITE(MWA16_RAM)	// NVRAM
	AM_RANGE(0xc0000000, 0xc00001ff) AM_WRITE(coolpool_34010_io_register_w)
	AM_RANGE(0xffe00000, 0xffffffff) AM_WRITE(MWA16_ROM) AM_BASE(&code_rom)
ADDRESS_MAP_END

static ADDRESS_MAP_START( nballsht_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x001fffff) AM_READ(MRA16_RAM)
	AM_RANGE(0x02000000, 0x0200000f) AM_READ(coolpool_iop_r)	// "IOP"
//	{ 0x02000010, 0x0200001f, 				// "IOP"
	AM_RANGE(0x04000000, 0x040000ff) AM_READ(tlc34076_lsb_r)	// IMSG176P-40
	AM_RANGE(0x06000000, 0x0601ffff) AM_READ(MRA16_RAM)	// more NVRAM?
	AM_RANGE(0x06020000, 0x0603ffff) AM_READ(MRA16_RAM)	// NVRAM
	AM_RANGE(0xc0000000, 0xc00001ff) AM_READ(tms34010_io_register_r)
	AM_RANGE(0xff000000, 0xff7fffff) AM_READ(coolpool_gfxrom_r)
	AM_RANGE(0xffc00000, 0xffffffff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( nballsht_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x00000000, 0x001fffff) AM_WRITE(MWA16_RAM) AM_BASE(&ram_base)	// video + work ram
	AM_RANGE(0x02000000, 0x0200000f) AM_WRITE(coolpool_iop_w)	// "IOP"
	AM_RANGE(0x03000000, 0x0300000f) AM_WRITE(coolpool_misc_w)	// IOP reset + other stuff
	AM_RANGE(0x04000000, 0x040000ff) AM_WRITE(tlc34076_lsb_w)
	AM_RANGE(0x06000000, 0x0601ffff) AM_WRITE(MWA16_RAM)	// more NVRAM?
	AM_RANGE(0x06020000, 0x0603ffff) AM_WRITE(MWA16_RAM)	// NVRAM
	AM_RANGE(0xc0000000, 0xc00001ff) AM_WRITE(coolpool_34010_io_register_w)
	AM_RANGE(0xffc00000, 0xffffffff) AM_WRITE(MWA16_ROM) AM_BASE(&code_rom)
ADDRESS_MAP_END

static ADDRESS_MAP_START( DSP_read_program, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x0000, 0x7fff) AM_READ(MRA16_ROM) /* External ROM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( DSP_write_program, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x0000, 0x7fff) AM_WRITE(MWA16_ROM)
ADDRESS_MAP_END

static ADDRESS_MAP_START( DSP_read_data, ADDRESS_SPACE_DATA, 16 )
	/* only internal RAM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( DSP_write_data, ADDRESS_SPACE_DATA, 16 )
	/* only internal RAM */
ADDRESS_MAP_END

static ADDRESS_MAP_START( DSP_read_io, ADDRESS_SPACE_IO, 16 )
	AM_RANGE(0x02, 0x02) AM_READ(dsp_cmd_r)
	AM_RANGE(0x04, 0x04) AM_READ(dsp_rom_r)
	AM_RANGE(0x05, 0x05) AM_READ(input_port_0_word_r)
	AM_RANGE(0x07, 0x07) AM_READ(input_port_1_word_r)
	AM_RANGE(TMS32025_BIO, TMS32025_BIO) AM_READ(dsp_bio_line_r)
	AM_RANGE(TMS32025_HOLD, TMS32025_HOLD) AM_READ(dsp_hold_line_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START( DSP_write_io, ADDRESS_SPACE_IO, 16 )
	AM_RANGE(0x00, 0x01) AM_WRITE(dsp_romaddr_w)
	AM_RANGE(0x02, 0x02) AM_WRITE(dsp_answer_w)
	AM_RANGE(0x03, 0x03) AM_WRITE(dsp_dac_w)
//	AM_RANGE(TMS32025_HOLDA, TMS32025_HOLDA) AM_WRITE(dsp_HOLDA_signal_w)
ADDRESS_MAP_END




/*************************************
 *
 *	Input ports
 *
 *************************************/

INPUT_PORTS_START( amerdart )
	PORT_START_TAG("IN0")
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_START1 )
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_START2 )
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_COIN1 )
	PORT_SERVICE( 0x0010, IP_ACTIVE_HIGH )
	PORT_BIT( 0x0020, IP_ACTIVE_HIGH, IPT_COIN2 )
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_UNUSED )
	PORT_BIT( 0x0080, IP_ACTIVE_HIGH, IPT_SERVICE1 )
	PORT_BIT( 0xff00, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START_TAG("IN1")
	PORT_BIT( 0x000f, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(3)
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(3)
	PORT_BIT( 0xffc0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START_TAG("IN2")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_TILT ) /* Slam Switch */
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_SPECIAL ) /* Test switch */
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x0040, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_COIN4 )
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_START3 )
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_VOLUME_DOWN )
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_VOLUME_UP )
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_SPECIAL ) /* coin door */
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_SPECIAL ) /* bill validator */

	PORT_START_TAG("DSW")
	PORT_DIPNAME( 0x0001, 0x0000, DEF_STR( Flip_Screen ))
	PORT_DIPSETTING(      0x0000, DEF_STR( Off ))
	PORT_DIPSETTING(      0x0001, DEF_STR( On ))
	PORT_DIPNAME( 0x0002, 0x0000, "Dipswitch Coinage" )
	PORT_DIPSETTING(      0x0000, DEF_STR( Off ))
	PORT_DIPSETTING(      0x0002, DEF_STR( On ))
	PORT_DIPNAME( 0x001c, 0x001c, DEF_STR( Coinage ))
	PORT_DIPSETTING(      0x001c, "1" )
	PORT_DIPSETTING(      0x0018, "2" )
	PORT_DIPSETTING(      0x0014, "3" )
	PORT_DIPSETTING(      0x000c, "USA ECA" )
	PORT_DIPSETTING(      0x0000, DEF_STR( Free_Play ))
	PORT_DIPNAME( 0x00e0, 0x0060, "Credits" )
	PORT_DIPSETTING(      0x0020, "3 Start/1 Continue" )
	PORT_DIPSETTING(      0x00e0, "2 Start/2 Continue" )
	PORT_DIPSETTING(      0x00a0, "2 Start/1 Continue" )
	PORT_DIPSETTING(      0x0000, "1 Start/4 Continue" )
	PORT_DIPSETTING(      0x0040, "1 Start/3 Continue" )
	PORT_DIPSETTING(      0x0060, "1 Start/1 Continue" )
	PORT_DIPNAME( 0x0300, 0x0300, "Country" )
	PORT_DIPSETTING(      0x0300, DEF_STR( USA ) )
	PORT_DIPSETTING(      0x0100, DEF_STR( French ) )
	PORT_DIPSETTING(      0x0200, DEF_STR( German ) )
	PORT_DIPSETTING(      0x0000, DEF_STR( Unused ))
	PORT_DIPNAME( 0x0400, 0x0400, "Bill Validator" )
	PORT_DIPSETTING(      0x0400, DEF_STR( Off ))
	PORT_DIPSETTING(      0x0000, DEF_STR( On ))
	PORT_DIPNAME( 0x0800, 0x0000, "Two Counters" )
	PORT_DIPSETTING(      0x0800, DEF_STR( Off ))
	PORT_DIPSETTING(      0x0000, DEF_STR( On ))
	PORT_DIPNAME( 0x1000, 0x1000, DEF_STR( Players ) )
	PORT_DIPSETTING(      0x1000, "3 Players" )
	PORT_DIPSETTING(      0x0000, "2 Players" )
	PORT_DIPNAME( 0x2000, 0x2000, DEF_STR( Cabinet ))
	PORT_DIPSETTING(      0x2000, "Rev X" )
	PORT_DIPSETTING(      0x0000, "Terminator 2" )
	PORT_DIPNAME( 0x4000, 0x4000, "Video Freeze" )
	PORT_DIPSETTING(      0x4000, DEF_STR( Off ))
	PORT_DIPSETTING(      0x0000, DEF_STR( On ))
	PORT_SERVICE( 0x8000, IP_ACTIVE_LOW )
INPUT_PORTS_END

INPUT_PORTS_START( 9ballsht )
	PORT_START_TAG("IN0")
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_SERVICE1 )
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_SERVICE2 )
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_SERVICE3 )
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_SERVICE4 )
	PORT_BIT( 0x0010, IP_ACTIVE_HIGH, IPT_SERVICE1 )
	PORT_BIT( 0x0020, IP_ACTIVE_HIGH, IPT_SERVICE2 )
	PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_SERVICE3 )
	PORT_BIT( 0x0080, IP_ACTIVE_HIGH, IPT_SERVICE4 )
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_START3 )
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_START4 )
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)	// correct
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(2)	// correct
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)	// correct
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)	// correct
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)	// correct
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1)	// correct

	PORT_START_TAG("IN1")
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_START1 )					// correct
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_START2 )					// correct
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2)
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_COIN1 )					// correct
	PORT_SERVICE_NO_TOGGLE(0x0010, IP_ACTIVE_LOW)
	PORT_BIT( 0x0020, IP_ACTIVE_LOW, IPT_COIN2 )					// correct
	PORT_BIT( 0x0040, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x0080, IP_ACTIVE_LOW, IPT_SERVICE1 )					// correct
	PORT_BIT( 0x0100, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(1)	// correct
	PORT_BIT( 0x0200, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1)	// correct
	PORT_BIT( 0x0400, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1)	// correct
	PORT_BIT( 0x0800, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1)	// correct
	PORT_BIT( 0x1000, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_PLAYER(2)	// correct
	PORT_BIT( 0x2000, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2)	// correct
	PORT_BIT( 0x4000, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2)	// correct
	PORT_BIT( 0x8000, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2)	// correct
INPUT_PORTS_END



/*************************************
 *
 *	34010 configuration
 *
 *************************************/

static struct tms34010_config cpu_config =
{
	0,								/* halt on reset */
	NULL,							/* generate interrupt */
	coolpool_to_shiftreg,			/* write to shiftreg function */
	coolpool_from_shiftreg,			/* read from shiftreg function */
	NULL,							/* display address changed */
	coolpool_dpyint_callback		/* display interrupt callback */
};



/*************************************
 *
 *	Machine drivers
 *
 *************************************/

MACHINE_DRIVER_START( amerdart )

	/* basic machine hardware */
	MDRV_CPU_ADD(TMS34010, 40000000/TMS34010_CLOCK_DIVIDER)
	MDRV_CPU_CONFIG(cpu_config)
	MDRV_CPU_PROGRAM_MAP(amerdart_readmem,amerdart_writemem)

#if 0
	MDRV_CPU_ADD(TMS32010, 15000000/8)
	MDRV_CPU_PROGRAM_MAP(io_readmem,io_writemem)
#endif

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_REAL_60HZ_VBLANK_DURATION)
	MDRV_INTERLEAVE(20)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(320, 261)	/* ??? */
	MDRV_VISIBLE_AREA(0, 320-1, 0, 240-1)

	MDRV_PALETTE_LENGTH(16)

	MDRV_VIDEO_START(coolpool)
	MDRV_VIDEO_UPDATE(amerdart)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( coolpool )

	/* basic machine hardware */
	MDRV_CPU_ADD(TMS34010, 40000000/TMS34010_CLOCK_DIVIDER)
	MDRV_CPU_CONFIG(cpu_config)
	MDRV_CPU_PROGRAM_MAP(coolpool_readmem,coolpool_writemem)
	MDRV_CPU_VBLANK_INT(coolpool_vblank_start,1)

	MDRV_CPU_ADD(TMS32026,40000000)
	MDRV_CPU_PROGRAM_MAP(DSP_read_program,DSP_write_program)
	MDRV_CPU_DATA_MAP(DSP_read_data,DSP_write_data)
	MDRV_CPU_IO_MAP(DSP_read_io,DSP_write_io)

	MDRV_MACHINE_INIT(coolpool)
	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(1000000 * (261 - 240) / (261 * 60))
	MDRV_INTERLEAVE(20)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_NEEDS_6BITS_PER_GUN)
	MDRV_SCREEN_SIZE(320, 261)	/* ??? */
	MDRV_VISIBLE_AREA(0, 320-1, 0, 240-1)

	MDRV_PALETTE_LENGTH(256)

	MDRV_VIDEO_START(coolpool)
	MDRV_VIDEO_UPDATE(coolpool)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( 9ballsht )

	/* basic machine hardware */
	MDRV_CPU_ADD(TMS34010, 40000000/TMS34010_CLOCK_DIVIDER)
	MDRV_CPU_CONFIG(cpu_config)
	MDRV_CPU_PROGRAM_MAP(nballsht_readmem,nballsht_writemem)
	MDRV_CPU_VBLANK_INT(coolpool_vblank_start,1)

	MDRV_CPU_ADD(TMS32026,40000000)
	MDRV_CPU_PROGRAM_MAP(DSP_read_program,DSP_write_program)
	MDRV_CPU_DATA_MAP(DSP_read_data,DSP_write_data)
	MDRV_CPU_IO_MAP(DSP_read_io,DSP_write_io)

	MDRV_MACHINE_INIT(coolpool)
	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(1000000 * (261 - 240) / (261 * 60))
	MDRV_INTERLEAVE(20)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_NEEDS_6BITS_PER_GUN)
	MDRV_SCREEN_SIZE(320, 261)	/* ??? */
	MDRV_VISIBLE_AREA(0, 320-1, 0, 240-1)

	MDRV_PALETTE_LENGTH(256)

	MDRV_VIDEO_START(coolpool)
	MDRV_VIDEO_UPDATE(coolpool)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END






/*************************************
 *
 *	ROM definitions
 *
 *************************************/

ROM_START( amerdart )
	ROM_REGION( TOBYTE(0x100000), REGION_CPU1, 0 )		/* 34010 dummy region */

	ROM_REGION16_LE( 0x0a0000, REGION_USER1, ROMREGION_DISPOSE )	/* 34010 code */
	ROM_LOAD16_BYTE( "u31",  0x000001, 0x10000, CRC(9628c422) SHA1(46b71acc746760962e34e9d7876f9499ea7d5c7c) )
	ROM_LOAD16_BYTE( "u32",  0x000000, 0x10000, CRC(2d651ed0) SHA1(e2da2c3d8f25c17e26fd435c75983b2db8691993) )
	ROM_LOAD16_BYTE( "u38",  0x020001, 0x10000, CRC(1eb8c887) SHA1(220f566043535c54ad1cf2216966c7f42099e50b) )
	ROM_LOAD16_BYTE( "u39",  0x020000, 0x10000, CRC(2ab1ea68) SHA1(4e29a274c5c62b6ca92119eb320200beb784ca55) )
	ROM_LOAD16_BYTE( "u45",  0x040001, 0x10000, CRC(74394375) SHA1(ceb7ae4e3253351da362cd0ada87702164005d17) )
	ROM_LOAD16_BYTE( "u46",  0x040000, 0x10000, CRC(1188047e) SHA1(249f25582ab72eeee37798418460de312053660e) )
	ROM_LOAD16_BYTE( "u52",  0x060001, 0x10000, CRC(5ac2f06d) SHA1(b3a5d0cd94bdffdbf5bd17dbb30c07bfad3fa5d0) )
	ROM_LOAD16_BYTE( "u53",  0x060000, 0x10000, CRC(4bd25cf0) SHA1(d1092cc3b6172d6567acd21f79b22043380102b7) )
	ROM_LOAD16_BYTE( "u57",  0x080001, 0x10000, CRC(f620f935) SHA1(bf891fce1f04f3ad5b8b72d43d041ceacb0b65bc) )
	ROM_LOAD16_BYTE( "u58",  0x080000, 0x10000, CRC(f1b3d7c4) SHA1(7b897230d110be7a5eb05eda927d00561ebb9ce3) )

	ROM_REGION( 0x18000, REGION_CPU2, 0 )	/* 32015 code (missing) */
	ROM_LOAD16_BYTE( "dspl",         0x00000, 0x08000, NO_DUMP )
	ROM_LOAD16_BYTE( "dsph",         0x00001, 0x08000, NO_DUMP )

	ROM_REGION( 0x100000, REGION_USER2, 0 )				/* 32015 data? (incl. samples?) */
	ROM_LOAD16_WORD( "u1",   0x000000, 0x10000, CRC(3f459482) SHA1(d9d489efd0d9217fceb3bf1a3b37a78d6823b4d9) )
	ROM_LOAD16_WORD( "u2",   0x010000, 0x10000, CRC(a587fffd) SHA1(f33f511d1bf1d6eb3c42535593a9718571174c4b) )
	ROM_LOAD16_WORD( "u3",   0x020000, 0x10000, CRC(984d343a) SHA1(ee214830de4cb22d2d8e9d3ca335eff05af4abb6) )
	ROM_LOAD16_WORD( "u4",   0x030000, 0x10000, CRC(c4765ff6) SHA1(7dca61d32300047ca1c089057e617553d60a0995) )
	ROM_LOAD16_WORD( "u5",   0x040000, 0x10000, CRC(3b63b890) SHA1(a1223cb8884d5365af7d3f607657efff877f8845) )
	ROM_LOAD16_WORD( "u6",   0x050000, 0x10000, CRC(5cdb9aa9) SHA1(fae5d2c7f649bcba8068c8bc8266ee411258535e) )
	ROM_LOAD16_WORD( "u7",   0x060000, 0x10000, CRC(147083a2) SHA1(c04c38145ab159bd519e6325477a3f7d0eebbda1) )
	ROM_LOAD16_WORD( "u8",   0x070000, 0x10000, CRC(975b368c) SHA1(1d637ce8c5d60833bb25aab2610e1a856720235e) )
	ROM_LOAD16_WORD( "u16",  0x080000, 0x10000, CRC(7437e8bf) SHA1(754be4822cd586590f09e706d7eb48e5ba8c8817) )
	ROM_LOAD16_WORD( "u17",  0x090000, 0x10000, CRC(e32bdd0f) SHA1(0662abbe84f0bad2631566b506ef016fcd79b9ee) )
	ROM_LOAD16_WORD( "u18",  0x0a0000, 0x10000, CRC(de3b4d7c) SHA1(68e7ffe2d84aef7c24d1787c4f9b6950c0107741) )
	ROM_LOAD16_WORD( "u19",  0x0b0000, 0x10000, CRC(7109247c) SHA1(201809ec6599b30c26823bde6851b6eaa2589710) )
	ROM_LOAD16_WORD( "u20",  0x0c0000, 0x10000, CRC(038b7d2d) SHA1(80bab18ca36d2bc101da7f3f6e1c82d8a802c14c) )
	ROM_LOAD16_WORD( "u21",  0x0d0000, 0x10000, CRC(9b0b8978) SHA1(b31d0451ecd7085c191d20b2b41d0e8fe551996c) )
	ROM_LOAD16_WORD( "u22",  0x0e0000, 0x10000, CRC(4b92588a) SHA1(eea262c1a122015364a0046ff2bc7816f5f6821d) )
	ROM_LOAD16_WORD( "u23",  0x0f0000, 0x10000, CRC(d7c2b13b) SHA1(3561e08011f649e4d0c47792745b2a014167e816) )
ROM_END

ROM_START( coolpool )
	ROM_REGION( 0x40000, REGION_CPU1, 0 )		/* dummy region for TMS34010 */

	ROM_REGION16_LE( 0x40000, REGION_USER1, ROMREGION_DISPOSE )	/* 34010 code */
	ROM_LOAD16_BYTE( "u112b",        0x00000, 0x20000, CRC(aa227769) SHA1(488e357a7aad07369cade3110cde14ba8562c66c) )
	ROM_LOAD16_BYTE( "u113b",        0x00001, 0x20000, CRC(5b5f82f1) SHA1(82afb6a8d94cf09960b962d5208aab451b56feae) )

	ROM_REGION( 0x200000, REGION_GFX1, 0 )	/* gfx data read by main CPU */
	ROM_LOAD16_BYTE( "u04",          0x000000, 0x20000, CRC(66a9940e) SHA1(7fa587280ecfad6b06194868de09cbdd57cf517f) )
	ROM_CONTINUE(                    0x100000, 0x20000 )
	ROM_LOAD16_BYTE( "u08",          0x000001, 0x20000, CRC(56789cf4) SHA1(5ad867d5029fdac9dccd01a6979171aa30d9a6eb) )
	ROM_CONTINUE(                    0x100001, 0x20000 )
	ROM_LOAD16_BYTE( "u03",          0x040000, 0x20000, CRC(02bc792a) SHA1(8085cff38868a307d6d29a7aadf3d6a99cbe85bb) )
	ROM_CONTINUE(                    0x140000, 0x20000 )
	ROM_LOAD16_BYTE( "u07",          0x040001, 0x20000, CRC(7b2fcb9f) SHA1(fa912663891bac6ba78519f030ba2c718e3514c3) )
	ROM_CONTINUE(                    0x140001, 0x20000 )
	ROM_LOAD16_BYTE( "u02",          0x080000, 0x20000, CRC(3b7d757d) SHA1(8737721764b181b050d776b2d2e1208419f8e5eb) )
	ROM_CONTINUE(                    0x180000, 0x20000 )
	ROM_LOAD16_BYTE( "u06",          0x080001, 0x20000, CRC(c09353a2) SHA1(f3588ec75b757232bdaa40d055e171a501122bfa) )
	ROM_CONTINUE(                    0x180001, 0x20000 )
	ROM_LOAD16_BYTE( "u01",          0x0c0000, 0x20000, CRC(948a5faf) SHA1(186ab3ab0ede168beaa4dae0cba753df10cdac46) )
	ROM_CONTINUE(                    0x1c0000, 0x20000 )
	ROM_LOAD16_BYTE( "u05",          0x0c0001, 0x20000, CRC(616965e2) SHA1(588ea3c5c7838c50b2157ff1074f629d9d85791c) )
	ROM_CONTINUE(                    0x1c0001, 0x20000 )

	ROM_REGION( 0x40000, REGION_CPU2, 0 )	/* TMS320C26 */
	ROM_LOAD16_BYTE( "u34",          0x00000, 0x08000, CRC(dc1df70b) SHA1(e42fa7e34e50e0bd2aaeea5c55d750ed3286610d) )
	ROM_LOAD16_BYTE( "u35",          0x00001, 0x08000, CRC(ac999431) SHA1(7e4c2dcaedcb7e7c67072a179e4b8488d2bbdac7) )

	ROM_REGION( 0x200000, REGION_USER2, 0 )	/* TMS32026 data */
	ROM_LOAD( "u17c",         0x000000, 0x40000, CRC(ea3cc41d) SHA1(e703e789dfbcfaec878a990031ce839164c51253) )
	ROM_LOAD( "u16c",         0x040000, 0x40000, CRC(2e6680ea) SHA1(cb30dc789039aab491428d075fee9e0bc04fd2ce) )
	ROM_LOAD( "u15c",         0x080000, 0x40000, CRC(8e5f248e) SHA1(a954d3c20dc0b70f83c4c238db30a33285fcb353) )
	ROM_LOAD( "u14c",         0x0c0000, 0x40000, CRC(dcd6cf71) SHA1(b1f53bffdd19f5da1d8664765d504568d1f5867c) )
	ROM_LOAD( "u13c",         0x100000, 0x40000, CRC(5a7fe750) SHA1(bbbd45380545cb0f17d9f6811b2a7300fa3b682d) )
	ROM_LOAD( "u12c",         0x140000, 0x40000, CRC(4f246958) SHA1(ee4446159635b6c44d88d8f6aac52787a89403c1) )
	ROM_LOAD( "u11c",         0x180000, 0x40000, CRC(92cd2b03) SHA1(e80df65f8ec5ed2178f623bdd975e2b01a12a184) )
	ROM_LOAD( "u10c",         0x1c0000, 0x40000, CRC(a3dbcae3) SHA1(af997f3f56f406d5eb9fa415e1672b2d129815b8) )
ROM_END

ROM_START( 9ballsht )
	ROM_REGION( 0x40000, REGION_CPU1, 0 )		/* dummy region for TMS34010 */

	ROM_REGION16_LE( 0x80000, REGION_USER1, ROMREGION_DISPOSE )	/* 34010 code */
	ROM_LOAD16_BYTE( "u112",         0x00000, 0x40000, CRC(b3855e59) SHA1(c3175df24b85897783169bcaccd61630e512f7f6) )
	ROM_LOAD16_BYTE( "u113",         0x00001, 0x40000, CRC(30cbf462) SHA1(64b2e2d40c2a92c4f4823dc866e5464792954ac3) )

	ROM_REGION( 0x100000, REGION_GFX1, 0 )	/* gfx data read by main CPU */
	ROM_LOAD16_BYTE( "u110",         0x00000, 0x80000, CRC(890ed5c0) SHA1(eaf06ee5b6c5ed0103b535396b4517012818a416) )
	ROM_LOAD16_BYTE( "u111",         0x00001, 0x80000, CRC(1a9f1145) SHA1(ba52a6d1aca26484c320518f69c66ce3ceb4adcf) )

	ROM_REGION( 0x40000, REGION_CPU2, 0 )	/* TMS320C26 */
	ROM_LOAD16_BYTE( "u34",          0x00000, 0x08000, CRC(dc1df70b) SHA1(e42fa7e34e50e0bd2aaeea5c55d750ed3286610d) )
	ROM_LOAD16_BYTE( "u35",          0x00001, 0x08000, CRC(ac999431) SHA1(7e4c2dcaedcb7e7c67072a179e4b8488d2bbdac7) )

	ROM_REGION( 0x100000, REGION_USER2, 0 )	/* TMS32026 data */
	ROM_LOAD( "u54",          0x00000, 0x80000, CRC(1be5819c) SHA1(308b5b1fe05634419d03956ae1b2e5a61206900f) )
	ROM_LOAD( "u53",          0x80000, 0x80000, CRC(d401805d) SHA1(f4bcb2bdc45c3bc5ca423e518cdea8b3a7e8d60e) )
ROM_END

/*
  all ROMs for this set were missing except for the main program,
  I assume the others are the same.
 */
ROM_START( 9ballsh2 )
	ROM_REGION( 0x40000, REGION_CPU1, 0 )		/* dummy region for TMS34010 */

	ROM_REGION16_LE( 0x80000, REGION_USER1, ROMREGION_DISPOSE )	/* 34010 code */
	ROM_LOAD16_BYTE( "e-scape.112",  0x00000, 0x40000, CRC(aee8114f) SHA1(a0d0e9e3a879393585b85ac6d04e31a7d4221179) )
	ROM_LOAD16_BYTE( "e-scape.113",  0x00001, 0x40000, CRC(ccd472a7) SHA1(d074080e987c233b26b3c72248411c575f7a2293) )

	ROM_REGION( 0x100000, REGION_GFX1, 0 )	/* gfx data read by main CPU */
	ROM_LOAD16_BYTE( "u110",         0x00000, 0x80000, CRC(890ed5c0) SHA1(eaf06ee5b6c5ed0103b535396b4517012818a416) )
	ROM_LOAD16_BYTE( "u111",         0x00001, 0x80000, CRC(1a9f1145) SHA1(ba52a6d1aca26484c320518f69c66ce3ceb4adcf) )

	ROM_REGION( 0x40000, REGION_CPU2, 0 )	/* TMS320C26 */
	ROM_LOAD16_BYTE( "u34",          0x00000, 0x08000, CRC(dc1df70b) SHA1(e42fa7e34e50e0bd2aaeea5c55d750ed3286610d) )
	ROM_LOAD16_BYTE( "u35",          0x00001, 0x08000, CRC(ac999431) SHA1(7e4c2dcaedcb7e7c67072a179e4b8488d2bbdac7) )

	ROM_REGION( 0x100000, REGION_USER2, 0 )	/* TMS32026 data */
	ROM_LOAD( "u54",          0x00000, 0x80000, CRC(1be5819c) SHA1(308b5b1fe05634419d03956ae1b2e5a61206900f) )
	ROM_LOAD( "u53",          0x80000, 0x80000, CRC(d401805d) SHA1(f4bcb2bdc45c3bc5ca423e518cdea8b3a7e8d60e) )
ROM_END

ROM_START( 9ballsh3 )
	ROM_REGION( 0x40000, REGION_CPU1, 0 )		/* dummy region for TMS34010 */

	ROM_REGION16_LE( 0x80000, REGION_USER1, ROMREGION_DISPOSE )	/* 34010 code */
	ROM_LOAD16_BYTE( "8e_1826.112",  0x00000, 0x40000, CRC(486f7a8b) SHA1(635e3b1e7a21a86dd3d0ea994e9b923b06df587e) )
	ROM_LOAD16_BYTE( "8e_6166.113",  0x00001, 0x40000, CRC(c41db70a) SHA1(162112f9f5bb6345920a45c41da6a249796bd21f) )

	ROM_REGION( 0x100000, REGION_GFX1, 0 )	/* gfx data read by main CPU */
	ROM_LOAD16_BYTE( "u110",         0x00000, 0x80000, CRC(890ed5c0) SHA1(eaf06ee5b6c5ed0103b535396b4517012818a416) )
	ROM_LOAD16_BYTE( "u111",         0x00001, 0x80000, CRC(1a9f1145) SHA1(ba52a6d1aca26484c320518f69c66ce3ceb4adcf) )

	ROM_REGION( 0x40000, REGION_CPU2, 0 )	/* TMS320C26 */
	ROM_LOAD16_BYTE( "u34",          0x00000, 0x08000, CRC(dc1df70b) SHA1(e42fa7e34e50e0bd2aaeea5c55d750ed3286610d) )
	ROM_LOAD16_BYTE( "u35",          0x00001, 0x08000, CRC(ac999431) SHA1(7e4c2dcaedcb7e7c67072a179e4b8488d2bbdac7) )

	ROM_REGION( 0x100000, REGION_USER2, 0 )	/* TMS32026 data */
	ROM_LOAD( "u54",          0x00000, 0x80000, CRC(1be5819c) SHA1(308b5b1fe05634419d03956ae1b2e5a61206900f) )
	ROM_LOAD( "u53",          0x80000, 0x80000, CRC(d401805d) SHA1(f4bcb2bdc45c3bc5ca423e518cdea8b3a7e8d60e) )
ROM_END


/*************************************
 *
 *	Driver init
 *
 *************************************/

static DRIVER_INIT( amerdart )
{
	/* set up code ROMs */
	memcpy(code_rom, memory_region(REGION_USER1), memory_region_length(REGION_USER1));
}

DRIVER_INIT( coolpool )
{
	memcpy(code_rom, memory_region(REGION_USER1), memory_region_length(REGION_USER1));

	/* remove IOP check */
	/* the check would pass, but the game crashes afterwards!
	   Disabling the check effectively disables the IOP communication, so inputs don't
	   work but the attract mode does. */
	code_rom[TOWORD(0xffe02c90-0xffe00000)] = 0x0300;
	code_rom[TOWORD(0xffe02ca0-0xffe00000)] = 0x0300;

	/* patch a loop during IOP test which doesn't get through... */
	code_rom[TOWORD(0xffe02ff0-0xffe00000)] = 0x0300;
}

static void decode_9ballsht(void)
{
	int a;
	data16_t *rom;

	/* decrypt the main program ROMs */
	for (a = 0;a < memory_region_length(REGION_USER1)/2;a++)
	{
		int hi,lo,nhi,nlo;

		hi = code_rom[a] >> 8;
		lo = code_rom[a] & 0xff;

		nhi = BITSWAP8(hi,5,2,0,7,6,4,3,1) ^ 0x29;
		if (hi & 0x01) nhi ^= 0x03;
		if (hi & 0x10) nhi ^= 0xc1;
		if (hi & 0x20) nhi ^= 0x40;
		if (hi & 0x40) nhi ^= 0x12;

		nlo = BITSWAP8(lo,5,3,4,6,7,1,2,0) ^ 0x80;
		if ((lo & 0x02) && (lo & 0x04)) nlo ^= 0x01;
		if (lo & 0x04) nlo ^= 0x0c;
		if (lo & 0x08) nlo ^= 0x10;

		code_rom[a] = (nhi << 8) | nlo;
	}

	/* decrypt the sub data ROMs */
	rom = (data16_t *)memory_region(REGION_USER2);
	for (a = 1;a < memory_region_length(REGION_USER2)/2;a+=4)
	{
		/* just swap bits 1 and 2 of the address */
		data16_t tmp = rom[a];
		rom[a] = rom[a+1];
		rom[a+1] = tmp;
	}
}

DRIVER_INIT( 9ballsht )
{
	memcpy(code_rom, memory_region(REGION_USER1), memory_region_length(REGION_USER1));

	decode_9ballsht();

	/* disable ROM test otherwise the patches would break it... */
	code_rom[TOWORD(0xffec0240-0xffc00000)] = 0x0300;

	/* patch out a part of the vblank interrupt handler that would cause a crash... */
	code_rom[TOWORD(0xffe55430-0xffc00000)] = 0xc059;
}

DRIVER_INIT( 9ballsh2 )
{
	memcpy(code_rom, memory_region(REGION_USER1), memory_region_length(REGION_USER1));

	decode_9ballsht();

	/* disable ROM test otherwise the patches would break it... */
	code_rom[TOWORD(0xffebd440-0xffc00000)] = 0x0300;

	/* patch out a part of the vblank interrupt handler that would cause a crash... */
	code_rom[TOWORD(0xffe54030-0xffc00000)] = 0xc059;
}

DRIVER_INIT( 9ballsh3 )
{
	memcpy(code_rom, memory_region(REGION_USER1), memory_region_length(REGION_USER1));

	decode_9ballsht();

	/* disable ROM test otherwise the patches would break it... */
	code_rom[TOWORD(0xffeba840-0xffc00000)] = 0x0300;

	/* patch out a part of the vblank interrupt handler that would cause a crash... */
	code_rom[TOWORD(0xffe53630-0xffc00000)] = 0xc059;
}




/*************************************
 *
 *	Game drivers
 *
 *************************************/

GAMEX( 1989, amerdart, 0,        amerdart, amerdart, amerdart, ROT0, "Ameri",   "AmeriDarts", GAME_UNEMULATED_PROTECTION | GAME_WRONG_COLORS | GAME_NO_SOUND | GAME_NOT_WORKING )
GAMEX( 1992, coolpool, 0,        coolpool, 9ballsht, coolpool, ROT0, "Catalina", "Cool Pool", GAME_NOT_WORKING | GAME_NO_SOUND )
GAMEX( 1993, 9ballsht, coolpool, 9ballsht, 9ballsht, 9ballsht, ROT0, "E-Scape EnterMedia (Bundra license)", "9-Ball Shootout (set 1)", GAME_NOT_WORKING | GAME_NO_SOUND )
GAMEX( 1993, 9ballsh2, coolpool, 9ballsht, 9ballsht, 9ballsh2, ROT0, "E-Scape EnterMedia (Bundra license)", "9-Ball Shootout (set 2)", GAME_NOT_WORKING | GAME_NO_SOUND )
GAMEX( 1993, 9ballsh3, coolpool, 9ballsht, 9ballsht, 9ballsh3, ROT0, "E-Scape EnterMedia (Bundra license)", "9-Ball Shootout (set 3)", GAME_NOT_WORKING | GAME_NO_SOUND )
