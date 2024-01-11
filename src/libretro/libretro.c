#include <stdint.h>

#include <libretro.h>

#include "mame.h"
#include "driver.h"
#include "state.h"
#include "log.h"


static int driverIndex; //< Index of mame game loaded

extern const struct OSCodeInfo retroKeys[];

static float f_beam = 2;
static float f_flicker = 1.5f;
static float f_intensity= 1.5f;


extern INT32 retroKeyState[];
extern INT32 retroJsState[];

int16_t prev_pointer_x;
int16_t prev_pointer_y;

extern struct osd_create_params videoConfig;

unsigned retroColorMode;
int16_t XsoundBuffer[2048];
char *fallbackDir;
char *systemDir;
char *romDir;
char *saveDir;

int frameskip;
int gotFrame;
unsigned samples = 0;
unsigned cheats = 0;
unsigned dial_share_xy = 0;
unsigned mouse_device = 0;
unsigned rstick_to_btns = 0;
unsigned option_tate_mode = 0;
int  pressure_check =  655.36 * 45;
int gun;
int32_t             analogjoy[4][8]= {0};

// Wrapper to build MAME on 3DS. It doesn't have stricmp.
#ifdef _3DS
int stricmp(const char *string1, const char *string2)
{
    return strcasecmp(string1, string2);
}
#endif

void mame_frame(void);
void mame_done(void);
int convert_analog_scale(int input);

#if defined(__CELLOS_LV2__) || defined(GEKKO) || defined(_XBOX)
unsigned activate_dcs_speedhack = 1;
#else
unsigned activate_dcs_speedhack = 0;
#endif

struct retro_perf_callback perf_cb;

retro_log_printf_t log_cb = NULL;
retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
static retro_input_state_t input_cb = NULL;
retro_audio_sample_batch_t audio_batch_cb = NULL;
retro_environment_t environ_cb = NULL;

unsigned long lastled = 0;
retro_set_led_state_t led_state_cb = NULL;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }
void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { "mame2005-frameskip", "Frameskip; 0|1|2|3|4|5" },
      { "mame2005-skip_disclaimer", "Skip Disclaimer; enabled|disabled" },
      { "mame2005-skip_warnings", "Skip Warnings; disabled|enabled" },
      { "mame2005-cheats", "Cheats(restart required); disabled|enabled" },
      { "mame2005-option_tate_mode", "vertical rotation Mode; on|off" },
      { "mame2005-gun", "use light gun interface; disabled|enabled" },
      { NULL, NULL },
   };
   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

#ifndef PATH_SEPARATOR
# if defined(WINDOWS_PATH_STYLE) || defined(_WIN32)
#  define PATH_SEPARATOR '\\'
# else
#  define PATH_SEPARATOR '/'
# endif
#endif

static char* normalizePath(char* aPath)
{
   char *tok;
   static const char replaced = (PATH_SEPARATOR == '\\') ? '/' : '\\';

   for (tok = strchr(aPath, replaced); tok; tok = strchr(aPath, replaced))
      *tok = PATH_SEPARATOR;

   return aPath;
}

static int getDriverIndex(const char* aPath)
{
    char driverName[128];
    char *path, *last;
    char *firstDot;
    int i;

    // Get all chars after the last slash
    path = normalizePath(strdup(aPath ? aPath : "."));
    last = strrchr(path, PATH_SEPARATOR);
    memset(driverName, 0, sizeof(driverName));
    strncpy(driverName, last ? last + 1 : path, sizeof(driverName) - 1);
    free(path);

    // Remove extension
    firstDot = strchr(driverName, '.');

    if(firstDot)
       *firstDot = 0;

    // Search list
    for (i = 0; drivers[i]; i++)
    {
       if(strcmp(driverName, drivers[i]->name) == 0)
       {
          options.romset_filename_noext = strdup(driverName);
          if (log_cb)
             log_cb(RETRO_LOG_INFO, "Found game: %s [%s].\n", driverName, drivers[i]->name);
          return i;
       }
    }

    return -1;
}

static char* peelPathItem(char* aPath)
{
    char* last = strrchr(aPath, PATH_SEPARATOR);
    if(last)
       *last = 0;

    return aPath;
}


unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "mame2005";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version = "0.095" GIT_VERSION;
   info->valid_extensions = "zip";
   info->need_fullpath = true;
   info->block_extract = true;
}


