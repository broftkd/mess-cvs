#include <math.h>
#include "sysdep/sysdep_display_priv.h"
#include "mode.h"

static int disabled_modes_count = 0;
static int perfect_aspect = 0;
static int use_aspect_ratio = 1;
static float display_aspect_ratio = 4.0 / 3.0;
static double display_resolution_aspect_ratio = 4.0 / 3.0;

static int mode_disable(struct rc_option *option, const char *s, int priority);
static int mode_force(struct rc_option *option, const char *s, int priority);

struct rc_option aspect_opts[] = {
   /* name, shortname, type, dest, deflt, min, max, func, help */
   { "Aspect ratio handling", NULL,	rc_seperator,	NULL,
     NULL,		0,			0,		NULL,
     NULL },
   { "keepaspect",	"ka",			rc_bool,	&use_aspect_ratio,
     "1",		0,			0,		NULL,
     "Try to keep the correct aspect ratio" },
   { "perfectaspect",   "pa",                   rc_bool,        &perfect_aspect,
     "0",		0,			0,		NULL,
     "Automatically set yarbsize to get the perfect aspect ratio" },
   { "displayaspectratio", "dar",		rc_float,	&display_aspect_ratio,
     NULL,		0.5,			2.0,		NULL,
     "Set the display aspect ratio of your monitor. This is used for -keepaspect. The default = 1.3333333333333 (4/3). Use 0.75 (3/4) for a portrait monitor" },
   { NULL,		NULL,			rc_end,		NULL,
     NULL,		0,			0,		NULL,
     NULL }
};

struct rc_option mode_opts[] = {
   /* name, shortname, type, dest, deflt, min, max, func, help */
   { "Video Mode Selection Related", NULL,	rc_seperator,	NULL,
     NULL,		0,			0,		NULL,
     NULL },
   { "disablemode",	"dm",			rc_use_function, NULL,
     NULL,		0,			0,		mode_disable,
     "Don't use mode XRESxYRESxDEPTH this can be used to disable specific video modes which don't work on your system. The xDEPTH part of the string is optional and can be set to 15,16,24 and 32. This option may be used more then once" },
   { "forcemode",	"fm",			rc_use_function, NULL,
     NULL,		0,			0,		mode_force,
     "Force use of mode XRESxYRESxDEPTH The DEPTH can be to 15,16,24 and 32." },
   { NULL,		NULL,			rc_end,		NULL,
     NULL,		0,			0,		NULL,
     NULL }
};

#define MODE_DISABLED_MAX 32

struct mode
{
   int width;
   int height;
   int depth;
};

static struct mode disabled_modes[MODE_DISABLED_MAX];
static struct mode forced_mode;

static int mode_disable(struct rc_option *option, const char *s, int priority)
{
   if (disabled_modes_count == MODE_DISABLED_MAX)
   {
      fprintf(stderr, "OSD: Warning: You can't disable more then %d modes. Mode %s not disabled\n",
          MODE_DISABLED_MAX, s);
      return 0;
   }
   if (sscanf(s, "%dx%dx%d",
       &disabled_modes[disabled_modes_count].width,
       &disabled_modes[disabled_modes_count].height,
       &disabled_modes[disabled_modes_count].depth) < 2)
   {
      fprintf(stderr, "Error: %s is not a valid mode\n", s);
      return 1;
   }
   switch (disabled_modes[disabled_modes_count].depth)
   {
      case 0:
      case 15:
      case 16:
      case 24:
      case 32:
         break;
      default:
         fprintf(stderr, "Error no such depth: %d.\n",
            disabled_modes[disabled_modes_count].depth);
         return 1;
   }
   disabled_modes_count++;
   return 0;
}

static int mode_force(struct rc_option *option, const char *s, int priority)
{
   if (sscanf(s, "%dx%dx%d",
       &forced_mode.width,
       &forced_mode.height,
       &forced_mode.depth) != 3)
   {
      fprintf(stderr, "Error: %s is not a valid mode\n", s);
      return 1;
   }
   option->priority = priority;
   return 0;
}

void mode_set_aspect_ratio(double _display_resolution_aspect_ratio)
{
  display_resolution_aspect_ratio = _display_resolution_aspect_ratio;

  if(!use_aspect_ratio || !perfect_aspect || sysdep_display_params.effect)
    return;
    
  sysdep_display_params.yarbsize =
    (sysdep_display_params.width * sysdep_display_params.widthscale) / 
    (sysdep_display_params.aspect_ratio *
      (display_resolution_aspect_ratio/display_aspect_ratio));
  /* Don't do scanlines with yarbsize! */
  sysdep_display_params.scanlines = 0;
}

static int mode_disabled(int width, int height, int depth)
{
   int i;
   for(i=0; i<disabled_modes_count; i++)
   {
      if ( disabled_modes[i].width  == width &&
           disabled_modes[i].height == height &&
          (disabled_modes[i].depth  == 0 ||
           disabled_modes[i].depth  == depth) )
         return 1;
   }
   return 0;
}

