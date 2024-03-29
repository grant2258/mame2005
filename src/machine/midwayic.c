/***************************************************************************

	Emulation of various Midway ICs

***************************************************************************/

#include "driver.h"
#include "midwayic.h"
#include "machine/idectrl.h"
#include "sndhrdw/cage.h"
#include "sndhrdw/dcs.h"
#include <time.h>


#define LOG_NVRAM			(0)

#define PRINTF_DEBUG		(0)
#define LOG_IOASIC			(0)
#define LOG_FIFO			(0)


/*************************************
 *
 *	Constants
 *
 *************************************/

#define PIC_NVRAM_SIZE		0x100
#define FIFO_SIZE			512



/*************************************
 *
 *	Type definitions
 *
 *************************************/

struct serial_state
{
	UINT8	data[16];
	UINT8	buffer;
	UINT8	index;
	UINT8	status;
	UINT8	bits;
	UINT8	ormask;
};

struct pic_state
{
	UINT16	latch;
	mame_time latch_expire_time;
	UINT8	state;
	UINT8	index;
	UINT8	total;
	UINT8	nvram_addr;
	UINT8	buffer[0x10];
	UINT8	nvram[PIC_NVRAM_SIZE];
	UINT8 	default_nvram[PIC_NVRAM_SIZE];
	UINT8	time_buf[8];
	UINT8	time_index;
	UINT8	time_just_written;
	UINT16	yearoffs;
	mame_timer *time_write_timer;
};

struct ioasic_state
{
	UINT32	reg[16];
	UINT8	has_dcs;
	UINT8	has_cage;
	UINT8	dcs_cpu;
	UINT8	shuffle_type;
	UINT8	shuffle_active;
	UINT8 *	shuffle_map;
	void 	(*irq_callback)(int);
	UINT8	irq_state;
	UINT16	sound_irq_state;
	UINT8	auto_ack;
	
	UINT16	fifo[FIFO_SIZE];
	UINT16	fifo_in;
	UINT16	fifo_out;
	UINT16	fifo_bytes;
	offs_t	fifo_force_buffer_empty_pc;
};



/*************************************
 *
 *	Local variables
 *
 *************************************/

static struct serial_state serial;
static struct pic_state pic;
static struct ioasic_state ioasic;




/*************************************
 *
 *	Serial number encoding
 *
 *************************************/

static void generate_serial_data(int upper)
{
	int year = atoi(Machine->gamedrv->year), month = 12, day = 11;
	UINT32 serial_number, temp;
	UINT8 serial_digit[9];

	serial_number = 123456;
	serial_number += upper * 1000000;

	serial_digit[0] = (serial_number / 100000000) % 10;
	serial_digit[1] = (serial_number / 10000000) % 10;
	serial_digit[2] = (serial_number / 1000000) % 10;
	serial_digit[3] = (serial_number / 100000) % 10;
	serial_digit[4] = (serial_number / 10000) % 10;
	serial_digit[5] = (serial_number / 1000) % 10;
	serial_digit[6] = (serial_number / 100) % 10;
	serial_digit[7] = (serial_number / 10) % 10;
	serial_digit[8] = (serial_number / 1) % 10;

	serial.data[12] = rand() & 0xff;
	serial.data[13] = rand() & 0xff;

	serial.data[14] = 0; /* ??? */
	serial.data[15] = 0; /* ??? */

	temp = 0x174 * (year - 1980) + 0x1f * (month - 1) + day;
	serial.data[10] = (temp >> 8) & 0xff;
	serial.data[11] = temp & 0xff;

	temp = serial_digit[4] + serial_digit[7] * 10 + serial_digit[1] * 100;
	temp = (temp + 5 * serial.data[13]) * 0x1bcd + 0x1f3f0;
	serial.data[7] = temp & 0xff;
	serial.data[8] = (temp >> 8) & 0xff;
	serial.data[9] = (temp >> 16) & 0xff;

	temp = serial_digit[6] + serial_digit[8] * 10 + serial_digit[0] * 100 + serial_digit[2] * 10000;
	temp = (temp + 2 * serial.data[13] + serial.data[12]) * 0x107f + 0x71e259;
	serial.data[3] = temp & 0xff;
	serial.data[4] = (temp >> 8) & 0xff;
	serial.data[5] = (temp >> 16) & 0xff;
	serial.data[6] = (temp >> 24) & 0xff;

	temp = serial_digit[5] * 10 + serial_digit[3] * 100;
	temp = (temp + serial.data[12]) * 0x245 + 0x3d74;
	serial.data[0] = temp & 0xff;
	serial.data[1] = (temp >> 8) & 0xff;
	serial.data[2] = (temp >> 16) & 0xff;

	/* special hack for RevX */
	serial.ormask = 0x80;
	if (upper == 419)
		serial.ormask = 0x00;
}



