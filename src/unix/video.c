/*
 * X-Mame generic video code
 *
 */
#define __VIDEO_C_
#include <math.h>
#include <stdio.h>

#include "driver.h"
#include "profiler.h"
#include "artwork.h"
#include "usrintrf.h"
#include "vidhrdw/vector.h"

#include "xmame.h"
#include "effect.h"
#include "devices.h"
#include "sysdep/misc.h"
#include "sysdep/sysdep_display.h"

#define FRAMESKIP_DRIVER_COUNT 2
static int use_auto_double = 1;
static int frameskipper = 0;
static int debugger_has_focus = 0;
static int led_state = 0;

static float f_beam;
static float f_flicker;
static float f_intensity;

static int use_artwork = 1;
static int use_backdrops = -1;
static int use_overlays = -1;
static int use_bezels = -1;

static int video_norotate = 0;
static int video_flipy = 0;
static int video_flipx = 0;
static int video_ror = 0;
static int video_rol = 0;
static int video_autoror = 0;
static int video_autorol = 0;

static struct sysdep_palette_struct *normal_palette = NULL;
static struct sysdep_palette_struct *debug_palette  = NULL;

static struct sysdep_display_open_params current_params;
static struct sysdep_display_open_params normal_params;
static struct sysdep_display_open_params debug_params = {
  0, 0, 16, 0, NAME " debug window", 1, 1, 0, 0, 0, 0, 0.0,
  xmame_keyboard_register_event, NULL };

static struct rectangle debug_bounds;

/* average FPS calculation */
static cycles_t start_time = 0;
static cycles_t end_time;
static int frames_displayed;
static int frames_to_display;

extern UINT8 trying_to_quit;

/* some prototypes */
static int video_handle_scale(struct rc_option *option, const char *arg,
		int priority);
static int video_verify_beam(struct rc_option *option, const char *arg,
		int priority);
static int video_verify_flicker(struct rc_option *option, const char *arg,
		int priority);
static int video_verify_intensity(struct rc_option *option, const char *arg,
		int priority);
static int video_verify_bpp(struct rc_option *option, const char *arg,
		int priority);
static int video_handle_vectorres(struct rc_option *option, const char *arg,
		int priority);
static int decode_ftr(struct rc_option *option, const char *arg, int priority);
static void osd_free_colors(void);

