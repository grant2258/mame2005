#ifndef LOG_H
#define LOG_H

#include <libretro.h>

/******************************************************************************

	Shared libretro log interface

******************************************************************************/

#define LOGPRE          "[mame2005] "

extern retro_log_printf_t log_cb;

#endif /* LOG_H */
