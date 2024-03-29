/***************************************************************************

  vidhrdw.c

  Functions to emulate the video hardware of the machine.

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"

static struct tilemap *bg_tilemap;
static struct tilemap *grid_tilemap;

UINT8 sraider_grid_status;

/***************************************************************************

  Convert the color PROMs into a more useable format.

  Lady Bug has a 32 bytes palette PROM and a 32 bytes sprite color lookup
  table PROM.
  The palette PROM is connected to the RGB output this way:

  bit 7 -- inverter -- 220 ohm resistor  -- BLUE
        -- inverter -- 220 ohm resistor  -- GREEN
        -- inverter -- 220 ohm resistor  -- RED
        -- inverter -- 470 ohm resistor  -- BLUE
        -- unused
        -- inverter -- 470 ohm resistor  -- GREEN
        -- unused
  bit 0 -- inverter -- 470 ohm resistor  -- RED

***************************************************************************/
PALETTE_INIT( ladybug )
{
	int i;

	for (i = 0;i < 32;i++)
	{
		int bit1,bit2,r,g,b;


		bit1 = (~color_prom[i] >> 0) & 0x01;
		bit2 = (~color_prom[i] >> 5) & 0x01;
		r = 0x47 * bit1 + 0x97 * bit2;
		bit1 = (~color_prom[i] >> 2) & 0x01;
		bit2 = (~color_prom[i] >> 6) & 0x01;
		g = 0x47 * bit1 + 0x97 * bit2;
		bit1 = (~color_prom[i] >> 4) & 0x01;
		bit2 = (~color_prom[i] >> 7) & 0x01;
		b = 0x47 * bit1 + 0x97 * bit2;
		palette_set_color(i,r,g,b);
	}

	/* characters */
	for (i = 0;i < 8;i++)
	{
		colortable[4 * i] = 0;
		colortable[4 * i + 1] = i + 0x08;
		colortable[4 * i + 2] = i + 0x10;
		colortable[4 * i + 3] = i + 0x18;
	}

	/* sprites */
	for (i = 0;i < 4 * 8;i++)
	{
		int bit0,bit1,bit2,bit3;


		/* low 4 bits are for sprite n */
		bit0 = (color_prom[i + 32] >> 3) & 0x01;
		bit1 = (color_prom[i + 32] >> 2) & 0x01;
		bit2 = (color_prom[i + 32] >> 1) & 0x01;
		bit3 = (color_prom[i + 32] >> 0) & 0x01;
		colortable[i + 4 * 8] = 1 * bit0 + 2 * bit1 + 4 * bit2 + 8 * bit3;

		/* high 4 bits are for sprite n + 8 */
		bit0 = (color_prom[i + 32] >> 7) & 0x01;
		bit1 = (color_prom[i + 32] >> 6) & 0x01;
		bit2 = (color_prom[i + 32] >> 5) & 0x01;
		bit3 = (color_prom[i + 32] >> 4) & 0x01;
		colortable[i + 4 * 16] = 1 * bit0 + 2 * bit1 + 4 * bit2 + 8 * bit3;
	}
}

WRITE8_HANDLER( ladybug_videoram_w )
{
	if (videoram[offset] != data)
	{
		videoram[offset] = data;
		tilemap_mark_tile_dirty(bg_tilemap, offset);
	}
}

WRITE8_HANDLER( ladybug_colorram_w )
{
	if (colorram[offset] != data)
	{
		colorram[offset] = data;

		tilemap_mark_tile_dirty(bg_tilemap, offset);
	}
}

WRITE8_HANDLER( ladybug_flipscreen_w )
{
	if (flip_screen != (data & 0x01))
	{
		flip_screen_set(data & 0x01);
		tilemap_mark_all_tiles_dirty(ALL_TILEMAPS);
	}
}

WRITE8_HANDLER( sraider_grid_w )
{
	static int old = -1;

	if(old!=data)
	{
		old=data;
		//printf("%04X: grid %X\n",activecpu_get_pc(),data);
		sraider_grid_status = data;
		tilemap_mark_all_tiles_dirty(grid_tilemap);
	}
	//cpu_set_irq_line(1,0,ASSERT_LINE);
}

static UINT8 gridline[256];

WRITE8_HANDLER( sraider_grid_control_w )
{
	static int x = 0;

	if (x == 0)
	{
		tilemap_mark_all_tiles_dirty(grid_tilemap);
		x = 1;
	}
	gridline[offset] = data;

}

static void get_bg_tile_info(int tile_index)
{
	int code = videoram[tile_index] + 32 * (colorram[tile_index] & 0x08);
	int color = colorram[tile_index] & 0x07;

	SET_TILE_INFO(0, code, color, 0)
}

static void get_grid_tile_info(int tile_index)
{
	int color = 1;

	if (tile_index < 512)
		SET_TILE_INFO(3, tile_index, color, 0)
	else
	{
		int temp = tile_index/32;
		tile_index = (31 - temp) * 32 + (tile_index % 32);
		SET_TILE_INFO(4, tile_index, color, 0)
	}
}

VIDEO_START( ladybug )
{
	bg_tilemap = tilemap_create(get_bg_tile_info, tilemap_scan_rows,
		TILEMAP_OPAQUE, 8, 8, 32, 32);

	if ( !bg_tilemap )
		return 1;

	tilemap_set_scroll_rows(bg_tilemap, 32);

	return 0;
}

