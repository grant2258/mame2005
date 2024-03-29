/***************************************************************************

	sndhrdw/mcr.c

	Functions to emulate general the various MCR sound cards.

***************************************************************************/

#include <stdio.h>

#include "driver.h"
#include "sndhrdw/mcr.h"
#include "sndhrdw/williams.h"
#include "cpu/m6800/m6800.h"
#include "cpu/m6809/m6809.h"
#include "sound/5220intf.h"
#include "sound/ay8910.h"
#include "sound/dac.h"
#include "mcr.h"



/*************************************
 *
 *	Global variables
 *
 *************************************/

UINT8 mcr_sound_config;



/*************************************
 *
 *	Statics
 *
 *************************************/

static UINT16 dacval;

/* SSIO-specific globals */
static UINT8 ssio_sound_cpu;
static UINT8 ssio_data[4];
static UINT8 ssio_status;
static UINT8 ssio_duty_cycle[2][3];

/* Chip Squeak Deluxe-specific globals */
static UINT8 csdeluxe_sound_cpu;
static UINT8 csdeluxe_dac_index;
extern struct pia6821_interface csdeluxe_pia_intf;

/* Turbo Chip Squeak-specific globals */
static UINT8 turbocs_sound_cpu;
static UINT8 turbocs_dac_index;
static UINT8 turbocs_status;
extern struct pia6821_interface turbocs_pia_intf;

/* Sounds Good-specific globals */
static UINT8 soundsgood_sound_cpu;
static UINT8 soundsgood_dac_index;
static UINT8 soundsgood_status;
extern struct pia6821_interface soundsgood_pia_intf;

/* Squawk n' Talk-specific globals */
static UINT8 squawkntalk_sound_cpu;
static UINT8 squawkntalk_tms_command;
static UINT8 squawkntalk_tms_strobes;
extern struct pia6821_interface squawkntalk_pia0_intf;
extern struct pia6821_interface squawkntalk_pia1_intf;



/*************************************
 *
 *	Generic MCR sound initialization
 *
 *************************************/

void mcr_sound_init(void)
{
	int sound_cpu = 1;
	int dac_index = 0;

	/* SSIO */
	if (mcr_sound_config & MCR_SSIO)
	{
		ssio_sound_cpu = sound_cpu++;
		ssio_reset_w(1);
		ssio_reset_w(0);
	}

	/* Turbo Chip Squeak */
	if (mcr_sound_config & MCR_TURBO_CHIP_SQUEAK)
	{
		pia_config(0, PIA_ALTERNATE_ORDERING, &turbocs_pia_intf);
		turbocs_dac_index = dac_index++;
		turbocs_sound_cpu = sound_cpu++;
		turbocs_reset_w(1);
		turbocs_reset_w(0);
	}

	/* Chip Squeak Deluxe */
	if (mcr_sound_config & MCR_CHIP_SQUEAK_DELUXE)
	{
		pia_config(0, PIA_ALTERNATE_ORDERING, &csdeluxe_pia_intf);
		csdeluxe_dac_index = dac_index++;
		csdeluxe_sound_cpu = sound_cpu++;
		csdeluxe_reset_w(1);
		csdeluxe_reset_w(0);
	}

	/* Sounds Good */
	if (mcr_sound_config & MCR_SOUNDS_GOOD)
	{
		/* special case: Spy Hunter 2 has both Turbo CS and Sounds Good, so we use PIA slot 1 */
		pia_config(1, PIA_ALTERNATE_ORDERING, &soundsgood_pia_intf);
		soundsgood_dac_index = dac_index++;
		soundsgood_sound_cpu = sound_cpu++;
		soundsgood_reset_w(1);
		soundsgood_reset_w(0);
	}

	/* Squawk n Talk */
	if (mcr_sound_config & MCR_SQUAWK_N_TALK)
	{
		pia_config(0, PIA_STANDARD_ORDERING, &squawkntalk_pia0_intf);
		pia_config(1, PIA_STANDARD_ORDERING, &squawkntalk_pia1_intf);
		squawkntalk_sound_cpu = sound_cpu++;
		squawkntalk_reset_w(1);
		squawkntalk_reset_w(0);
	}

	/* Advanced Audio */
	if (mcr_sound_config & MCR_WILLIAMS_SOUND)
	{
		williams_cvsd_init(sound_cpu++, 0);
		dac_index++;
		williams_cvsd_reset_w(1);
		williams_cvsd_reset_w(0);
	}

	/* reset any PIAs */
	pia_reset();
}



