/*
 * X-Mame x11 input code
 *
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include "xmame.h"
#include "devices.h"
#include "x11.h"
#include "xkeyboard.h"

#ifdef USE_XINPUT_DEVICES
#include <X11/extensions/XInput.h>

enum { XMAME_NULLDEVICE, XMAME_TRACKBALL, XMAME_JOYSTICK };
enum { XINPUT_MOUSE_0, XINPUT_MOUSE_1, XINPUT_MOUSE_2, XINPUT_MOUSE_3,
       XINPUT_JOYSTICK_0, XINPUT_JOYSTICK_1, XINPUT_JOYSTICK_2, XINPUT_JOYSTICK_3,
       XINPUT_MAX_NUM_DEVICES };

/* struct which keeps all info for a XInput-devices */
typedef struct {
  char *deviceName;
  XDeviceInfo *info;
  int mameDevice;
  int previousValue[JOY_AXES];
  int neverMoved;
} XInputDeviceData;

static XInputDeviceData XIdevices[XINPUT_MAX_NUM_DEVICES] = {
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 },
   { NULL, NULL, XMAME_NULLDEVICE, {}, 1 }
} ;

/* prototypes */
static void XInputDevices_init(void);
static void XInputClearDeltas(void);
static int XInputProcessEvent(XEvent *);
#endif

/* options */
static int xinput_use_winkeys = 0;
static int xinput_grab_mouse = 0;
static int xinput_grab_keyboard = 0;
static int xinput_show_cursor = 1;

/* private variables */
static int xinput_force_grab = 0;
static int xinput_focus = 1;
static int xinput_old_mouse_grab = 0;
static int xinput_keyboard_grabbed = 0;
static int xinput_mouse_grabbed = 0;
static int xinput_cursors_allocated = 0;
/* 2 should be MOUSE_AXES but we don't support more than 2 axes at the moment */
static int xinput_current_mouse[2];
static int xinput_mouse_motion[2];
static Cursor xinput_normal_cursor;
static Cursor xinput_invisible_cursor;

static int xinput_mapkey(struct rc_option *option, const char *arg, int priority);

struct rc_option x11_input_opts[] = {
	/* name, shortname, type, dest, deflt, min, max, func, help */
	{ "X11-input related", NULL, rc_seperator, NULL, NULL, 0, 0, NULL, NULL },
	{ "grabmouse", "gm", rc_bool, &xinput_grab_mouse, "0", 0, 0, NULL, "Enable/disable mousegrabbing (also alt + pagedown)" },
	{ "grabkeyboard", "gkb", rc_bool, &xinput_grab_keyboard, "0", 0, 0, NULL, "Enable/disable keyboardgrabbing (also alt + pageup)" },
	{ "cursor", "cu", rc_bool, &xinput_show_cursor, "1", 0, 0, NULL, "Show/don't show the cursor" },
	{ "winkeys", "wk", rc_bool, &xinput_use_winkeys, "0", 0, 0, NULL, "Enable/disable mapping of windowskeys under X" },
	{ "mapkey", "mk", rc_use_function, NULL, NULL, 0, 0, xinput_mapkey, "Set a specific key mapping, see xmamerc.dist" },
#ifdef USE_XINPUT_DEVICES
   { "XInput-trackball1",	"XItb1",	rc_string,
     &XIdevices[XINPUT_MOUSE_0].deviceName,
     NULL,	1,		0,	NULL,
     "Device name for trackball of player 1 (see XInput)" },
   { "XInput-trackball2",	"XItb2",	rc_string,
     &XIdevices[XINPUT_MOUSE_1].deviceName,
     NULL,	1,		0,	NULL,
     "Device name for trackball of player 2 (see XInput)" },
   { "XInput-trackball3",	"XItb3",	rc_string,
     &XIdevices[XINPUT_MOUSE_2].deviceName,
     NULL,	1,		0,	NULL,
     "Device name for trackball of player 3 (see XInput)" },
   { "XInput-trackball4",	"XItb4",	rc_string,
     &XIdevices[XINPUT_MOUSE_3].deviceName,
     NULL,	1,		0,	NULL,
     "Device name for trackball of player 4 (see XInput)" },
#endif
	{ NULL, NULL, rc_end, NULL, NULL, 0, 0, NULL, NULL }
};

