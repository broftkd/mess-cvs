/* Sysdep display object

   Copyright 2000-2004 Hans de Goede
   
   This file and the acompanying files in this directory are free software;
   you can redistribute them and/or modify them under the terms of the GNU
   Library General Public License as published by the Free Software Foundation;
   either version 2 of the License, or (at your option) any later version.

   These files are distributed in the hope that they will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with these files; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/
#ifndef __SYSDEP_DISPLAY_H
#define __SYSDEP_DISPLAY_H

#include "sysdep/rc.h"
#include "sysdep/sysdep_palette.h"
#include "begin_code.h"

#ifndef USE_XINPUT_DEVICES
/* only one mouse for now */
#	define SYSDEP_DISPLAY_MOUSE_MAX	1
#else
/* now we have 5 */
#	define SYSDEP_DISPLAY_MOUSE_MAX	5
#endif
#define SYSDEP_DISPLAY_MOUSE_BUTTONS	8
#define SYSDEP_DISPLAY_MOUSE_AXES	8

#define SYSDEP_DISPLAY_HOTKEY_VIDMODE0  0x01
#define SYSDEP_DISPLAY_HOTKEY_VIDMODE1  0x02
#define SYSDEP_DISPLAY_HOTKEY_VIDMODE2  0x04
#define SYSDEP_DISPLAY_HOTKEY_VIDMODE3  0x08
#define SYSDEP_DISPLAY_HOTKEY_VIDMODE4  0x10

#define SYSDEP_DISPLAY_HOTKEY_GRABMOUSE 0x20
#define SYSDEP_DISPLAY_HOTKEY_GRABKEYB  0x40


struct sysdep_display_mousedata
{
	int buttons[SYSDEP_DISPLAY_MOUSE_BUTTONS];
	int deltas[SYSDEP_DISPLAY_MOUSE_AXES];
};

struct sysdep_display_keyboard_event
{
	unsigned char press;
	unsigned char scancode;
	unsigned short unicode;
};

struct sysdep_display_open_params {
  /* width and height before scaling of the bitmap to be displayed */  
  int width;
  int height;
  /* depth of the bitmap to be displayed (15/32 direct or 16 palettised) */
  int depth;
  /* should we rotate and or flip ? */
  int orientation;
  /* vector game? (only used by drivers which have special vector code) */
  int vecgame;
  /* title of the window */
  const char *title;
  /* scaling and effect options */
  int widthscale;
  int heightscale;
  int yarbsize;
  int scanlines;
  int effect;
  int fullscreen;
  /* aspect ratio of the bitmap, or 0 if the aspect ratio should not be taken
     into account */
  double aspect_ratio;
  /* keyboard event handler */
  void (*keyboard_handler)(struct sysdep_display_keyboard_event *event);
  /* Everything below is for private display driver use, any values asigned
     to these members are ignored */
  /* X-alignment stuff.  */
  int aligned_width;
  int x_align;
  /* original (not corrected for orientation) width and height */
  int orig_width;
  int orig_height;
};

/* orientation flags */
#define SYSDEP_DISPLAY_FLIPX  1
#define SYSDEP_DISPLAY_FLIPY  2
#define SYSDEP_DISPLAY_SWAPXY 4

/* from mame's palette.h */
#ifndef PALETTE_H
#include "osd_cpu.h"
typedef UINT32 pen_t;
#endif

/* from mame's common.h */
#ifndef COMMON_H
struct mame_bitmap
{
	int width,height;	/* width and height of the bitmap */
	int depth;			/* bits per pixel */
	void **line;		/* pointers to the start of each line - can be UINT8 **, UINT16 ** or UINT32 ** */

	/* alternate way of accessing the pixels */
	void *base;			/* pointer to pixel (0,0) (adjusted for padding) */
	int rowpixels;		/* pixels per row (including padding) */
	int rowbytes;		/* bytes per row (including padding) */

	/* functions to render in the correct orientation */
	void (*plot)(struct mame_bitmap *bitmap,int x,int y,pen_t pen);
	pen_t (*read)(struct mame_bitmap *bitmap,int x,int y);
	void (*plot_box)(struct mame_bitmap *bitmap,int x,int y,int width,int height,pen_t pen);
};
#endif

/* from mame's drawgfx.h */
#ifndef DRAWGFX_H
struct rectangle
{
	int min_x,max_x;
	int min_y,max_y;
};
#endif

/* init / exit */
int sysdep_display_init(void);
void sysdep_display_exit(void);

/* open / close */
int sysdep_display_open(const struct sysdep_display_open_params *params);
void sysdep_display_close(void);

/* update */
int sysdep_display_update(struct mame_bitmap *bitmap,
  struct rectangle *vis_area, struct rectangle *dirty_area,
  struct sysdep_palette_struct *palette, unsigned int hotkeys);

/* input */
int  sysdep_display_update_keyboard(void);
void sysdep_display_update_mouse(void);
void sysdep_display_set_keybleds(int leds);

extern struct sysdep_display_mousedata sysdep_display_mouse_data[SYSDEP_DISPLAY_MOUSE_MAX];
extern struct sysdep_palette_info sysdep_display_palette_info;
extern struct rc_option sysdep_display_opts[];

#include "end_code.h"
#endif /* ifndef __SYSDEP_DISPLAY_H */
