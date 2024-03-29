/***************************************************************************

	Hard Drivin' machine hardware

****************************************************************************/

#include "machine/atarigen.h"
#include "cpu/tms34010/tms34010.h"
#include "cpu/tms34010/34010ops.h"
#include "cpu/adsp2100/adsp2100.h"
#include "cpu/m68000/m68000.h"
#include "cpu/dsp32/dsp32.h"
#include "machine/asic65.h"
#include "sndhrdw/atarijsa.h"
#include "harddriv.h"


/*************************************
 *
 *	Constants and macros
 *
 *************************************/

#define DUART_CLOCK 		TIME_IN_HZ(36864000)
#define DS3_TRIGGER			7777

/* debugging tools */
#define LOG_COMMANDS		0



/*************************************
 *
 *	External definitions
 *
 *************************************/

/* externally accessible */
INT8 hdcpu_main;
INT8 hdcpu_gsp;
INT8 hdcpu_msp;
INT8 hdcpu_adsp;
INT8 hdcpu_sound;
INT8 hdcpu_sounddsp;
INT8 hdcpu_jsa;
INT8 hdcpu_dsp32;

UINT8 hd34010_host_access;
UINT8 hddsk_pio_access;

data16_t *hdmsp_ram;
data16_t *hddsk_ram;
data16_t *hddsk_rom;
data16_t *hddsk_zram;
data16_t *hd68k_slapstic_base;
data16_t *st68k_sloop_alt_base;

data16_t *hdadsp_data_memory;
data32_t *hdadsp_pgm_memory;

data16_t *hdgsp_protection;
data16_t *stmsp_sync[3];

data16_t *hdgsp_speedup_addr[2];
offs_t hdgsp_speedup_pc;

data16_t *hdmsp_speedup_addr;
offs_t hdmsp_speedup_pc;

data16_t *hdds3_speedup_addr;
offs_t hdds3_speedup_pc;
offs_t hdds3_transfer_pc;

data32_t *rddsp32_sync[2];

UINT32 gsp_speedup_count[4];
UINT32 msp_speedup_count[4];
UINT32 adsp_speedup_count[4];


/* from slapstic.c */
int slapstic_tweak(offs_t offset);
void slapstic_reset(void);


/* from vidhrdw */
extern UINT8 *hdgsp_vram;



/*************************************
 *
 *	Static globals
 *
 *************************************/

static UINT8 irq_state;
static UINT8 gsp_irq_state;
static UINT8 msp_irq_state;
static UINT8 adsp_irq_state;
static UINT8 duart_irq_state;

static UINT8 duart_read_data[16];
static UINT8 duart_write_data[16];
static UINT8 duart_output_port;
static void *duart_timer;

static UINT8 last_gsp_shiftreg;

static UINT8 m68k_zp1, m68k_zp2;
static UINT8 m68k_adsp_buffer_bank;

static UINT8 adsp_halt, adsp_br;
static UINT8 adsp_xflag;

static UINT16 adsp_sim_address;
static UINT16 adsp_som_address;
static UINT32 adsp_eprom_base;

static data16_t *sim_memory;
static data16_t *som_memory;
static UINT32 sim_memory_size;
static data16_t *adsp_pgm_memory_word;

static UINT8 ds3_gcmd, ds3_gflag, ds3_g68irqs, ds3_gfirqs, ds3_g68flag, ds3_send, ds3_reset;
static UINT16 ds3_gdata, ds3_g68data;
static UINT32 ds3_sim_address;

static UINT16 adc_control;
static UINT8 adc8_select;
static UINT8 adc8_data;
static UINT8 adc12_select;
static UINT8 adc12_byte;
static UINT16 adc12_data;

static UINT16 hdc68k_last_wheel;
static UINT16 hdc68k_last_port1;
static UINT8 hdc68k_wheel_edge;
static UINT8 hdc68k_shifter_state;

static UINT8 st68k_sloop_bank = 0;
static offs_t st68k_last_alt_sloop_offset;

#define MAX_MSP_SYNC	16
static data32_t *dataptr[MAX_MSP_SYNC];
static data32_t dataval[MAX_MSP_SYNC];
static int next_msp_sync;

static void hd68k_update_interrupts(void);
static void duart_callback(int param);



#if 0
#pragma mark * DRIVER/MULTISYNC BOARD
#endif


/*************************************
 *
 *	Initialization
 *
 *************************************/

MACHINE_INIT( harddriv )
{
	/* generic reset */
	atarigen_eeprom_reset();
	slapstic_reset();
	atarigen_interrupt_reset(hd68k_update_interrupts);

	/* halt several of the DSPs to start */
	if (hdcpu_adsp != -1) cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, ASSERT_LINE);
	if (hdcpu_dsp32 != -1) cpunum_set_input_line(hdcpu_dsp32, INPUT_LINE_HALT, ASSERT_LINE);
	if (hdcpu_sounddsp != -1) cpunum_set_input_line(hdcpu_sounddsp, INPUT_LINE_HALT, ASSERT_LINE);

	/* if we found a 6502, reset the JSA board */
	if (hdcpu_jsa != -1)
		atarijsa_reset();

	/* predetermine memory regions */
	sim_memory = (data16_t *)memory_region(REGION_USER1);
	som_memory = (data16_t *)memory_region(REGION_USER2);
	sim_memory_size = memory_region_length(REGION_USER1) / 2;
	adsp_pgm_memory_word = (data16_t *)((UINT8 *)hdadsp_pgm_memory + 1);

	last_gsp_shiftreg = 0;

	m68k_adsp_buffer_bank = 0;

	/* reset IRQ states */
	irq_state = gsp_irq_state = msp_irq_state = adsp_irq_state = duart_irq_state = 0;

	/* reset the DUART */
	memset(duart_read_data, 0, sizeof(duart_read_data));
	memset(duart_write_data, 0, sizeof(duart_write_data));
	duart_output_port = 0;
	duart_timer = timer_alloc(duart_callback);

	/* reset the ADSP/DSIII/DSIV boards */
	adsp_halt = 1;
	adsp_br = 0;
	adsp_xflag = 0;
}



/*************************************
 *
 *	68000 interrupt handling
 *
 *************************************/

static void hd68k_update_interrupts(void)
{
	int newstate = 0;

	if (msp_irq_state)
		newstate = 1;
	if (adsp_irq_state)
		newstate = 2;
	if (gsp_irq_state)
		newstate = 3;
	if (atarigen_sound_int_state)	/* /LINKIRQ on STUN Runner */
		newstate = 4;
	if (irq_state)
		newstate = 5;
	if (duart_irq_state)
		newstate = 6;

	if (newstate)
		cpunum_set_input_line(hdcpu_main, newstate, ASSERT_LINE);
	else
		cpunum_set_input_line(hdcpu_main, 7, CLEAR_LINE);
}