/*
 * Parse keyboard (and other) events
 */
void sysdep_update_keyboard (void)
{
	XEvent				E;
	KeySym				keysym;
	char				keyname[16+1];
	int				mask;
	struct xmame_keyboard_event	event;
	
#ifdef USE_XINPUT_DEVICES
	XInputClearDeltas();
#endif

#ifdef NOT_DEFINED /* x11 */
	if(run_in_root_window && x11_video_mode == X11_WINDOW)
	{
		static int i=0;
		i = ++i % 3;
		switch (i)
		{
			case 0:
				xkey[KEY_O] = 0;
				xkey[KEY_K] = 0;
				break;
			case 1:
				xkey[KEY_O] = 1;
				xkey[KEY_K] = 0;
				break;
			case 2:
				xkey[KEY_O] = 0;
				xkey[KEY_K] = 1;
				break;
		}
	}
	else
#endif

		/* query all events that we have previously requested */
		while ( XPending(display) )
		{
			mask = FALSE;
			event.press = FALSE;

			XNextEvent(display,&E);
			/*  fprintf(stderr_file,"Event: %d\n",E.type); */

			/* we don't have to check x11_video_mode or extensions like xil here,
			   since our eventmask should make sure that we only get the event's matching
			   the current update method */
			switch (E.type)
			{
				/* display events */
				case FocusIn:
					/* check for multiple events and ignore them */
					if (xinput_focus) break;
					xinput_focus = TRUE;
					/* to avoid some meta-keys to get locked when wm iconify xmame, we must
					   perform a key reset whenever we retrieve keyboard focus */
					xmame_keyboard_clear();
					if ((xinput_force_grab || xinput_grab_mouse || xinput_old_mouse_grab) &&
					    !XGrabPointer(display, window, True,
					      PointerMotionMask|ButtonPressMask|ButtonReleaseMask,
					      GrabModeAsync, GrabModeAsync, window, None, CurrentTime))
					{
						xinput_mouse_grabbed = 1;
                                                xinput_old_mouse_grab = 0;
						if (xinput_cursors_allocated && xinput_show_cursor)
						  XDefineCursor(display,window,xinput_invisible_cursor);
					}
					break;
				case FocusOut:
					/* check for multiple events and ignore them */
					if (!xinput_focus) break;
					xinput_focus = FALSE;
					if (xinput_mouse_grabbed && !xinput_force_grab)
					{
						XUngrabPointer(display, CurrentTime);
						if (xinput_cursors_allocated && xinput_show_cursor)
						  XDefineCursor(display,window,xinput_normal_cursor);
						xinput_mouse_grabbed = 0;
						xinput_old_mouse_grab = 1;
					}
					break;
				case EnterNotify:
					if (use_private_cmap) XInstallColormap(display,colormap);
					break;	
				case LeaveNotify:
					if (use_private_cmap) XInstallColormap(display,DefaultColormapOfScreen(screen));
					break;	
#ifdef USE_XIL
				case ConfigureNotify:
					update_xil_window_size( E.xconfigure.width, E.xconfigure.height );
					break;
#endif
				/* input events */
				case MotionNotify:
					xinput_mouse_motion[0] += E.xmotion.x_root;
					xinput_mouse_motion[1] += E.xmotion.y_root;
					break;
				case ButtonPress:
					mask = TRUE;
#ifdef USE_DGA
					/* Some buggy combination of XFree and virge screwup the viewport
					   on the first mouseclick */
					if(xf86_dga_first_click) { xf86_dga_first_click = 0; xf86_dga_fix_viewport = 1; }
#endif          
				case ButtonRelease:
					mouse_data[0].buttons[E.xbutton.button-1] = mask;
					break;
				case KeyPress:
					event.press = TRUE;
				case KeyRelease:
					/* get bare keysym, for the scancode */
					keysym = XLookupKeysym ((XKeyEvent *) &E, 0);
					/* get key name, using modifiers for the unicode */
					XLookupString ((XKeyEvent *) &E, keyname, 16, NULL, NULL);

					/*	fprintf(stderr, "Keyevent keycode: %04X, keysym: %04X, unicode: %02X\n",
						E.xkey.keycode, (unsigned int)keysym, (unsigned int)keyname[0]); */

					/* look which table should be used */
					if ( (keysym & ~0x1ff) == 0xfe00 )
						event.scancode = extended_code_table[keysym & 0x01ff];
					else if (keysym < 0xff)
						event.scancode = code_table[keysym & 0x00ff];
					else
						event.scancode = 0;

					event.unicode = keyname[0];

					xmame_keyboard_register_event(&event);
					break;
				default:
					/* grrr we can't use case here since the event types for XInput devices
					   aren't hardcoded, since we should have caught anything else above,
					   just asume it's an XInput event */
#ifdef USE_XINPUT_DEVICES
					if (XInputProcessEvent(&E)) break;
#endif
#ifdef X11_JOYSTICK
					process_x11_joy_event(&E);
#endif
					break;
			} /* switch */
		} /* while */
}