struct rc_option video_opts[] = {
   /* name, shortname, type, dest, deflt, min, max, func, help */
   { "Video Related",	NULL,			rc_seperator,	NULL,
     NULL,		0,			0,		NULL,
     NULL },
   { "fullscreen",   	NULL,    		rc_bool,	&normal_params.fullscreen,
     "0",           	0,       		0,		NULL,
     "Start in fullscreen mode (default: false)" },
   { "bpp",		"b",			rc_int,		&options.color_depth,
     "0",		0,			0,		video_verify_bpp,
     "Specify the colordepth the core should render, one of: auto(0), 15, 32" },
   { "arbheight",	"ah",			rc_int,		&normal_params.yarbsize,
     "0",		0,			4096,		NULL,
     "Scale video to exactly this height (0 = disable)" },
   { "heightscale",	"hs",			rc_int,		&normal_params.heightscale,
     "1",		1,			8,		NULL,
     "Set Y-Scale aspect ratio" },
   { "widthscale",	"ws",			rc_int,		&normal_params.widthscale,
     "1",		1,			8,		NULL,
     "Set X-Scale aspect ratio" },
   { "scale",		"s",			rc_use_function, NULL,
     NULL,		0,			0,		video_handle_scale,
     "Set X-Y Scale to the same aspect ratio. For vector games scale (and also width- and heightscale) may have value's like 1.5 and even 0.5. For scaling of regular games this will be rounded to an int" },
   { "effect",		"ef",			rc_int,		&normal_params.effect,
     EFFECT_NONE,	EFFECT_NONE,		EFFECT_LAST,	NULL,
     "Video effect:\n"
	     "0 = none (default)\n"
	     "1 = scale2x (smooth scaling effect)\n"
	     "2 = scan2 (light scanlines)\n"
	     "3 = rgbstripe (3x2 rgb vertical stripes)\n"
	     "4 = rgbscan (2x3 rgb horizontal scanlines)\n"
	     "5 = scan3 (3x3 deluxe scanlines)\n"
	     "6 = lq2x (2x low  quality magnification filter)\n"
	     "7 = hq2x (2x high quality magnification filter)\n"
	     "8 = 6tap2x (2x sinc-based 6-tap filter with scanlines\n" },
   { "autodouble",	"adb",			rc_bool,	&use_auto_double,
     "1",		0,			0,		NULL,
     "Enable/disable automatic scale doubling for 1:2 pixel aspect ratio games" },
   { "scanlines",	"sl",			rc_bool,	&normal_params.scanlines,
     "0",		0,			0,		NULL,
     "Enable/disable displaying simulated scanlines" },
   { "artwork",		"art",			rc_bool,	&use_artwork,
     "1",		0,			0,		NULL,
     "Use additional game artwork (sets default for specific options below)" },
   { "use_backdrops",	"backdrop",		rc_bool,	&use_backdrops,
     "1",		0,			0,		NULL,
     "Use backdrop artwork" },
   { "use_overlays",	"overlay",		rc_bool,	&use_overlays,
     "1",		0,			0,		NULL,
     "Use overlay artwork" },
   { "use_bezels",	"bezel",		rc_bool,	&use_bezels,
     "1",		0,			0,		NULL,
     "Use bezel artwork" },
   { "artwork_crop",	"artcrop",		rc_bool,	&options.artwork_crop,
     "0",		0,			0,		NULL,
     "Crop artwork to game screen only." },
   { "artwork_resolution","artres",		rc_int,		&options.artwork_res,
     "0",		0,			0,		NULL,
     "Artwork resolution (0 for auto)" },
   { "frameskipper",	"fsr",			rc_int,		&frameskipper,
     "1",		0,			FRAMESKIP_DRIVER_COUNT-1, NULL,
     "Select which autoframeskip and throttle routines to use. Available choices are:\n0 Dos frameskip code\n1 Enhanced frameskip code by William A. Barath" },
   { "throttle",	"th",			rc_bool,	&throttle,
     "1",		0,			0,		NULL,
     "Enable/disable throttle" },
   { "frames_to_run",	"ftr",			rc_int,		&frames_to_display,
     "0",		0,			0,		decode_ftr,
     "Sets the number of frames to run within the game" },
   { "sleepidle",	"si",			rc_bool,	&sleep_idle,
     "0",		0,			0,		NULL,
     "Enable/disable sleep during idle" },
   { "autoframeskip",	"afs",			rc_bool,	&autoframeskip,
     "1",		0,			0,		NULL,
     "Enable/disable autoframeskip" },
   { "maxautoframeskip", "mafs",		rc_int,		&max_autoframeskip,
     "8",		0,			FRAMESKIP_LEVELS-1, NULL,
     "Set highest allowed frameskip for autoframeskip" },
   { "frameskip",	"fs",			rc_int,		&frameskip,
     "0",		0,			FRAMESKIP_LEVELS-1, NULL,
     "Set frameskip when not using autoframeskip" },
   { "brightness",	"brt",			rc_float,	&options.brightness,
     "1.0",		0.5,			2.0,		NULL,
     "Set the brightness correction (0.5 - 2.0)" },
   { "pause_brightness","pbrt",			rc_float,	&options.pause_bright,
     "0.65",		0.5,			2.0,		NULL,
     "Additional pause brightness" },
   { "gamma",		"gc",			rc_float,	&options.gamma,
     "1.0",		0.5,			2.0,		NULL,
     "Set the gamma correction (0.5 - 2.0)" },
   { "norotate",	"nr",			rc_bool,	&video_norotate,
     "0",		0,			0,		NULL,
     "Do not apply rotation" },
   { "ror",		"rr",			rc_bool,	&video_ror,
     "0",		0,			0,		NULL,
     "Rotate screen clockwise" },
   { "rol",		"rl",			rc_bool,	&video_rol,
     "0",		0,			0,		NULL,
     "Rotate screen counter-clockwise" },
   { "autoror",		NULL,			rc_bool,	&video_autoror,
     "0",		0,			0,		NULL,
     "Automatically rotate screen clockwise for vertical games" },
   { "autorol",		NULL,			rc_bool,	&video_autorol,
     "0",		0,			0,		NULL,
     "Automatically rotate screen counter-clockwise for vertical games" },
   { "flipx",		"fx",			rc_bool,	&video_flipx,
     "0",		0,			0,		NULL,
     "Flip screen left-right" },
   { "flipy",		"fy",			rc_bool,	&video_flipy,
     "0",		0,			0,		NULL,
     "Flip screen upside-down" },
   { "Vector Games Related", NULL,		rc_seperator,	NULL,
     NULL,		0,			0,		NULL,
     NULL },
   { "vectorres",	"vres",			rc_use_function, NULL,
     NULL,		0,			0,		video_handle_vectorres,
     "Always scale vectorgames to XresxYres, keeping their aspect ratio. This overrides the scale options" },
	{ "beam", "B", rc_float, &f_beam, "1.0", 1.0, 16.0, video_verify_beam, "Set the beam size for vector games" },
	{ "flicker", "f", rc_float, &f_flicker, "0.0", 0.0, 100.0, video_verify_flicker, "Set the flicker for vector games" },
	{ "intensity", NULL, rc_float, &f_intensity, "1.5", 0.5, 3.0, video_verify_intensity, "Set intensity in vector games" },
   { "antialias",	"aa",			rc_bool,	&options.antialias,
     "1",		0,			0,		NULL,
     "Enable/disable antialiasing" },
   { "translucency",	"t",			rc_bool,	&options.translucency,
     "1",		0,			0,		NULL,
     "Enable/disable tranlucency" },
   { NULL,		NULL,			rc_link,	sysdep_display_opts,
     NULL,		0,			0,		NULL,
     NULL },
   { NULL, NULL, rc_end, NULL, NULL, 0, 0, NULL, NULL }
};