/*************************************
 *
 *	Original serial number PIC
 *	interface
 *
 *************************************/

void midway_serial_pic_init(int upper)
{
	generate_serial_data(upper);
}


void midway_serial_pic_reset_w(int state)
{
	if (state)
	{
		serial.index = 0;
		serial.status = 0;
		serial.buffer = 0;
	}
}


UINT8 midway_serial_pic_status_r(void)
{
	return serial.status;
}


UINT8 midway_serial_pic_r(void)
{
	logerror("%08X:security R = %04X\n", activecpu_get_pc(), serial.buffer);
	serial.status = 1;
	return serial.buffer;
}


void midway_serial_pic_w(UINT8 data)
{
	logerror("%08X:security W = %04X\n", activecpu_get_pc(), data);

	/* status seems to reflect the clock bit */
	serial.status = (data >> 4) & 1;

	/* on the falling edge, clock the next data byte through */
	if (!serial.status)
	{
		/* the self-test writes 1F, 0F, and expects to read an F in the low 4 bits */
		/* Cruis'n World expects the high bit to be set as well */
		if (data & 0x0f)
			serial.buffer = serial.ormask | data;
		else
			serial.buffer = serial.data[serial.index++ % sizeof(serial.data)];
	}
}



/*************************************
 *
 *	Second generation serial number
 *	PIC interface; this version also
 *	contained some NVRAM and a real
 *	time clock
 *
 *************************************/

INLINE UINT8 make_bcd(UINT8 data)
{
	return ((data / 10) << 4) | (data % 10);
}


INLINE UINT8 unmake_bcd(UINT8 data)
{
	return ((data & 0xf0) >> 4) * 10 + (data & 0x0f);
}


static void reset_timer(int param)
{
	pic.time_just_written = 0;
}


void midway_serial_pic2_init(int upper, int yearoffs)
{
	pic.yearoffs = yearoffs;
	pic.time_just_written = 0;
	pic.time_write_timer = timer_alloc(reset_timer);
	memset(pic.default_nvram, 0xff, sizeof(pic.default_nvram));
	generate_serial_data(upper);
}


void midway_serial_pic2_set_default_nvram(const UINT8 *nvram)
{
	memcpy(pic.default_nvram, nvram, sizeof(pic.default_nvram));
}


UINT8 midway_serial_pic2_status_r(void)
{
	UINT8 result = 0;

	/* if we're still holding the data ready bit high, do it */
	if (pic.latch & 0xf00)
	{
		if (compare_mame_times(mame_timer_get_time(), pic.latch_expire_time) > 0)
			pic.latch &= 0xff;
		else
			pic.latch -= 0x100;
		result = 1;
	}

	logerror("%06X:PIC status %d\n", activecpu_get_pc(), result);
	return result;
}


UINT8 midway_serial_pic2_r(void)
{
	UINT8 result = 0;

	/* PIC data register */
	logerror("%06X:PIC data read (index=%d total=%d latch=%03X) =", activecpu_get_pc(), pic.index, pic.total, pic.latch);

	/* return the current result */
	if (pic.latch & 0xf00)
		result = pic.latch & 0xff;

	/* otherwise, return 0xff if we have data ready */
	else if (pic.index < pic.total)
		result = 0xff;

	logerror("%02X\n", result);
	return result;
}