/*
 *  keyboard remapping routine
 *  invoiced in startup code
 *  returns 0-> success 1-> invalid from or to
 */
static int xinput_mapkey(struct rc_option *option, const char *arg, int priority)
{
	unsigned int from,to;
	/* ultrix sscanf() requires explicit leading of 0x for hex numbers */
	if ( sscanf(arg,"0x%x,0x%x",&from,&to) == 2)
	{
		/* perform tests */
		/* fprintf(stderr,"trying to map %x to %x\n",from,to); */
		if ( to <= 127 )
		{
			if ( from <= 0x00ff ) 
			{
				code_table[from]=to; return OSD_OK;
			}
			else if ( (from>=0xfe00) && (from<=0xffff) ) 
			{
				extended_code_table[from&0x01ff]=to; return OSD_OK;
			}
		}
		/* stderr_file isn't defined yet when we're called. */
		fprintf(stderr,"Invalid keymapping %s. Ignoring...\n", arg);
	}
	return OSD_NOT_OK;
}

void sysdep_mouse_poll(void)
{
	int i;
	if(x11_video_mode == X11_DGA)
	{
		/* 2 should be MOUSE_AXES but we don't support more
		   than 2 axes at the moment so this is faster */
		for (i=0; i<2; i++)
		{
			mouse_data[0].deltas[i] = xinput_mouse_motion[i];
			xinput_mouse_motion[i] = 0;
		}
	}
	else
	{
		Window root,child;
		int root_x, root_y, pos_x, pos_y;
		unsigned int keys_buttons;

		if (!XQueryPointer(display, window, &root,&child, &root_x,&root_y,
					&pos_x,&pos_y,&keys_buttons) )
		{
			mouse_data[0].deltas[0] = 0;
			mouse_data[0].deltas[1] = 0;
			return;
		}

		if ( xinput_mouse_grabbed )
		{
			XWarpPointer(display, None, window, 0, 0, 0, 0,
					visual_width/2, visual_height/2);
			mouse_data[0].deltas[0] = pos_x - visual_width/2;
			mouse_data[0].deltas[1] = pos_y - visual_height/2;
		}
		else
		{
			mouse_data[0].deltas[0] = pos_x - xinput_current_mouse[0];
			mouse_data[0].deltas[1] = pos_y - xinput_current_mouse[1];
		}
		xinput_current_mouse[0] = pos_x;
		xinput_current_mouse[1] = pos_y;
	}
}

/*
 * This function creates an invisible cursor.
 *
 * I got the idea and code fragment from in the Apple II+ Emulator
 * version 0.06 for Linux by Aaron Culliney
 * <chernabog@baldmountain.bbn.com>
 *
 * I also found a post from Steve Lamont <spl@szechuan.ucsd.edu> on
 * xforms@bob.usuf2.usuhs.mil.  His comments read:
 *
 * Lifted from unclutter
 * Mark M Martin. cetia 1991 mmm@cetia.fr
 * Version 4 changes by Charles Hannum <mycroft@ai.mit.edu>
 *
 * So I guess this code has been around the block a few times.
 */