static int video_handle_scale(struct rc_option *option, const char *arg,
   int priority)
{
	if (rc_set_option2(video_opts, "widthscale", arg, priority))
		return -1;
	if (rc_set_option2(video_opts, "heightscale", arg, priority))
		return -1;

	option->priority = priority;

	return 0;
}

static int video_verify_beam(struct rc_option *option, const char *arg,
		int priority)
{
	options.beam = (int)(f_beam * 0x00010000);
	if (options.beam < 0x00010000)
		options.beam = 0x00010000;
	else if (options.beam > 0x00100000)
		options.beam = 0x00100000;

	option->priority = priority;

	return 0;
}

static int video_verify_flicker(struct rc_option *option, const char *arg,
		int priority)
{
	options.vector_flicker = (int)(f_flicker * 2.55);
	if (options.vector_flicker < 0)
		options.vector_flicker = 0;
	else if (options.vector_flicker > 255)
		options.vector_flicker = 255;

	option->priority = priority;

	return 0;
}

static int video_verify_intensity(struct rc_option *option, const char *arg,
		int priority)
{
	options.vector_intensity = f_intensity;
	option->priority = priority;
	return 0;
}

static int video_verify_bpp(struct rc_option *option, const char *arg,
   int priority)
{
	if (options.color_depth != 0
			&& options.color_depth != 15
			&& options.color_depth != 32)
	{
		options.color_depth = 0;
		fprintf(stderr, "error: invalid value for bpp: %s\n", arg);
		return -1;
	}

	option->priority = priority;

	return 0;
}

static int video_handle_vectorres(struct rc_option *option, const char *arg,
   int priority)
{
	if (sscanf(arg, "%dx%d", &options.vector_width, &options.vector_height) != 2)
	{
		options.vector_width = options.vector_height = 0;
		fprintf(stderr, "error: invalid value for vectorres: %s\n", arg);
		return -1;
	}

	option->priority = priority;

	return 0;
}


/*============================================================ */
/*	decode_ftr */
/*============================================================ */

static int decode_ftr(struct rc_option *option, const char *arg, int priority)
{
	int ftr;

	if (sscanf(arg, "%d", &ftr) != 1)
	{
		fprintf(stderr, "error: invalid value for frames_to_run: %s\n", arg);
		return -1;
	}

	/*
	 * if we're running < 5 minutes, allow us to skip warnings to
	 * facilitate benchmarking/validation testing
	 */
	frames_to_display = ftr;
	if (frames_to_display > 0 && frames_to_display < 60*60*5)
		options.skip_warnings = 1;

	option->priority = priority;
	return 0;
}