void midway_serial_pic2_w(UINT8 data)
{
	static FILE *nvramlog;
	if (LOG_NVRAM && !nvramlog)
		nvramlog = fopen("nvram.log", "w");

	/* PIC command register */
	if (pic.state == 0)
		logerror("%06X:PIC command %02X\n", activecpu_get_pc(), data);
	else
		logerror("%06X:PIC data %02X\n", activecpu_get_pc(), data);

	/* store in the latch, along with a bit to indicate we have data */
	pic.latch = (data & 0x00f) | 0x480;
	pic.latch_expire_time = add_subseconds_to_mame_time(mame_timer_get_time(), DOUBLE_TO_SUBSECONDS(1. / 1000.));
	if (data & 0x10)
	{
		int cmd = pic.state ? (pic.state & 0x0f) : (pic.latch & 0x0f);
		switch (cmd)
		{
			/* written to latch the next byte of data */
			case 0:
				if (pic.index < pic.total)
					pic.latch = 0x400 | pic.buffer[pic.index++];
				break;

			/* fetch the serial number */
			case 1:
				/* note: Biofreaks assumes that it can latch the next byte this way */
				if (pic.index < pic.total)
					pic.latch = 0x400 | pic.buffer[pic.index++];
				else
				{
					memcpy(pic.buffer, serial.data, 16);
					pic.total = 16;
					pic.index = 0;
#ifdef MAME_DEBUG
{
	extern int debug_key_pressed;
	debug_key_pressed = 1;
}
#endif
				}
				break;

			/* read the clock */
			case 3:
			{
				/* stuff it into the data bytes */
				pic.index = 0;
				pic.total = 0;
				
				/* if we haven't written a new time recently, use the real live time */
				if (!pic.time_just_written)
				{
					struct tm *exptime;
					time_t curtime;
					time(&curtime);
					exptime = localtime(&curtime);

					pic.buffer[pic.total++] = make_bcd(exptime->tm_sec);
					pic.buffer[pic.total++] = make_bcd(exptime->tm_min);
					pic.buffer[pic.total++] = make_bcd(exptime->tm_hour);
					pic.buffer[pic.total++] = make_bcd(exptime->tm_wday + 1);
					pic.buffer[pic.total++] = make_bcd(exptime->tm_mday);
					pic.buffer[pic.total++] = make_bcd(exptime->tm_mon + 1);
					pic.buffer[pic.total++] = make_bcd(exptime->tm_year - pic.yearoffs);
				}
				
				/* otherwise, just parrot back what was written to pass self tests */
				else
				{
					pic.buffer[pic.total++] = pic.time_buf[0];
					pic.buffer[pic.total++] = pic.time_buf[1];
					pic.buffer[pic.total++] = pic.time_buf[2];
					pic.buffer[pic.total++] = pic.time_buf[3];
					pic.buffer[pic.total++] = pic.time_buf[4];
					pic.buffer[pic.total++] = pic.time_buf[5];
					pic.buffer[pic.total++] = pic.time_buf[6];
				}
				break;
			}
			
			/* write the clock */
			case 4:
				
				/* if coming from state 0, go to state 1 (this is just the command byte) */
				if (pic.state == 0)
				{
					pic.state = 0x14;
					pic.time_index = 0;
				}
				
				/* if in states 1-2 put data in the buffer until it's full */
				else if (pic.state == 0x14)
				{
					pic.time_buf[pic.time_index] = pic.latch & 0x0f;
					pic.state = 0x24;
				}
				else if (pic.state == 0x24)
				{
					pic.time_buf[pic.time_index++] |= pic.latch << 4;
					
					/* if less than 7 bytes accumulated, go back to state 1 */
					if (pic.time_index < 7)
						pic.state = 0x14;
					
					/* otherwise, flag the time as having just been written for 1/2 second */
					else
					{
						timer_adjust(pic.time_write_timer, TIME_IN_SEC(0.5), 0, TIME_NEVER);
						pic.time_just_written = 1;
						pic.state = 0;
					}
				}
				break;

			/* write to NVRAM */
			case 5:

				/* if coming from state 0, go to state 1 (this is just the command byte) */
				if (pic.state == 0)
					pic.state = 0x15;

				/* coming from state 1, go to state 2 and latch the low 4 address bits */
				else if (pic.state == 0x15)
				{
					pic.nvram_addr = pic.latch & 0x0f;
					pic.state = 0x25;
				}

				/* coming from state 2, go to state 3 and latch the high 4 address bits */
				else if (pic.state == 0x25)
				{
					pic.state = 0x35;
					pic.nvram_addr |= pic.latch << 4;
				}

				/* coming from state 3, go to state 4 and write the low 4 bits */
				else if (pic.state == 0x35)
				{
					pic.state = 0x45;
					pic.nvram[pic.nvram_addr] = pic.latch & 0x0f;
				}

				/* coming from state 4, reset the states and write the upper 4 bits */
				else if (pic.state == 0x45)
				{
					pic.state = 0;
					pic.nvram[pic.nvram_addr] |= pic.latch << 4;
					if (nvramlog)
						fprintf(nvramlog, "Write byte %02X = %02X\n", pic.nvram_addr, pic.nvram[pic.nvram_addr]);
				}
				break;

			/* read from NVRAM */
			case 6:

				/* if coming from state 0, go to state 1 (this is just the command byte) */
				if (pic.state == 0)
					pic.state = 0x16;

				/* coming from state 1, go to state 2 and latch the low 4 address bits */
				else if (pic.state == 0x16)
				{
					pic.nvram_addr = pic.latch & 0x0f;
					pic.state = 0x26;
				}

				/* coming from state 2, reset the states and make the data available */
				else if (pic.state == 0x26)
				{
					pic.state = 0;
					pic.nvram_addr |= pic.latch << 4;

					pic.total = 0;
					pic.index = 0;
					pic.buffer[pic.total++] = pic.nvram[pic.nvram_addr];
					if (nvramlog)
						fprintf(nvramlog, "Read byte %02X = %02X\n", pic.nvram_addr, pic.nvram[pic.nvram_addr]);
				}
				break;
			
			/* reflect inverted? (Cruisin' Exotica) */
			case 8:
				pic.latch = 0x400 | (~cmd & 0xff);
				break;
		}
	}
}


