/*
	Flash ROM emulation

	Explicitly supports:
	Intel 28F016S5 (byte-wide)
	AMD/Fujitsu 29F016 (byte-wide)
	Sharp LH28F400 (word-wide)

	Flash ROMs use a standardized command set accross manufacturers,
	so this emulation should work even for non-Intel and non-Sharp chips
	as long as the game doesn't query the maker ID.
*/

#include "driver.h"
#include "intelfsh.h"

enum
{
	FM_NORMAL,	// normal read/write
	FM_READID,	// read ID
	FM_READSTATUS,	// read status
	FM_WRITEPART1,	// first half of programming, awaiting second
	FM_CLEARPART1,	// first half of clear, awaiting second
	FM_SETMASTER,	// first half of set master lock, awaiting on/off
	FM_READAMDID1,	// part 1 of alt ID sequence
	FM_READAMDID2,	// part 2 of alt ID sequence
	FM_READAMDID3,	// part 3 of alt ID sequence
};

struct flash_chip
{
	int type;
	int size;
	int bits;
	int flash_mode;
	int flash_master_lock;
	int device_id;
	int maker_id;
	void *flash_memory;
};

static struct flash_chip chips[FLASH_CHIPS_MAX];

void intelflash_init(int chip, int type, void *data)
{
	struct flash_chip *c;
	if( chip >= FLASH_CHIPS_MAX )
	{
		logerror( "intelflash_init: invalid chip %d\n", chip );
		return;
	}
	c = &chips[ chip ];

	c->type = type;
	switch( c->type )
	{
	case FLASH_INTEL_28F016S5:
		c->bits = 8;
		c->size = 0x200000;
		c->maker_id = 0x89;
		c->device_id = 0xaa;
		break;
	case FLASH_SHARP_LH28F400:
		c->bits = 16;
		c->size = 0x80000;
		c->maker_id = 0xb0;
		c->device_id = 0xed;
		break;
	case FLASH_FUJITSU_29F016A:
		c->bits = 8;
		c->size = 0x200000;
		c->maker_id = 0x89;
		c->device_id = 0xaa;
		break;
	case FLASH_INTEL_E28F008SA:
		c->bits = 8;
		c->size = 0x100000;
		c->maker_id = 0x89;
		c->device_id = 0xa2;
		break;
	case FLASH_INTEL_TE28F160:
		c->bits = 16;
		c->size = 0x200000;
		c->maker_id = 0xb0;
		c->device_id = 0xd0;
		break;
	}
	if( data == NULL )
	{
		data = auto_malloc( c->size );
		memset( data, 0xff, c->size );
	}

	c->flash_mode = FM_NORMAL;
	c->flash_master_lock = 0;
	c->flash_memory = data;
}

data32_t intelflash_read(int chip, data32_t address)
{
	data32_t data = 0;
	struct flash_chip *c;
	if( chip >= FLASH_CHIPS_MAX )
	{
		logerror( "intelflash_read: invalid chip %d\n", chip );
		return 0;
	}
	c = &chips[ chip ];

	switch( c->flash_mode )
	{
	default:
	case FM_NORMAL:
		switch( c->bits )
		{
		case 8:
			{
				data8_t *flash_memory;
				flash_memory = c->flash_memory;
				data = flash_memory[ address ];
			}
			break;
		case 16:
			{
				data16_t *flash_memory;
				flash_memory = c->flash_memory;
				data = flash_memory[ address ];
			}
			break;
		}
		break;
	case FM_READSTATUS:
//		c->flash_mode = FM_NORMAL;
		data = 0x80;
		break;
	case FM_READAMDID3:
		// DDR and baseball require Intel 29F016, fishing requires 280F16
		if( ( address & 1 ) != 0 )
		{
			data = c->device_id;
		}
		else
		{
			data = c->maker_id;
		}
		break;
	case FM_READID:
		switch (address)
		{
		case 0:	// maker ID
			data = c->maker_id;
			break;
		case 1:	// chip ID
			data = c->device_id;
			break;
		case 2:	// block lock config
			data = 0; // we don't support this yet
			break;
		case 3: // master lock config
			if (c->flash_master_lock)
			{
				data = 1;
			}
			else
			{
				data = 0;
			}
			break;
		}
		break;
	}

//	logerror( "%08x: intelflash_read( %d, %08x ) %08x\n", activecpu_get_pc(), chip, address, data );

	return data;
}