/*************************************
 *
 *	MCR SSIO communications
 *
 *	Z80, 2 AY-3812
 *
 *************************************/

/********* internal interfaces ***********/
static WRITE8_HANDLER( ssio_status_w )
{
	ssio_status = data;
}

static READ8_HANDLER( ssio_data_r )
{
	return ssio_data[offset];
}

static void ssio_delayed_data_w(int param)
{
	ssio_data[param >> 8] = param & 0xff;
}

static void ssio_update_volumes(void)
{
	int chip, chan;
	for (chip = 0; chip < 2; chip++)
		for (chan = 0; chan < 3; chan++)
			AY8910_set_volume(chip, chan, (ssio_duty_cycle[chip][chan] ^ 15) * 100 / 15);
}

static WRITE8_HANDLER( ssio_porta0_w )
{
	ssio_duty_cycle[0][0] = data & 15;
	ssio_duty_cycle[0][1] = data >> 4;
	ssio_update_volumes();
}

static WRITE8_HANDLER( ssio_portb0_w )
{
	ssio_duty_cycle[0][2] = data & 15;
	ssio_update_volumes();
}

static WRITE8_HANDLER( ssio_porta1_w )
{
	ssio_duty_cycle[1][0] = data & 15;
	ssio_duty_cycle[1][1] = data >> 4;
	ssio_update_volumes();
}

static WRITE8_HANDLER( ssio_portb1_w )
{
	ssio_duty_cycle[1][2] = data & 15;
	sound_global_enable(!(data & 0x80));
	ssio_update_volumes();
}

/********* external interfaces ***********/
WRITE8_HANDLER( ssio_data_w )
{
	timer_set(TIME_NOW, (offset << 8) | (data & 0xff), ssio_delayed_data_w);
}

READ8_HANDLER( ssio_status_r )
{
	return ssio_status;
}

void ssio_reset_w(int state)
{
	/* going high halts the CPU */
	if (state)
	{
		int i;

		cpunum_set_input_line(ssio_sound_cpu, INPUT_LINE_RESET, ASSERT_LINE);

		/* latches also get reset */
		for (i = 0; i < 4; i++)
			ssio_data[i] = 0;
		ssio_status = 0;
	}
	/* going low resets and reactivates the CPU */
	else
		cpunum_set_input_line(ssio_sound_cpu, INPUT_LINE_RESET, CLEAR_LINE);
}


/********* sound interfaces ***********/
struct AY8910interface ssio_ay8910_interface_1 =
{
	0,
	0,
	ssio_porta0_w,
	ssio_portb0_w
};

struct AY8910interface ssio_ay8910_interface_2 =
{
	0,
	0,
	ssio_porta1_w,
	ssio_portb1_w
};


/********* memory interfaces ***********/
ADDRESS_MAP_START( ssio_readmem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x3fff) AM_READ(MRA8_ROM)
	AM_RANGE(0x8000, 0x83ff) AM_READ(MRA8_RAM)
	AM_RANGE(0x9000, 0x9003) AM_READ(ssio_data_r)
	AM_RANGE(0xa001, 0xa001) AM_READ(AY8910_read_port_0_r)
	AM_RANGE(0xb001, 0xb001) AM_READ(AY8910_read_port_1_r)
	AM_RANGE(0xe000, 0xe000) AM_READ(MRA8_NOP)
	AM_RANGE(0xf000, 0xf000) AM_READ(input_port_5_r)
