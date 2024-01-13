#define controls 32
#define joycodes 4 * controls
#define keycodes RETROK_LAST

extern const struct OSCodeInfo retroKeys[];
extern INT32 retroKeyState[]; 
extern INT32 retroJsState[];
extern const struct OSCodeInfo retroKeys[];
extern struct osd_create_params videoConfig;


void mame2003_video_get_geometry(struct retro_game_geometry *geom);