void osd_video_initpre()
{
	/* first start with the game's built-in orientation */
	int orientation = drivers[game_index]->flags & ORIENTATION_MASK;
	options.ui_orientation = orientation;

	if (options.ui_orientation & ORIENTATION_SWAP_XY)
	{
		/* if only one of the components is inverted, switch them */
		if ((options.ui_orientation & ROT180) == ORIENTATION_FLIP_X ||
				(options.ui_orientation & ROT180) == ORIENTATION_FLIP_Y)
			options.ui_orientation ^= ROT180;
	}

	/* override if no rotation requested */
	if (video_norotate)
		orientation = ROT0;

	/* rotate right */
	if (video_ror)
	{
		/* if only one of the components is inverted, switch them */
		if ((orientation & ROT180) == ORIENTATION_FLIP_X ||
				(orientation & ROT180) == ORIENTATION_FLIP_Y)
			orientation ^= ROT180;

		orientation ^= ROT90;
	}

	/* rotate left */
	if (video_rol)
	{
		/* if only one of the components is inverted, switch them */
		if ((orientation & ROT180) == ORIENTATION_FLIP_X ||
				(orientation & ROT180) == ORIENTATION_FLIP_Y)
			orientation ^= ROT180;

		orientation ^= ROT270;
	}

	/* auto-rotate right (e.g. for rotating lcds), based on original orientation */
	if (video_autoror && (drivers[game_index]->flags & ORIENTATION_SWAP_XY))
	{
		/* if only one of the components is inverted, switch them */
		if ((orientation & ROT180) == ORIENTATION_FLIP_X ||
				(orientation & ROT180) == ORIENTATION_FLIP_Y)
			orientation ^= ROT180;

		orientation ^= ROT90;
	}

	/* auto-rotate left (e.g. for rotating lcds), based on original orientation */
	if (video_autorol && (drivers[game_index]->flags & ORIENTATION_SWAP_XY))
	{
		/* if only one of the components is inverted, switch them */
		if ((orientation & ROT180) == ORIENTATION_FLIP_X ||
				(orientation & ROT180) == ORIENTATION_FLIP_Y)
			orientation ^= ROT180;

		orientation ^= ROT270;
	}

	/* flip X/Y */
	if (video_flipx)
		orientation ^= ORIENTATION_FLIP_X;
	if (video_flipy)
		orientation ^= ORIENTATION_FLIP_Y;

	normal_params.orientation = 0;
	if (orientation & ORIENTATION_FLIP_X)
	   normal_params.orientation |= SYSDEP_DISPLAY_FLIPX;
	if (orientation & ORIENTATION_FLIP_Y)
	   normal_params.orientation |= SYSDEP_DISPLAY_FLIPY;
	if (orientation & ORIENTATION_SWAP_XY)
	   normal_params.orientation |= SYSDEP_DISPLAY_SWAPXY;

	/* setup vector specific stuff */
	if (options.vector_width == 0 && options.vector_height == 0)
	{
		options.vector_width = 640;
		options.vector_height = 480;
	}

#if 0 /* FIXME */	
	if (sysdep_display_properties.vector_aux_renderer)
	{
		/* HACK to find out if this is a vector game before calling
		   run_game() */
		struct InternalMachineDriver machine;

		memset(&machine, 0, sizeof(machine));
		drivers[game_index]->drv(&machine);

		if (machine.video_attributes & VIDEO_TYPE_VECTOR)
		{
			vector_register_aux_renderer(sysdep_display_properties.vector_aux_renderer);
			use_overlays         = 0;
			use_bezels           = 0;
			options.artwork_crop = 1;
		}
	}
#endif

	/* set the artwork options */
	options.use_artwork = ARTWORK_USE_ALL;
	if (use_backdrops == 0)
		options.use_artwork &= ~ARTWORK_USE_BACKDROPS;
	if (use_overlays == 0)
		options.use_artwork &= ~ARTWORK_USE_OVERLAYS;
	if (use_bezels == 0)
		options.use_artwork &= ~ARTWORK_USE_BEZELS;
	if (!use_artwork)
		options.use_artwork = ARTWORK_USE_NONE;
		
	/* setup debugger related stuff */
	debug_params.width   = options.debug_width;
	debug_params.height  = options.debug_height;
	debug_bounds.min_x    = 0;
	debug_bounds.max_x    = options.debug_width - 1;
	debug_bounds.min_y    = 0;
	debug_bounds.max_y    = options.debug_height - 1;
}

int osd_create_display(const struct osd_create_params *params,
		UINT32 *rgb_components)
{
	video_fps            = params->fps;
	normal_params.width  = params->width;
	normal_params.height = params->height;
	normal_params.depth  = params->depth;
	normal_params.aspect_ratio = (double)params->aspect_x / (double)params->aspect_y;
	normal_params.title  = title;
	normal_params.keyboard_handler = xmame_keyboard_register_event;
	
	if (params->video_attributes & VIDEO_TYPE_VECTOR)
		normal_params.vec_bounds = &(Machine->visible_area);
	else
		normal_params.vec_bounds = NULL;
	
	if (use_auto_double)
	{
		if ((params->video_attributes & VIDEO_PIXEL_ASPECT_RATIO_MASK)
				== VIDEO_PIXEL_ASPECT_RATIO_1_2)
		{
			if (normal_params.orientation & SYSDEP_DISPLAY_SWAPXY)
				normal_params.widthscale *= 2;
			else
				normal_params.heightscale *= 2;
		}

		if ((params->video_attributes & VIDEO_PIXEL_ASPECT_RATIO_MASK)
				== VIDEO_PIXEL_ASPECT_RATIO_2_1)
		{
			if (normal_params.orientation & SYSDEP_DISPLAY_SWAPXY)
				normal_params.heightscale *= 2;
			else
				normal_params.widthscale *= 2;
		}
	}

	if (sysdep_display_open(&normal_params) != OSD_OK)
		return -1;

	current_params = normal_params;
	vector_register_aux_renderer(sysdep_display_properties.vector_renderer);

	if (!(normal_palette = sysdep_palette_create(&sysdep_display_properties.palette_info, normal_params.depth)))
		return -1;
		
	
	/* a lot of display_targets need to have the display initialised before
	   initialising any input devices */
	if (osd_input_initpost() != OSD_OK)
		return -1;

	/* fill in the resulting RGB components */
	if (rgb_components)
	{
		if (params->depth == 32)
		{
			rgb_components[0] = (0xff << 16) | (0x00 << 8) | 0x00;
			rgb_components[1] = (0x00 << 16) | (0xff << 8) | 0x00;
			rgb_components[2] = (0x00 << 16) | (0x00 << 8) | 0xff;
		}
		else
		{
			rgb_components[0] = 0x7c00;
			rgb_components[1] = 0x03e0;
			rgb_components[2] = 0x001f;
		}
	}

	return 0;
}