static void update_variables(void)
{
   struct retro_led_interface ledintf;
   struct retro_variable var;

   var.value = NULL;
   var.key = "mame2005-frameskip";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value)
      frameskip = atoi(var.value);

   var.value = NULL;
   var.key = "mame2005-skip_disclaimer";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value)
   {
      if(strcmp(var.value, "enabled") == 0)
         options.skip_disclaimer = 1;
      else
         options.skip_disclaimer = 0;
   }
   else
      options.skip_disclaimer = 0;

   var.value = NULL;
   var.key = "mame2005-skip_warnings";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value)
   {
      if(strcmp(var.value, "enabled") == 0)
         options.skip_warnings = 1;
      else
         options.skip_warnings = 0;
   }
   else
      options.skip_warnings = 0;

   var.value = NULL;
   var.key = "mame2005-cheats";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value)
   {
      if(strcmp(var.value, "enabled") == 0)
         options.cheat = 1;
      else
         options.cheat = 0;
   }
   else
      cheats = 0;

   var.value = NULL;
   var.key = "mame2005-option_tate_mode";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value)
   {
      if(strcmp(var.value, "off") == 0)
         options.tate_mode = 1;

      else
         options.tate_mode = 0;
   }
   else
      options.tate_mode = 0;

   var.value = NULL;
   var.key = "mame2005-gun";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value)
   {
      if(strcmp(var.value, "enabled") == 0)
         gun = 1;
      else
         gun = 0;
   }
   else
      gun = 0;

   ledintf.set_led_state = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &ledintf))
      led_state_cb = ledintf.set_led_state;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
  mame2003_video_get_geometry(&info->geometry);
  info->timing.fps = Machine->drv->frames_per_second;
  if ( Machine->drv->frames_per_second * 1000 < 44100)
   info->timing.sample_rate = 22050;
  else
   info->timing.sample_rate = 44100;
}

static void check_system_specs(void)
{
   // TODO - set variably
   // Midway DCS - Mortal Kombat/NBA Jam etc. require level 9
   unsigned level = 10;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init (void)
{
   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

#ifdef LOG_PERFORMANCE
   environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb);
#endif

   update_variables();
   check_system_specs();
}

void retro_deinit(void)
{
#ifdef LOG_PERFORMANCE
   perf_cb.perf_log();
#endif
}

void retro_reset (void)
{
    machine_reset();
}

/* get pointer axis vector from coord */
int16_t get_pointer_delta(int16_t coord, int16_t *prev_coord)
{
   int16_t delta = 0;
   if (*prev_coord == 0 || coord == 0)
   {
      *prev_coord = coord;
   }
   else
   {
      if (coord != *prev_coord)
      {
         delta = coord - *prev_coord;
         *prev_coord = coord;
      }
   }

   return delta;
}

void retro_run (void)
{
   int i;
   const struct OSCodeInfo *thisInput;
   bool updated = false;
   poll_cb();

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   // Keyboard
   thisInput = retroKeys;
   while(thisInput->name)
   {
      retroKeyState[thisInput->oscode] = input_cb(0, RETRO_DEVICE_KEYBOARD, 0, thisInput->oscode);
      thisInput ++;
   }

   for (i = 0; i < 4; i ++)
   {
      unsigned int offset = (i * 32);

      analogjoy[i][0] = convert_analog_scale(input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X));
      analogjoy[i][1] = convert_analog_scale(input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y));
      analogjoy[i][2] = convert_analog_scale(input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
      analogjoy[i][3] = convert_analog_scale(input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));
      analogjoy[i][4] = convert_analog_scale(-abs(input_cb( i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L2)));
      analogjoy[i][5] = convert_analog_scale(-abs(input_cb( i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2)));
      analogjoy[i][6] = convert_analog_scale(input_cb(i, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X));
      analogjoy[i][7] = convert_analog_scale(input_cb(i, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y));

      retroJsState[ RETRO_DEVICE_ID_JOYPAD_B + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_Y + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_SELECT + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_START + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_UP + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_DOWN + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_LEFT + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_RIGHT + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_A + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_X + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);

      retroJsState[ RETRO_DEVICE_ID_JOYPAD_L + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_R + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_L2 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_R2 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_L3 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
      retroJsState[ RETRO_DEVICE_ID_JOYPAD_R3 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);


      retroJsState[ 16 + offset] = 0;
      retroJsState[ 17 + offset] = 0;
      retroJsState[ 18 + offset] = 0;
      retroJsState[ 19 + offset] = 0;
      retroJsState[ 20 + offset] = 0;
      retroJsState[ 21 + offset] = 0;
      retroJsState[ 22 + offset] = 0;
      retroJsState[ 23 + offset] = 0;

      if (analogjoy[i][0] < -pressure_check) // d left
        retroJsState[ 16 + offset] = 1;

      if (analogjoy[i][0] >  pressure_check)
        retroJsState[ 17 + offset] = 1; //d right

      if (analogjoy[i][1] < -pressure_check) // d up
        retroJsState[ 18 + offset] = 1;

      if (analogjoy[i][1] >  pressure_check) // d down
        retroJsState[ 19 + offset] = 1;

      if (analogjoy[i][2] < -pressure_check) //same as above for r stick 
        retroJsState[ 20 + offset] = 1;

      if (analogjoy[i][2] >  pressure_check)
        retroJsState[ 21 + offset] = 1;

      if (analogjoy[i][3] < -pressure_check)
        retroJsState[ 22 + offset] = 1;

      if (analogjoy[i][3] >  pressure_check)
        retroJsState[ 23 + offset] = 1;

      retroJsState[ 24 + offset] = analogjoy[i][0]; //analog_x
      retroJsState[ 25 + offset] = analogjoy[i][1];//analog_y
      retroJsState[ 26 + offset] = analogjoy[i][2];//analog_z
      retroJsState[ 27 + offset] = analogjoy[i][4];//l2
      retroJsState[ 28 + offset] = analogjoy[i][5];//r2
      retroJsState[ 29 + offset] = 0;
      retroJsState[ 30 + offset] = 0;
      if (gun)
      {
         retroJsState[ 29 + offset] = 2 * analogjoy[i][6]; //gunx
         retroJsState[ 30 + offset] = 2 * analogjoy[i][7];//gun y
      }
   }
   mame_frame();


}