NVRAM_HANDLER( midway_serial_pic2 )
{
	if (read_or_write)
		mame_fwrite(file, pic.nvram, sizeof(pic.nvram));
	else if (file)
		mame_fread(file, pic.nvram, sizeof(pic.nvram));
	else
		memcpy(pic.nvram, pic.default_nvram, sizeof(pic.nvram));
}



/*************************************
 *
 *	The I/O ASIC was first introduced
 *	in War Gods, then later used on
 *	the Seattle hardware
 *
 *************************************/

enum
{
	IOASIC_PORT0,		/* 0: input port 0 */
	IOASIC_PORT1,		/* 1: input port 1 */
	IOASIC_PORT2,		/* 2: input port 2 */
	IOASIC_PORT3,		/* 3: input port 3 */
	IOASIC_UARTCONTROL,	/* 4: controls some UART behavior */
	IOASIC_UARTOUT,		/* 5: UART output */
	IOASIC_UARTIN,		/* 6: UART input */
	IOASIC_UNKNOWN7,	/* 7: ??? */
	IOASIC_SOUNDCTL,	/* 8: sound communications control */
	IOASIC_SOUNDOUT,	/* 9: sound output port */
	IOASIC_SOUNDSTAT,	/* a: sound status port */
	IOASIC_SOUNDIN,		/* b: sound input port */
	IOASIC_PICOUT,		/* c: PIC output port */
	IOASIC_PICIN,		/* d: PIC input port */
	IOASIC_INTSTAT,		/* e: interrupt status */
	IOASIC_INTCTL		/* f: interrupt control */
};

static UINT16 ioasic_fifo_r(void);
static UINT16 ioasic_fifo_status_r(void);
static void ioasic_fifo_reset_w(int state);
static void ioasic_input_empty(int state);
static void ioasic_output_full(int state);
static void update_ioasic_irq(void);
static void cage_irq_handler(int state);

