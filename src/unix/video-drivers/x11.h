#ifndef __X11_H_
#define __X11_H_

#include <X11/Xlib.h>
#include "effect.h"

#ifdef __X11_C_
#define EXTERN
#else
#define EXTERN extern
#endif

enum { X11_WINDOW, X11_DGA, X11_XV_WINDOW, X11_XV_FULLSCREEN };
#define X11_MODE_COUNT 4

EXTERN Display 		*display;
EXTERN Window		window;
EXTERN Screen 		*screen;
EXTERN unsigned char	*scaled_buffer_ptr;
EXTERN int		mode_available[X11_MODE_COUNT];
EXTERN int		x11_video_mode;
EXTERN int		run_in_root_window;
#ifdef USE_XIL
EXTERN int		use_xil;
EXTERN int		use_mt_xil;
#endif
extern struct rc_option xf86_dga_opts[];
extern struct rc_option x11_window_opts[];
extern struct rc_option	x11_input_opts[];

#if defined x11 && defined USE_DGA
EXTERN int		xf86_dga_fix_viewport;
EXTERN int		xf86_dga_first_click;
#endif

#ifdef X11_JOYSTICK
EXTERN int devicebuttonpress;
EXTERN int devicebuttonrelease;
EXTERN int devicemotionnotify;
EXTERN int devicebuttonmotion;
#endif

/*** prototypes ***/

/* device related */
void process_x11_joy_event(XEvent *event);

/* xinput functions */
int xinput_open(int force_grab, int event_mask);
void xinput_close(void);
void xinput_check_hotkeys(void);
/* Create a window, type can be:
   0: Fixed size of width and height
   1: Resizable initial size is width and height
   2: Fullscreen return width and height in width and height */
int x11_create_window(int *width, int *height, int type);
/* Set the hints for a window, window-type can be:
   0: Fixed size of width and height
   1: Resizable initial size is width and height
   2: Fullscreen of width and height */
void x11_set_window_hints(int width, int height, int type);

#ifdef x11

/* Normal x11_window functions */
int  x11_window_create_display(int depth);
void x11_window_close_display(void);
void x11_window_update_display(struct mame_bitmap *bitmap);
void x11_window_refresh_screen(void);

/* Xf86_dga functions */
int  xf86_dga_init(void);
int  xf86_dga_create_display(int depth);
void xf86_dga_close_display(void);
void xf86_dga_update_display(struct mame_bitmap *bitmap);
int  xf86_dga1_init(void);
int  xf86_dga1_create_display(int depth);
void xf86_dga1_close_display(void);
void xf86_dga1_update_display(struct mame_bitmap *bitmap);
int  xf86_dga2_init(void);
int  xf86_dga2_create_display(int depth);
void xf86_dga2_close_display(void);
void xf86_dga2_update_display(struct mame_bitmap *bitmap);

/* XIL functions */
#ifdef USE_XIL
void init_xil( void );
void setup_xil_images( int, int );
void refresh_xil_screen( void );
#endif

/* DBE functions */
#ifdef USE_DBE
void setup_dbe( void );
void swap_dbe_buffers( void );
#endif

/* generic helper functions */
int x11_init_palette_info(Visual *xvisual);

#endif /* ifdef x11 */

#undef EXTERN
#endif /* ifndef __X11_H_ */