void osd_close_display(void)
{
	osd_free_colors();
	sysdep_display_close();

	/* print a final result to the stdout */
	if (frames_displayed != 0)
	{
		cycles_t cps = osd_cycles_per_second();
		fprintf(stderr_file, "Average FPS: %f (%d frames)\n", (double)cps / (end_time - start_time) * frames_displayed, frames_displayed);
	}
}

static void display_settings_changed(void)
{
	xmame_keyboard_clear();
	osd_free_colors();
	vector_register_aux_renderer(sysdep_display_properties.vector_renderer);

	/* we could have switched between hw and sw drawn vectors,
	   so clear Machine->scrbitmap and don't use vector_dirty_pixels
	   for the next (not skipped) frame */
	if (normal_params.vec_bounds)
	{
		schedule_full_refresh();
		ui_dirty = FRAMESKIP_LEVELS + 1;
	}

	sysdep_display_set_keybleds(led_state);
}

static void change_display_settings(struct sysdep_display_open_params *new_params, int force)
{
	if (force || memcmp(new_params, &current_params, sizeof(current_params)))
	{
		sysdep_display_close();
		/* Close sound, my guess is DGA somehow (perhaps fork/exec?) makes
		   the filehandle open twice, so closing it here and re-openeing after
		   the transition should fix that.  Fixed it for me anyways.
		   -- Steve bpk@hoopajoo.net */
		osd_sound_enable( 0 );

		if (sysdep_display_open(new_params))
		{
			sysdep_display_close();
			/* try again with old settings */
			if (force || sysdep_display_open(&current_params))
			{
				sysdep_display_close();
				/* oops this sorta sucks */
				fprintf(stderr_file, "Argh, resizing the display failed in osd_set_visible_area, aborting\n");
				exit(1);
			}
			*new_params = current_params;
		}
		else
			current_params = *new_params;

		display_settings_changed();
		/* Re-enable sound */
		osd_sound_enable( 1 );
	}
}

static void update_visible_area(struct mame_display *display)
{
	int old_width  = normal_params.width;
	int old_height = normal_params.height;
	
	normal_params.width  = (display->game_visible_area.max_x + 1) -
	   display->game_visible_area.min_x;
	normal_params.height = (display->game_visible_area.max_y + 1) -
	   display->game_visible_area.min_y;

	if (((normal_params.width  != old_width) ||
	     (normal_params.height != old_height)) &&
	    !debugger_has_focus)
		change_display_settings(&normal_params, 1);

	set_ui_visarea(display->game_visible_area.min_x,
			display->game_visible_area.min_y,
			display->game_visible_area.max_x,
			display->game_visible_area.max_y);
}

static void update_palette(struct mame_display *display, int force_dirty)
{
	int i, j;

	/* loop over dirty colors in batches of 32 */
	for (i = 0; i < display->game_palette_entries; i += 32)
	{
		UINT32 dirtyflags = display->game_palette_dirty[i / 32];
		if (dirtyflags || force_dirty)
		{
			display->game_palette_dirty[i / 32] = 0;

			/* loop over all 32 bits and update dirty entries */
			for (j = 0; (j < 32) && (i + j < display->game_palette_entries); j++, dirtyflags >>= 1)
				if (((dirtyflags & 1) || force_dirty) && (i + j < display->game_palette_entries))
				{
					/* extract the RGB values */
					rgb_t rgbvalue = display->game_palette[i + j];
					int r = RGB_RED(rgbvalue);
					int g = RGB_GREEN(rgbvalue);
					int b = RGB_BLUE(rgbvalue);

					sysdep_palette_set_pen(normal_palette,
							i + j, r, g, b);
				}
		}
	}
}

static void update_debug_display(struct mame_display *display)
{
	if (!debug_palette)
	{
		int  i, r, g, b;
		debug_palette = sysdep_palette_create(&sysdep_display_properties.palette_info, 16);
		if (!debug_palette)
		{
			/* oops this sorta sucks */
			fprintf(stderr_file, "Argh, creating the palette failed (out of memory?) aborting\n");
			sysdep_display_close();
			exit(1);
		}

		/* Initialize the lookup table for the debug palette. */
		for (i = 0; i < DEBUGGER_TOTAL_COLORS; i++)
		{
			/* extract the RGB values */
			rgb_t rgbvalue = display->debug_palette[i];
			r = RGB_RED(rgbvalue);
			g = RGB_GREEN(rgbvalue);
			b = RGB_BLUE(rgbvalue);

			sysdep_palette_set_pen(debug_palette,
					i, r, g, b);
		}
	}
	if(sysdep_display_update(display->debug_bitmap, &debug_bounds,
	   &debug_bounds, debug_palette, 0))
		display_settings_changed();	
}