INTERRUPT_GEN( hd68k_irq_gen )
{
	irq_state = 1;
	atarigen_update_interrupts();
}


WRITE16_HANDLER( hd68k_irq_ack_w )
{
	irq_state = 0;
	atarigen_update_interrupts();
}


void hdgsp_irq_gen(int state)
{
	gsp_irq_state = state;
	atarigen_update_interrupts();
}


void hdmsp_irq_gen(int state)
{
	msp_irq_state = state;
	atarigen_update_interrupts();
}



/*************************************
 *
 *	68000 access to GSP
 *
 *************************************/

READ16_HANDLER( hd68k_gsp_io_r )
{
	data16_t result;
	offset = (offset / 2) ^ 1;
	hd34010_host_access = 1;
	result = tms34010_host_r(hdcpu_gsp, offset);
	hd34010_host_access = 0;
	return result;
}


WRITE16_HANDLER( hd68k_gsp_io_w )
{
	offset = (offset / 2) ^ 1;
	hd34010_host_access = 1;
	tms34010_host_w(hdcpu_gsp, offset, data);
	hd34010_host_access = 0;
}



/*************************************
 *
 *	68000 access to MSP
 *
 *************************************/

READ16_HANDLER( hd68k_msp_io_r )
{
	data16_t result;
	offset = (offset / 2) ^ 1;
	hd34010_host_access = 1;
	result = (hdcpu_msp != -1) ? tms34010_host_r(hdcpu_msp, offset) : 0xffff;
	hd34010_host_access = 0;
	return result;
}


WRITE16_HANDLER( hd68k_msp_io_w )
{
	offset = (offset / 2) ^ 1;
	if (hdcpu_msp != -1)
	{
		hd34010_host_access = 1;
		tms34010_host_w(hdcpu_msp, offset, data);
		hd34010_host_access = 0;
	}
}



/*************************************
 *
 *	68000 input handlers
 *
 *************************************/

READ16_HANDLER( hd68k_port0_r )
{
	/* port is as follows:

		0x0001 = DIAGN
		0x0002 = /HSYNCB
		0x0004 = /VSYNCB
		0x0008 = EOC12
		0x0010 = EOC8
		0x0020 = SELF-TEST
		0x0040 = COIN2
		0x0080 = COIN1
		0x0100 = SW1 #8
		0x0200 = SW1 #7
			.....
		0x8000 = SW1 #1
	*/
	int temp = readinputport(0);
	if (atarigen_get_hblank()) temp ^= 0x0002;
	temp ^= 0x0018;		/* both EOCs always high for now */
	return temp;
}


READ16_HANDLER( hdc68k_port1_r )
{
	data16_t result = readinputport(1);
	data16_t diff = result ^ hdc68k_last_port1;

	/* if a new shifter position is selected, use it */
	/* if it's the same shifter position as last time, go back to neutral */
	if ((diff & 0x0100) && !(result & 0x0100))
		hdc68k_shifter_state = (hdc68k_shifter_state == 1) ? 0 : 1;
	if ((diff & 0x0200) && !(result & 0x0200))
		hdc68k_shifter_state = (hdc68k_shifter_state == 2) ? 0 : 2;
	if ((diff & 0x0400) && !(result & 0x0400))
		hdc68k_shifter_state = (hdc68k_shifter_state == 4) ? 0 : 4;
	if ((diff & 0x0800) && !(result & 0x0800))
		hdc68k_shifter_state = (hdc68k_shifter_state == 8) ? 0 : 8;

	/* merge in the new shifter value */
	result = (result | 0x0f00) ^ (hdc68k_shifter_state << 8);

	/* merge in the wheel edge latch bit */
	if (hdc68k_wheel_edge)
		result ^= 0x4000;

	hdc68k_last_port1 = result;
	return result;
}


READ16_HANDLER( hda68k_port1_r )
{
	data16_t result = readinputport(1);

	/* merge in the wheel edge latch bit */
	if (hdc68k_wheel_edge)
		result ^= 0x4000;

	return result;
}


READ16_HANDLER( hdc68k_wheel_r )
{
	/* grab the new wheel value and upconvert to 12 bits */
	UINT16 new_wheel = readinputport(10) << 4;

	/* hack to display the wheel position */
	if (code_pressed(KEYCODE_LSHIFT))
		usrintf_showmessage("%04X", new_wheel);

	/* if we crossed the center line, latch the edge bit */
	if ((hdc68k_last_wheel / 0xf0) != (new_wheel / 0xf0))
		hdc68k_wheel_edge = 1;

	/* remember the last value and return the low 8 bits */
	hdc68k_last_wheel = new_wheel;
	return (new_wheel << 8) | 0xff;
}


READ16_HANDLER( hd68k_adc8_r )
{
	return adc8_data;
}


READ16_HANDLER( hd68k_adc12_r )
{
	return adc12_byte ? ((adc12_data >> 8) & 0x0f) : (adc12_data & 0xff);
}


READ16_HANDLER( hd68k_sound_reset_r )
{
	if (hdcpu_jsa != -1)
		atarijsa_reset();
	return ~0;
}



/*************************************
 *
 *	68000 output handlers
 *
 *************************************/

WRITE16_HANDLER( hd68k_adc_control_w )
{
	COMBINE_DATA(&adc_control);

	/* handle a write to the 8-bit ADC address select */
	if (adc_control & 0x08)
	{
		adc8_select = adc_control & 0x07;
		adc8_data = readinputport(2 + adc8_select);
	}

	/* handle a write to the 12-bit ADC address select */
	if (adc_control & 0x40)
	{
		adc12_select = (adc_control >> 4) & 0x03;
		adc12_data = readinputport(10 + adc12_select) << 4;
	}

	/* bit 7 selects which byte of the 12 bit data to read */
	adc12_byte = (adc_control >> 7) & 1;
}


WRITE16_HANDLER( hd68k_wr0_write )
{
	/* bit 3 selects the value; data is ignored */
	data = (offset >> 3) & 1;

	/* low 3 bits select the function */
	offset &= 7;
	switch (offset)
	{
		case 1:	/* SEL1 */
		case 2:	/* SEL2 */
		case 3:	/* SEL3 */
		case 4:	/* SEL4 */
		default:
			/* just ignore */
			break;

		case 6:	/* CC1 */
		case 7:	/* CC2 */
			coin_counter_w(offset - 6, data);
			break;
	}
}


WRITE16_HANDLER( hd68k_wr1_write )
{
	if (offset == 0) { //	logerror("Shifter Interface Latch = %02X\n", data);
	} else { 				logerror("/WR1(%04X)=%02X\n", offset, data);
	}
}