void midway_ioasic_init(int shuffle, int upper, int yearoffs, void (*irq_callback)(int))
{
	static UINT8 shuffle_maps[][16] =
	{
		{ 0x0,0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xa,0xb,0xc,0xd,0xe,0xf },	/* WarGods, WG3DH, SFRush, MK4 */
		{ 0x4,0x5,0x6,0x7,0xb,0xa,0x9,0x8,0x3,0x2,0x1,0x0,0xf,0xe,0xd,0xc },	/* Blitz, Blitz99 */
		{ 0x7,0x3,0x2,0x0,0x1,0xc,0xd,0xe,0xf,0x4,0x5,0x6,0x8,0x9,0xa,0xb },	/* Carnevil */
		{ 0x8,0x9,0xa,0xb,0x0,0x1,0x2,0x3,0xf,0xe,0xc,0xd,0x4,0x5,0x6,0x7 },	/* Calspeed, Gauntlet Legends */
		{ 0xf,0xe,0xd,0xc,0x4,0x5,0x6,0x7,0x9,0x8,0xa,0xb,0x2,0x3,0x1,0x0 },	/* Mace */
		{ 0xc,0xd,0xe,0xf,0x0,0x1,0x2,0x3,0x7,0x8,0x9,0xb,0xa,0x5,0x6,0x4 },	/* Gauntlet Dark Legacy */
		{ 0x7,0x4,0x5,0x6,0x2,0x0,0x1,0x3,0x8,0x9,0xa,0xb,0xd,0xc,0xe,0xf },	/* Vapor TRX */
		{ 0x7,0x4,0x5,0x6,0x2,0x0,0x1,0x3,0x8,0x9,0xa,0xb,0xd,0xc,0xe,0xf },	/* San Francisco Rush: The Rock */
		{ 0x1,0x2,0x3,0x0,0x4,0x5,0x6,0x7,0xa,0xb,0x8,0x9,0xc,0xd,0xe,0xf },	/* Hyperdrive */
	};

	/* do we have a DCS2 sound chip connected? (most likely) */
	ioasic.has_dcs = (mame_find_cpu_index("dcs2") != -1);
	ioasic.has_cage = (mame_find_cpu_index("cage") != -1);
	ioasic.dcs_cpu = mame_find_cpu_index("dcs2");
	ioasic.shuffle_type = shuffle;
	ioasic.shuffle_map = &shuffle_maps[shuffle][0];
	ioasic.auto_ack = 0;
	ioasic.irq_callback = irq_callback;
	
	/* initialize the PIC */
	midway_serial_pic2_init(upper, yearoffs);
	
	/* reset the chip */
	midway_ioasic_reset();
	ioasic.reg[IOASIC_SOUNDCTL] = 0x0001;
	
	/* configure the fifo */
	if (ioasic.has_dcs)
	{
		dcs_set_fifo_callbacks(ioasic_fifo_r, ioasic_fifo_status_r);
		dcs_set_io_callbacks(ioasic_output_full, ioasic_input_empty);
	}
	ioasic_fifo_reset_w(1);
	
	/* configure the CAGE IRQ */
	if (ioasic.has_cage)
		cage_set_irq_handler(cage_irq_handler);
}


void midway_ioasic_set_auto_ack(int auto_ack)
{
	ioasic.auto_ack = auto_ack;
}


void midway_ioasic_set_shuffle_state(int state)
{
	ioasic.shuffle_active = state;
}


void midway_ioasic_reset(void)
{
	ioasic.shuffle_active = 0;
	ioasic.sound_irq_state = 0x0080;
	ioasic.reg[IOASIC_INTCTL] = 0;
	if (ioasic.has_dcs)
		ioasic_fifo_reset_w(1);
	update_ioasic_irq();
	midway_serial_pic_reset_w(1);
}


static void update_ioasic_irq(void)
{
	UINT16 fifo_state = ioasic_fifo_status_r();
	UINT16 irqbits = 0x2000;
	UINT8 new_state;
	
	irqbits |= ioasic.sound_irq_state;
	if (ioasic.reg[IOASIC_UARTIN] & 0x1000)
		irqbits |= 0x1000;
	if (fifo_state & 8)
		irqbits |= 0x0008;
	if (irqbits)
		irqbits |= 0x0001;

	ioasic.reg[IOASIC_INTSTAT] = irqbits;
	
	new_state = ((ioasic.reg[IOASIC_INTCTL] & 0x0001) != 0) && ((ioasic.reg[IOASIC_INTSTAT] & ioasic.reg[IOASIC_INTCTL] & 0x3ffe) != 0);
	if (new_state != ioasic.irq_state)
	{
		ioasic.irq_state = new_state;
		if (ioasic.irq_callback)
			(*ioasic.irq_callback)(ioasic.irq_state ? ASSERT_LINE : CLEAR_LINE);
	}
}