static void osd_free_colors(void)
{
	if (normal_palette)
	{
		sysdep_palette_destroy(normal_palette);
		normal_palette = NULL;
	}

	if (debug_palette)
	{
		sysdep_palette_destroy(debug_palette);
		debug_palette = NULL;
	}
}

static int skip_next_frame = 0;

typedef int (*skip_next_frame_func)(void);
static skip_next_frame_func skip_next_frame_functions[FRAMESKIP_DRIVER_COUNT] =
{
	dos_skip_next_frame,
	barath_skip_next_frame
};

typedef int (*show_fps_frame_func)(char *buffer);
static show_fps_frame_func show_fps_frame_functions[FRAMESKIP_DRIVER_COUNT] =
{
	dos_show_fps,
	barath_show_fps
};

int osd_skip_this_frame(void)
{
	return skip_next_frame;
}

int osd_get_frameskip(void)
{
	return autoframeskip ? -(frameskip + 1) : frameskip;
}

void change_debugger_focus(int new_debugger_focus)
{
	if (debugger_has_focus != new_debugger_focus)
	{
		if (new_debugger_focus)
			change_display_settings(&debug_params, 1);
		else
			change_display_settings(&normal_params, 1);
		debugger_has_focus = new_debugger_focus;
	}
}

/* Update the display. */
void osd_update_video_and_audio(struct mame_display *display)
{
	cycles_t curr;
	unsigned int flags = 0;
	int normal_params_changed = 0;
	
	/*** STEP 1: handle frameskip ***/
	if (input_ui_pressed(IPT_UI_FRAMESKIP_INC))
	{
		/* if autoframeskip, disable auto and go to 0 */
		if (autoframeskip)
		{
			autoframeskip = 0;
			frameskip = 0;
		}

		/* wrap from maximum to auto */
		else if (frameskip == FRAMESKIP_LEVELS - 1)
		{
			frameskip = 0;
			autoframeskip = 1;
		}

		/* else just increment */
		else
			frameskip++;

		/* display the FPS counter for 2 seconds */
		ui_show_fps_temp(2.0);

		/* reset the frame counter so we'll measure the average FPS on a consistent status */
		frames_displayed = 0;
	}
	if (input_ui_pressed(IPT_UI_FRAMESKIP_DEC))
	{
		/* if autoframeskip, disable auto and go to max */
		if (autoframeskip)
		{
			autoframeskip = 0;
			frameskip = FRAMESKIP_LEVELS-1;
		}

		/* wrap from 0 to auto */
		else if (frameskip == 0)
			autoframeskip = 1;

		/* else just decrement */
		else
			frameskip--;

		/* display the FPS counter for 2 seconds */
		ui_show_fps_temp(2.0);

		/* reset the frame counter so we'll measure the average FPS on a consistent status */
		frames_displayed = 0;
	}
	if (input_ui_pressed(IPT_UI_THROTTLE))
	{
		if (!code_pressed(KEYCODE_LSHIFT) && !code_pressed(KEYCODE_RSHIFT))
		{
			throttle ^= 1;

			/*
			 * reset the frame counter so we'll measure the average
			 * FPS on a consistent status
			 */
			frames_displayed = 0;
		}
		else
			sleep_idle ^= 1;
	}
	if (code_pressed(KEYCODE_LCONTROL))
	{
		if (code_pressed_memory(KEYCODE_INSERT))
			frameskipper = 0;
		if (code_pressed_memory(KEYCODE_HOME))
			frameskipper = 1;
	}
	skip_next_frame = (*skip_next_frame_functions[frameskipper])();
	
	/*** STEP 2: determine if the debugger or the normal game window
	     should be shown ***/
	if (display->changed_flags & DEBUG_FOCUS_CHANGED)
		change_debugger_focus(display->debug_focus);
	/* If the user presses the F5 key, toggle the debugger's focus */
	else if (input_ui_pressed(IPT_UI_TOGGLE_DEBUG) && mame_debug)
		change_debugger_focus(!debugger_has_focus);

	/*** STEP 3: handle visual area changes, do this before handling any
	     of the display_params hotkeys, because this can *force* changes
	     to the normal display if focussed ***/
	if (display->changed_flags & GAME_VISIBLE_AREA_CHANGED)
		update_visible_area(display);

	/*** STEP 4: now handle display_params hotkeys, but only if the normal
	     display has focus */
	if (!debugger_has_focus && code_pressed(KEYCODE_LALT))
	{
		if (code_pressed_memory(KEYCODE_INSERT))
			flags |= SYSDEP_DISPLAY_HOTKEY_VIDMODE0;
		if (code_pressed_memory(KEYCODE_HOME))
			flags |= SYSDEP_DISPLAY_HOTKEY_VIDMODE1;
		if (code_pressed_memory(KEYCODE_PGUP))
			flags |= SYSDEP_DISPLAY_HOTKEY_VIDMODE2;
		if (code_pressed_memory(KEYCODE_DEL))
			flags |= SYSDEP_DISPLAY_HOTKEY_VIDMODE3;
		if (code_pressed_memory(KEYCODE_END))
			flags |= SYSDEP_DISPLAY_HOTKEY_VIDMODE4;
		if (code_pressed_memory(KEYCODE_PGDN))
		{
			normal_params.fullscreen = 1 - normal_params.fullscreen;
			normal_params_changed = 1;
		}
	}
	if (!debugger_has_focus && code_pressed(KEYCODE_LCONTROL))
	{
		if (code_pressed_memory(KEYCODE_PGUP))
			flags |= SYSDEP_DISPLAY_HOTKEY_GRABKEYB;
		if (code_pressed_memory(KEYCODE_PGDN))
			flags |= SYSDEP_DISPLAY_HOTKEY_GRABMOUSE;
		if (code_pressed_memory(KEYCODE_DEL))
			flags |= SYSDEP_DISPLAY_HOTKEY_OPTION1;
		if (code_pressed_memory(KEYCODE_END))
			flags |= SYSDEP_DISPLAY_HOTKEY_OPTION2;
	}
	if (!debugger_has_focus && !normal_params.effect && 
	     code_pressed(KEYCODE_LSHIFT))
	{
		int widthscale_mod  = 0;
		int heightscale_mod = 0;

		if (code_pressed_memory(KEYCODE_INSERT))
			widthscale_mod = 1;
		if (code_pressed_memory(KEYCODE_DEL))
			widthscale_mod = -1;
		if (code_pressed_memory(KEYCODE_HOME))
			heightscale_mod = 1;
		if (code_pressed_memory(KEYCODE_END))
			heightscale_mod = -1;
		if (code_pressed_memory(KEYCODE_PGUP))
		{
			widthscale_mod  = 1;
			heightscale_mod = 1;
		}
		if (code_pressed_memory(KEYCODE_PGDN))
		{
			widthscale_mod  = -1;
			heightscale_mod = -1;
		}
		if (widthscale_mod || heightscale_mod)
		{
			normal_params.widthscale  += widthscale_mod;
			normal_params.heightscale += heightscale_mod;

			if (normal_params.widthscale > 8)
				normal_params.widthscale = 8;
			else if (normal_params.widthscale < 1)
				normal_params.widthscale = 1;

			if (normal_params.heightscale > 8)
				normal_params.heightscale = 8;
			else if (normal_params.heightscale < 1)
				normal_params.heightscale = 1;
				
			normal_params_changed = 1;
		}
	}
	if (normal_params_changed)
		change_display_settings(&normal_params, 0);

	/*** STEP 5 update sound,fps and leds ***/
	if (sound_stream)
	{
		sound_stream_update(sound_stream);
	}
	if (display->changed_flags & GAME_REFRESH_RATE_CHANGED)
	{
		video_fps = display->game_refresh_rate;
		sound_update_refresh_rate(display->game_refresh_rate);
	}
	if (display->changed_flags & LED_STATE_CHANGED)
	{
		led_state = display->led_state;
		sysdep_display_set_keybleds(led_state);
	}
	
	/*** STEP 6 update the palette ***/
	if (!normal_palette)
	{
		/* the palette had been destroyed because of display changes */
		normal_palette = sysdep_palette_create(&sysdep_display_properties.palette_info, normal_params.depth);
		if (!normal_palette)
		{
			/* oops this sorta sucks */
			fprintf(stderr_file, "Argh, creating the palette failed (out of memory?) aborting\n");
			sysdep_display_close();
			exit(1);
		}
		update_palette(display, 1);
	}
	else if ((display->changed_flags & GAME_PALETTE_CHANGED))
		update_palette(display, 0);

	/*** STEP 7: update the focussed display ***/
	if (debugger_has_focus)
	{
		if (display->changed_flags & DEBUG_BITMAP_CHANGED)
			update_debug_display(display);
	}
	else if (display->changed_flags & GAME_BITMAP_CHANGED)
	{
		/* determine non hotkey flags */
		if (ui_dirty)
			flags |= SYSDEP_DISPLAY_UI_DIRTY;
		
		/* at the end, we need the current time */
		curr = osd_cycles();

		/* update stats for the FPS average calculation */
		if (start_time == 0)
		{
			/* start the timer going 1 second into the game */
			if (timer_get_time() > 1.0)
				start_time = curr;
		}
		else
		{
			frames_displayed++;
			if (frames_displayed + 1 == frames_to_display)
			{
				char name[20];
				mame_file *fp;

				/* make a filename with an underscore prefix */
				sprintf(name, "_%.8s", Machine->gamedrv->name);

				/* write out the screenshot */
				if ((fp = mame_fopen(Machine->gamedrv->name, name, FILETYPE_SCREENSHOT, 1)) != NULL)
				{
					save_screen_snapshot_as(fp, artwork_get_ui_bitmap());
					mame_fclose(fp);
				}
				trying_to_quit = 1;
			}
			end_time = curr;
		}
		
		profiler_mark(PROFILER_BLIT);
		/* udpate and check if the display properties were changed */
		if(sysdep_display_update(display->game_bitmap,
		   &(display->game_visible_area),
		   &(display->game_bitmap_update),
		   normal_palette, flags))
			display_settings_changed();
		profiler_mark(PROFILER_END);
	}

	/* this needs to be called every frame, so do this here */
	osd_poll_joysticks();
}