WRITE16_HANDLER( hd68k_wr2_write )
{
	if (offset == 0) { //	logerror("Steering Wheel Latch = %02X\n", data);
	} else { 				logerror("/WR2(%04X)=%02X\n", offset, data);
	}
}


WRITE16_HANDLER( hd68k_nwr_w )
{
	/* bit 3 selects the value; data is ignored */
	data = (offset >> 3) & 1;

	/* low 3 bits select the function */
	offset &= 7;
	switch (offset)
	{
		case 0:	/* CR2 */
		case 1:	/* CR1 */
			set_led_status(offset, data);
			break;
		case 2:	/* LC1 */
			break;
		case 3:	/* LC2 */
			break;
		case 4:	/* ZP1 */
			m68k_zp1 = data;
			break;
		case 5:	/* ZP2 */
			m68k_zp2 = data;
			break;
		case 6:	/* /GSPRES */
			logerror("Write to /GSPRES(%d)\n", data);
			if (hdcpu_gsp != -1)
				cpunum_set_input_line(hdcpu_gsp, INPUT_LINE_RESET, data ? CLEAR_LINE : ASSERT_LINE);
			break;
		case 7:	/* /MSPRES */
			logerror("Write to /MSPRES(%d)\n", data);
			if (hdcpu_msp != -1)
				cpunum_set_input_line(hdcpu_msp, INPUT_LINE_RESET, data ? CLEAR_LINE : ASSERT_LINE);
			break;
	}
}


WRITE16_HANDLER( hdc68k_wheel_edge_reset_w )
{
	/* reset the edge latch */
	hdc68k_wheel_edge = 0;
}



/*************************************
 *
 *	68000 ZRAM access
 *
 *************************************/

READ16_HANDLER( hd68k_zram_r )
{
	return atarigen_eeprom[offset];
}


WRITE16_HANDLER( hd68k_zram_w )
{
	if (m68k_zp1 == 0 && m68k_zp2 == 1)
		COMBINE_DATA(&atarigen_eeprom[offset]);
}



/*************************************
 *
 *	68000 DUART interface
 *
 *************************************/

/*
									DUART registers

			Read								Write
			----------------------------------	-------------------------------------------
	0x00 = 	Mode Register A (MR1A, MR2A) 		Mode Register A (MR1A, MR2A)
	0x02 =	Status Register A (SRA) 			Clock-Select Register A (CSRA)
	0x04 =	Clock-Select Register A 1 (CSRA) 	Command Register A (CRA)
	0x06 = 	Receiver Buffer A (RBA) 			Transmitter Buffer A (TBA)
	0x08 =	Input Port Change Register (IPCR) 	Auxiliary Control Register (ACR)
	0x0a = 	Interrupt Status Register (ISR) 	Interrupt Mask Register (IMR)
	0x0c = 	Counter Mode: Current MSB of 		Counter/Timer Upper Register (CTUR)
					Counter (CUR)
	0x0e = 	Counter Mode: Current LSB of 		Counter/Timer Lower Register (CTLR)
					Counter (CLR)
	0x10 = Mode Register B (MR1B, MR2B) 		Mode Register B (MR1B, MR2B)
	0x12 = Status Register B (SRB) 				Clock-Select Register B (CSRB)
	0x14 = Clock-Select Register B 2 (CSRB) 	Command Register B (CRB)
	0x16 = Receiver Buffer B (RBB) 				Transmitter Buffer B (TBB)
	0x18 = Interrupt-Vector Register (IVR) 		Interrupt-Vector Register (IVR)
	0x1a = Input Port (IP) 						Output Port Configuration Register (OPCR)
	0x1c = Start-Counter Command 3				Output Port Register (OPR): Bit Set Command 3
	0x1e = Stop-Counter Command 3				Output Port Register (OPR): Bit Reset Command 3
*/


INLINE double duart_clock_period(void)
{
	int mode = (duart_write_data[0x04] >> 4) & 7;
	if (mode != 3)
		logerror("DUART: unsupported clock mode %d\n", mode);
	return DUART_CLOCK * 16.0;
}


static void duart_callback(int param)
{
	logerror("DUART timer fired\n");
	if (duart_write_data[0x05] & 0x08)
	{
		logerror("DUART interrupt generated\n");
		duart_read_data[0x05] |= 0x08;
		duart_irq_state = (duart_read_data[0x05] & duart_write_data[0x05]) != 0;
		atarigen_update_interrupts();
	}
	timer_adjust(duart_timer, duart_clock_period() * 65536.0, 0, 0);
}


READ16_HANDLER( hd68k_duart_r )
{
	switch (offset)
	{
		case 0x00:		/* Mode Register A (MR1A, MR2A) */
		case 0x08:		/* Mode Register B (MR1B, MR2B) */
			return (duart_write_data[0x00] << 8) | 0x00ff;
		case 0x01:		/* Status Register A (SRA) */
		case 0x02:		/* Clock-Select Register A 1 (CSRA) */
		case 0x03:		/* Receiver Buffer A (RBA) */
		case 0x04:		/* Input Port Change Register (IPCR) */
		case 0x05:		/* Interrupt Status Register (ISR) */
		case 0x06:		/* Counter Mode: Current MSB of Counter (CUR) */
		case 0x07:		/* Counter Mode: Current LSB of Counter (CLR) */
		case 0x09:		/* Status Register B (SRB) */
		case 0x0a:		/* Clock-Select Register B 2 (CSRB) */
		case 0x0b:		/* Receiver Buffer B (RBB) */
		case 0x0c:		/* Interrupt-Vector Register (IVR) */
		case 0x0d:		/* Input Port (IP) */
			return (duart_read_data[offset] << 8) | 0x00ff;
		case 0x0e:		/* Start-Counter Command 3 */
		{
			int reps = (duart_write_data[0x06] << 8) | duart_write_data[0x07];
			timer_adjust(duart_timer, duart_clock_period() * (double)reps, 0, 0);
			logerror("DUART timer started (period=%f)\n", duart_clock_period() * (double)reps);
			return 0x00ff;
		}
		case 0x0f:		/* Stop-Counter Command 3 */
			{
				int reps = timer_timeleft(duart_timer) / duart_clock_period();
				timer_adjust(duart_timer, TIME_NEVER, 0, 0);
				duart_read_data[0x06] = reps >> 8;
				duart_read_data[0x07] = reps & 0xff;
				logerror("DUART timer stopped (final count=%04X)\n", reps);
			}
			duart_read_data[0x05] &= ~0x08;
			duart_irq_state = (duart_read_data[0x05] & duart_write_data[0x05]) != 0;
			atarigen_update_interrupts();
			return 0x00ff;
	}
	return 0x00ff;
}


