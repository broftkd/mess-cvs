/*
 * Modifications For OpenVMS By:  Robert Alan Byer
 *                                byer@mail.ourservers.net
 *                                Jan. 9, 2004
 */

/*
 * X-Mame video specifics code
 *
 */
#ifdef x11
#define __X11_WINDOW_C_

/*
 * Include files.
 */

/* for FLT_MAX */
#include <float.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef USE_MITSHM
#  if defined(__DECC) && defined(VMS)
#    include <X11/extensions/ipc.h>
#    include <X11/extensions/shm.h>
#  else
#    include <sys/ipc.h>
#    include <sys/shm.h>
#  endif
#  include <X11/extensions/XShm.h>
#endif

#ifdef USE_XV
/* we need MITSHM! */
#ifndef USE_MITSHM
#error "USE_XV defined but USE_MITSHM is not defined, XV needs MITSHM!"
#endif
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#endif

#include "xmame.h"
#include "x11.h"
#include "driver.h"
#include "pixel_convert.h"

/* for xscreensaver support */
/* Commented out for now since it causes problems with some 
 * versions of KDE.
 */
/* #include "vroot.h" */

#ifdef USE_HWSCALE
static void x11_window_update_16_to_YUY2 (struct mame_bitmap *bitmap);
static void x11_window_update_16_to_YV12 (struct mame_bitmap *bitmap);
static void x11_window_update_16_to_YV12_perfect (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_YUY2_direct (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_YV12_direct (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_YV12_direct_perfect (struct mame_bitmap *bitmap);
#endif
static void x11_window_update_16_to_16bpp (struct mame_bitmap *bitmap);
static void x11_window_update_16_to_24bpp (struct mame_bitmap *bitmap);
static void x11_window_update_16_to_32bpp (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_16bpp_direct (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_16bpp_rgb_565_direct (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_16bpp_rgb_555_direct (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_24bpp_direct (struct mame_bitmap *bitmap);
static void x11_window_update_32_to_32bpp_direct (struct mame_bitmap *bitmap);
static void (*x11_window_update_display_func) (struct mame_bitmap *bitmap) = NULL;

/* we need these to do the clean up correctly */
#ifdef USE_MITSHM
static int mit_shm_attached = 0;
static XShmSegmentInfo shm_info;
static int use_mit_shm = 1;  /* use mitshm if available */
#endif

#ifdef USE_HWSCALE
static int use_hwscale=0;
static int hwscale_bpp=0;
static long hwscale_format=0;
static int hwscale_yuv=0;
static int hwscale_yv12=0;
static char *hwscale_yv12_rotate_buf0=NULL;
static char *hwscale_yv12_rotate_buf1=NULL;
static int hwscale_perfect_yuv=1;
static int hwscale_fullscreen=0;
static Visual hwscale_visual;
#define FOURCC_YUY2 0x32595559
#define FOURCC_YV12 0x32315659
#define FOURCC_I420 0x30323449
#define FOURCC_UYVY 0x59565955

/* HACK - HACK - HACK for fullscreen */
#define MWM_HINTS_DECORATIONS   2
typedef struct {
	long flags;
	long functions;
	long decorations;
	long input_mode;
} MotifWmHints;
#endif

#ifdef USE_XV
static XvImage *xvimage = NULL;
static int xv_port=-1;
static int use_xv=0;
#define HWSCALE_WIDTH (xvimage->width)
#define HWSCALE_HEIGHT (xvimage->height)
#define HWSCALE_YPLANE (xvimage->data+xvimage->offsets[0])
#define HWSCALE_UPLANE (xvimage->data+xvimage->offsets[1])
#define HWSCALE_VPLANE (xvimage->data+xvimage->offsets[2])
#endif

static XImage *image = NULL;
static GC gc;
static int orig_widthscale, orig_heightscale, orig_yarbsize;
static int image_width;
enum { X11_NORMAL, X11_MITSHM, X11_XV, X11_XIL };
static int x11_window_update_method;
static int startx = 0;
static int starty = 0;
static int use_xsync = 0;
static int root_window_id; /* root window id (for swallowing the mame window) */
static char *geometry = NULL;
static double x11_resolution_aspect=0.0;

struct rc_option x11_window_opts[] = {
	/* name, shortname, type, dest, deflt, min, max, func, help */
	{ "X11-window Related", NULL, rc_seperator, NULL, NULL, 0, 0, NULL,
 NULL },
#ifdef USE_MITSHM
	{ "mitshm", "ms", rc_bool, &use_mit_shm, "1", 0, 0, NULL, "Use/don't use MIT Shared Mem (if available and compiled in)" },
#endif
#ifdef USE_XIL
	{ "xil", "x", rc_bool, &use_xil, "1", 0, 0, NULL, "Enable/disable use of XIL for scaling (if available and compiled in)" },
	{ "mtxil", "mtx", rc_bool, &use_mt_xil, "0", 0, 0, NULL, "Enable/disable multi threading of XIL" },
#endif	
#ifdef USE_HWSCALE
	{ "yuv", NULL, rc_bool, &hwscale_yuv, "0", 0, 0, NULL, "Force YUV mode (for video cards with broken RGB hwscales)" },
	{ "yv12", NULL, rc_bool, &hwscale_yv12, "0", 0, 0, NULL, "Force YV12 mode (for video cards with broken RGB hwscales)" },
	{ "perfect-yuv", NULL, rc_bool, &hwscale_perfect_yuv, "1", 0, 0, NULL, "Use perfect (slower) blitting code for XV YUV blits" },
#endif
	{ "xsync", "xs", rc_bool, &use_xsync, "1", 0, 0, NULL, "Use/don't use XSync instead of XFlush as screen refresh method" },
	{ "run-in-root-window", "root", rc_bool, &run_in_root_window, "0", 0, 0, NULL, "Enable/disable running in root window" },
	{ "root_window_id", "rid", rc_int, &root_window_id,
 "0", 0, 0, NULL, "Create the xmame window in an alternate root window; mostly useful for front-ends!" },
	{ "geometry", "geo", rc_string, &geometry, "640x480", 0, 0, NULL, "Specify the location of the window" },
	{ NULL, NULL, rc_end, NULL, NULL, 0, 0, NULL, NULL }
};

#if defined(__sgi)
/* Needed for setting the application class */
static XClassHint class_hints = {
  NAME, NAME,
};
#endif

/*
 * Create a display screen, or window, large enough to accomodate a bitmap
 * of the given dimensions.
 */

#ifdef USE_MITSHM
/* following routine traps missused MIT-SHM if not available */
int test_mit_shm (Display * display, XErrorEvent * error)
{
	char msg[256];
	unsigned char ret = error->error_code;

	XGetErrorText (display, ret, msg, 256);
	/* if MIT-SHM request failed, note and continue */
	if ((ret == BadAccess) || (ret == BadAlloc))
	{
		use_mit_shm = 0;
		return 0;
	}
	/* else unspected error code: notify and exit */
	fprintf (stderr_file, "Unspected X Error %d: %s\n", ret, msg);
	exit(1);
	/* to make newer gcc's shut up, grrr */
	return 0;
}
#endif

#ifdef USE_XV
int FindXvPort(Display *dpy, long format, int *port)
{
	int i,j,p,ret,num_formats;
	unsigned int num_adaptors;
	XvAdaptorInfo *ai;
	XvImageFormatValues *fo;

	ret = XvQueryAdaptors(dpy, DefaultRootWindow(dpy),
			&num_adaptors, &ai);

	if (ret != Success)
	{
		fprintf(stderr,"XV: QueryAdaptors failed\n");
		return 0;
	}

	for (i = 0; i < num_adaptors; i++)
	{
		int firstport=ai[i].base_id;
		int portcount=ai[i].num_ports;

		for (p = firstport; p < ai[i].base_id+portcount; p++)
		{
			fo = XvListImageFormats(dpy, p, &num_formats);
			for (j = 0; j < num_formats; j++)
			{
				if((fo[j].id==format))
				{
					if(XvGrabPort(dpy,p,CurrentTime)==Success)
					{
						*port=p;
						XFree(fo);
						return 1;
					}
				}
			}
			XFree(fo);
		}
	}
	XvFreeAdaptorInfo(ai);
	return 0;
}

int FindRGBXvFormat(Display *dpy, int *port,long *format,int *bpp)
{
	int i,j,p,ret,num_formats;
	unsigned int num_adaptors;
	XvAdaptorInfo *ai;
	XvImageFormatValues *fo;

	ret = XvQueryAdaptors(dpy, DefaultRootWindow(dpy),
			&num_adaptors, &ai);

	if (ret != Success)
	{
		fprintf(stderr,"XV: QueryAdaptors failed\n");
		return 0;
	}

	for (i = 0; i < num_adaptors; i++)
	{
		int firstport=ai[i].base_id;
		int portcount=ai[i].num_ports;

		for (p = firstport; p < ai[i].base_id+portcount; p++)
		{
			fo = XvListImageFormats(dpy, p, &num_formats);
			for (j = 0; j < num_formats; j++)
			{
				if((fo[j].type==XvRGB) && (fo[j].format==XvPacked))
				{
					if(XvGrabPort(dpy,p,CurrentTime)==Success)
					{
						*bpp=fo[j].bits_per_pixel;
						*port=p;
						*format=fo[j].id;
						hwscale_visual.red_mask  =fo[j].red_mask;
						hwscale_visual.green_mask=fo[j].green_mask;
						hwscale_visual.blue_mask =fo[j].blue_mask;
						XFree(fo);
						return 1;
					}
				}
			}
			XFree(fo);
		}
	}
	XvFreeAdaptorInfo(ai);
	return 0;
}

#endif
#ifdef USE_HWSCALE

/* Since with YUV formats a field of zeros is generally
   loud green, rather than black, it makes sense
   to clear the image before use (since scanline algorithms
   leave alternate lines "black") */
void ClearYUY2()
{
  int i,j;
  char *yuv=HWSCALE_YPLANE;
  fprintf(stderr,"Clearing YUY2\n");
  for (i = 0; i < HWSCALE_HEIGHT; i++)
  {
    for (j = 0; j < HWSCALE_WIDTH; j++)
    {
      int offset=(HWSCALE_WIDTH*i+j)*2;
      yuv[offset] = 0;
      yuv[offset+1]=-128;
    }
  }
}

void ClearYV12()
{
  int i,j;
  char *y=HWSCALE_YPLANE;
  char *u=HWSCALE_UPLANE;
  char *v=HWSCALE_VPLANE;
  fprintf(stderr,"Clearing YV12\n");
  for (i = 0; i < HWSCALE_HEIGHT; i++) {
    for (j = 0; j < HWSCALE_WIDTH; j++) {
      int offset=(HWSCALE_WIDTH*i+j);
      y[offset] = 0;
      if((i&1) && (j&1))
      {
        offset = (HWSCALE_WIDTH/2)*(i/2) + (j/2);
        u[offset] = -128;
        v[offset] = -128;
      }
    }
  }
}
#endif

static int x11_window_create_window(XSizeHints *hints, int width, int height)
{
        XWMHints wm_hints;
	XSetWindowAttributes winattr;
	XEvent event;

        /* Create and setup the window. No buttons, no fancy stuff. */
        winattr.background_pixel  = BlackPixelOfScreen (screen);
        winattr.border_pixel      = WhitePixelOfScreen (screen);
        winattr.bit_gravity       = ForgetGravity;
        winattr.win_gravity       = hints->win_gravity;
        winattr.backing_store     = NotUseful;
        winattr.override_redirect = False;
        winattr.save_under        = False;
        winattr.event_mask        = 0;
        winattr.do_not_propagate_mask = 0;
        winattr.colormap          = DefaultColormapOfScreen (screen);
        winattr.cursor            = None;
        
        if (root_window_id == 0)
        {
                root_window_id = RootWindowOfScreen (screen);
        }
        window = XCreateWindow (display, root_window_id, hints->x, hints->y,
                        width, height,
                        0, screen->root_depth,
                        InputOutput, screen->root_visual,
                        (CWBorderPixel | CWBackPixel | CWBitGravity |
                         CWWinGravity | CWBackingStore |
                         CWOverrideRedirect | CWSaveUnder | CWEventMask |
                         CWDontPropagate | CWColormap | CWCursor),
                        &winattr);
        if (!window)
        {
                fprintf (stderr_file, "OSD ERROR: failed in XCreateWindow().\n");
                return 1;
        }
        
        wm_hints.input    = TRUE;
        wm_hints.flags    = InputHint;
        hints->min_width  = hints->max_width  = hints->base_width  = width;
        hints->min_height = hints->max_height = hints->base_height = height;
        
        XSetWMHints (display, window, &wm_hints);
        XSetWMNormalHints (display, window, hints);
        
#ifdef USE_HWSCALE
        /* Hack to get rid of window title bar */
        if(use_hwscale && hwscale_fullscreen)
        {
                Atom mwmatom;
                MotifWmHints mwmhints;
                mwmhints.flags=MWM_HINTS_DECORATIONS;
                mwmhints.decorations=0;
                mwmatom=XInternAtom(display,"_MOTIF_WM_HINTS",0);

                XChangeProperty(display,window,mwmatom,mwmatom,32,
                                PropModeReplace,(unsigned char *)&mwmhints,4);
        }
#endif

#if defined(__sgi)
        /* Force first resource class char to be uppercase */
        class_hints.res_class[0] &= 0xDF;
        /*
         * Set the application class (WM_CLASS) so that 4Dwm can display
         * the appropriate pixmap when the application is iconified
         */
        XSetClassHint(display, window, &class_hints);
        /* Use a simpler name for the icon */
        XSetIconName(display, window, NAME);
#endif
        
        XStoreName (display, window, title);
        
        XSelectInput (display, window, ExposureMask);
        XMapRaised (display, window);
        XClearWindow (display, window);
        XWindowEvent (display, window, ExposureMask, &event);
        
        return 0;
}

/* This name doesn't really cover this function, since it also sets up mouse
   and keyboard. This is done over here, since on most display targets the
   mouse and keyboard can't be setup before the display has. */
int x11_window_create_display (int bitmap_depth)
{
        Visual *xvisual;
	XGCValues xgcv;
	XSizeHints hints;
	int geom_width, geom_height;
	int image_height;
	int i;
	int window_width, window_height;
	int event_mask = 0;
	int dest_depth;

	/* set all the default values */
	window = 0;
	x11_window_update_method = X11_NORMAL;
	image  = NULL;
#ifdef USE_XV
	xvimage = NULL;
#endif
#ifdef USE_MITSHM
	mit_shm_attached = 0;
#endif
	screen           = DefaultScreenOfDisplay (display);

        /* fix the aspect ratio, do this here since
           this can change yarbsize */
        x11_resolution_aspect = (float)screen->width/screen->height;
        mode_fix_aspect(x11_resolution_aspect);

	window_width     = widthscale  * visual_width;
	window_height    = yarbsize ? yarbsize : (heightscale * visual_height);
	image_width      = widthscale  * visual_width;
	image_height     = yarbsize ? yarbsize : (heightscale * visual_height);
	orig_widthscale  = widthscale;
	orig_heightscale = heightscale;
	orig_yarbsize    = yarbsize;
#ifdef USE_HWSCALE
        use_hwscale	 = 0;

	if (hwscale_yv12)
		hwscale_yuv=1;
#endif
#ifdef USE_XV
	use_xv = (x11_video_mode == X11_XV_WINDOW
			|| x11_video_mode == X11_XV_FULLSCREEN);

	hwscale_fullscreen = (x11_video_mode == X11_XV_FULLSCREEN);

        /* we need MIT-SHM ! */
        if (use_xv)
           use_mit_shm=1;
#endif

	/* check the available extensions if compiled in */
#ifdef USE_MITSHM
        if (use_mit_shm) /* look for available Mitshm extensions */
        {
                /* get XExtensions to be if mit shared memory is available */
                if (XQueryExtension (display, "MIT-SHM", &i, &i, &i))
                {
                        x11_window_update_method = X11_MITSHM;
                }
                else
                {
                        fprintf (stderr_file, "X-Server Doesn't support MIT-SHM extension\n");
                        use_mit_shm = 0;
                }
        }
#endif
#ifdef USE_XIL
	if (use_xil)
	{
		init_xil ();
	}

	/*
	 *  If the XIL initialization worked, then use_xil will still be set.
	 */
	if (use_xil)
	{
	    /* xil does normal scaling for us */
            if (effect == 0)
            {
		image_width  = visual_width;
		image_height = visual_height;
		widthscale   = 1;
		heightscale  = 1;
		yarbsize     = 0;
            }
            x11_window_update_method = X11_XIL;
	}
#endif
#ifdef USE_XV
	if (use_mit_shm && use_xv) /* look for available Xv extensions */
	{
		unsigned int p_version,p_release,p_request_base,p_event_base,p_error_base;
		if(XvQueryExtension(display, &p_version, &p_release, &p_request_base,
					&p_event_base, &p_error_base)==Success)
		{
	            /* Xv does normal scaling for us */
		    if(effect==0)
		    {
                        image_width  = visual_width;
                        image_height = visual_height;
                        widthscale   = 1;
                        heightscale  = 1;
                        yarbsize     = 0;
                    }
#ifdef USE_XIL
                    use_xil = 0;
#endif
                    x11_window_update_method = X11_XV;
		}
		else
		{
			fprintf (stderr_file, "X-Server Doesn't support Xv extension\n");
			use_xv = 0;
		}
	}
	else
	   use_xv = 0;
#endif

        /* get a visual */
	xvisual = screen->root_visual;
	fprintf(stderr_file, "Using a Visual with a depth of %dbpp.\n", screen->root_depth);

	/* create a window */
	if (run_in_root_window)
	{
		int width  = screen->width;
		int height = screen->height;
		if (window_width > width || window_height > height)
		{
			fprintf (stderr_file, "OSD ERROR: Root window is to small: %dx%d, needed %dx%d\n",
					width, height, window_width, window_height);
			return OSD_NOT_OK;
		}

		startx        = ((width  - window_width)  / 2) & ~0x07;
		starty        = ((height - window_height) / 2) & ~0x07;
		window        = RootWindowOfScreen (screen);
		window_width  = width;
		window_height = height;
	}
	else
	{
		/*  Placement hints etc. */
		hints.flags = PSize | PMinSize | PMaxSize;
		hints.x = hints.y = 0;
		hints.win_gravity = NorthWestGravity;

		i = XWMGeometry(display, DefaultScreen(display), geometry, NULL, 0, &hints, &hints.x,
				&hints.y, &geom_width, &geom_height, &hints.win_gravity);
		if ((i&XValue) && (i&YValue))
			hints.flags |= PPosition | PWinGravity;
                
                /* create the actual window */
                if (x11_window_create_window(&hints, window_width, window_height))
                        return 1;
	}

	/* create gc */
	gc = XCreateGC (display, window, 0, &xgcv);

	/* create and setup the image */
	switch (x11_window_update_method)
	{
#ifdef USE_XIL
		case X11_XIL:
			/*
			 *  XIL takes priority over MITSHM
			 */
			setup_xil_images (image_width, image_height);
			break;
#endif
#ifdef USE_XV
		case X11_XV:
			/* Create an XV MITSHM image. */
                        fprintf (stderr_file, "MIT-SHM & XV Extensions Available. trying to use... ");
                        /* find a suitable format */
                        if(!hwscale_yuv)
                        {
                                if(!(FindRGBXvFormat(display, &xv_port,&hwscale_format,&hwscale_bpp)))
                                {
                                        hwscale_yuv=1;
                                        fprintf(stderr,"\nCan't find a suitable RGB format - trying YUY2 instead... ");
                                }
                        }
                        if(hwscale_yuv && !hwscale_yv12)
                        {
                                hwscale_format=FOURCC_YUY2;
                                if(!(FindXvPort(display, hwscale_format, &xv_port)))
                                {
                                        fprintf(stderr,"\nYUY2 not available - trying YV12... ");
                                        hwscale_yv12=1;
                                }
                                else if (!effect && hwscale_perfect_yuv)
                                {
                                        image_width  = 2*visual_width;
                                        widthscale   = 2;
                                }
                        }
                        if(hwscale_yv12)
                        {
                                hwscale_format=FOURCC_YV12;
                                if(!(FindXvPort(display, hwscale_format, &xv_port)))
                                {
                                        fprintf(stderr,"\nError: Couldn't initialise Xv port - ");
                                        fprintf(stderr,"\n  Either all ports are in use, or the video card");
                                        fprintf(stderr,"\n  doesn't provide a suitable format.\n");
                                        use_xv = 0;
                                }
                                else
                                {
                                        fprintf(stderr,"\nWarning: YV12 support is incomplete... ");
                                        if (effect)
                                          fprintf(stderr, "\nWarning: YV12 doesn't do effects... ");
                                        if (hwscale_perfect_yuv)
                                        {
                                          /* hack */
                                          image_width  = 2*visual_width;
                                          image_height = 2*visual_height;
                                          widthscale   = 2;
                                          heightscale  = 2;
                                          yarbsize     = 0;
                                        }
                                        else /* we don't do effects! */
                                        {
                                          image_width  = visual_width;
                                          image_height = visual_height;
                                          widthscale   = 1;
                                          heightscale  = 1;
                                          yarbsize     = 0;
                                        }
                                }
                        }
                        /* if use_xv is still set we've found a suitable format */
                        if (use_xv)
                        {
                                int i;
                                int count;
                                XvAttribute *attr;

                                attr = XvQueryPortAttributes(display, xv_port, &count);
                                for (i = 0; i < count; i++)
                                    if (!strcmp(attr[i].name, "XV_AUTOPAINT_COLORKEY"))
                                    {
                                                Atom atom = XInternAtom(display, "XV_AUTOPAINT_COLORKEY", False);
                                                XvSetPortAttribute(display, xv_port, atom, 1);
                                                break;
                                    }
                                
                                XSetErrorHandler (test_mit_shm);
                                xvimage = XvShmCreateImage (display,
                                        xv_port,
                                        hwscale_format,
                                        0,
                                        image_width,
                                        image_height,
                                        &shm_info);
                        }
                        /* sometimes xv gives us a smaller image then we want ! */
                        if (xvimage)
                        {
                            if ((xvimage->width  < image_width) ||
                                (xvimage->height < image_height))
                            {
                                fprintf (stderr_file, "\nError: XVimage smaller then requested size. ");
                                XFree(xvimage);
                                xvimage = NULL;
                            }
                        }
                        if (xvimage)
                        {
                                shm_info.shmid = shmget (IPC_PRIVATE,
                                                xvimage->data_size,
                                                (IPC_CREAT | 0777));
                                if (shm_info.shmid < 0)
                                {
                                        fprintf (stderr_file, "\nError: failed to create MITSHM block.\n");
                                        return OSD_NOT_OK;
                                }

                                /* And allocate the bitmap buffer. */
                                /* new pen color code force double buffering in every cases */
                                xvimage->data = shm_info.shmaddr =
                                        (char *) shmat (shm_info.shmid, 0, 0);

                                scaled_buffer_ptr = (unsigned char *) xvimage->data;
                                if (!scaled_buffer_ptr)
                                {
                                        fprintf (stderr_file, "\nError: failed to allocate MITSHM bitmap buffer.\n");
                                        return OSD_NOT_OK;
                                }

                                shm_info.readOnly = FALSE;

                                /* Attach the MITSHM block. this will cause an exception if */
                                /* MIT-SHM is not available. so trap it and process         */
                                if (!XShmAttach (display, &shm_info))
                                {
                                        fprintf (stderr_file, "\nError: failed to attach MITSHM block.\n");
                                        return OSD_NOT_OK;
                                }
                                XSync (display, False);  /* be sure to get request processed */
                                /* sleep (2);          enought time to notify error if any */
                                /* Mark segment as deletable after we attach.  When all processes
                                   detach from the segment (progam exits), it will be deleted.
                                   This way it won't be left in memory if we crash or something.
                                   Grr, have todo this after X attaches too since slowlaris doesn't
                                   like it otherwise */
                                shmctl(shm_info.shmid, IPC_RMID, NULL);

                                /* HACK, GRRR sometimes this all succeeds, but the first call to
                                   XvShmPutImage to a mapped window fails with:
                                   "BadAlloc (insufficient resources for operation)" */
                                if (use_mit_shm)
                                {
                                  if (hwscale_yuv)
                                  {
                                    int orig_fullscreen = hwscale_fullscreen;
                                    
                                    if (hwscale_yv12)
                                      ClearYV12();
                                    else
                                      ClearYUY2();
                                    
                                    /* set fullscreen to stop refresh_screen
                                       resizing the window */
                                    hwscale_fullscreen = 1;
                                    x11_window_refresh_screen();
                                    hwscale_fullscreen = orig_fullscreen;
                                    
                                    XSync (display, False);  /* be sure to get request processed */
                                    /* sleep (1);          enought time to notify error if any */
                                  }
                                }
                                
                                /* if use_mit_shm is still set we've succeeded */
                                if (use_mit_shm)
                                {
                                        fprintf (stderr_file, "Success.\nUsing Xv & Shared Memory Features to speed up\n");
                                        XSetErrorHandler (None);  /* Restore error handler to default */
                                        mit_shm_attached = 1;
                                        use_hwscale = 1;
                                        break;
                                }
                                /* else we have failed clean up before retrying without XV */
                                shmdt ((char *) scaled_buffer_ptr);
                                scaled_buffer_ptr = NULL;
                                XFree(xvimage);
                                xvimage = NULL;
                        }
			if (use_xv)
			        XSetErrorHandler (None);  /* Restore error handler to default */
                        widthscale  = orig_widthscale;
                        heightscale = orig_heightscale;
                        yarbsize    = orig_yarbsize;
                        image_width  = widthscale  * visual_width;
                        image_height = yarbsize ? yarbsize : (heightscale * visual_height);
			use_xv = 0;
			use_mit_shm = 1;
		        fprintf (stderr_file, "Failed\nReverting to MIT-SHM mode\n");
			x11_window_update_method = X11_MITSHM;
#endif
#ifdef USE_MITSHM
		case X11_MITSHM:
			/* Create a MITSHM image. */
			fprintf (stderr_file, "MIT-SHM Extension Available. trying to use... ");
			XSetErrorHandler (test_mit_shm);

			image = XShmCreateImage (display,
					xvisual,
					screen->root_depth,
					ZPixmap,
					NULL,
					&shm_info,
					image_width,
					image_height);
			if (image)
			{
				shm_info.shmid = shmget (IPC_PRIVATE,
						image->bytes_per_line * image->height,
						(IPC_CREAT | 0777));
				if (shm_info.shmid < 0)
				{
					fprintf (stderr_file, "\nError: failed to create MITSHM block.\n");
					return OSD_NOT_OK;
				}

				/* And allocate the bitmap buffer. */
				/* new pen color code force double buffering in every cases */
				image->data = shm_info.shmaddr =
					(char *) shmat (shm_info.shmid, 0, 0);

				scaled_buffer_ptr = (unsigned char *) image->data;
				if (!scaled_buffer_ptr)
				{
					fprintf (stderr_file, "\nError: failed to allocate MITSHM bitmap buffer.\n");
					return OSD_NOT_OK;
				}

				shm_info.readOnly = FALSE;

				/* Attach the MITSHM block. this will cause an exception if */
				/* MIT-SHM is not available. so trap it and process         */
				if (!XShmAttach (display, &shm_info))
				{
					fprintf (stderr_file, "\nError: failed to attach MITSHM block.\n");
					return OSD_NOT_OK;
				}
				XSync (display, False);  /* be sure to get request processed */
				/* sleep (2);          enought time to notify error if any */
				/* Mark segment as deletable after we attach.  When all processes
				   detach from the segment (progam exits), it will be deleted.
				   This way it won't be left in memory if we crash or something.
				   Grr, have todo this after X attaches too since slowlaris doesn't
				   like it otherwise */
				shmctl(shm_info.shmid, IPC_RMID, NULL);

				/* if use_mit_shm is still set we've succeeded */
				if (use_mit_shm)
				{
					fprintf (stderr_file, "Success.\nUsing Shared Memory Features to speed up\n");
					XSetErrorHandler (None);  /* Restore error handler to default */
					mit_shm_attached = 1;
					break;
				}
				/* else we have failed clean up before retrying without MITSHM */
				shmdt ((char *) scaled_buffer_ptr);
				scaled_buffer_ptr = NULL;
				XDestroyImage (image);
				image = NULL;
			}
			XSetErrorHandler (None);  /* Restore error handler to default */
			use_mit_shm = 0;
			fprintf (stderr_file, "Failed\nReverting to normal XPutImage() mode\n");
			x11_window_update_method = X11_NORMAL;
#endif
		case X11_NORMAL:
			scaled_buffer_ptr = malloc (4 * image_width * image_height);
			if (!scaled_buffer_ptr)
			{
				fprintf (stderr_file, "Error: failed to allocate bitmap buffer.\n");
				return OSD_NOT_OK;
			}
			image = XCreateImage (display,
					xvisual,
					screen->root_depth,
					ZPixmap,
					0,
					(char *) scaled_buffer_ptr,
					image_width, image_height,
					32, /* image_width always is a multiple of 8 */
					0);

			if (!image)
			{
				fprintf (stderr_file, "OSD ERROR: could not create image.\n");
				return OSD_NOT_OK;
			}
			break;
		default:
			fprintf (stderr_file, "Error unknown X11 update method, this shouldn't happen\n");
			return OSD_NOT_OK;
	}

	/* Now we know if we have XIL/ Xv / MIT-SHM / Normal:
	   change the hints and size if needed */
	if (!run_in_root_window)
	{
#ifdef USE_XIL
		/*
		 *  XIL allows us to rescale the window on the fly,
		 *  so in this case, we don't prevent the user from
		 *  resizing.
		 */
		if (use_xil)
		{
			event_mask  = StructureNotifyMask;
			hints.flags = PSize;

                        XSetWMNormalHints (display, window, &hints);
		}
#endif
#ifdef USE_HWSCALE
		if(use_hwscale)
		{
			if(!hwscale_fullscreen)
			{
			        hints.flags = PSize;
                                XSetWMNormalHints (display, window, &hints);
                                
                                mode_stretch_aspect(window_width, window_height, &window_width, &window_height, x11_resolution_aspect);
                                XResizeWindow(display, window, window_width, window_height);
                        }
                        else    
                        {
				window_width  = screen->width;
				window_height = screen->height;

				hints.flags=PMinSize|PMaxSize;
				hints.flags|=USPosition|USSize;
				
				/* we need to recreate the window,
				   some window managers don't
				   allow switching to fullscreen after
				   the window has been mapped */
				XDestroyWindow(display,window);
				if (x11_window_create_window(&hints, window_width,
				    window_height))
				       return 1;
			}
		}
#endif
	}

	/* verify the number of bits per pixel and choose the correct update method */
#ifdef USE_HWSCALE
	if (use_hwscale)
	{
		dest_depth = hwscale_bpp;
		hwscale_visual.class = TrueColor;
		xvisual = &hwscale_visual;
        }
        else
#endif
#ifdef USE_XIL
	if (use_xil)
		/* XIL uses 16 bit visuals and does any conversion it self */
		dest_depth = 16;
        else
#endif
	        dest_depth = image->bits_per_pixel;

	/* setup the palette_info struct now we have the visual */
	if (x11_init_palette_info(xvisual) != OSD_OK)
	{
		return OSD_NOT_OK;
	}

	fprintf(stderr_file, "Actual bits per pixel = %d... ", dest_depth);
	if (bitmap_depth == 32)
	{
#ifdef USE_HWSCALE
		if(use_hwscale && hwscale_yuv)
		{
			display_palette_info.fourcc_format = hwscale_format;
			switch(hwscale_format)
			{
				case FOURCC_YUY2:
					x11_window_update_display_func = x11_window_update_32_to_YUY2_direct;
					break;
				case FOURCC_YV12:
					if (blit_flipx || blit_flipy || blit_swapxy)
                                        {
                                                hwscale_yv12_rotate_buf0=malloc(
                                                  2*visual_width);
                                                hwscale_yv12_rotate_buf1=malloc(
                                                  2*visual_width);
                                                if (!hwscale_yv12_rotate_buf0 ||
                                                    !hwscale_yv12_rotate_buf1)
                                                {
                                                  fprintf (stderr_file, "Error: failed to allocate rotate buffer.\n");
                                                  return OSD_NOT_OK;
                                                }
                                        }
					if (hwscale_perfect_yuv)
						x11_window_update_display_func
							= x11_window_update_32_to_YV12_direct_perfect;
					else
						x11_window_update_display_func
							= x11_window_update_32_to_YV12_direct;
					break;
			}
		}
		else
#endif
			switch(dest_depth)
			{
                            case 16:
                                if ((xvisual->red_mask   == (0x1F << 11)) &&
                                    (xvisual->green_mask == (0x3F <<  5)) &&
                                    (xvisual->blue_mask  == (0x1F      )))
				  x11_window_update_display_func = x11_window_update_32_to_16bpp_rgb_565_direct;
                                else if ((xvisual->red_mask   == (0x1F << 10)) &&
                                         (xvisual->green_mask == (0x1F <<  5)) &&
                                         (xvisual->blue_mask  == (0x1F      )))
				  x11_window_update_display_func = x11_window_update_32_to_16bpp_rgb_555_direct;
                                else
                                {
				  x11_window_update_display_func = x11_window_update_32_to_16bpp_direct;
				  fprintf(stderr_file, "\n Using generic (slow) 32 bpp to 16 bpp downsampling... ");
                                }
                                break;
			    case 24:
				x11_window_update_display_func = x11_window_update_32_to_24bpp_direct;
				break;
			    case 32:
				x11_window_update_display_func = x11_window_update_32_to_32bpp_direct;
                                break;
                        }
	}
	else if (bitmap_depth == 16)
	{
#ifdef USE_HWSCALE
		if(use_hwscale && hwscale_yuv)
		{
			display_palette_info.fourcc_format = hwscale_format;
			switch(hwscale_format)
			{
				case FOURCC_YUY2:
					x11_window_update_display_func = x11_window_update_16_to_YUY2;
					break;
				case FOURCC_YV12:
					if (blit_flipx || blit_flipy || blit_swapxy)
                                        {
                                                hwscale_yv12_rotate_buf0=malloc(
                                                  4*visual_width);
                                                hwscale_yv12_rotate_buf1=malloc(
                                                  4*visual_width);
                                                if (!hwscale_yv12_rotate_buf0 ||
                                                    !hwscale_yv12_rotate_buf1)
                                                {
                                                  fprintf (stderr_file, "Error: failed to allocate rotate buffer.\n");
                                                  return OSD_NOT_OK;
                                                }
                                        }
					if (hwscale_perfect_yuv)
						x11_window_update_display_func
							= x11_window_update_16_to_YV12_perfect;
					else
						x11_window_update_display_func
							= x11_window_update_16_to_YV12;
					break;
			}
		}
		else
#endif
			switch (dest_depth)
			{
				case 16:
					x11_window_update_display_func = x11_window_update_16_to_16bpp;
					break;
				case 24:
					x11_window_update_display_func = x11_window_update_16_to_24bpp;
					break;
				case 32:
					x11_window_update_display_func = x11_window_update_16_to_32bpp;
					break;
			}
	}

	if (x11_window_update_display_func == NULL)
	{
		fprintf(stderr_file, "Error: Unsupported bitmap depth = %dbpp, video depth = %dbpp\n", bitmap_depth, dest_depth);
		return OSD_NOT_OK;
	}
	fprintf(stderr_file, "Ok\n");

	if (effect_open())
		return OSD_NOT_OK;

#ifdef USE_HWSCALE
	xinput_open((use_hwscale && hwscale_fullscreen)? 1:0, event_mask);
#else
	xinput_open(0, event_mask);
#endif

	return OSD_OK;
}

/*
 * Shut down the display, also called by the core to clean up if any error
 * happens when creating the display.
 */
void x11_window_close_display (void)
{
   widthscale  = orig_widthscale;
   heightscale = orig_heightscale;
   yarbsize    = orig_yarbsize;

   /* Restore error handler to default */
   XSetErrorHandler (None);

   /* free effect buffers */
   effect_close();

   /* ungrab keyb and mouse */
   xinput_close();

   /* now just free everything else */
   if (window)
   {
      XDestroyWindow (display, window);
      window = 0;
   }
#ifdef USE_MITSHM
   if (mit_shm_attached)
   {
      XShmDetach (display, &shm_info);
      mit_shm_attached = 0;
   }
   if (use_mit_shm && scaled_buffer_ptr)
   {
      shmdt (scaled_buffer_ptr);
      scaled_buffer_ptr = NULL;
   }
#endif
   if (image)
   {
       XDestroyImage (image);
       scaled_buffer_ptr = NULL;
       image = NULL;
   }
#ifdef USE_XV
   if(xv_port>-1)
   {
      XvUngrabPort(display,xv_port,CurrentTime);
      xv_port=-1;
   }
   if(xvimage)
   {
       XFree(xvimage);
       scaled_buffer_ptr = NULL;
       xvimage=NULL;
   }
#endif
   if (scaled_buffer_ptr)
   {
      free (scaled_buffer_ptr);
      scaled_buffer_ptr = NULL;
   }
   
   if (hwscale_yv12_rotate_buf0)
   {
      free (hwscale_yv12_rotate_buf0);
      hwscale_yv12_rotate_buf0 = NULL;
   }

   if (hwscale_yv12_rotate_buf1)
   {
      free (hwscale_yv12_rotate_buf1);
      hwscale_yv12_rotate_buf1 = NULL;
   }

   XSync (display, True); /* send all events to sync; discard events */
}

/* invoked by main tree code to update bitmap into screen */
void x11_window_update_display (struct mame_bitmap *bitmap)
{
   (*x11_window_update_display_func) (bitmap);

   x11_window_refresh_screen();
   
   xinput_check_hotkeys();

   /* some games "flickers" with XFlush, so command line option is provided */
   if (use_xsync)
      XSync (display, False);   /* be sure to get request processed */
   else
      XFlush (display);         /* flush buffer to server */
}

void x11_window_refresh_screen (void)
{
   switch (x11_window_update_method)
   {
      case X11_XIL:
#ifdef USE_XIL
         refresh_xil_screen ();
#endif
         break;
      case X11_MITSHM:
#ifdef USE_MITSHM
         XShmPutImage (display, window, gc, image, 0, 0, startx, starty,
                       image->width, image->height, FALSE);
#endif
         break;
      case X11_XV:
#ifdef USE_XV
         {
            Window _dw;
            int _dint;
	    unsigned int _w,_h,_duint;
            int pw,ph;

            XGetGeometry(display, window, &_dw, &_dint, &_dint, &_w, &_h, &_duint, &_duint);
            mode_clip_aspect(_w, _h, &pw, &ph, x11_resolution_aspect);
            if (!hwscale_fullscreen && (pw!=_w || ph!=_h))
            {
               XResizeWindow(display, window, pw, ph);
               _w = pw;
               _h = ph;
            }
            XvShmPutImage (display, xv_port, window, gc, xvimage,
              0, 0, HWSCALE_WIDTH, HWSCALE_HEIGHT,
              (_w-pw)/2, (_h-ph)/2, pw, ph, True);
         }
#endif
         break;
      case X11_NORMAL:
         XPutImage (display, window, gc, image, 0, 0, startx, starty,
                    image->width, image->height);
         break;
   }
}

#define DEST_WIDTH image_width
#define DEST scaled_buffer_ptr

#ifdef USE_HWSCALE
#define RMASK 0xff0000
#define GMASK 0x00ff00
#define BMASK 0x0000ff

#define RGB2YUV(r,g,b,y,u,v) \
    (y) = ((  9836*(r) + 19310*(g) +  3750*(b)          ) >> 15); \
    (u) = (( -5527*(r) - 10921*(g) + 16448*(b) + 4194304) >> 15); \
    (v) = (( 16448*(r) - 13783*(g) -  2665*(b) + 4194304) >> 15)

/* Hacked into place, until I integrate YV12 support into the blit core... */
static void x11_window_update_16_to_YV12(struct mame_bitmap *bitmap)
{
   int _x,_y;
   char *dest_y;
   char *dest_u;
   char *dest_v;
   unsigned short *src;
   unsigned short *src2;
   int u,v,y,u2,v2,y2,u3,v3,y3,u4,v4,y4;     /* 12 */
   char rotate = 0;

   if (current_palette != debug_palette &&
       (blit_flipx || blit_flipy || blit_swapxy))
      rotate = 1;

   for(_y=visual.min_y;_y<visual.max_y;_y+=2)
   {
      if (rotate)
      {
         rotate_func(hwscale_yv12_rotate_buf0, bitmap, _y);
         rotate_func(hwscale_yv12_rotate_buf1, bitmap, _y+1);
         src  = (unsigned short*)hwscale_yv12_rotate_buf0;
         src2 = (unsigned short*)hwscale_yv12_rotate_buf1;
      }
      else
      {
         src=bitmap->line[_y] ;
         src+= visual.min_x;
         src2=bitmap->line[_y+1];
         src2+= visual.min_x;
      }

      dest_y=HWSCALE_YPLANE+(HWSCALE_WIDTH*(_y-visual.min_y));
      dest_v=HWSCALE_UPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)/2));
      dest_u=HWSCALE_VPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)/2));
      for(_x=visual.min_x;_x<visual.max_x;_x+=2)
      {
            v = current_palette->lookup[*src++];
            y = (v)  & 0xff;
            u = (v>>8) & 0xff;
            v = (v>>24)     & 0xff;

            v2 = current_palette->lookup[*src++];
            y2 = (v2)  & 0xff;
            u2 = (v2>>8) & 0xff;
            v2 = (v2>>24)     & 0xff;

            v3 = current_palette->lookup[*src2++];
            y3 = (v3)  & 0xff;
            u3 = (v3>>8) & 0xff;
            v3 = (v3>>24)     & 0xff;

            v4 = current_palette->lookup[*src2++];
            y4 = (v4)  & 0xff;
            u4 = (v4>>8) & 0xff;
            v4 = (v4>>24)     & 0xff;

         *dest_y = y;
         *(dest_y++ + HWSCALE_WIDTH) = y3;
         *dest_y = y2;
         *(dest_y++ + HWSCALE_WIDTH) = y4;

         *dest_u++ = (u+u2+u3+u4)/4;
         *dest_v++ = (v+v2+v3+v4)/4;

         /* I thought that the following would be better, but it is not
          * the case. The color gets blurred
         if (y || y2 || y3 || y4) {
                 *dest_u++ = (u*y+u2*y2+u3*y3+u4*y4)/(y+y2+y3+y4);
                 *dest_v++ = (v*y+v2*y2+v3*y3+v4*y4)/(y+y2+y3+y4);
         } else {
                 *dest_u++ =128;
                 *dest_v++ =128;
         }
         */
      }
   }
}


static void x11_window_update_16_to_YV12_perfect(struct mame_bitmap *bitmap)
{      /* this one is used when scale==2 */
   unsigned int _x,_y;
   char *dest_y;
   char *dest_u;
   char *dest_v;
   unsigned short *src;
   unsigned short *src2;
   int u,v,y;
   char rotate = 0;
   char *tmp;

   if (current_palette != debug_palette &&
       (blit_flipx || blit_flipy || blit_swapxy))
   {
      rotate = 1;
      rotate_func(hwscale_yv12_rotate_buf1, bitmap, visual.min_y);
   }

   for(_y=visual.min_y;_y<=visual.max_y;_y++)
   {
      if (rotate)
      {
         tmp = hwscale_yv12_rotate_buf0;
         hwscale_yv12_rotate_buf0 = hwscale_yv12_rotate_buf1;
         hwscale_yv12_rotate_buf1 = tmp;
         rotate_func(hwscale_yv12_rotate_buf1, bitmap, _y+1);
         src  = (unsigned short*)hwscale_yv12_rotate_buf0;
         src2 = (unsigned short*)hwscale_yv12_rotate_buf1;
      }
      else
      {
         src=bitmap->line[_y] ;
         src+= visual.min_x;
         src2=bitmap->line[_y+1];
         src2+= visual.min_x;
      }

      dest_y=HWSCALE_YPLANE+2*(HWSCALE_WIDTH*(_y-visual.min_y));
      dest_v=HWSCALE_UPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)));
      dest_u=HWSCALE_VPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)));
      for(_x=visual.min_x;_x<=visual.max_x;_x++)
      {
            v= current_palette->lookup[*src++];
            y = (v)  & 0xff;
            u = (v>>8) & 0xff;
            v = (v>>24)     & 0xff;

         *(dest_y+HWSCALE_WIDTH)=y;
         *dest_y++=y;
         *(dest_y+HWSCALE_WIDTH)=y;
         *dest_y++=y;
         *dest_u++ = u;
         *dest_v++ = v;
      }
   }
}