void intelflash_write(int chip, data32_t address, data32_t data)
{
	struct flash_chip *c;
	if( chip >= FLASH_CHIPS_MAX )
	{
		logerror( "intelflash_write: invalid chip %d\n", chip );
		return;
	}
	c = &chips[ chip ];

//	logerror( "%08x: intelflash_write( %d, %08x, %08x )\n", activecpu_get_pc(), chip, address, data );

	switch( c->flash_mode )
	{
	case FM_NORMAL:
	case FM_READSTATUS:
	case FM_READID:
	case FM_READAMDID3:
		switch( data & 0xff )
		{
		case 0xff:	// reset chip mode
			c->flash_mode = FM_NORMAL;
			break;
		case 0x90:	// read ID
			c->flash_mode = FM_READID;
			break;
		case 0x40:
		case 0x10:	// program
			c->flash_mode = FM_WRITEPART1;
			break;
		case 0x50:	// clear status reg
			c->flash_mode = FM_READSTATUS;
			break;
		case 0x20:	// block erase
			c->flash_mode = FM_CLEARPART1;
			break;
		case 0x60:	// set master lock
			c->flash_mode = FM_SETMASTER;
			break;
		case 0x70:	// read status	
			c->flash_mode = FM_READSTATUS;
			break;
		case 0xaa:	// AMD ID select part 1
			if (address == 0x555)
			{
				c->flash_mode = FM_READAMDID1;
			}
			break;
		default:
			printf( "Unknown flash mode byte %x\n", data & 0xff );
			break;
		}
		break;
	case FM_READAMDID1:
		if( address == 0x2aa && ( data & 0xff ) == 0x55 )
		{
			c->flash_mode = FM_READAMDID2;
		}
		else
		{
			c->flash_mode = FM_NORMAL;
		}
		break;
	case FM_READAMDID2:
		if( address == 0x555 && ( data & 0xff ) == 0x90 )
		{
			c->flash_mode = FM_READAMDID3;
		}
		else
		{
			c->flash_mode = FM_NORMAL;
		}
		break;
	case FM_WRITEPART1:
		switch( c->bits )
		{
		case 8:
			{
				data8_t *flash_memory = c->flash_memory;
				flash_memory[ address ] = data;
				break;
			}
		case 16:
			{
				data16_t *flash_memory = c->flash_memory;
				flash_memory[ address ] = data;
				break;
			}
		}
		c->flash_mode = FM_READSTATUS;
		break;
	case FM_CLEARPART1:
		if( ( data & 0xff ) == 0xd0 )
		{
			// clear the 64k block containing the current address
			// to all 0xffs
			data8_t *flash_memory = c->flash_memory;
			memset( &flash_memory[ address & 0xff0000 ], 0xff, 64 * 1024 );
			c->flash_mode = FM_READSTATUS;
			break;
		}
		break;
	case FM_SETMASTER:
		switch( data & 0xff )
		{
		case 0xf1:
			c->flash_master_lock = 1;
			break;
		case 0xd0:
			c->flash_master_lock = 0;
			break;
		}
		c->flash_mode = FM_NORMAL;
		break;
	}
}

static void nvram_handler_intelflash(int chip,mame_file *file,int read_or_write)
{
	struct flash_chip *c;
	if( chip >= FLASH_CHIPS_MAX )
	{
		logerror( "nvram_handler_intelflash: invalid chip %d\n", chip );
		return;
	}
	c = &chips[ chip ];

	if (read_or_write)
	{
		mame_fwrite( file, c->flash_memory, c->size );
	}
	else
	{
		if (file)
		{
			mame_fread( file, c->flash_memory, c->size );
		}
	}
}

NVRAM_HANDLER( intelflash_0 ) { nvram_handler_intelflash( 0, file, read_or_write ); }
NVRAM_HANDLER( intelflash_1 ) { nvram_handler_intelflash( 1, file, read_or_write ); }
NVRAM_HANDLER( intelflash_2 ) { nvram_handler_intelflash( 2, file, read_or_write ); }
NVRAM_HANDLER( intelflash_3 ) { nvram_handler_intelflash( 3, file, read_or_write ); }
NVRAM_HANDLER( intelflash_4 ) { nvram_handler_intelflash( 4, file, read_or_write ); }
NVRAM_HANDLER( intelflash_5 ) { nvram_handler_intelflash( 5, file, read_or_write ); }
NVRAM_HANDLER( intelflash_6 ) { nvram_handler_intelflash( 6, file, read_or_write ); }
NVRAM_HANDLER( intelflash_7 ) { nvram_handler_intelflash( 7, file, read_or_write ); }

static void nvram_handler_intelflash_16le( int chip, mame_file *file, int read_or_write )
{
	struct flash_chip *c;
	if( chip >= FLASH_CHIPS_MAX )
	{
		logerror( "nvram_handler_intelflash_16le: invalid chip %d\n", chip );
		return;
	}
	c = &chips[ chip ];

	if (read_or_write)
	{
		mame_fwrite_lsbfirst( file, c->flash_memory, c->size );
	}
	else
	{
		if (file)
		{
			mame_fread_lsbfirst( file, c->flash_memory, c->size );
		}
	}
}

NVRAM_HANDLER( intelflash_16le_0 ) { nvram_handler_intelflash_16le( 0, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_1 ) { nvram_handler_intelflash_16le( 1, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_2 ) { nvram_handler_intelflash_16le( 2, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_3 ) { nvram_handler_intelflash_16le( 3, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_4 ) { nvram_handler_intelflash_16le( 4, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_5 ) { nvram_handler_intelflash_16le( 5, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_6 ) { nvram_handler_intelflash_16le( 6, file, read_or_write ); }
NVRAM_HANDLER( intelflash_16le_7 ) { nvram_handler_intelflash_16le( 7, file, read_or_write ); }
