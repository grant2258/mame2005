/*
 * am53cf96.c
 *
 * AMD/NCR/Symbios 53CF96 SCSI-2 controller.
 * Qlogic FAS-236 and Emulex ESP-236 are equivalents
 *
 * References:
 * AMD Am53CF96 manual
 *
 */

#include "driver.h"
#include "state.h"
#include "am53cf96.h"

data8_t scsi_regs[32], fifo[16], fptr = 0, xfer_state, last_id;
static struct AM53CF96interface *intf;

typedef struct
{
	void *data;		// device's "this" pointer
	pSCSIDispatch handler;	// device's handler routine	
} SCSIDev;

static SCSIDev devices[8];	// SCSI IDs 0-7

// 53CF96 register set
enum
{
	REG_XFERCNTLOW = 0,	// read = current xfer count lo byte, write = set xfer count lo byte
	REG_XFERCNTMID,		// read = current xfer count mid byte, write = set xfer count mid byte
	REG_FIFO,		// read/write = FIFO
	REG_COMMAND,		// read/write = command

	REG_STATUS,		// read = status, write = destination SCSI ID (4)
	REG_IRQSTATE,		// read = IRQ status, write = timeout	      (5)
	REG_INTSTATE,		// read = internal state, write = sync xfer period (6)
	REG_FIFOSTATE,		// read = FIFO status, write = sync offset
	REG_CTRL1,		// read/write = control 1
	REG_CLOCKFCTR,		// clock factor (write only)
	REG_TESTMODE,		// test mode (write only)
	REG_CTRL2,		// read/write = control 2
	REG_CTRL3,		// read/write = control 3
	REG_CTRL4,		// read/write = control 4
	REG_XFERCNTHI,		// read = current xfer count hi byte, write = set xfer count hi byte
	REG_DATAALIGN		// data alignment (write only)
};

READ32_HANDLER( am53cf96_r )
{
	int reg, shift, rv;
	int states[] = { 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 0 };

	reg = offset * 2;
	if (mem_mask == 0xffffff00)
	{
		shift = 0;
	}
	else
	{
		reg++;
		shift = 16;
	}

	if (reg == REG_STATUS)
	{
		scsi_regs[REG_STATUS] &= ~0x7;
		scsi_regs[REG_STATUS] |= states[xfer_state];
		if (xfer_state < 10)
		{
			xfer_state++;
		}
	}

	rv = scsi_regs[reg]<<shift;

	if (reg == REG_FIFO)
	{
//		printf("53cf96: read FIFO PC=%x\n", activecpu_get_pc());
		return 0;
	}

//	logerror("53cf96: read reg %d = %x (PC=%x)\n", reg, rv>>shift, activecpu_get_pc());

	if (reg == REG_IRQSTATE)
	{
		scsi_regs[REG_STATUS] &= ~0x80;	// clear IRQ flag
	}

	return rv;
}

static void am53cf96_irq( int unused )
{
	scsi_regs[REG_IRQSTATE] = 8;	// indicate success
	scsi_regs[REG_STATUS] |= 0x80;	// indicate IRQ
	intf->irq_callback();
}