#ifndef xgl
struct mame_bitmap *osd_override_snapshot(struct mame_bitmap *bitmap,
		struct rectangle *bounds)
{
	struct rectangle newbounds;
	struct mame_bitmap *copy;
	int x, y, w, h, t;

	/* if we can send it in raw, no need to override anything */
	if (!(normal_params.orientation & SYSDEP_DISPLAY_SWAPXY) && !(normal_params.orientation & SYSDEP_DISPLAY_FLIPX) && !(normal_params.orientation & SYSDEP_DISPLAY_FLIPY))
		return NULL;

	/* allocate a copy */
	w = (normal_params.orientation & SYSDEP_DISPLAY_SWAPXY) ? bitmap->height : bitmap->width;
	h = (normal_params.orientation & SYSDEP_DISPLAY_SWAPXY) ? bitmap->width : bitmap->height;
	copy = bitmap_alloc_depth(w, h, bitmap->depth);
	if (!copy)
		return NULL;

	/* populate the copy */
	for (y = bounds->min_y; y <= bounds->max_y; y++)
		for (x = bounds->min_x; x <= bounds->max_x; x++)
		{
			int tx = x, ty = y;

			/* apply the rotation/flipping */
			if ((normal_params.orientation & SYSDEP_DISPLAY_SWAPXY))
			{
				t = tx; tx = ty; ty = t;
			}
			if ((normal_params.orientation & SYSDEP_DISPLAY_FLIPX))
				tx = copy->width - tx - 1;
			if ((normal_params.orientation & SYSDEP_DISPLAY_FLIPY))
				ty = copy->height - ty - 1;

			/* read the old pixel and copy to the new location */
			switch (copy->depth)
			{
				case 15:
				case 16:
					*((UINT16 *)copy->base + ty * copy->rowpixels + tx) =
						*((UINT16 *)bitmap->base + y * bitmap->rowpixels + x);
					break;

				case 32:
					*((UINT32 *)copy->base + ty * copy->rowpixels + tx) =
						*((UINT32 *)bitmap->base + y * bitmap->rowpixels + x);
					break;
			}
		}