WRITE16_HANDLER( hd68k_duart_w )
{
	if (ACCESSING_MSB)
	{
		int newdata = (data >> 8) & 0xff;
		duart_write_data[offset] = newdata;

		switch (offset)
		{
			case 0x00:		/* Mode Register A (MR1A, MR2A) */
			case 0x01:		/* Clock-Select Register A (CSRA) */
			case 0x02:		/* Command Register A (CRA) */
			case 0x03:		/* Transmitter Buffer A (TBA) */
			case 0x04:		/* Auxiliary Control Register (ACR) */
			case 0x05:		/* Interrupt Mask Register (IMR) */
			case 0x06:		/* Counter/Timer Upper Register (CTUR) */
			case 0x07:		/* Counter/Timer Lower Register (CTLR) */
			case 0x08:		/* Mode Register B (MR1B, MR2B) */
			case 0x09:		/* Clock-Select Register B (CSRB) */
			case 0x0a:		/* Command Register B (CRB) */
			case 0x0b:		/* Transmitter Buffer B (TBB) */
			case 0x0c:		/* Interrupt-Vector Register (IVR) */
			case 0x0d:		/* Output Port Configuration Register (OPCR) */
				break;
			case 0x0e:		/* Output Port Register (OPR): Bit Set Command 3 */
				duart_output_port |= newdata;
				break;
			case 0x0f:		/* Output Port Register (OPR): Bit Reset Command 3 */
				duart_output_port &= ~newdata;
				break;
		}
		logerror("DUART write %02X @ %02X\n", (data >> 8) & 0xff, offset);
	}
	else
		logerror("Unexpected DUART write %02X @ %02X\n", data, offset);
}



/*************************************
 *
 *	GSP I/O register writes
 *
 *************************************/

WRITE16_HANDLER( hdgsp_io_w )
{
	/* detect an enabling of the shift register and force yielding */
	if (offset == REG_DPYCTL)
	{
		UINT8 new_shiftreg = (data >> 11) & 1;
		if (new_shiftreg != last_gsp_shiftreg)
		{
			last_gsp_shiftreg = new_shiftreg;
			if (new_shiftreg)
				cpu_yield();
		}
	}

	/* detect changes to HEBLNK and HSBLNK and force an update before they change */
	if ((offset == REG_HEBLNK || offset == REG_HSBLNK) && data != tms34010_io_register_r(offset, 0))
		force_partial_update(cpu_getscanline() - 1);

	tms34010_io_register_w(offset, data, mem_mask);
}



/*************************************
 *
 *	GSP protection workarounds
 *
 *************************************/

WRITE16_HANDLER( hdgsp_protection_w )
{
	/* this memory address is incremented whenever a protection check fails */
	/* after it reaches a certain value, the GSP will randomly trash a */
	/* register; we just prevent it from ever going above 0 */
	*hdgsp_protection = 0;
}



/*************************************
 *
 *	MSP synchronization helpers
 *
 *************************************/

static void stmsp_sync_update(int param)
{
	int which = param >> 28;
	offs_t offset = (param >> 16) & 0xfff;
	data16_t data = param;
	stmsp_sync[which][offset] = data;
	cpu_triggerint(hdcpu_msp);
}


INLINE void stmsp_sync_w(int which, offs_t offset, data16_t data, data16_t mem_mask)
{
	data16_t newdata = stmsp_sync[which][offset];
	COMBINE_DATA(&newdata);

	/* if being written from the 68000, synchronize on it */
	if (hd34010_host_access)
		timer_set(TIME_NOW, newdata | (offset << 16) | (which << 28), stmsp_sync_update);

	/* otherwise, just update */
	else
		stmsp_sync[which][offset] = newdata;
}


WRITE16_HANDLER( stmsp_sync0_w )
{
	stmsp_sync_w(0, offset, data, mem_mask);
}


WRITE16_HANDLER( stmsp_sync1_w )
{
	stmsp_sync_w(1, offset, data, mem_mask);
}


WRITE16_HANDLER( stmsp_sync2_w )
{
	stmsp_sync_w(2, offset, data, mem_mask);
}



#if 0
#pragma mark -
#pragma mark * ADSP BOARD
#endif

/*************************************
 *
 *	68000 access to ADSP program memory
 *
 *************************************/

READ16_HANDLER( hd68k_adsp_program_r )
{
	UINT32 word = hdadsp_pgm_memory[offset/2];
	return (!(offset & 1)) ? (word >> 16) : (word & 0xffff);
}


WRITE16_HANDLER( hd68k_adsp_program_w )
{
	UINT32 *base = &hdadsp_pgm_memory[offset/2];
	UINT32 oldword = *base;
	data16_t temp;

	if (!(offset & 1))
	{
		temp = oldword >> 16;
		COMBINE_DATA(&temp);
		oldword = (oldword & 0x0000ffff) | (temp << 16);
	}
	else
	{
		temp = oldword & 0xffff;
		COMBINE_DATA(&temp);
		oldword = (oldword & 0xffff0000) | temp;
	}
	ADSP2100_WRPGM(base, oldword);
}



/*************************************
 *
 *	68000 access to ADSP data memory
 *
 *************************************/

READ16_HANDLER( hd68k_adsp_data_r )
{
	return hdadsp_data_memory[offset];
}


WRITE16_HANDLER( hd68k_adsp_data_w )
{
	COMBINE_DATA(&hdadsp_data_memory[offset]);

	/* any write to $1FFF is taken to be a trigger; synchronize the CPUs */
	if (offset == 0x1fff)
	{
		logerror("%06X:ADSP sync address written (%04X)\n", activecpu_get_previouspc(), data);
		timer_set(TIME_NOW, 0, 0);
		cpu_triggerint(hdcpu_adsp);
	}
	else
		logerror("%06X:ADSP W@%04X (%04X)\n", activecpu_get_previouspc(), offset, data);
}



/*************************************
 *
 *	68000 access to ADSP output memory
 *
 *************************************/

READ16_HANDLER( hd68k_adsp_buffer_r )
{
/*	logerror("hd68k_adsp_buffer_r(%04X)\n", offset);*/
	return som_memory[m68k_adsp_buffer_bank * 0x2000 + offset];
}


WRITE16_HANDLER( hd68k_adsp_buffer_w )
{
	COMBINE_DATA(&som_memory[m68k_adsp_buffer_bank * 0x2000 + offset]);
}



/*************************************
 *
 *	68000 access to ADSP control regs
 *
 *************************************/