static Cursor xinput_create_invisible_cursor (Display * display, Window win)
{
   Pixmap cursormask;
   XGCValues xgc;
   XColor dummycolour;
   Cursor cursor;
   GC gc;

   cursormask = XCreatePixmap (display, win, 1, 1, 1 /*depth */ );
   xgc.function = GXclear;
   gc = XCreateGC (display, cursormask, GCFunction, &xgc);
   XFillRectangle (display, cursormask, gc, 0, 0, 1, 1);
   dummycolour.pixel = 0;
   dummycolour.red = 0;
   dummycolour.flags = 04;
   cursor = XCreatePixmapCursor (display, cursormask, cursormask,
                                 &dummycolour, &dummycolour, 0, 0);
   XFreeGC (display, gc);
   XFreePixmap (display, cursormask);
   return cursor;
}

void sysdep_set_leds(int leds)
{
}

int xinput_open(int force_grab, int event_mask)
{
  xinput_force_grab = force_grab;
  
  /* handle winkey mappings */
  if (xinput_use_winkeys)
  {
  	extended_code_table[XK_Meta_L&0x1FF] = KEY_LWIN; 
  	extended_code_table[XK_Meta_R&0x1FF] = KEY_RWIN; 
  }

#ifdef USE_XINPUT_DEVICES
  XInputDevices_init();
#endif

  /* mouse grab failing is not really a problem */
  if (xinput_force_grab || xinput_grab_mouse)
  {
    if(XGrabPointer (display, window, True,
         PointerMotionMask|ButtonPressMask|ButtonReleaseMask,
         GrabModeAsync, GrabModeAsync, window, None, CurrentTime))
      fprintf(stderr_file, "Warning mouse grab failed\n");
    else
      xinput_mouse_grabbed = 1;
  }
  
  /* Call sysdep_mouse_poll to get the current mouse position.
     Do this twice to make the mouse[0].deltas[x] == 0 */
  sysdep_mouse_poll();
  sysdep_mouse_poll();
  
  if (window != DefaultRootWindow(display))
  {
    /* setup the cursor */
    xinput_normal_cursor     = XCreateFontCursor (display, XC_trek);
    xinput_invisible_cursor  = xinput_create_invisible_cursor (display, window);
    xinput_cursors_allocated = 1;

    if (xinput_mouse_grabbed || !xinput_show_cursor || xinput_force_grab)
 	XDefineCursor (display, window, xinput_invisible_cursor);
    else
    	XDefineCursor (display, window, xinput_normal_cursor);

    /* Select event mask */
    event_mask |= FocusChangeMask | KeyPressMask | KeyReleaseMask |
                  ButtonPressMask | ButtonReleaseMask;
    XSelectInput (display, window, event_mask);
  }

  if ((xinput_force_grab == 2) || xinput_grab_keyboard)
  {
    /* keyboard grab failing could be fatal so let the caller know */
    if (XGrabKeyboard(display, window, True, GrabModeAsync, GrabModeAsync,
         CurrentTime))
      return 1;

    xinput_keyboard_grabbed = 1;
  }
  
  return 0;
}

void xinput_close(void)
{
  if (xinput_mouse_grabbed)
  {
     XUngrabPointer (display, CurrentTime);
     xinput_mouse_grabbed = 0;
  }

  if (xinput_keyboard_grabbed)
  {
     XUngrabKeyboard (display, CurrentTime);
     xinput_keyboard_grabbed = 0;
  }

  if(xinput_cursors_allocated)
  {
     XFreeCursor(display, xinput_normal_cursor);
     XFreeCursor(display, xinput_invisible_cursor);
     xinput_cursors_allocated = 0;
  }
}