static void cage_irq_handler(int reason)
{
	logerror("CAGE irq handler: %d\n", reason);
	ioasic.sound_irq_state = 0;
	if (reason & CAGE_IRQ_REASON_DATA_READY)
		ioasic.sound_irq_state |= 0x0040;
	if (reason & CAGE_IRQ_REASON_BUFFER_EMPTY)
		ioasic.sound_irq_state |= 0x0080;
	update_ioasic_irq();
}


static void ioasic_input_empty(int state)
{
//	logerror("ioasic_input_empty(%d)\n", state);
	if (state)
		ioasic.sound_irq_state |= 0x0080;
	else
		ioasic.sound_irq_state &= ~0x0080;
	update_ioasic_irq();
}


static void ioasic_output_full(int state)
{
//	logerror("ioasic_output_full(%d)\n", state);
	if (state)
		ioasic.sound_irq_state |= 0x0040;
	else
		ioasic.sound_irq_state &= ~0x0040;
	update_ioasic_irq();
}



/*************************************
 *
 *	ASIC sound FIFO; used by CarnEvil
 *
 *************************************/

static UINT16 ioasic_fifo_r(void)
{
	UINT16 result = 0;
	
	/* we can only read data if there's some to read! */
	if (ioasic.fifo_bytes != 0)
	{
		/* fetch the data from the buffer and update the IOASIC state */
		result = ioasic.fifo[ioasic.fifo_out++ % FIFO_SIZE];
		ioasic.fifo_bytes--;
		update_ioasic_irq();

		if (LOG_FIFO && (ioasic.fifo_bytes < 4 || ioasic.fifo_bytes >= FIFO_SIZE - 4))
			logerror("fifo_r(%04X): FIFO bytes = %d!\n", result, ioasic.fifo_bytes);
		
		/* if we just cleared the buffer, this may generate an IRQ on the master CPU */
		/* because of the way the streaming code works, we need to make sure that the */
		/* next status read indicates an empty buffer, even if we've timesliced and the */
		/* main CPU is handling the I/O ASIC interrupt */
		if (ioasic.fifo_bytes == 0 && ioasic.has_dcs)
		{
			ioasic.fifo_force_buffer_empty_pc = activecpu_get_pc();
			if (LOG_FIFO)
				logerror("fifo_r(%04X): FIFO empty, PC = %04X\n", result, ioasic.fifo_force_buffer_empty_pc);
		}
	}
	else
	{
		if (LOG_FIFO)
			logerror("fifo_r(): nothing to read!\n");
	}
	return result;
}


static UINT16 ioasic_fifo_status_r(void)
{
	UINT16 result = 0;
	
	if (ioasic.fifo_bytes == 0)
		result |= 0x08;
	if (ioasic.fifo_bytes >= FIFO_SIZE/2)
		result |= 0x10;
	if (ioasic.fifo_bytes >= FIFO_SIZE)
		result |= 0x20;
	
	/* kludge alert: if we're reading this from the DCS CPU itself, and we recently cleared */
	/* the FIFO, and we're within 16 instructions of the read that cleared the FIFO, make */
	/* sure the FIFO clear bit is set */
	if (ioasic.fifo_force_buffer_empty_pc && cpu_getactivecpu() == ioasic.dcs_cpu)
	{
		offs_t currpc = activecpu_get_pc();
		if (currpc >= ioasic.fifo_force_buffer_empty_pc && currpc < ioasic.fifo_force_buffer_empty_pc + 0x10)
		{
			ioasic.fifo_force_buffer_empty_pc = 0;
			result |= 0x08;
			if (LOG_FIFO)
				logerror("ioasic_fifo_status_r(%04X): force empty, PC = %04X\n", result, currpc);
		}
	}

	return result;
}


static void ioasic_fifo_reset_w(int state)
{
	/* on the high state, reset the FIFO data */
	if (state)
	{
		ioasic.fifo_in = 0;
		ioasic.fifo_out = 0;
		ioasic.fifo_bytes = 0;
		update_ioasic_irq();
	}
	if (LOG_FIFO)
		logerror("fifo_reset(%d)\n", state);
}