static void deferred_adsp_bank_switch(int data)
{
#if LOG_COMMANDS
	if (m68k_adsp_buffer_bank != data && code_pressed(KEYCODE_L))
	{
		static FILE *commands;
		if (!commands) commands = fopen("commands.log", "w");
		if (commands)
		{
			INT16 *base = (INT16 *)&som_memory[data * 0x2000];
			INT16 *end = base + (UINT16)*base++;
			INT16 *current = base;
			INT16 *table = base + (UINT16)*current++;

			fprintf(commands, "\n---------------\n");

			while ((current + 5) < table)
			{
				int offset = (int)(current - base);
				int c1 = *current++;
				int c2 = *current++;
				int c3 = *current++;
				int c4 = *current++;
				fprintf(commands, "Cmd @ %04X = %04X  %d-%d @ %d\n", offset, c1, c2, c3, c4);
				while (current < table)
				{
					UINT32 rslope, lslope;
					rslope = (UINT16)*current++,
					rslope |= *current++ << 16;
					if (rslope == 0xffffffff)
					{
						fprintf(commands, "  (end)\n");
						break;
					}
					lslope = (UINT16)*current++,
					lslope |= *current++ << 16;
					fprintf(commands, "  L=%08X R=%08X count=%d\n",
							(int)lslope, (int)rslope, (int)*current++);
				}
			}
			fprintf(commands, "\nTable:\n");
			current = table;
			while (current < end)
				fprintf(commands, "  %04X\n", *current++);
		}
	}
#endif
	m68k_adsp_buffer_bank = data;
	logerror("ADSP bank = %d\n", data);
}


WRITE16_HANDLER( hd68k_adsp_control_w )
{
	/* bit 3 selects the value; data is ignored */
	int val = (offset >> 3) & 1;

	/* low 3 bits select the function */
	offset &= 7;
	switch (offset)
	{
		case 0:
		case 1:
			/* LEDs */
			break;

		case 3:
			logerror("ADSP bank = %d (deferred)\n", val);
			timer_set(TIME_NOW, val, deferred_adsp_bank_switch);
			break;

		case 5:
			/* connected to the /BR (bus request) line; this effectively halts */
			/* the ADSP at the next instruction boundary */
			adsp_br = !val;
			logerror("ADSP /BR = %d\n", !adsp_br);
			if (adsp_br || adsp_halt)
				cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, ASSERT_LINE);
			else
			{
				cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, CLEAR_LINE);
				/* a yield in this case is not enough */
				/* we would need to increase the interleaving otherwise */
				/* note that this only affects the test mode */
				cpu_spin();
			}
			break;

		case 6:
			/* connected to the /HALT line; this effectively halts */
			/* the ADSP at the next instruction boundary */
			adsp_halt = !val;
			logerror("ADSP /HALT = %d\n", !adsp_halt);
			if (adsp_br || adsp_halt)
				cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, ASSERT_LINE);
			else
			{
				cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, CLEAR_LINE);
				/* a yield in this case is not enough */
				/* we would need to increase the interleaving otherwise */
				/* note that this only affects the test mode */
				cpu_spin();
			}
			break;

		case 7:
			logerror("ADSP reset = %d\n", val);
			cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_RESET, val ? CLEAR_LINE : ASSERT_LINE);
			cpu_yield();
			break;

		default:
			logerror("ADSP control %02X = %04X\n", offset, data);
			break;
	}
}


WRITE16_HANDLER( hd68k_adsp_irq_clear_w )
{
	logerror("%06X:68k clears ADSP interrupt\n", activecpu_get_previouspc());
	adsp_irq_state = 0;
	atarigen_update_interrupts();
}


READ16_HANDLER( hd68k_adsp_irq_state_r )
{
	int result = 0xfffd;
	if (adsp_xflag) result ^= 2;
	if (adsp_irq_state) result ^= 1;
	logerror("%06X:68k reads ADSP interrupt state = %04x\n", activecpu_get_previouspc(), result);
	return result;
}



/*************************************
 *
 *	ADSP memory-mapped I/O
 *
 *************************************/

READ16_HANDLER( hdadsp_special_r )
{
	switch (offset & 7)
	{
		case 0:	/* /SIMBUF */
			if (adsp_eprom_base + adsp_sim_address < sim_memory_size)
				return sim_memory[adsp_eprom_base + adsp_sim_address++];
			else
				return 0xff;

		case 1:	/* /SIMLD */
			break;

		case 2:	/* /SOMO */
			break;

		case 3:	/* /SOMLD */
			break;

		default:
			logerror("%04X:hdadsp_special_r(%04X)\n", activecpu_get_previouspc(), offset);
			break;
	}
	return 0;
}


WRITE16_HANDLER( hdadsp_special_w )
{
	switch (offset & 7)
	{
		case 1:	/* /SIMCLK */
			adsp_sim_address = data;
			break;

		case 2:	/* SOMLATCH */
			som_memory[(m68k_adsp_buffer_bank ^ 1) * 0x2000 + (adsp_som_address++ & 0x1fff)] = data;
			break;

		case 3:	/* /SOMCLK */
			adsp_som_address = data;
			break;

		case 5:	/* /XOUT */
			adsp_xflag = data & 1;
			break;

		case 6:	/* /GINT */
			logerror("%04X:ADSP signals interrupt\n", activecpu_get_previouspc());
			adsp_irq_state = 1;
			atarigen_update_interrupts();
			break;

		case 7:	/* /MP */
			adsp_eprom_base = 0x10000 * data;
			break;

		default:
			logerror("%04X:hdadsp_special_w(%04X)=%04X\n", activecpu_get_previouspc(), offset, data);
			break;
	}
}



#if 0
#pragma mark -
#pragma mark * DS III BOARD
#endif

/*************************************
 *
 *	General DS III I/O
 *
 *************************************/

static void update_ds3_irq(void)
{
	/* update the IRQ2 signal to the ADSP2101 */
	if (!(!ds3_g68flag && ds3_g68irqs) && !(ds3_gflag && ds3_gfirqs))
		cpunum_set_input_line(hdcpu_adsp, ADSP2100_IRQ2, ASSERT_LINE);
	else
		cpunum_set_input_line(hdcpu_adsp, ADSP2100_IRQ2, CLEAR_LINE);
}


WRITE16_HANDLER( hd68k_ds3_control_w )
{
	int val = (offset >> 3) & 1;

	switch (offset & 7)
	{
		case 0:
			/* SRES - reset sound CPU */
			break;

		case 1:
			/* XRES - reset sound helper CPU */
			break;

		case 2:
			/* connected to the /BR (bus request) line; this effectively halts */
			/* the ADSP at the next instruction boundary */
			adsp_br = !val;
			if (adsp_br)
				cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, ASSERT_LINE);
			else
			{
				cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_HALT, CLEAR_LINE);
				/* a yield in this case is not enough */
				/* we would need to increase the interleaving otherwise */
				/* note that this only affects the test mode */
				cpu_spin();
			}
			break;

		case 3:
			cpunum_set_input_line(hdcpu_adsp, INPUT_LINE_RESET, val ? CLEAR_LINE : ASSERT_LINE);
			if (val && !ds3_reset)
			{
				ds3_gflag = 0;
				ds3_gcmd = 0;
				ds3_gfirqs = 0;
				ds3_g68irqs = !ds3_gfirqs;
				ds3_send = 0;
				update_ds3_irq();
			}
			ds3_reset = val;
			cpu_yield();
			logerror("DS III reset = %d\n", val);
			break;

		case 7:
			/* LED */
			break;

		default:
			logerror("DS III control %02X = %04X\n", offset, data);
			break;
	}
}



