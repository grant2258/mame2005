/***************************************************************************

  vidhrdw.c

  Functions to emulate the video hardware of the machine.

***************************************************************************/

#include "driver.h"
#include "tilemap.h"
#include "vidhrdw/generic.h"

data16_t *splash_vregs;
data16_t *splash_videoram;
data16_t *splash_spriteram;
data16_t *splash_pixelram;
data16_t *roldfrog_bitmap_mode;
int splash_bitmap_type;
static struct tilemap *screen[2];

/***************************************************************************

	Callbacks for the TileMap code

***************************************************************************/

/*
	Tile format
	-----------

	Screen 0: (64*32, 8x8 tiles)

	Word | Bit(s)			 | Description
	-----+-FEDCBA98-76543210-+--------------------------
	  0  | -------- xxxxxxxx | sprite code (low 8 bits)
	  0  | ----xxxx -------- | sprite code (high 4 bits)
	  0  | xxxx---- -------- | color

	Screen 1: (32*32, 16x16 tiles)

	Word | Bit(s)			 | Description
	-----+-FEDCBA98-76543210-+--------------------------
	  0  | -------- -------x | flip y
	  0  | -------- ------x- | flip x
	  0  | -------- xxxxxx-- | sprite code (low 6 bits)
	  0  | ----xxxx -------- | sprite code (high 4 bits)
	  0  | xxxx---- -------- | color
*/

static void get_tile_info_splash_screen0(int tile_index)
{
	int data = splash_videoram[tile_index];
	int attr = data >> 8;
	int code = data & 0xff;

	SET_TILE_INFO(
			0,
			code + ((0x20 + (attr & 0x0f)) << 8),
			(attr & 0xf0) >> 4,
			0)
}

static void get_tile_info_splash_screen1(int tile_index)
{
	int data = splash_videoram[(0x1000/2) + tile_index];
	int attr = data >> 8;
	int code = data & 0xff;

	SET_TILE_INFO(
			1,
			(code >> 2) + ((0x30 + (attr & 0x0f)) << 6),
			(attr & 0xf0) >> 4,
			TILE_FLIPXY(code & 0x03))
}

/***************************************************************************

	Memory Handlers

***************************************************************************/

READ16_HANDLER( splash_vram_r )
{
	return splash_videoram[offset];
}

WRITE16_HANDLER( splash_vram_w )
{
	int oldword = splash_videoram[offset];
	COMBINE_DATA(&splash_videoram[offset]);

	if (oldword != splash_videoram[offset])
		tilemap_mark_tile_dirty(screen[offset >> 11],((offset << 1) & 0x0fff) >> 1);
}

static void splash_draw_bitmap(struct mame_bitmap *bitmap,const struct rectangle *cliprect)
{
	int sx,sy,color,count,colxor,bitswap;
	colxor = 0; /* splash and some bitmap modes in roldfrog */
	bitswap = 0;

	if (splash_bitmap_type==1) /* roldfrog */
	{
		if (roldfrog_bitmap_mode[0]==0x0000)
		{
			colxor = 0x7f;
		}
		else if (roldfrog_bitmap_mode[0]==0x0100)
		{
			bitswap = 1;
		}
		else if (roldfrog_bitmap_mode[0]==0x0200)
		{
			colxor = 0x55;
		}
		else if (roldfrog_bitmap_mode[0]==0x0300)
		{
			bitswap = 2;
			colxor = 0x7f;
		}
		else if (roldfrog_bitmap_mode[0]==0x0400)
		{
			bitswap = 3;
		}
		else if (roldfrog_bitmap_mode[0]==0x0500)
		{
			bitswap = 4;
		}
		else if (roldfrog_bitmap_mode[0]==0x0600)
		{
			bitswap = 5;
			colxor = 0x7f;
		}
		else if (roldfrog_bitmap_mode[0]==0x0700)
		{
			bitswap = 6;
			colxor = 0x55;
		}
	}

	count = 0;
	for (sy=0;sy<256;sy++)
	{
		for (sx=0;sx<512;sx++)
		{
			color = splash_pixelram[count]&0xff;
			count++;

			switch( bitswap )
			{
			case 1:
				color = BITSWAP8(color,7,0,1,2,3,4,5,6);
				break;
			case 2:
				color = BITSWAP8(color,7,4,6,5,1,0,3,2);
				break;
			case 3:
				color = BITSWAP8(color,7,3,2,1,0,6,5,4);
				break;
			case 4:
				color = BITSWAP8(color,7,6,4,2,0,5,3,1);
				break;
			case 5:
				color = BITSWAP8(color,7,0,6,5,4,3,2,1);
				break;
			case 6:
				color = BITSWAP8(color,7,4,3,2,1,0,6,5);
				break;
			}

			plot_pixel(bitmap, sx-9, sy, Machine->pens[0x300+(color^colxor)]);
		}
	}

}
/***************************************************************************

	Start the video hardware emulation.

***************************************************************************/