void midway_ioasic_fifo_w(data16_t data)
{
	/* if we have room, add it to the FIFO buffer */
	if (ioasic.fifo_bytes < FIFO_SIZE)
	{
		ioasic.fifo[ioasic.fifo_in++ % FIFO_SIZE] = data;
		ioasic.fifo_bytes++;
		update_ioasic_irq();
		if (LOG_FIFO && (ioasic.fifo_bytes < 4 || ioasic.fifo_bytes >= FIFO_SIZE - 4))
			logerror("fifo_w(%04X): FIFO bytes = %d!\n", data, ioasic.fifo_bytes);
	}
	else
	{
		if (LOG_FIFO)
			logerror("fifo_w(%04X): out of space!\n", data);
	}
	dcs_fifo_notify(ioasic.fifo_bytes, FIFO_SIZE);
}



/*************************************
 *
 *	I/O ASIC master read/write
 *
 *************************************/

READ32_HANDLER( midway_ioasic_packed_r )
{
	data32_t result = 0;
	if ((mem_mask & 0x0000ffff) != 0x0000ffff)
		result |= midway_ioasic_r(offset*2, 0xffff0000) & 0xffff;
	if ((mem_mask & 0xffff0000) != 0xffff0000)
		result |= (midway_ioasic_r(offset*2+1, 0xffff0000) & 0xffff) << 16;
	return result;
}


READ32_HANDLER( midway_ioasic_r )
{
	data32_t result;
	
	offset = ioasic.shuffle_active ? ioasic.shuffle_map[offset & 15] : offset;
	result = ioasic.reg[offset];

	switch (offset)
	{
		case IOASIC_PORT0:
			result = readinputport(0);
			/* bit 0 seems to be a ready flag before shuffling happens */
			if (!ioasic.shuffle_active)
			{
				result |= 0x0001;
				/* blitz99 wants bit bits 13-15 to be 1 */
				result &= ~0xe000;
				result |= 0x2000;
			}
			break;
			
		case IOASIC_PORT1:
			result = readinputport(1);
			break;
		
		case IOASIC_PORT2:
			result = readinputport(2);
			break;
		
		case IOASIC_PORT3:
			result = readinputport(3);
			break;
		
		case IOASIC_UARTIN:
			ioasic.reg[offset] &= ~0x1000;
			break;
		
		case IOASIC_SOUNDSTAT:
			/* status from sound CPU */
			result = 0;
			if (ioasic.has_dcs)
			{
				result |= ((dcs_control_r() >> 4) ^ 0x40) & 0x00c0;
				result |= ioasic_fifo_status_r() & 0x0038;
				result |= dcs_data2_r() & 0xff00;
			}
			else if (ioasic.has_cage)
			{
				result |= (cage_control_r() << 6) ^ 0x80;
			}
			else
				result |= 0x48;
			break;

		case IOASIC_SOUNDIN:
			result = 0;
			if (ioasic.has_dcs)
			{
				result = dcs_data_r();
				if (ioasic.auto_ack)
					dcs_ack_w();
			}
			else if (ioasic.has_cage)
				result = main_from_cage_r();
			else
			{
				static UINT16 val = 0;
				result = val = ~val;
			}
			break;
		
		case IOASIC_PICIN:
			result = midway_serial_pic2_r() | (midway_serial_pic2_status_r() << 8);
			break;
		
		default:
			break;
	}

	if (LOG_IOASIC && offset != IOASIC_SOUNDSTAT && offset != IOASIC_SOUNDIN)
		logerror("%06X:ioasic_r(%d) = %08X\n", activecpu_get_pc(), offset, result);

	return result;
}


WRITE32_HANDLER( midway_ioasic_packed_w )
{
	if ((mem_mask & 0x0000ffff) != 0x0000ffff)
		midway_ioasic_w(offset*2, data & 0xffff, 0xffff0000);
	if ((mem_mask & 0xffff0000) != 0xffff0000)
		midway_ioasic_w(offset*2+1, data >> 16, 0xffff0000);
}


