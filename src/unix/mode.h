#ifndef __MODE_H
#define __MODE_H

#include "sysdep/rc.h"

/* mode handling functions */
void mode_set_aspect_ratio(double display_resolution_aspect_ratio);
void mode_clip_aspect(unsigned int width, unsigned int height, 
		unsigned int *corr_width, unsigned int *corr_height);
void mode_stretch_aspect(unsigned int width, unsigned int height, 
		unsigned int *corr_width, unsigned int *corr_height);
/* match a given mode to the needed width, height and aspect ratio to
   perfectly display a game. This function returns 0 for a not usable mode
   and 100 for the perfect mode +10 for a mode with a well matched depth&bpp
   and +20 for a mode with the perfect depth&bpp
   (=120 for the really perfect mode). */
int mode_match(int width, int height, int depth, int bpp, int dfb);

extern struct rc_option aspect_opts[];
extern struct rc_option mode_opts[];

#endif