WRITE32_HANDLER( am53cf96_w )
{
	int reg, val, dma;

	reg = offset * 2;
	val = data;
	if (mem_mask == 0xffffff00)
	{
	}
	else
	{
		reg++;
		val >>= 16;
	}
	val &= 0xff;

//	logerror("53cf96: w %x to reg %d (ofs %02x data %08x mask %08x PC=%x)\n", val, reg, offset, data, mem_mask, activecpu_get_pc());

	// if writing to the target ID, cache it off for later
	if (reg == REG_STATUS)
	{
		last_id = val;
	}

	if (reg == REG_XFERCNTLOW || reg == REG_XFERCNTMID || reg == REG_XFERCNTHI)
	{
		scsi_regs[REG_STATUS] &= ~0x10;	// clear CTZ bit
	}

	// FIFO
	if (reg == REG_FIFO)
	{
//		printf("%02x to FIFO @ %02d\n", val, fptr);
		fifo[fptr++] = val;
		if (fptr > 15)
		{
			fptr = 15;
		}
	}

	// command
	if (reg == REG_COMMAND)
	{
		dma = (val & 0x80) ? 1 : 0;
		fptr = 0;
		switch (val & 0x7f)
		{
			case 0:	// NOP
				scsi_regs[REG_IRQSTATE] = 8;	// indicate success
				xfer_state = 0;
				break;
			case 3:	// reset SCSI bus
				scsi_regs[REG_INTSTATE] = 4;	// command sent OK
				xfer_state = 0;
				timer_set( TIME_IN_HZ( 16384 ), 0, am53cf96_irq );
				break;
			case 0x42:    	// select with ATN steps
				timer_set( TIME_IN_HZ( 16384 ), 0, am53cf96_irq );
				if ((fifo[1] == 0) || (fifo[1] == 0x48) || (fifo[1] == 0x4b))
				{
					scsi_regs[REG_INTSTATE] = 6;
				}
				else
				{
					scsi_regs[REG_INTSTATE] = 4;
				}

				logerror("53cf96: command %x exec.  target ID = %d (PC = %x)\n", fifo[1], last_id, activecpu_get_pc());
				if (devices[last_id].handler)
				{
					devices[last_id].handler(SCSIOP_EXEC_COMMAND, devices[last_id].data, 0, &fifo[1]);
				}
				else
				{
					logerror("53cf96: request for unknown device SCSI ID %d\n", last_id);
				}
				xfer_state = 0;
				break;
			case 0x44:	// enable selection/reselection
				xfer_state = 0;
				break;
			case 0x10:	// information transfer (must not change xfer_state)
			case 0x11:	// second phase of information transfer
			case 0x12:	// message accepted
				timer_set( TIME_IN_HZ( 16384 ), 0, am53cf96_irq );
				scsi_regs[REG_INTSTATE] = 6;	// command sent OK
				break;
		}
	}

	// only update the register mirror if it's not a write-only reg
	if (reg != REG_STATUS && reg != REG_INTSTATE && reg != REG_IRQSTATE && reg != REG_FIFOSTATE)
	{
		scsi_regs[reg] = val;
	}
}

void am53cf96_init( struct AM53CF96interface *interface )
{
	int i;

	// save interface pointer for later
	intf = interface;

	memset(scsi_regs, 0, sizeof(scsi_regs));
	memset(devices, 0, sizeof(devices));

	// try to open the devices
	for (i = 0; i < interface->scsidevs->devs_present; i++)
	{
		devices[interface->scsidevs->devices[i].scsiID].handler = interface->scsidevs->devices[i].handler;
		interface->scsidevs->devices[i].handler(SCSIOP_ALLOC_INSTANCE, &devices[interface->scsidevs->devices[i].scsiID].data, interface->scsidevs->devices[i].diskID, (data8_t *)NULL);
	}	

	state_save_register_UINT8("53cf96", 0, "registers", scsi_regs, 32);
	state_save_register_UINT8("53cf96", 0, "fifo", fifo, 16);
	state_save_register_UINT8("53cf96", 0, "fifo pointer", &fptr, 1);
	state_save_register_UINT8("53cf96", 0, "transfer state", &xfer_state, 1);
}

// retrieve data from the SCSI controller
void am53cf96_read_data(int bytes, data8_t *pData)
{
	scsi_regs[REG_STATUS] |= 0x10;	// indicate DMA finished

	if (devices[last_id].handler)
	{
		devices[last_id].handler(SCSIOP_READ_DATA, devices[last_id].data, bytes, pData);
	}
	else
	{
		logerror("53cf96: request for unknown device SCSI ID %d\n", last_id);
	}
}

// write data to the SCSI controller
void am53cf96_write_data(int bytes, data8_t *pData)
{
//	int i;

	scsi_regs[REG_STATUS] |= 0x10;	// indicate DMA finished

	if (devices[last_id].handler)
	{
		devices[last_id].handler(SCSIOP_WRITE_DATA, devices[last_id].data, bytes, pData);
	}
	else
	{
		logerror("53cf96: request for unknown device SCSI ID %d\n", last_id);
	}
}

// get the device handle (HD or CD) for the specified SCSI ID
void *am53cf96_get_device(int id)
{
	void *ret;

	if (devices[id].handler)
	{
		logerror("53cf96: fetching dev pointer for SCSI ID %d\n", id);
		devices[id].handler(SCSIOP_GET_DEVICE, devices[id].data, 0, (data8_t *)&ret);

		return ret;
	}

	return NULL;
}