/*************************************
 *
 *	DS III graphics I/O
 *
 *************************************/

READ16_HANDLER( hd68k_ds3_girq_state_r )
{
	int result = 0x0fff;
	if (ds3_g68flag) result ^= 0x8000;
	if (ds3_gflag) result ^= 0x4000;
	if (ds3_g68irqs) result ^= 0x2000;
	if (!adsp_irq_state) result ^= 0x1000;
	return result;
}


READ16_HANDLER( hd68k_ds3_gdata_r )
{
	offs_t pc = activecpu_get_pc();

	ds3_gflag = 0;
	update_ds3_irq();

	logerror("%06X:hd68k_ds3_gdata_r(%04X)\n", activecpu_get_previouspc(), ds3_gdata);

	/* attempt to optimize the transfer if conditions are right */
	if (cpu_getactivecpu() == 0 && pc == hdds3_transfer_pc &&
		!(!ds3_g68flag && ds3_g68irqs) && !(ds3_gflag && ds3_gfirqs))
	{
		data32_t destaddr = activecpu_get_reg(M68K_A1);
		data16_t count68k = activecpu_get_reg(M68K_D1);
		data16_t mstat = cpunum_get_reg(hdcpu_adsp, ADSP2100_MSTAT);
		data16_t i6 = cpunum_get_reg(hdcpu_adsp, (mstat & 1) ? ADSP2100_MR0 : ADSP2100_MR0_SEC);
		data16_t l6 = cpunum_get_reg(hdcpu_adsp, ADSP2100_L6) - 1;
		data16_t m7 = cpunum_get_reg(hdcpu_adsp, ADSP2100_M7);

		logerror("%06X:optimizing 68k transfer, %d words\n", activecpu_get_previouspc(), count68k);

		while (count68k > 0 && hdadsp_data_memory[0x16e6] > 0)
		{
			program_write_word(destaddr, ds3_gdata);
			{
				hdadsp_data_memory[0x16e6]--;
				ds3_gdata = hdadsp_pgm_memory[i6] >> 8;
				i6 = (i6 & ~l6) | ((i6 + m7) & l6);
			}
			count68k--;
		}
		activecpu_set_reg(M68K_D1, count68k);
		cpunum_set_reg(hdcpu_adsp, (mstat & 1) ? ADSP2100_MR0 : ADSP2100_MR0_SEC, i6);
		adsp_speedup_count[1]++;
	}

	/* if we just cleared the IRQ, we are going to do some VERY timing critical reads */
	/* it is important that all the CPUs be in sync before we continue, so spin a little */
	/* while to let everyone else catch up */
	cpu_spinuntil_trigger(DS3_TRIGGER);
	cpu_triggertime(TIME_IN_USEC(5), DS3_TRIGGER);

	return ds3_gdata;
}


WRITE16_HANDLER( hd68k_ds3_gdata_w )
{
	logerror("%06X:hd68k_ds3_gdata_w(%04X)\n", activecpu_get_previouspc(), ds3_gdata);

	COMBINE_DATA(&ds3_g68data);
	ds3_g68flag = 1;
	ds3_gcmd = offset & 1;
	cpu_triggerint(hdcpu_adsp);
	update_ds3_irq();
}



/*************************************
 *
 *	DS III sound I/O
 *
 *************************************/

READ16_HANDLER( hd68k_ds3_sirq_state_r )
{
	return 0x4000;
}


READ16_HANDLER( hd68k_ds3_sdata_r )
{
	return 0;
}


WRITE16_HANDLER( hd68k_ds3_sdata_w )
{
}


/*************************************
 *
 *	DS III internal I/O
 *
 *************************************/

READ16_HANDLER( hdds3_special_r )
{
	int result;

	switch (offset & 7)
	{
		case 0:
			ds3_g68flag = 0;
			update_ds3_irq();
			return ds3_g68data;

		case 1:
			result = 0x0fff;
			if (ds3_gcmd) result ^= 0x8000;
			if (ds3_g68flag) result ^= 0x4000;
			if (ds3_gflag) result ^= 0x2000;
			return result;

		case 6:
			logerror("ADSP r @ %04x\n", ds3_sim_address);
			if (ds3_sim_address < sim_memory_size)
				return sim_memory[ds3_sim_address];
			else
				return 0xff;
	}
	return 0;
}


WRITE16_HANDLER( hdds3_special_w )
{
	/* IMPORTANT! these data values also write through to the underlying RAM */
	hdadsp_data_memory[offset] = data;

	switch (offset & 7)
	{
		case 0:
			logerror("%04X:ADSP sets gdata to %04X\n", activecpu_get_previouspc(), data);
			ds3_gdata = data;
			ds3_gflag = 1;
			update_ds3_irq();

			/* once we've written data, trigger the main CPU to wake up again */
			cpu_trigger(DS3_TRIGGER);
			break;

		case 1:
			logerror("%04X:ADSP sets interrupt = %d\n", activecpu_get_previouspc(), (data >> 1) & 1);
			adsp_irq_state = (data >> 1) & 1;
			hd68k_update_interrupts();
			break;

		case 2:
			ds3_send = (data >> 0) & 1;
			break;

		case 3:
			ds3_gfirqs = (data >> 1) & 1;
			ds3_g68irqs = !ds3_gfirqs;
			update_ds3_irq();
			break;

		case 4:
			ds3_sim_address = (ds3_sim_address & 0xffff0000) | (data & 0xffff);
			break;

		case 5:
			ds3_sim_address = (ds3_sim_address & 0xffff) | ((data << 16) & 0x00070000);
			break;
	}
}


READ16_HANDLER( hdds3_control_r )
{
	logerror("adsp2101 control r @ %04X\n", 0x3fe0 + offset);
	return 0;
}


WRITE16_HANDLER( hdds3_control_w )
{
	if (offset != 0x1e && offset != 0x1f)
		logerror("adsp2101 control w @ %04X = %04X\n", 0x3fe0 + offset, data);
}



/*************************************
 *
 *	DS III program memory handlers
 *
 *************************************/

READ16_HANDLER( hd68k_ds3_program_r )
{
	UINT32 *base = &hdadsp_pgm_memory[offset & 0x1fff];
	UINT32 word = *base;
	return (!(offset & 0x2000)) ? (word >> 8) : (word & 0xff);
}