void xinput_check_hotkeys(void)
{
  if (!xinput_force_grab &&
      code_pressed (KEYCODE_LALT) &&
      code_pressed_memory (KEYCODE_PGDN))
  {
     if (xinput_mouse_grabbed)
     {
        XUngrabPointer (display, CurrentTime);
        if (xinput_cursors_allocated && xinput_show_cursor)
           XDefineCursor (display, window, xinput_normal_cursor);
        xinput_mouse_grabbed = 0;
     }
     else if (!XGrabPointer (display, window, True,
                 PointerMotionMask|ButtonPressMask|ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, window, None, CurrentTime))
     {
        if (xinput_cursors_allocated && xinput_show_cursor)
           XDefineCursor (display, window, xinput_invisible_cursor);
        xinput_mouse_grabbed = 1;
     }
  }

  /* toggle keyboard grabbing */
  if ((xinput_force_grab!=2) && code_pressed (KEYCODE_LALT) &&
      code_pressed_memory (KEYCODE_PGUP))
  {
    if (xinput_keyboard_grabbed)
    {
      XUngrabKeyboard (display, CurrentTime);
      xinput_keyboard_grabbed = 0;
    }
    else if (!XGrabKeyboard(display, window, True, GrabModeAsync, GrabModeAsync, CurrentTime))
    {
      xinput_keyboard_grabbed = 1;
    }
  }
}

/*
 * X-Mame XInput trackball code
 *
 */

#ifdef USE_XINPUT_DEVICES

/* XInput-Event types */
static int           motion_type         = -1;
static int           button_press_type   = -1;
static int           button_release_type = -1;
static int           key_press_type      = -1;
static int           key_release_type    = -1;

/* prototypes */
/* these two functions were taken from the source of the program 'xinput',
   available at ftp://xxx.xxx.xxx */
static XDeviceInfo*
find_device_info(Display *display, char *name, Bool only_extended);
static int
register_events(int player_id, Display *dpy, XDeviceInfo *info, char *dev_name);


/* initializes XInput devices */
static void
XInputDevices_init(void)
{
	int i,j,k;

	fprintf(stderr_file, "XInput: Initialization...\n");

	if (!XQueryExtension(display,"XInputExtension",&i,&j,&k)) {
		fprintf(stderr_file,"XInput: Your Xserver doesn't support XInput Extensions\n");
		return;
	}

	/* parse all devicenames */
	for(i=0;i<XINPUT_MAX_NUM_DEVICES;++i) {
		if (XIdevices[i].deviceName) {
			/* if not NULL, check for an existing device */
			XIdevices[i].info=find_device_info(display,XIdevices[i].deviceName,True);
			if (! XIdevices[i].info) {
				fprintf(stderr_file,"XInput: Unable to find device `%s'. Ignoring it!\n",
						XIdevices[i].deviceName);
				XIdevices[i].deviceName=NULL;
			} else {
				/* ok, found a device, now register device for motion events */
				if (i < XINPUT_JOYSTICK_1) {
					XIdevices[i].mameDevice=XMAME_TRACKBALL;
				} else {
					/* prepared for XInput-Joysticks
					   XIdevices[i].mameDevice=XMAME_JOYSTICK;
					   */
				}
				if (! register_events(i, display,XIdevices[i].info,XIdevices[i].deviceName)) {
					fprintf(stderr_file,"XInput: Couldn't register device `%s' for events. Ignoring it\n",
							XIdevices[i].deviceName);
					XIdevices[i].deviceName=NULL;
				}
			}
		}
	}
}

static void XInputClearDeltas(void)
{
   int i;
   for(i = 1; i < MOUSE_MAX; i++)
   {
      mouse_data[i].deltas[0] = 0;
      mouse_data[i].deltas[1] = 0;
   }
}

/* Process events generated by XInput-devices. For now, just trackballs are supported */
static int
XInputProcessEvent(XEvent *ev)
{
	int i;

	if (ev->type == motion_type) {
		XDeviceMotionEvent *motion=(XDeviceMotionEvent *) ev;

		for(i = 1; i < MOUSE_MAX; i++)
			if (XIdevices[i-1].deviceName && motion->deviceid == XIdevices[i-1].info->id)
				break;

		if (i == MOUSE_MAX)
			return 0;

		if (XIdevices[i-1].neverMoved) {
			XIdevices[i-1].neverMoved=0;
			XIdevices[i-1].previousValue[0] = motion->axis_data[0];
			XIdevices[i-1].previousValue[1] = motion->axis_data[1];
		}

		mouse_data[i].deltas[0] += motion->axis_data[0] - XIdevices[i-1].previousValue[0];
		mouse_data[i].deltas[1] += motion->axis_data[1] - XIdevices[i-1].previousValue[1];

		XIdevices[i-1].previousValue[0] = motion->axis_data[0];
		XIdevices[i-1].previousValue[1] = motion->axis_data[1];

		return 1;
	} else if (ev->type == button_press_type || ev->type == button_release_type) {
		XDeviceButtonEvent *button = (XDeviceButtonEvent *)ev;

		for(i = 1; i < MOUSE_MAX; i++)
			if (XIdevices[i-1].deviceName && button->deviceid == XIdevices[i-1].info->id)
				break;

		if (i == MOUSE_MAX)
			return 0;

		/* fprintf(stderr_file, "XInput: Player %d: Button %d %s\n",
		   i + 1, button->button, button->state ? "released" : "pressed"); */

		mouse_data[i].buttons[button->button - 1] = (ev->type == button_press_type) ? 1 : 0;

		return 1;
	}

	return 0;
}