	/* compute the oriented bounds */
	newbounds = *bounds;

	/* apply X/Y swap first */
	if (normal_params.orientation & SYSDEP_DISPLAY_SWAPXY)
	{
		t = newbounds.min_x; newbounds.min_x = newbounds.min_y; newbounds.min_y = t;
		t = newbounds.max_x; newbounds.max_x = newbounds.max_y; newbounds.max_y = t;
	}

	/* apply X flip */
	if (normal_params.orientation & SYSDEP_DISPLAY_FLIPX)
	{
		t = copy->width - newbounds.min_x - 1;
		newbounds.min_x = copy->width - newbounds.max_x - 1;
		newbounds.max_x = t;
	}

	/* apply Y flip */
	if (normal_params.orientation & SYSDEP_DISPLAY_FLIPY)
	{
		t = copy->height - newbounds.min_y - 1;
		newbounds.min_y = copy->height - newbounds.max_y - 1;
		newbounds.max_y = t;
	}

	*bounds = newbounds;
	return copy;
}
#endif

void osd_pause(int paused)
{
}

const char *osd_get_fps_text(const struct performance_info *performance)
{
	static char buffer[1024];
	char *dest = buffer;

	int chars_filled
		= (*show_fps_frame_functions[frameskipper])(dest);

	if (chars_filled)
		dest += chars_filled;
	else
	{
		/* display the FPS, frameskip, percent, fps and target fps */
		dest += sprintf(dest, "%s%s%s%2d%4d%%%4d/%d fps",
				throttle ? "T " : "",
				(throttle && sleep_idle) ? "S " : "",
				autoframeskip ? "auto" : "fskp", frameskip,
				(int)(performance->game_speed_percent + 0.5),
				(int)(performance->frames_per_second + 0.5),
				(int)(Machine->refresh_rate + 0.5));
	}

	/* for vector games, add the number of vector updates */
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		dest += sprintf(dest, "\n %d vector updates", performance->vector_updates_last_second);
	}
	else if (performance->partial_updates_this_frame > 1)
	{
		dest += sprintf(dest, "\n %d partial updates", performance->partial_updates_this_frame);
	}

	/* return a pointer to the static buffer */
	return buffer;
}

/*
 * We don't want to sleep when idle while the setup menu is
 * active, since this causes problems with registering
 * keypresses.
 */
int should_sleep_idle()
{
	return sleep_idle && !setup_active();
}