ADDRESS_MAP_END

ADDRESS_MAP_START( ssio_writemem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x3fff) AM_WRITE(MWA8_ROM)
	AM_RANGE(0x8000, 0x83ff) AM_WRITE(MWA8_RAM)
	AM_RANGE(0xa000, 0xa000) AM_WRITE(AY8910_control_port_0_w)
	AM_RANGE(0xa002, 0xa002) AM_WRITE(AY8910_write_port_0_w)
	AM_RANGE(0xb000, 0xb000) AM_WRITE(AY8910_control_port_1_w)
	AM_RANGE(0xb002, 0xb002) AM_WRITE(AY8910_write_port_1_w)
	AM_RANGE(0xc000, 0xc000) AM_WRITE(ssio_status_w)
	AM_RANGE(0xe000, 0xe000) AM_WRITE(MWA8_NOP)
ADDRESS_MAP_END


/********* machine driver ***********/
MACHINE_DRIVER_START(mcr_ssio)
	MDRV_CPU_ADD_TAG("ssio", Z80, 2000000)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_PROGRAM_MAP(ssio_readmem,ssio_writemem)
	MDRV_CPU_VBLANK_INT(irq0_line_hold,26)
	
	MDRV_SPEAKER_STANDARD_STEREO("left", "right")

	MDRV_SOUND_ADD_TAG("ssio.1", AY8910, 2000000)
	MDRV_SOUND_CONFIG(ssio_ay8910_interface_1)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "left", 0.33)

	MDRV_SOUND_ADD_TAG("ssio.2", AY8910, 2000000)
	MDRV_SOUND_CONFIG(ssio_ay8910_interface_2)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "right", 0.33)
MACHINE_DRIVER_END



/*************************************
 *
 *	Chip Squeak Deluxe communications
 *
 *	MC68000, 1 PIA, 10-bit DAC
 *
 *************************************/

/********* internal interfaces ***********/
static WRITE8_HANDLER( csdeluxe_porta_w )
{
	dacval = (dacval & ~0x3fc) | (data << 2);
	DAC_signed_data_16_w(csdeluxe_dac_index, dacval << 6);
}

static WRITE8_HANDLER( csdeluxe_portb_w )
{
	dacval = (dacval & ~0x003) | (data >> 6);
	DAC_signed_data_16_w(csdeluxe_dac_index, dacval << 6);
}

static void csdeluxe_irq(int state)
{
  	cpunum_set_input_line(csdeluxe_sound_cpu, 4, state ? ASSERT_LINE : CLEAR_LINE);
}

static void csdeluxe_delayed_data_w(int param)
{
	pia_0_portb_w(0, param & 0x0f);
	pia_0_ca1_w(0, ~param & 0x10);
}


/********* external interfaces ***********/
WRITE8_HANDLER( csdeluxe_data_w )
{
	timer_set(TIME_NOW, data, csdeluxe_delayed_data_w);
}

void csdeluxe_reset_w(int state)
{
	cpunum_set_input_line(csdeluxe_sound_cpu, INPUT_LINE_RESET, state ? ASSERT_LINE : CLEAR_LINE);
}


/********* memory interfaces ***********/
ADDRESS_MAP_START( csdeluxe_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x007fff) AM_READ(MRA16_ROM)
	AM_RANGE(0x018000, 0x018007) AM_READ(pia_0_msb_r)
	AM_RANGE(0x01c000, 0x01cfff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END

ADDRESS_MAP_START( csdeluxe_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x007fff) AM_WRITE(MWA16_ROM)
	AM_RANGE(0x018000, 0x018007) AM_WRITE(pia_0_msb_w)
	AM_RANGE(0x01c000, 0x01cfff) AM_WRITE(MWA16_RAM)
ADDRESS_MAP_END


/********* PIA interfaces ***********/
struct pia6821_interface csdeluxe_pia_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ 0, 0, 0, 0, 0, 0,
	/*outputs: A/B,CA/B2       */ csdeluxe_porta_w, csdeluxe_portb_w, 0, 0,
	/*irqs   : A/B             */ csdeluxe_irq, csdeluxe_irq
};


