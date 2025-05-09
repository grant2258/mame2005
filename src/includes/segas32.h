/***************************************************************************

    Sega System 32/Multi 32 hardware

***************************************************************************/


/*----------- defined in drivers/segas32.c -----------*/

extern data8_t *ga2_dpram;
extern data16_t *system32_workram;
extern data16_t *system32_protram;


/*----------- defined in machine/segas32.c -----------*/

READ16_HANDLER( arabfgt_protection_r );
READ16_HANDLER( arf_wakeup_protection_r );
WRITE16_HANDLER( arabfgt_protection_w );

READ16_HANDLER( brival_protection_r );
WRITE16_HANDLER( brival_protection_w );

READ16_HANDLER( darkedge_protection_r );
WRITE16_HANDLER( darkedge_protection_w );

READ16_HANDLER( dbzvrvs_protection_r );
WRITE16_HANDLER( dbzvrvs_protection_w );

void decrypt_ga2_protrom(void);
READ16_HANDLER( ga2_dpram_r );
WRITE16_HANDLER( ga2_dpram_w );

WRITE16_HANDLER(sonic_level_load_protection);


/*----------- defined in vidhrdw/segas32.c -----------*/

extern data16_t *system32_videoram;
extern data16_t *system32_spriteram;
extern data16_t *system32_paletteram[2];
extern data16_t system32_displayenable[2];
extern data16_t system32_tilebank_external;

VIDEO_START(system32);
VIDEO_START(multi32);
VIDEO_UPDATE(system32);
VIDEO_UPDATE(multi32);
void system32_set_vblank(int state);

READ16_HANDLER( system32_videoram_r );
WRITE16_HANDLER( system32_videoram_w );
READ32_HANDLER( multi32_videoram_r );
WRITE32_HANDLER( multi32_videoram_w );

READ16_HANDLER( system32_spriteram_r );
WRITE16_HANDLER( system32_spriteram_w );
READ32_HANDLER( multi32_spriteram_r );
WRITE32_HANDLER( multi32_spriteram_w );

READ16_HANDLER( system32_paletteram_r );
WRITE16_HANDLER( system32_paletteram_w );
READ32_HANDLER( multi32_paletteram_0_r );
WRITE32_HANDLER( multi32_paletteram_0_w );
READ32_HANDLER( multi32_paletteram_1_r );
WRITE32_HANDLER( multi32_paletteram_1_w );

READ16_HANDLER( system32_sprite_control_r );
WRITE16_HANDLER( system32_sprite_control_w );
READ32_HANDLER( multi32_sprite_control_r );
WRITE32_HANDLER( multi32_sprite_control_w );

WRITE16_HANDLER( system32_mixer_w );
WRITE32_HANDLER( multi32_mixer_0_w );
WRITE32_HANDLER( multi32_mixer_1_w );
