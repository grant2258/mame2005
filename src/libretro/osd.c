#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifndef _WIN32
#include <sys/time.h>
#endif
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>

#include "libretro.h"
#include "osdepend.h"

#include "fileio.h"
#include "palette.h"
#include "common.h"
#include "mame.h"
#include "driver.h"

//#define DEBUG_LOG

FILE *logfile;
static int errorlog=0; 
static int erroroslog=0;
static char log_buffer[2048];
extern char* systemDir;
extern char* saveDir;
extern char* romDir;
const char* parentDir = "mame2005"; /* groups mame dirs together to avoid conflicts in shared dirs */
#if defined(_WIN32)
char slash = '\\';
#else
char slash = '/';
#endif

extern retro_log_printf_t log_cb;
extern retro_audio_sample_batch_t audio_batch_cb;

#if defined(__CELLOS_LV2__) && !defined(__PSL1GHT__)
#include <unistd.h> //stat() is defined here
#define S_ISDIR(x) (x & CELL_FS_S_IFDIR)
#endif


int osd_create_directory(const char *dir)
{
	/* test to see if directory exists */
	struct stat statbuf;
	int err = stat(dir, &statbuf);
	if (err == -1)
   {
      if (errno == ENOENT)
      {
         int mkdirok;

         /* does not exist */
#ifdef DEBUG_LOG
         logerror("Directory %s not found - creating...\n", dir);
#endif
         /* don't care if already exists) */
#if defined(_WIN32)
         mkdirok = _mkdir(dir);
#elif defined(VITA) || defined(PSP)
         mkdirok = sceIoMkdir(dir, 0777);
#else 
         mkdirok = mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif

         if (mkdirok != 0 && errno != EEXIST)
         {
#ifdef DEBUG_LOG
            logerror("Error creating directory %s ERRNO %d (%s)\n", dir, errno, strerror(errno));
#endif
            return 0;
         }
      }
   }
	return 1;
}

int osd_init(void)
{
	/* ensure parent dir for various mame dirs is created */
	char buffer[1024];
	snprintf(buffer, 1024, "%s%c%s", saveDir, slash, parentDir);
	osd_create_directory(buffer);
	snprintf(buffer, 1024, "%s%c%s", systemDir, slash, parentDir);
	osd_create_directory(buffer);
	
	if (errorlog && !logfile)
			logfile = fopen("debug.log", "wa");
	return 0;
}

void osd_exit(void)
{

}


/******************************************************************************

Sound

******************************************************************************/
static float              delta_samples;
int samples_per_frame = 0;
int usestereo = 1;
int orig_samples_per_frame =0;
short* conversion_buffer;
short* samples_buffer;

int osd_start_audio_stream(int stereo)
{
  if ( Machine->drv->frames_per_second * 1000 < 44100)
   Machine->sample_rate = 22050;
  else
   Machine->sample_rate=44100;

 
  delta_samples = 0.0f;
  usestereo = stereo ? 1 : 0;

  /* determine the number of samples per frame */
  samples_per_frame = Machine->sample_rate / Machine->drv->frames_per_second;
  orig_samples_per_frame = samples_per_frame;

  if (Machine->sample_rate == 0) return 0;

  samples_buffer = (short *) calloc(samples_per_frame+16, 2 + usestereo * 2);
  if (!usestereo) conversion_buffer = (short *) calloc(samples_per_frame+16, 4);
  
  return samples_per_frame;
}


int osd_update_audio_stream(INT16 *buffer)
{
	int i,j;
	if ( Machine->sample_rate !=0 && buffer )
	{
   		memcpy(samples_buffer, buffer, samples_per_frame * (usestereo ? 4 : 2));
		if (usestereo)
			audio_batch_cb(samples_buffer, samples_per_frame);
		else
		{
			for (i = 0, j = 0; i < samples_per_frame; i++)
        		{
				conversion_buffer[j++] = samples_buffer[i];
				conversion_buffer[j++] = samples_buffer[i];
		        }
         		audio_batch_cb(conversion_buffer,samples_per_frame);
		}	
		
			
		//process next frame
			
		if ( samples_per_frame  != orig_samples_per_frame ) samples_per_frame = orig_samples_per_frame;
		
		// dont drop any sample frames some games like mk will drift with time

		delta_samples += (Machine->sample_rate / Machine->drv->frames_per_second) - orig_samples_per_frame;
		if ( delta_samples >= 1.0f )
		{
		
			int integer_delta = (int)delta_samples;
			if (integer_delta <= 16 )
                        {
				//logerror("sound: Delta added value %d added to frame\n",integer_delta);
				samples_per_frame += integer_delta;
			}
			else if(integer_delta >= 16) logerror("sound: Delta not added to samples_per_frame too large integer_delta:%d\n", integer_delta);
			else logerror("sound(delta) no contitions met\n");	
			delta_samples -= integer_delta;

		}
	}
        return samples_per_frame;
}