/********* machine driver ***********/
MACHINE_DRIVER_START(chip_squeak_deluxe)
	MDRV_CPU_ADD_TAG("csd", M68000, 15000000/2)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_PROGRAM_MAP(csdeluxe_readmem,csdeluxe_writemem)
	
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD_TAG("csd", DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END



/*************************************
 *
 *	MCR Sounds Good communications
 *
 *	MC68000, 1 PIA, 10-bit DAC
 *
 *************************************/

/********* internal interfaces ***********/
static WRITE8_HANDLER( soundsgood_porta_w )
{
	dacval = (dacval & ~0x3fc) | (data << 2);
	DAC_signed_data_16_w(soundsgood_dac_index, dacval << 6);
}

static WRITE8_HANDLER( soundsgood_portb_w )
{
	dacval = (dacval & ~0x003) | (data >> 6);
	DAC_signed_data_16_w(soundsgood_dac_index, dacval << 6);
	soundsgood_status = (data >> 4) & 3;
}

static void soundsgood_irq(int state)
{
  	cpunum_set_input_line(soundsgood_sound_cpu, 4, state ? ASSERT_LINE : CLEAR_LINE);
}

static void soundsgood_delayed_data_w(int param)
{
	pia_1_portb_w(0, (param >> 1) & 0x0f);
	pia_1_ca1_w(0, ~param & 0x01);
}


/********* external interfaces ***********/
WRITE8_HANDLER( soundsgood_data_w )
{
	timer_set(TIME_NOW, data, soundsgood_delayed_data_w);
}

READ8_HANDLER( soundsgood_status_r )
{
	return soundsgood_status;
}

void soundsgood_reset_w(int state)
{
	cpunum_set_input_line(soundsgood_sound_cpu, INPUT_LINE_RESET, state ? ASSERT_LINE : CLEAR_LINE);
}


/********* memory interfaces ***********/
ADDRESS_MAP_START( soundsgood_readmem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x03ffff) AM_READ(MRA16_ROM)
	AM_RANGE(0x060000, 0x060007) AM_READ(pia_1_msb_r)
	AM_RANGE(0x070000, 0x070fff) AM_READ(MRA16_RAM)
ADDRESS_MAP_END

ADDRESS_MAP_START( soundsgood_writemem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE(0x000000, 0x03ffff) AM_WRITE(MWA16_ROM)
	AM_RANGE(0x060000, 0x060007) AM_WRITE(pia_1_msb_w)
	AM_RANGE(0x070000, 0x070fff) AM_WRITE(MWA16_RAM)
ADDRESS_MAP_END


/********* PIA interfaces ***********/
/* Note: we map this board to PIA #1. It is only used in Spy Hunter and Spy Hunter 2 */
/* For Spy Hunter 2, we also have a Turbo Chip Squeak in PIA slot 0, so we don't want */
/* to interfere */
struct pia6821_interface soundsgood_pia_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ 0, 0, 0, 0, 0, 0,
	/*outputs: A/B,CA/B2       */ soundsgood_porta_w, soundsgood_portb_w, 0, 0,
	/*irqs   : A/B             */ soundsgood_irq, soundsgood_irq
};


/********* machine driver ***********/
MACHINE_DRIVER_START(sounds_good)
	MDRV_CPU_ADD_TAG("sg", M68000, 16000000/2)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_PROGRAM_MAP(soundsgood_readmem,soundsgood_writemem)
	
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD_TAG("sg", DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END



/*************************************
 *
 *	MCR Turbo Chip Squeak communications
 *
 *	MC6809, 1 PIA, 8-bit DAC
 *
 *************************************/

/********* internal interfaces ***********/
static WRITE8_HANDLER( turbocs_porta_w )
{
	dacval = (dacval & ~0x3fc) | (data << 2);
	DAC_signed_data_16_w(turbocs_dac_index, dacval << 6);
}

static WRITE8_HANDLER( turbocs_portb_w )
{
	dacval = (dacval & ~0x003) | (data >> 6);
	DAC_signed_data_16_w(turbocs_dac_index, dacval << 6);
	turbocs_status = (data >> 4) & 3;
}

static void turbocs_irq(int state)
{
	cpunum_set_input_line(turbocs_sound_cpu, M6809_IRQ_LINE, state ? ASSERT_LINE : CLEAR_LINE);
}

static void turbocs_delayed_data_w(int param)
{
	pia_0_portb_w(0, (param >> 1) & 0x0f);
	pia_0_ca1_w(0, ~param & 0x01);
}


/********* external interfaces ***********/
WRITE8_HANDLER( turbocs_data_w )
{
	timer_set(TIME_NOW, data, turbocs_delayed_data_w);
}

READ8_HANDLER( turbocs_status_r )
{
	return turbocs_status;
}

void turbocs_reset_w(int state)
{
	cpunum_set_input_line(turbocs_sound_cpu, INPUT_LINE_RESET, state ? ASSERT_LINE : CLEAR_LINE);
}


/********* memory interfaces ***********/
ADDRESS_MAP_START( turbocs_readmem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x07ff) AM_READ(MRA8_RAM)
	AM_RANGE(0x4000, 0x4003) AM_READ(pia_0_r)	/* Max RPM accesses the PIA here */
	AM_RANGE(0x6000, 0x6003) AM_READ(pia_0_r)
	AM_RANGE(0x8000, 0xffff) AM_READ(MRA8_ROM)
ADDRESS_MAP_END

ADDRESS_MAP_START( turbocs_writemem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x07ff) AM_WRITE(MWA8_RAM)
	AM_RANGE(0x4000, 0x4003) AM_WRITE(pia_0_w)	/* Max RPM accesses the PIA here */
	AM_RANGE(0x6000, 0x6003) AM_WRITE(pia_0_w)
	AM_RANGE(0x8000, 0xffff) AM_WRITE(MWA8_ROM)
ADDRESS_MAP_END


/********* PIA interfaces ***********/
struct pia6821_interface turbocs_pia_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ 0, 0, 0, 0, 0, 0,
	/*outputs: A/B,CA/B2       */ turbocs_porta_w, turbocs_portb_w, 0, 0,
	/*irqs   : A/B             */ turbocs_irq, turbocs_irq
};


/********* machine driver ***********/
MACHINE_DRIVER_START(turbo_chip_squeak)
	MDRV_CPU_ADD_TAG("tcs", M6809, 9000000/4)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_PROGRAM_MAP(turbocs_readmem,turbocs_writemem)
	
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD_TAG("tcs", DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.0)
MACHINE_DRIVER_END


MACHINE_DRIVER_START(turbo_chip_squeak_plus_sounds_good)
	MDRV_IMPORT_FROM(turbo_chip_squeak)
	MDRV_SPEAKER_REMOVE("mono")
	MDRV_IMPORT_FROM(sounds_good)
	
	MDRV_SOUND_REPLACE("tcs", DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.80)

	MDRV_SOUND_REPLACE("sg", DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.80)
MACHINE_DRIVER_END



/*************************************
 *
 *	MCR Squawk n Talk communications
 *
 *	MC6802, 2 PIAs, TMS5220, AY8912 (not used), 8-bit DAC (not used)
 *
 *************************************/

/********* internal interfaces ***********/
static WRITE8_HANDLER( squawkntalk_porta1_w )
{
	logerror("Write to AY-8912\n");
}

static WRITE8_HANDLER( squawkntalk_porta2_w )
{
	squawkntalk_tms_command = data;
}

static WRITE8_HANDLER( squawkntalk_portb2_w )
{
	/* bits 0-1 select read/write strobes on the TMS5220 */
	data &= 0x03;

	/* write strobe -- pass the current command to the TMS5220 */
	if (((data ^ squawkntalk_tms_strobes) & 0x02) && !(data & 0x02))
	{
		tms5220_data_w(offset, squawkntalk_tms_command);

		/* DoT expects the ready line to transition on a command/write here, so we oblige */
		pia_1_ca2_w(0, 1);
		pia_1_ca2_w(0, 0);
	}

	/* read strobe -- read the current status from the TMS5220 */
	else if (((data ^ squawkntalk_tms_strobes) & 0x01) && !(data & 0x01))
	{
		pia_1_porta_w(0, tms5220_status_r(offset));

		/* DoT expects the ready line to transition on a command/write here, so we oblige */
		pia_1_ca2_w(0, 1);
		pia_1_ca2_w(0, 0);
	}

	/* remember the state */
	squawkntalk_tms_strobes = data;
}

static void squawkntalk_irq(int state)
{
	cpunum_set_input_line(squawkntalk_sound_cpu, M6808_IRQ_LINE, state ? ASSERT_LINE : CLEAR_LINE);
}

static void squawkntalk_delayed_data_w(int param)
{
	pia_0_porta_w(0, ~param & 0x0f);
	pia_0_cb1_w(0, ~param & 0x10);
}


/********* external interfaces ***********/
WRITE8_HANDLER( squawkntalk_data_w )
{
	timer_set(TIME_NOW, data, squawkntalk_delayed_data_w);
}

void squawkntalk_reset_w(int state)
{
	cpunum_set_input_line(squawkntalk_sound_cpu, INPUT_LINE_RESET, state ? ASSERT_LINE : CLEAR_LINE);
}


/********* memory interfaces ***********/
ADDRESS_MAP_START( squawkntalk_readmem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x007f) AM_READ(MRA8_RAM)
	AM_RANGE(0x0080, 0x0083) AM_READ(pia_0_r)
	AM_RANGE(0x0090, 0x0093) AM_READ(pia_1_r)
	AM_RANGE(0xd000, 0xffff) AM_READ(MRA8_ROM)
ADDRESS_MAP_END

ADDRESS_MAP_START( squawkntalk_writemem, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0x007f) AM_WRITE(MWA8_RAM)
	AM_RANGE(0x0080, 0x0083) AM_WRITE(pia_0_w)
	AM_RANGE(0x0090, 0x0093) AM_WRITE(pia_1_w)
	AM_RANGE(0xd000, 0xffff) AM_WRITE(MWA8_ROM)
ADDRESS_MAP_END


/********* PIA interfaces ***********/
struct pia6821_interface squawkntalk_pia0_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ 0, 0, 0, 0, 0, 0,
	/*outputs: A/B,CA/B2       */ squawkntalk_porta1_w, 0, 0, 0,
	/*irqs   : A/B             */ squawkntalk_irq, squawkntalk_irq
};

struct pia6821_interface squawkntalk_pia1_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ 0, 0, 0, 0, 0, 0,
	/*outputs: A/B,CA/B2       */ squawkntalk_porta2_w, squawkntalk_portb2_w, 0, 0,
	/*irqs   : A/B             */ squawkntalk_irq, squawkntalk_irq
};


/********* machine driver ***********/
MACHINE_DRIVER_START(squawk_n_talk)
	MDRV_CPU_ADD_TAG("snt", M6802, 3580000/4)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_PROGRAM_MAP(squawkntalk_readmem,squawkntalk_writemem)
	
	MDRV_SPEAKER_STANDARD_MONO("mono")
	
	MDRV_SOUND_ADD_TAG("snt", TMS5220, 640000)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.60)
MACHINE_DRIVER_END