VIDEO_START( splash )
{
	screen[0] = tilemap_create(get_tile_info_splash_screen0,tilemap_scan_rows,TILEMAP_TRANSPARENT, 8, 8,64,32);
	screen[1] = tilemap_create(get_tile_info_splash_screen1,tilemap_scan_rows,TILEMAP_TRANSPARENT,16,16,32,32);

	if (!screen[0] || !screen[1])
		return 1;

	tilemap_set_transparent_pen(screen[0],0);
	tilemap_set_transparent_pen(screen[1],0);

	tilemap_set_scrollx(screen[0], 0, 4);

	return 0;
}

/***************************************************************************

	Sprites

***************************************************************************/

/*
	Sprite Format
	-------------

  	Word | Bit(s)			 | Description
	-----+-FEDCBA98-76543210-+--------------------------
	  0  | -------- xxxxxxxx | sprite number (low 8 bits)
	  0  | xxxxxxxx -------- | unused
	  1  | -------- xxxxxxxx | y position
	  1  | xxxxxxxx -------- | unused
	  2  | -------- xxxxxxxx | x position (low 8 bits)
	  2  | xxxxxxxx -------- | unused
	  3  | -------- ----xxxx | sprite number (high 4 bits)
	  3  | -------- --xx---- | unknown
	  3  | -------- -x------ | flip x
	  3  | -------- x------- | flip y
	  3  | xxxxxxxx -------- | unused
  	  400| -------- ----xxxx | sprite color
	  400| -------- -xxx---- | unknown
	  400| -------- x------- | x position (high bit)
	  400| xxxxxxxx -------- | unused
*/

static void splash_draw_sprites(struct mame_bitmap *bitmap,const struct rectangle *cliprect)
{
	int i;
	const struct GfxElement *gfx = Machine->gfx[1];

	for (i = 0; i < 0x400; i += 4){
		int sx = splash_spriteram[i+2] & 0xff;
		int sy = (240 - (splash_spriteram[i+1] & 0xff)) & 0xff;
		int attr = splash_spriteram[i+3] & 0xff;
		int attr2 = splash_spriteram[i+0x400] >> 8;
		int number = (splash_spriteram[i] & 0xff) + (attr & 0xf)*256;

		if (attr2 & 0x80) sx += 256;

		drawgfx(bitmap,gfx,number,
			0x10 + (attr2 & 0x0f),attr & 0x40,attr & 0x80,
			sx-8,sy,
			cliprect,TRANSPARENCY_PEN,0);
	}
}

static void funystrp_draw_sprites(struct mame_bitmap *bitmap,const struct rectangle *cliprect)
{
	int i;
	const struct GfxElement *gfx = Machine->gfx[1];

	for (i = 0; i < 0x400; i += 4){
		int sx = splash_spriteram[i+2] & 0x1ff;
		int sy = (240 - (splash_spriteram[i+1] & 0xff)) & 0xff;
		int attr = splash_spriteram[i+3] & 0xff;
		int attr2 = splash_spriteram[i+0x400];
		int number = (splash_spriteram[i] & 0xff) + (attr & 0xf)*256;

		drawgfx(bitmap,gfx,number,
			(attr2 & 0x7f),attr & 0x40,attr & 0x80,
			sx-8,sy,
			cliprect,TRANSPARENCY_PEN,0);
	}
}

/***************************************************************************

	Display Refresh

***************************************************************************/

VIDEO_UPDATE( splash )
{
	/* set scroll registers */
	tilemap_set_scrolly(screen[0], 0, splash_vregs[0]);
	tilemap_set_scrolly(screen[1], 0, splash_vregs[1]);

	splash_draw_bitmap(bitmap,cliprect);

	tilemap_draw(bitmap,cliprect,screen[1],0,0);
	splash_draw_sprites(bitmap,cliprect);
	tilemap_draw(bitmap,cliprect,screen[0],0,0);
}

VIDEO_UPDATE( funystrp )
{
	/* set scroll registers */
	tilemap_set_scrolly(screen[0], 0, splash_vregs[0]);
	tilemap_set_scrolly(screen[1], 0, splash_vregs[1]);

	splash_draw_bitmap(bitmap,cliprect);

	tilemap_draw(bitmap,cliprect,screen[1],0,0);
	/*Sprite chip is similar but not the same*/
	funystrp_draw_sprites(bitmap,cliprect);
	tilemap_draw(bitmap,cliprect,screen[0],0,0);
}