WRITE16_HANDLER( hd68k_ds3_program_w )
{
	UINT32 *base = &hdadsp_pgm_memory[offset & 0x1fff];
	UINT32 oldword = *base;
	UINT16 temp;

	if (!(offset & 0x2000))
	{
		temp = oldword >> 8;
		COMBINE_DATA(&temp);
		oldword = (oldword & 0x000000ff) | (temp << 8);
	}
	else
	{
		temp = oldword & 0xff;
		COMBINE_DATA(&temp);
		oldword = (oldword & 0xffffff00) | (temp & 0xff);
	}
	ADSP2100_WRPGM(base, oldword);
}



#if 0
#pragma mark -
#pragma mark * DSK BOARD
#endif

/*************************************
 *
 *	DSK board IRQ generation
 *
 *************************************/

void hddsk_update_pif(UINT32 pins)
{
	atarigen_sound_int_state = ((pins & DSP32_OUTPUT_PIF) != 0);
	hd68k_update_interrupts();
}



/*************************************
 *
 *	DSK board control handlers
 *
 *************************************/

WRITE16_HANDLER( hd68k_dsk_control_w )
{
	int val = (offset >> 3) & 1;
	switch (offset & 7)
	{
		case 0:	/* DSPRESTN */
			cpunum_set_input_line(hdcpu_dsp32, INPUT_LINE_RESET, val ? CLEAR_LINE : ASSERT_LINE);
			break;

		case 1:	/* DSPZN */
			cpunum_set_input_line(hdcpu_dsp32, INPUT_LINE_HALT, val ? CLEAR_LINE : ASSERT_LINE);
			break;

		case 2:	/* ZW1 */
			break;

		case 3:	/* ZW2 */
			break;

		case 4:	/* ASIC65 reset */
			asic65_reset(!val);
			break;

		case 7:	/* LED */
			break;

		default:
			logerror("hd68k_dsk_control_w(%d) = %d\n", offset & 7, val);
			break;
	}
}



/*************************************
 *
 *	DSK board RAM/ZRAM/ROM handlers
 *
 *************************************/

READ16_HANDLER( hd68k_dsk_ram_r )
{
	return hddsk_ram[offset];
}


WRITE16_HANDLER( hd68k_dsk_ram_w )
{
	COMBINE_DATA(&hddsk_ram[offset]);
}


READ16_HANDLER( hd68k_dsk_zram_r )
{
	return hddsk_zram[offset];
}


WRITE16_HANDLER( hd68k_dsk_zram_w )
{
	COMBINE_DATA(&hddsk_zram[offset]);
}


READ16_HANDLER( hd68k_dsk_small_rom_r )
{
	return hddsk_rom[offset & 0x1ffff];
}


READ16_HANDLER( hd68k_dsk_rom_r )
{
	return hddsk_rom[offset];
}



/*************************************
 *
 *	DSK board DSP32C I/O handlers
 *
 *************************************/

WRITE16_HANDLER( hd68k_dsk_dsp32_w )
{
	hddsk_pio_access = 1;
	dsp32c_pio_w(hdcpu_dsp32, offset, data);
	hddsk_pio_access = 0;
}


READ16_HANDLER( hd68k_dsk_dsp32_r )
{
	data16_t result;
	hddsk_pio_access = 1;
	result = dsp32c_pio_r(hdcpu_dsp32, offset);
	hddsk_pio_access = 0;
	return result;
}


/*************************************
 *
 *	DSP32C synchronization
 *
 *************************************/

static void rddsp32_sync_cb(int param)
{
	*dataptr[param] = dataval[param];
}


WRITE32_HANDLER( rddsp32_sync0_w )
{
	if (hddsk_pio_access)
	{
		data32_t *dptr = &rddsp32_sync[0][offset];
		data32_t newdata = *dptr;
		COMBINE_DATA(&newdata);
		dataptr[next_msp_sync % MAX_MSP_SYNC] = dptr;
		dataval[next_msp_sync % MAX_MSP_SYNC] = newdata;
		timer_set(TIME_NOW, next_msp_sync++ % MAX_MSP_SYNC, rddsp32_sync_cb);
	}
	else
		COMBINE_DATA(&rddsp32_sync[0][offset]);
}


WRITE32_HANDLER( rddsp32_sync1_w )
{
	if (hddsk_pio_access)
	{
		data32_t *dptr = &rddsp32_sync[1][offset];
		data32_t newdata = *dptr;
		COMBINE_DATA(&newdata);
		dataptr[next_msp_sync % MAX_MSP_SYNC] = dptr;
		dataval[next_msp_sync % MAX_MSP_SYNC] = newdata;
		timer_set(TIME_NOW, next_msp_sync++ % MAX_MSP_SYNC, rddsp32_sync_cb);
	}
	else
		COMBINE_DATA(&rddsp32_sync[1][offset]);
}



#if 0
#pragma mark -
#pragma mark * DSPCOM BOARD
#endif

/*************************************
 *
 *	DSPCOM control handlers
 *
 *************************************/

WRITE16_HANDLER( hddspcom_control_w )
{
	int val = (offset >> 3) & 1;
	switch (offset & 7)
	{
		case 2:	/* ASIC65 reset */
			asic65_reset(!val);
			break;

		default:
			logerror("hddspcom_control_w(%d) = %d\n", offset & 7, val);
			break;
	}
}



#if 0
#pragma mark -
#pragma mark * GAME-SPECIFIC PROTECTION
#endif

/*************************************
 *
 *	Race Drivin' slapstic handling
 *
 *************************************/

WRITE16_HANDLER( rd68k_slapstic_w )
{
	slapstic_tweak(offset & 0x3fff);
}


READ16_HANDLER( rd68k_slapstic_r )
{
	int bank = slapstic_tweak(offset & 0x3fff) * 0x4000;
	return hd68k_slapstic_base[bank + (offset & 0x3fff)];
}



/*************************************
 *
 *	Steel Talons SLOOP handling
 *
 *************************************/

static int st68k_sloop_tweak(offs_t offset)
{
	static int last_offset;

	if (last_offset == 0)
	{
		switch (offset)
		{
			case 0x78e8:
				st68k_sloop_bank = 0;
				break;
			case 0x6ca4:
				st68k_sloop_bank = 1;
				break;
			case 0x15ea:
				st68k_sloop_bank = 2;
				break;
			case 0x6b28:
				st68k_sloop_bank = 3;
				break;
		}
	}
	last_offset = offset;
	return st68k_sloop_bank;
}


WRITE16_HANDLER( st68k_sloop_w )
{
	st68k_sloop_tweak(offset & 0x3fff);
}


READ16_HANDLER( st68k_sloop_r )
{
	int bank = st68k_sloop_tweak(offset) * 0x4000;
	return hd68k_slapstic_base[bank + (offset & 0x3fff)];
}