static void x11_window_update_32_to_YV12_direct(struct mame_bitmap *bitmap)
{
   int _x,_y,r,g,b;
   char *dest_y;
   char *dest_u;
   char *dest_v;
   unsigned int *src;
   unsigned int *src2;
   int u,v,y,u2,v2,y2,u3,v3,y3,u4,v4,y4;     /* 12 */
   char rotate = 0;                          /* 34 */

   if (current_palette != debug_palette &&
       (blit_flipx || blit_flipy || blit_swapxy))
      rotate = 1;

   for(_y=visual.min_y;_y<visual.max_y;_y+=2)
   {
      if (rotate)
      {
         rotate_func(hwscale_yv12_rotate_buf0, bitmap, _y);
         rotate_func(hwscale_yv12_rotate_buf1, bitmap, _y+1);
         src  = (unsigned int*)hwscale_yv12_rotate_buf0;
         src2 = (unsigned int*)hwscale_yv12_rotate_buf1;
      }
      else
      {
         src=bitmap->line[_y] ;
         src+= visual.min_x;
         src2=bitmap->line[_y+1];
         src2+= visual.min_x;
      }

      dest_y=HWSCALE_YPLANE+(HWSCALE_WIDTH*(_y-visual.min_y));
      dest_v=HWSCALE_UPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)/2));
      dest_u=HWSCALE_VPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)/2));

      for(_x=visual.min_x;_x<visual.max_x;_x+=2)
      {
         b = *src++;
         r = (b>>16) & 0xFF;
         g = (b>>8)  & 0xFF;
         b = (b)     & 0xFF;
         RGB2YUV(r,g,b,y,u,v);

         b = *src++;
         r = (b>>16) & 0xFF;
         g = (b>>8)  & 0xFF;
         b = (b)     & 0xFF;
         RGB2YUV(r,g,b,y2,u2,v2);

         b = *src2++;
         r = (b>>16) & 0xFF;
         g = (b>>8)  & 0xFF;
         b = (b)     & 0xFF;
         RGB2YUV(r,g,b,y3,u3,v3);

         b = *src2++;
         r = (b>>16) & 0xFF;
         g = (b>>8)  & 0xFF;
         b = (b)     & 0xFF;
         RGB2YUV(r,g,b,y4,u4,v4);

         *dest_y = y;
         *(dest_y++ + HWSCALE_WIDTH) = y3;
         *dest_y = y2;
         *(dest_y++ + HWSCALE_WIDTH) = y4;

         r&=RMASK;  r>>=16;
         g&=GMASK;  g>>=8;
         b&=BMASK;  b>>=0;
         *dest_u++ = (u+u2+u3+u4)/4;
         *dest_v++ = (v+v2+v3+v4)/4;
      }
   }
}