/* this piece of code was taken from package xinput-1.12 */
static XDeviceInfo*
find_device_info(Display	*display,
		 char		*name,
		 Bool		only_extended)
{
	XDeviceInfo	*devices;
	int		loop;
	int		num_devices;
	int		len = strlen(name);
	Bool		is_id = True;
	XID		id = 0;

	for(loop=0; loop<len; loop++) {
		if (!isdigit(name[loop])) {
			is_id = False;
			break;
		}
	}

	if (is_id) {
		id = atoi(name);
	}

	devices = XListInputDevices(display, &num_devices);

	for(loop=0; loop<num_devices; loop++) {
		if ((!only_extended || (devices[loop].use == IsXExtensionDevice)) &&
				((!is_id && strcmp(devices[loop].name, name) == 0) ||
				 (is_id && devices[loop].id == id))) {
			return &devices[loop];
		}
	}
	return NULL;
}

/* this piece of code was taken from package xinput-1.12 */
static int
register_events(int		player_id,
		Display		*dpy,
		XDeviceInfo	*info,
		char		*dev_name)
{
	int			number = 0;	/* number of events registered */
	XEventClass		event_list[7];
	int			i;
	XAnyClassPtr	any;
	XDevice		*device;
	Window		root_win;
	unsigned long	screen;
	XInputClassInfo	*ip;
	XButtonInfoPtr      binfo;
	XValuatorInfoPtr    vinfo;

	screen = DefaultScreen(dpy);
	root_win = RootWindow(dpy, screen);

	device = XOpenDevice(dpy, info->id);

	if (!device) {
		fprintf(stderr_file, "XInput: Unable to open XInput device `%s'\n", dev_name);
		return 0;
	}

	fprintf(stderr_file, "XInput: Player %d using Device `%s'", player_id + 1, dev_name);

	if (device->num_classes > 0) {
		any = (XAnyClassPtr)(info->inputclassinfo);

		for (ip = device->classes, i=0; i<info->num_classes; ip++, i++) {
			switch (ip->input_class) {
				case KeyClass:
					DeviceKeyPress(device, key_press_type, event_list[number]); number++;
					DeviceKeyRelease(device, key_release_type, event_list[number]); number++;
					break;

				case ButtonClass:
					binfo = (XButtonInfoPtr) any;
					DeviceButtonPress(device, button_press_type, event_list[number]); number++;
					DeviceButtonRelease(device, button_release_type, event_list[number]); number++;
					fprintf(stderr_file, ", %d buttons", binfo->num_buttons);
					break;

				case ValuatorClass:
					vinfo=(XValuatorInfoPtr) any;
					DeviceMotionNotify(device, motion_type, event_list[number]); number++;
					fprintf(stderr_file, ", %d axis", vinfo->num_axes);
					break;

				default:
					break;
			}
			any = (XAnyClassPtr) ((char *) any+any->length);
		}

		if (XSelectExtensionEvent(dpy, root_win, event_list, number)) {
			fprintf(stderr_file, ": Could not select extended events, not using");
			number = 0;
		}
	} else
		fprintf(stderr_file, " contains no classes, not using");

	fprintf(stderr_file, "\n");

	return number;
}

#endif /* USE_XINPUT_DEVICES */