VIDEO_START( sraider )
{
	grid_tilemap = tilemap_create(get_grid_tile_info, tilemap_scan_rows,
		TILEMAP_OPAQUE, 8, 8, 32, 32);

	if ( !grid_tilemap )
		return 1;

	tilemap_set_scroll_rows(grid_tilemap, 32);

	bg_tilemap = tilemap_create(get_bg_tile_info, tilemap_scan_rows,
//		TILEMAP_OPAQUE, 8, 8, 32, 32);
		TILEMAP_TRANSPARENT, 8, 8, 32, 32);

	if ( !bg_tilemap )
		return 1;

	tilemap_set_scroll_rows(bg_tilemap, 32);

	tilemap_set_transparent_pen(bg_tilemap, 0);

	return 0;
}

static void ladybug_draw_sprites( struct mame_bitmap *bitmap )
{
	int offs;

	for (offs = spriteram_size - 2*0x40;offs >= 2*0x40;offs -= 0x40)
	{
		int i = 0;

		while (i < 0x40 && spriteram[offs + i] != 0)
			i += 4;

		while (i > 0)
		{
/*
 abccdddd eeeeeeee fffghhhh iiiiiiii

 a enable?
 b size (0 = 8x8, 1 = 16x16)
 cc flip
 dddd y offset
 eeeeeeee sprite code (shift right 2 bits for 16x16 sprites)
 fff unknown
 g sprite bank
 hhhh color
 iiiiiiii x position
*/
			i -= 4;

			if (spriteram[offs + i] & 0x80)
			{
				if (spriteram[offs + i] & 0x40)	/* 16x16 */
					drawgfx(bitmap,Machine->gfx[1],
							(spriteram[offs + i + 1] >> 2) + 4 * (spriteram[offs + i + 2] & 0x10),
							spriteram[offs + i + 2] & 0x0f,
							spriteram[offs + i] & 0x20,spriteram[offs + i] & 0x10,
							spriteram[offs + i + 3],
							offs / 4 - 8 + (spriteram[offs + i] & 0x0f),
							&Machine->visible_area,TRANSPARENCY_PEN,0);
				else	/* 8x8 */
					drawgfx(bitmap,Machine->gfx[2],
							spriteram[offs + i + 1] + 16 * (spriteram[offs + i + 2] & 0x10),
							spriteram[offs + i + 2] & 0x0f,
							spriteram[offs + i] & 0x20,spriteram[offs + i] & 0x10,
							spriteram[offs + i + 3],
							offs / 4 + (spriteram[offs + i] & 0x0f),
							&Machine->visible_area,TRANSPARENCY_PEN,0);
			}
		}
	}
}

VIDEO_UPDATE( ladybug )
{
	int offs;

	for (offs = 0; offs < 32; offs++)
	{
		int sx = offs % 4;
		int sy = offs / 4;

		if (flip_screen)
			tilemap_set_scrollx(bg_tilemap, offs, -videoram[32 * sx + sy]);
		else
			tilemap_set_scrollx(bg_tilemap, offs, videoram[32 * sx + sy]);
	}

	tilemap_draw(bitmap, &Machine->visible_area, bg_tilemap, 0, 0);
	ladybug_draw_sprites(bitmap);
}

extern UINT8 sraider_wierd_value[8];
UINT8 sraider_0x30, sraider_0x38;

VIDEO_UPDATE( sraider )
{
	int offs;

	for (offs = 0; offs < 32; offs++)
	{
		int sx = offs % 4;
		int sy = offs / 4;

		if (flip_screen)
			tilemap_set_scrollx(bg_tilemap, offs, -videoram[32 * sx + sy]);
		else
			tilemap_set_scrollx(bg_tilemap, offs, videoram[32 * sx + sy]);
	}

	//if (gridline[0xd8] != 0)
	if (gridline[0xd8] != 0)
	{
		int i;
		tilemap_draw(bitmap, &Machine->visible_area, grid_tilemap, 0, 0);
		for(i=0;i<256;i++)
		{
			if (gridline[i] != 0)
			{
				plot_box(bitmap,i,0,1,255,
					Machine->pens[
						Machine->gfx[0]->colortable[4*gridline[i]+3]]);
				//plot_box(bitmap,i,0,1,255,Machine->pens[gridline[i]]);
			}
		}
	}

	//plot_box(bitmap,sraider_0x30,sraider_0x38,4,4,Machine->pens[5]);

	//for(int i=0;i<8;i++)
	//{
	//	plot_box(bitmap,sraider_wierd_value[i],108,1,40,Machine->pens[i]);
	//}
	//	plot_box(bitmap,sraider_wierd_value[i],108,1,40,Machine->pens[i]);
	//}
	tilemap_draw(bitmap, &Machine->visible_area, bg_tilemap, 0, 0);
	ladybug_draw_sprites(bitmap);
#if 0
	for(int i=0;i<8;i++)
	{
		for(int j=0;j<8;j++)
		{
			int x,y,color;
			x = j;
			y = i;
			color = (sraider_wierd_value[i]>>(7-j))&1;
			plot_box(bitmap,y*8+32,4 - 4*(y&1) + x*4+32,4,4,Machine->pens[color+1]);
		}
	}
#endif
}