static void x11_window_update_32_to_YV12_direct_perfect(struct mame_bitmap *bitmap)
{ /* This one is used when scale == 2 */
   int _x,_y,r,g,b;
   char *dest_y;
   char *dest_u;
   char *dest_v;
   unsigned int *src;
   unsigned int *src2;
   int u,v,y;
   char rotate = 0;
   char *tmp;

   if (current_palette != debug_palette &&
       (blit_flipx || blit_flipy || blit_swapxy))
   {
      rotate = 1;
      rotate_func(hwscale_yv12_rotate_buf1, bitmap, visual.min_y);
   }

   for(_y=visual.min_y;_y<=visual.max_y;_y++)
   {
      if (rotate)
      {
         tmp = hwscale_yv12_rotate_buf0;
         hwscale_yv12_rotate_buf0 = hwscale_yv12_rotate_buf1;
         hwscale_yv12_rotate_buf1 = tmp;
         rotate_func(hwscale_yv12_rotate_buf1, bitmap, _y+1);
         src  = (unsigned int*)hwscale_yv12_rotate_buf0;
         src2 = (unsigned int*)hwscale_yv12_rotate_buf1;
      }
      else
      {
         src=bitmap->line[_y] ;
         src+= visual.min_x;
         src2=bitmap->line[_y+1];
         src2+= visual.min_x;
      }

      dest_y=HWSCALE_YPLANE+2*(HWSCALE_WIDTH*(_y-visual.min_y));
      dest_v=HWSCALE_UPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)));
      dest_u=HWSCALE_VPLANE+((HWSCALE_WIDTH/2)*((_y-visual.min_y)));
      for(_x=visual.min_x;_x<=visual.max_x;_x++)
      {
         b = *src++;
         r = (b>>16) & 0xFF;
         g = (b>>8)  & 0xFF;
         b = (b)     & 0xFF;
         RGB2YUV(r,g,b,y,u,v);

         *(dest_y+HWSCALE_WIDTH) = y;
         *dest_y++ = y;
         *(dest_y+HWSCALE_WIDTH) = y;
         *dest_y++ = y;
         *dest_u++ = u;
         *dest_v++ = v;
      }
   }
}