/* match a given mode to the needed width, height and aspect ratio to
   perfectly display a game. This function returns 0 for a not usable mode
   and 100 for the perfect mode +10 for a mode with a well matched depth and
   +20 for a mode with the perfect depth (=120 for the really perfect mode).
   
   Params:
   depth	not really the standard definition of depth, but, the depth
                for all modes, except for depth 24 32bpp sparse where 32 should be
                passed. This is done to differentiate depth 24 24bpp
                packed pixel and depth 24 32bpp sparse.
*/
int mode_match(int width, int height, int depth, int dga)
{
  int score, viswidth, visheight;
  double perfect_width, perfect_height, perfect_aspect = 0.0;
  static int first_time = 1;
  double aspect = (double)width/height;
  
  /* is this mode disabled? */
  if(mode_disabled(width, height, depth))
     return 0;
  
  /* if using direct framebuffer access, make sure the width is properly
     aligned */
  if(dga && (width & 3))
     return 0;

  /* get the width and height after scaling */
  viswidth = sysdep_display_params.width * sysdep_display_params.widthscale;
  if(!use_aspect_ratio || !perfect_aspect || sysdep_display_params.effect)
  {
    visheight = sysdep_display_params.yarbsize? sysdep_display_params.yarbsize:
      sysdep_display_params.height*sysdep_display_params.heightscale;
  }
  else
  {
    visheight = viswidth / (sysdep_display_params.aspect_ratio *
      (aspect/display_aspect_ratio));
  }
  
  /* does the game fit at all ? */
  if(width  < (dga?
      ((sysdep_display_params.width+3)&~3)*sysdep_display_params.widthscale:
      viswidth) ||
     height < visheight)
    return 0;
    
  /* is this mode forced? */
  if ((width  == forced_mode.width) &&
      (height == forced_mode.height) &&
      (depth  == forced_mode.depth))
    return 200;
   
  if (use_aspect_ratio && (sysdep_display_params.aspect_ratio != 0.0))
  {
    /* first of all calculate the pixel aspect_ratio the game has */
    double pixel_aspect_ratio = viswidth / 
      (visheight * sysdep_display_params.aspect_ratio);

    perfect_width  = display_aspect_ratio * pixel_aspect_ratio * visheight;
    perfect_height = visheight;
         
    if (perfect_width < viswidth)
    {
      perfect_height *= viswidth / perfect_width;
      perfect_width   = viswidth;
    }

    if (first_time)
    {
      fprintf(stderr, "OSD: Info: Ideal mode for this game = %.0fx%.0f\n",
         perfect_width, perfect_height);
      first_time = 0;
    }
    perfect_aspect = perfect_width/perfect_height;
  }
  else
  {
    perfect_width  = viswidth;
    perfect_height = visheight;
    perfect_aspect = aspect;
  }

  score = 100 *
    (perfect_width  / (fabs(width -perfect_width )+perfect_width )) *
    (perfect_height / (fabs(height-perfect_height)+perfect_height)) *
    (perfect_aspect / (fabs(aspect-perfect_aspect)+perfect_aspect));
  
  switch (sysdep_display_params.depth)
  {
    case 15:
      switch(depth)
      {
        case 15:
          return score + 15;
        case 16:
          return score + 10;
        case 24:
          return score;
        case 32:
          return score + 5;
      }
      break;
    case 16:
      switch(depth)
      {
        case 15:
          return score + 10;
        case 16:
          return score + 15; /* is this really the best for 16 bpp palettised ? */
        case 24:
          return score;
        case 32:
          return score + 5;
      }
      break;
    case 32:
      switch(depth)
      {
        case 15:
          return score;
        case 16:
          return score + 5;
        case 24:
          return score + 10;
        case 32:
          return score + 15;
      }
      break;
  }
  return 0;
}

/* calculate a virtual screen contained within the given dimensions
   which will give the game the correct aspect ratio */
void mode_clip_aspect(unsigned int width, unsigned int height, 
		unsigned int *corr_width, unsigned int *corr_height)
{
  double ch, cw;

  ch = height;
  
  if (use_aspect_ratio && (sysdep_display_params.aspect_ratio != 0.0))
  {
    cw = height * sysdep_display_params.aspect_ratio *
      (display_resolution_aspect_ratio/display_aspect_ratio);
    if ((int)(cw + 0.5) > width )
    {
      ch *= width / cw;
      cw  = width;
    }
  }
  else
    cw = width;
    
  *corr_width  = cw + 0.5;
  *corr_height = ch + 0.5;
}

/* calculate a screen with at least the given dimensions
   which will give the game the correct aspect ratio */
void mode_stretch_aspect(unsigned int width, unsigned int height, 
		unsigned int *corr_width, unsigned int *corr_height)
{
  double ch, cw;

  ch = height;
  
  if (use_aspect_ratio && (sysdep_display_params.aspect_ratio != 0.0))
  {
    cw = height * sysdep_display_params.aspect_ratio *
      (display_resolution_aspect_ratio/display_aspect_ratio);
    if ((int)(cw+0.5) < width)
    {
      ch *= width / cw;
      cw  = width;
    }
  }
  else
    cw = width;
  
  *corr_width  = cw + 0.5;
  *corr_height = ch + 0.5;
}