bool retro_load_game(const struct retro_game_info *game)
{
   if (!game)
   {
	   printf("game false \n");
      return false;
   }
    // Find game index
    driverIndex = getDriverIndex(game->path);

    if(driverIndex)
    {
        #define describe_buttons(INDEX) \
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,     "Hat Left" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,    "Hat Right" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "Hat Up" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,     "Hat Down" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,        "Button 1" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,        "Button 2" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,        "Button 3" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,        "Button 4" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,        "Button 5" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,        "Button 6" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,       "Button 7" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,       "Button 8" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,       "Button 9" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,       "Button 10" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Insert Coin" },\
        { INDEX, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" }, \
        { INDEX, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "X axis" }, \
        { INDEX, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Y axis" }, \
        { INDEX, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Z axis" },\
        { INDEX, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "unmapped" }, \
        { INDEX, RETRO_DEVICE_ANALOG, RETRO_DEVICE_ANALOG, RETRO_DEVICE_ID_JOYPAD_L2, "Pedal 1" }, \
        { INDEX, RETRO_DEVICE_ANALOG, RETRO_DEVICE_ANALOG, RETRO_DEVICE_ID_JOYPAD_R2, "Pedal 2" },

        struct retro_input_descriptor desc[] = {
            describe_buttons(0)
            describe_buttons(1)
            describe_buttons(2)
            describe_buttons(3)
            { 0, 0, 0, 0, NULL }
            };

        fallbackDir = strdup(game->path);

        /* Get system directory from frontend */
        environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&systemDir);
        if (systemDir == NULL || systemDir[0] == '\0')
        {
            /* if non set, use old method */
            systemDir = normalizePath(fallbackDir);
            systemDir = peelPathItem(systemDir);
        }

        /* Get save directory from frontend */
        environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&saveDir);
        if (saveDir == NULL || saveDir[0] == '\0')
        {
            /* if non set, use old method */
            saveDir = normalizePath(fallbackDir);
            saveDir = peelPathItem(saveDir);
        }

        // Get ROM directory
        romDir = normalizePath(fallbackDir);
        romDir = peelPathItem(romDir);

        // Set all options before starting the game
       // options.vector_resolution_multiplier = 2;



        options.beam = (int)(f_beam * 0x00010000);
        if (options.beam < 0x00010000)
          options.beam = 0x00010000;
        if (options.beam > 0x00100000)

        options.vector_flicker = (int)(f_flicker * 2.55);
        if (options.vector_flicker < 0)
          options.vector_flicker = 0;
        if (options.vector_flicker > 255)
          options.vector_flicker = 255;

        options.vector_intensity = f_intensity;
        options.antialias = 1; // 1 or 0
        options.translucency = 1; //integer: 1 to enable translucency on vectors

        options.frameskip = frameskip;
        options.gui_host =1; // stops mame asking for a keypress
        options.skip_validitychecks =1; // will hang check dont change

        options.use_samples =1;
        environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

        // Boot the emulator
        return run_game(driverIndex) == 0;
    }
    else
    {
	    printf("we got a flase return\n");
        return false;
    }
}

void retro_unload_game(void)
{
    mame_done();

    free(fallbackDir);
    systemDir = 0;
}

size_t retro_serialize_size(void)
{
   // extern size_t state_get_dump_size(void);

    //return state_get_dump_size();
return false;
}

bool retro_serialize(void *data, size_t size)
{

	return false;
}

bool retro_unserialize(const void * data, size_t size)
{

	return false;
}


// Stubs
unsigned retro_get_region (void) {return RETRO_REGION_NTSC;}
void *retro_get_memory_data(unsigned type) {return 0;}
size_t retro_get_memory_size(unsigned type) {return 0;}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info){return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2){}
void retro_set_controller_port_device(unsigned in_port, unsigned device){}