static void x11_window_update_16_to_YUY2(struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned short
#define DEST_PIXEL unsigned short
#define BLIT_HWSCALE_YUY2
#define INDIRECT current_palette->lookup
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef BLIT_HWSCALE_YUY2
#undef INDIRECT
}

static void x11_window_update_32_to_YUY2_direct(struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned int
#define DEST_PIXEL unsigned short
#define BLIT_HWSCALE_YUY2
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef BLIT_HWSCALE_YUY2
}

#undef RMASK
#undef GMASK
#undef BMASK

#endif

static void x11_window_update_16_to_16bpp (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned short
#define DEST_PIXEL unsigned short
   if (current_palette->lookup)
   {
#define INDIRECT current_palette->lookup
#include "blit.h"
#undef  INDIRECT
   }
   else
   {
#include "blit.h"
   }
#undef SRC_PIXEL
#undef DEST_PIXEL
}

static void x11_window_update_16_to_24bpp (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned short
#define DEST_PIXEL unsigned int
#define INDIRECT current_palette->lookup
#define PACK_BITS
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef INDIRECT
#undef PACK_BITS
}

static void x11_window_update_16_to_32bpp (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned short
#define DEST_PIXEL unsigned int
#define INDIRECT current_palette->lookup
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef INDIRECT
}

static void x11_window_update_32_to_16bpp_rgb_565_direct (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned int
#define DEST_PIXEL unsigned short
#define CONVERT_PIXEL(p) _32TO16_RGB_565(p)
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef CONVERT_PIXEL
}

static void x11_window_update_32_to_16bpp_rgb_555_direct (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned int
#define DEST_PIXEL unsigned short
#define CONVERT_PIXEL(p) _32TO16_RGB_555(p)
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef CONVERT_PIXEL
}

static void x11_window_update_32_to_16bpp_direct (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned int
#define DEST_PIXEL unsigned short
#define CONVERT_PIXEL(p) \
   sysdep_palette_make_pen(current_palette, p)
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef CONVERT_PIXEL
}

static void x11_window_update_32_to_24bpp_direct (struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned int
#define DEST_PIXEL unsigned int
#define PACK_BITS
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
#undef PACK_BITS
}

static void x11_window_update_32_to_32bpp_direct(struct mame_bitmap *bitmap)
{
#define SRC_PIXEL unsigned int
#define DEST_PIXEL unsigned int
#include "blit.h"
#undef SRC_PIXEL
#undef DEST_PIXEL
}

#endif /* ifdef x11 */