WRITE32_HANDLER( midway_ioasic_w )
{
	UINT32 oldreg, newreg;
	
	offset = ioasic.shuffle_active ? ioasic.shuffle_map[offset & 15] : offset;
	oldreg = ioasic.reg[offset];
	COMBINE_DATA(&ioasic.reg[offset]);
	newreg = ioasic.reg[offset];

	if (LOG_IOASIC && offset != IOASIC_SOUNDOUT)
		logerror("%06X:ioasic_w(%d) = %08X\n", activecpu_get_pc(), offset, data);

	switch (offset)
	{
		case IOASIC_PORT0:
			/* the last write here seems to turn on shuffling */
			if (data == 0xe2)
			{
				ioasic.shuffle_active = 1;
				logerror("*** I/O ASIC shuffling enabled!\n");
				ioasic.reg[IOASIC_INTCTL] = 0;
				ioasic.reg[IOASIC_UARTCONTROL] = 0;	/* bug in 10th Degree assumes this */
			}
			break;
			
		case IOASIC_PORT2:
		case IOASIC_PORT3:
			/* ignore writes here if we're not shuffling yet */
			if (!ioasic.shuffle_active)
				break;
			break;
		
		case IOASIC_UARTOUT:
			if (ioasic.reg[IOASIC_UARTCONTROL] & 0x800)
			{
				/* we're in loopback mode -- copy to the input */
				ioasic.reg[IOASIC_UARTIN] = (newreg & 0x00ff) | 0x1000;
				update_ioasic_irq();
			}
			else if (PRINTF_DEBUG)
				printf("%c", data & 0xff);
			break;
			
		case IOASIC_SOUNDCTL:
			/* sound reset? */
			if (ioasic.has_dcs)
			{
				dcs_reset_w(~newreg & 1);
			}
			else if (ioasic.has_cage)
			{
				if ((oldreg ^ newreg) & 1)
				{
					cage_control_w(0);
					if (!(~newreg & 1))
						cage_control_w(3);
				}
			}
			
			/* FIFO reset? */
			ioasic_fifo_reset_w(~newreg & 4);
			break;

		case IOASIC_SOUNDOUT:
			if (ioasic.has_dcs)
				dcs_data_w(newreg);
			else if (ioasic.has_cage)
				main_to_cage_w(newreg);
			break;

		case IOASIC_SOUNDIN:
			dcs_ack_w();
			/* acknowledge data read */
			break;

		case IOASIC_PICOUT:
			if (ioasic.shuffle_type == MIDWAY_IOASIC_VAPORTRX)
				midway_serial_pic2_w(newreg ^ 0x0a);
			else if (ioasic.shuffle_type == MIDWAY_IOASIC_SFRUSHRK)
				midway_serial_pic2_w(newreg ^ 0x05);
			else
				midway_serial_pic2_w(newreg);
			break;
		
		case IOASIC_INTCTL:
			/* interrupt enables */
			/* bit  0 = global interrupt enable */
			/* bit  3 = FIFO empty */
			/* bit  6 = sound input buffer full */
			/* bit  7 = sound output buffer empty */
			/* bit 14 = LED? */
			if ((oldreg ^ newreg) & 0x3ff6)
				logerror("IOASIC int control = %04X\n", data);
			update_ioasic_irq();
			break;

		default:
			break;
	}
}



/*************************************
 *
 *	The IDE ASIC was used on War Gods
 *	and Killer Instinct to map the IDE
 *	registers
 *
 *************************************/

READ32_HANDLER( midway_ide_asic_r )
{
	/* convert to standard IDE offsets */
	offs_t ideoffs = 0x1f0/4 + (offset >> 2);
	UINT8 shift = 8 * (offset & 3);
	data32_t result;

	/* offset 0 is a special case */
	if (offset == 0)
		result = ide_controller32_0_r(ideoffs, 0xffff0000);

	/* everything else is byte-sized */
	else
		result = ide_controller32_0_r(ideoffs, ~(0xff << shift)) >> shift;
	return result;
}


WRITE32_HANDLER( midway_ide_asic_w )
{
	/* convert to standard IDE offsets */
	offs_t ideoffs = 0x1f0/4 + (offset >> 2);
	UINT8 shift = 8 * (offset & 3);

	/* offset 0 is a special case */
	if (offset == 0)
		ide_controller32_0_w(ideoffs, data, 0xffff0000);

	/* everything else is byte-sized */
	else
		ide_controller32_0_w(ideoffs, data << shift, ~(0xff << shift));;
}