READ16_HANDLER( st68k_sloop_alt_r )
{
	if (st68k_last_alt_sloop_offset == 0x00fe)
	{
		switch (offset*2)
		{
			case 0x22c:
				st68k_sloop_bank = 0;
				break;
			case 0x1e2:
				st68k_sloop_bank = 1;
				break;
			case 0x1fa:
				st68k_sloop_bank = 2;
				break;
			case 0x206:
				st68k_sloop_bank = 3;
				break;
		}
	}
	st68k_last_alt_sloop_offset = offset*2;
	return st68k_sloop_alt_base[offset];
}


static int st68k_protosloop_tweak(offs_t offset)
{
	static int last_offset;

	if (last_offset == 0)
	{
		switch (offset)
		{
			case 0x0001:
				st68k_sloop_bank = 0;
				break;
			case 0x0002:
				st68k_sloop_bank = 1;
				break;
			case 0x0003:
				st68k_sloop_bank = 2;
				break;
			case 0x0004:
				st68k_sloop_bank = 3;
				break;
		}
	}
	last_offset = offset;
	return st68k_sloop_bank;
}


WRITE16_HANDLER( st68k_protosloop_w )
{
	st68k_protosloop_tweak(offset & 0x3fff);
}


READ16_HANDLER( st68k_protosloop_r )
{
	int bank = st68k_protosloop_tweak(offset) * 0x4000;
	return hd68k_slapstic_base[bank + (offset & 0x3fff)];
}



#if 0
#pragma mark -
#pragma mark * GSP OPTIMIZATIONS
#endif

/*************************************
 *
 *	GSP Optimizations - case 1
 *	Works for:
 *		Hard Drivin'
 *		STUN Runner
 *
 *************************************/

READ16_HANDLER( hdgsp_speedup_r )
{
	int result = hdgsp_speedup_addr[0][offset];

	/* if both this address and the other important address are not $ffff */
	/* then we can spin until something gets written */
	if (result != 0xffff && hdgsp_speedup_addr[1][0] != 0xffff &&
		cpu_getactivecpu() == hdcpu_gsp && activecpu_get_pc() == hdgsp_speedup_pc)
	{
		gsp_speedup_count[0]++;
		cpu_spinuntil_int();
	}

	return result;
}


WRITE16_HANDLER( hdgsp_speedup1_w )
{
	COMBINE_DATA(&hdgsp_speedup_addr[0][offset]);

	/* if $ffff is written, send an "interrupt" trigger to break us out of the spin loop */
	if (hdgsp_speedup_addr[0][offset] == 0xffff)
		cpu_triggerint(hdcpu_gsp);
}


WRITE16_HANDLER( hdgsp_speedup2_w )
{
	COMBINE_DATA(&hdgsp_speedup_addr[1][offset]);

	/* if $ffff is written, send an "interrupt" trigger to break us out of the spin loop */
	if (hdgsp_speedup_addr[1][offset] == 0xffff)
		cpu_triggerint(hdcpu_gsp);
}



/*************************************
 *
 *	GSP Optimizations - case 2
 *	Works for:
 *		Race Drivin'
 *
 *************************************/

READ16_HANDLER( rdgsp_speedup1_r )
{
	int result = hdgsp_speedup_addr[0][offset];

	/* if this address is equal to $f000, spin until something gets written */
	if (cpu_getactivecpu() == hdcpu_gsp && activecpu_get_pc() == hdgsp_speedup_pc &&
		(result & 0xff) < activecpu_get_reg(TMS34010_A1))
	{
		gsp_speedup_count[0]++;
		cpu_spinuntil_int();
	}

	return result;
}


WRITE16_HANDLER( rdgsp_speedup1_w )
{
	COMBINE_DATA(&hdgsp_speedup_addr[0][offset]);
	if (cpu_getactivecpu() != hdcpu_gsp)
		cpu_triggerint(hdcpu_gsp);
}



#if 0
#pragma mark -
#pragma mark * MSP OPTIMIZATIONS
#endif

/*************************************
 *
 *	MSP Optimizations
 *
 *************************************/

READ16_HANDLER( hdmsp_speedup_r )
{
	int data = hdmsp_speedup_addr[offset];

	if (data == 0 && activecpu_get_pc() == hdmsp_speedup_pc && cpu_getactivecpu() == hdcpu_msp)
	{
		msp_speedup_count[0]++;
		cpu_spinuntil_int();
	}

	return data;
}


WRITE16_HANDLER( hdmsp_speedup_w )
{
	COMBINE_DATA(&hdmsp_speedup_addr[offset]);
	if (offset == 0 && hdmsp_speedup_addr[offset] != 0)
		cpu_triggerint(hdcpu_msp);
}


READ16_HANDLER( stmsp_speedup_r )
{
	/* assumes: stmsp_sync[0] -> $80010, stmsp_sync[1] -> $99680, stmsp_sync[2] -> $99d30 */
	if (stmsp_sync[0][0] == 0 &&		/* 80010 */
		stmsp_sync[0][1] == 0 &&		/* 80020 */
		stmsp_sync[0][2] == 0 &&		/* 80030 */
		stmsp_sync[0][3] == 0 &&		/* 80040 */
		stmsp_sync[0][4] == 0 &&		/* 80050 */
		stmsp_sync[0][5] == 0 && 		/* 80060 */
		stmsp_sync[0][6] == 0 && 		/* 80070 */
		stmsp_sync[1][0] == 0 && 		/* 99680 */
		stmsp_sync[2][0] == 0xffff && 	/* 99d30 */
		stmsp_sync[2][1] == 0xffff && 	/* 99d40 */
		stmsp_sync[2][2] == 0 &&	 	/* 99d50 */
		activecpu_get_pc() == 0x3c0)
	{
		msp_speedup_count[0]++;
		cpu_spinuntil_int();
	}
	return stmsp_sync[0][1];
}



#if 0
#pragma mark -
#pragma mark * ADSP OPTIMIZATIONS
#endif

/*************************************
 *
 *	ADSP Optimizations
 *
 *************************************/

READ16_HANDLER( hdadsp_speedup_r )
{
	int data = hdadsp_data_memory[0x1fff];

	if (data == 0xffff && activecpu_get_pc() <= 0x3b && cpu_getactivecpu() == hdcpu_adsp)
	{
		adsp_speedup_count[0]++;
		cpu_spinuntil_int();
	}

	return data;
}


READ16_HANDLER( hdds3_speedup_r )
{
	int data = *hdds3_speedup_addr;

	if (data != 0 && activecpu_get_pc() == hdds3_speedup_pc && cpu_getactivecpu() == hdcpu_adsp)
	{
		adsp_speedup_count[2]++;
		cpu_spinuntil_int();
	}

	return data;
}