void osd_stop_audio_stream(void)
{
}

void osd_set_mastervolume(int attenuation)
{
}

int osd_get_mastervolume(void)
{
	return 0;
}

void osd_sound_enable(int enable)
{
}



/******************************************************************************

File I/O

******************************************************************************/

//static const char* const paths[] = { "raw", "rom", "image", "diff", "samples", "artwork", "nvram", "hi", "hsdb", "cfg", "inp", "memcard", "snap", "history", "cheat", "lang", "ctrlr", "ini" };

static const char* const paths[]= {
"raw",
0, // rom
"image",
"diff",
"samples",
"artwork",
"nvram",
"hi",
0, // "HIGHSCORE_DB"
"cfg",
"inp",
"state",
"memcard",
"snap",
0, //history
0, //"CHEAT",
"lang",
"ctrlr",
"ini",
"hash"
};

struct _osd_file
{
	FILE* file;
};

int osd_get_path_count(int pathtype)
{
	return 1;
}


int osd_get_path_info(int pathtype, int pathindex, const char *filename)
{
   char buffer[1024]={0};
   char currDir[1024]={0};
   struct stat statbuf;

   switch (pathtype)
   {
      case FILETYPE_ROM: /* ROM */
      case FILETYPE_IMAGE:
         /* removes the stupid restriction where we need to have roms in a 'rom' folder */
         strcpy(currDir, romDir);
         break;
      case FILETYPE_IMAGE_DIFF:
      case FILETYPE_NVRAM:
      case FILETYPE_HIGHSCORE:
      case FILETYPE_CONFIG:
      case FILETYPE_INPUTLOG:
      case FILETYPE_MEMCARD:
      case FILETYPE_SCREENSHOT:
      case FILETYPE_STATE:
      case FILETYPE_LANGUAGE:
      case FILETYPE_CTRLR:
      case FILETYPE_INI:
         /* user generated content goes in Retroarch save directory */
         snprintf(currDir, 1024, "%s%c%s%c%s", saveDir, slash, parentDir, slash, paths[pathtype]);
         break;
      case FILETYPE_HIGHSCORE_DB:
      case FILETYPE_HISTORY:
      case FILETYPE_CHEAT:
      case FILETYPE_ARTWORK:
      case FILETYPE_SAMPLE:
          snprintf(currDir, 1024, "%s%c%s%c%s", systemDir, slash, parentDir, slash, paths[pathtype]);
         break;
      default:
         /* additonal core content goes in Retroarch system directory */
         snprintf(currDir, 1024, "%s%c%s%c%s", systemDir, slash, parentDir, slash, paths[pathtype]);
   }
   
   snprintf(buffer, 1024, "%s%c%s", currDir, slash, filename);

#ifdef DEBUG_LOG
   logerror("osd_get_path_info (buffer = [%s]), (directory: [%s]), (path type dir: [%s]), (path type: [%d]), : [%s]) \n", buffer, currDir, paths[pathtype], pathtype, filename);
#endif

   if (stat(buffer, &statbuf) == 0)
      return (S_ISDIR(statbuf.st_mode)) ? PATH_IS_DIRECTORY : PATH_IS_FILE;

   return PATH_NOT_FOUND;
}

osd_file *osd_fopen(int pathtype, int pathindex, const char *filename, const char *mode)
{
  char buffer[1024];
  char currDir[1024];
  osd_file *out;
/*
not done
	FILETYPE_RAW = 0,
	FILETYPE_LANGUAGE,
	FILETYPE_CTRLR,
	FILETYPE_INI,
	FILETYPE_HASH,
*/

  switch (pathtype)
  {
    case FILETYPE_ROM:  /* ROM */
    case FILETYPE_IMAGE:  /* IMAGE */
    /* removes the stupid restriction where we need to have roms in a 'rom' folder */
        strcpy(currDir, romDir);
      break;
    case FILETYPE_IMAGE_DIFF:  /* IMAGE DIFF */
    case FILETYPE_NVRAM:  /* NVRAM */
    case FILETYPE_HIGHSCORE:  /* HIGHSCORE */
    case FILETYPE_CONFIG:  /* CONFIG */
    case FILETYPE_INPUTLOG: /* INPUT LOG */
    case FILETYPE_MEMCARD: /* MEMORY CARD */
    case FILETYPE_SCREENSHOT: /* SCREENSHOT */
    /* user generated content goes in Retroarch save directory */
        snprintf(currDir, 1024, "%s%c%s%c%s", saveDir, slash, parentDir, slash, paths[pathtype]);
      break;
    case FILETYPE_HISTORY: /* HISTORY */
    case FILETYPE_CHEAT: /* CHEAT */
    case FILETYPE_HIGHSCORE_DB:  /* HIGHSCORE DB */
    /* .dat files go directly in the Retroarch system directory */
        snprintf(currDir, 1024, "%s%c%s", systemDir, slash, parentDir);
      break;
    case FILETYPE_ARTWORK:  /* HIGHSCORE DB */	
        snprintf(currDir, 1024, "%s%c%s%c%s", systemDir, slash, parentDir, slash, paths[pathtype]);
    default:
      /* additonal core content goes in Retroarch system directory */
        snprintf(currDir, 1024, "%s%c%s%c%s", systemDir, slash, parentDir, slash, paths[pathtype]);
   }

   snprintf(buffer, 1024, "%s%c%s", currDir, slash, filename);
  // printf("path %s \n path2 %s,\n\ buffer %s \n", paths[pathtype],buffer); 

#ifdef DEBUG_LOG
      logerror("osd_fopen (buffer = [%s]), (directory: [%s]), (path type dir: [%s]), (path type: [%d]), (filename: [%s]) \n", buffer, currDir, paths[pathtype], pathtype, filename);
#endif

  osd_create_directory(currDir);
  out = (osd_file*)malloc(sizeof(osd_file));
  out->file = fopen(buffer, mode);
  if (out->file == 0)
  {
    free(out);
#ifdef DEBUG_LOG
    logerror ("osd_open failed %s\n",buffer);
#endif
    return 0;
  }
  return out;
}

int osd_fseek(osd_file *file, INT64 offset, int whence)
{
  return fseek(file->file, offset, whence);
}

UINT64 osd_ftell(osd_file *file)
{
  return ftell(file->file);
}

int osd_feof(osd_file *file)
{
  return feof(file->file);
}

UINT32 osd_fread(osd_file *file, void *buffer, UINT32 length)
{
  return fread(buffer, 1, length, file->file);
}

UINT32 osd_fwrite(osd_file *file, const void *buffer, UINT32 length)
{
  return fwrite(buffer, 1, length, file->file);
}

void osd_fclose(osd_file *file)
{
  fclose(file->file);
  free(file);
}


/******************************************************************************

Miscellaneous

******************************************************************************/

//int osd_display_loading_rom_message(const char *name, struct rom_load_data *romdata) { return 0; }

void osd_pause(int paused) {}
int osd_display_loading_rom_message(const char *name, struct rom_load_data *romdata) { return 0; }

void CLIB_DECL logerror(const char *text,...)
{
  if (erroroslog)
  {
     va_list arg;
  	 va_start(arg,text);
	 vsprintf(log_buffer,text,arg);
	 va_end(arg);
	 printf("(LOGERROR) %s",log_buffer);
   }
}

void CLIB_DECL osd_die(const char *text,...)
{
	va_list arg;
	va_start(arg,text);
	vsprintf(log_buffer,text,arg);
	va_end(arg);
	printf("(OSD_DIE) %s",log_buffer);
	exit(0);
}

void CLIB_DECL fatalerror(const char *text,...)
{
	va_list arg;
	va_start(arg,text);
	vsprintf(log_buffer,text,arg);
	va_end(arg);
	printf("(OSD_DIE) %s",log_buffer);
	exit(0);
}
