/*****************************************************************

  Glide vector routines

  Copyright 1998 by Mike Oliphant - oliphant@ling.ed.ac.uk
  Copyright 2004 Hans de Goede - j.w.r.degoede@hhs.nl

    http://www.ling.ed.ac.uk/~oliphant/glmame

  This code may be used and distributed under the terms of the
  Mame license

*****************************************************************/

#include <math.h>
#include "fxcompat.h"
#include "sysdep/sysdep_display_priv.h"

/* defined in svgafx.c or x11.c */
extern int window_height;

/* from fxgen.c */
extern int vscrntlx;
extern int vscrntly;
extern int vscrnwidth;
extern int vscrnheight;
extern int vecvscrntlx;
extern int vecvscrntly;
extern int vecvscrnwidth;
extern int vecvscrnheight;

/* from mame's vidhrdw/vector.h */
#define VCLEAN  0
#define VDIRTY  1
#define VCLIP   2

static int vecwidth, vecheight;

/* Convert an xy point to xyz in the 3D scene */
static void PointConvert(int x,int y,float *sx,float *sy)
{
  float dx,dy,tmp;

  dx=(float)((x+0x8000)>>16)/vecwidth;
  dy=(float)((y+0x8000)>>16)/vecheight;
  
  if (sysdep_display_params.orientation & SYSDEP_DISPLAY_SWAPXY)
  {
    tmp = dx;
    dx = dy;
    dy = tmp;
  }

  if (sysdep_display_params.orientation & SYSDEP_DISPLAY_FLIPX)
    dx = 1.0 - dx;

  if (sysdep_display_params.orientation & SYSDEP_DISPLAY_FLIPY)
    dy = 1.0 - dy;
    
  *sx=vecvscrntlx + dx*vecvscrnwidth;
  *sy=window_height - (vecvscrntly + dy*vecvscrnheight);
}

/*
 * Adds a line end point to the vertices list. The vector processor emulation
 * needs to call this.
 */

static void fxvec_fill_vert(GrVertex *vert, int x, int y, rgb_t color, int intensity)
{
	vert->oow=1.0;

	vert->r=(float)((color>>16)&0x000000ff);
	vert->g=(float)((color>>8)&0x000000ff);
	vert->b=(float)(color&0x000000ff);
	vert->a=(float)intensity;

	PointConvert(x,y,&(vert->x),&(vert->y));
}

int fxvec_renderer(point *pt, int num_points)
{
  if (num_points)
  {
    GrVertex v1,v2;
    int vertexcount = 0;
    
    vecwidth  = (sysdep_display_params.vec_bounds->max_x + 1) -
      sysdep_display_params.vec_bounds->min_x;
    vecheight = (sysdep_display_params.vec_bounds->max_y + 1) -
      sysdep_display_params.vec_bounds->min_y;

    grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                                   GR_COMBINE_FACTOR_NONE,
                                   GR_COMBINE_LOCAL_ITERATED,
                                   GR_COMBINE_OTHER_NONE,
                                   FXFALSE);

    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                                   GR_COMBINE_FACTOR_LOCAL,
                                   GR_COMBINE_LOCAL_ITERATED,
                                   GR_COMBINE_OTHER_NONE,
                                   FXFALSE);

    grClipWindow(vecvscrntlx, window_height - (vecvscrntly + vecvscrnheight),
      vecvscrntlx + vecvscrnwidth, window_height - vecvscrntly);
      
    grEnableAA();

    while(num_points)
    {
      if (pt->status == VCLIP)
      {
        float xmin, ymin, xmax, ymax, tmp;
        PointConvert(pt->x, pt->x, &xmin, &ymin);
        PointConvert(pt->arg1, pt->arg2, &xmax, &ymax);
        /* this can be caused by blit_flip* */
        if (xmin > xmax)
        {
          tmp = xmin;
          xmin = xmax;
          xmax = tmp;
        }
        if (ymin > ymax)
        {
          tmp = ymin;
          ymin = ymax;
          ymax = tmp;
        }
        grClipWindow(xmin, ymin, xmax, ymax);
      }
      else
      {
        vertexcount++;
        
        if (pt->callback)
          fxvec_fill_vert(&v2, pt->x, pt->y, pt->callback(), pt->intensity);
        else
          fxvec_fill_vert(&v2, pt->x, pt->y, pt->col, pt->intensity);
          
	if((vertexcount >= 2) && (v2.a>0.0)) {
		if((fabs(v1.x-v2.x)<1.0) && (fabs(v1.y-v2.y)<1.0))
		{
		  grAADrawPoint(&v2);
                }
		else {
		  v1.r=v2.r; v1.g=v2.g; v1.b=v2.b; v1.a=v2.a;
		  grAADrawLine(&v1,&v2);
		}
	}
        v1 = v2;
      }
      pt++;
      num_points--;
    }
    grDisableAA();

    grClipWindow(vscrntlx, vscrntly, vscrntlx + vscrnwidth,
      vscrntly + vscrnheight);

    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                                       GR_COMBINE_FACTOR_ONE,
                                       GR_COMBINE_LOCAL_NONE,
                                       GR_COMBINE_OTHER_TEXTURE,
                                       FXFALSE);

    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                                       GR_COMBINE_FACTOR_LOCAL,
                                       GR_COMBINE_LOCAL_CONSTANT,
                                       GR_COMBINE_OTHER_NONE,
                                       FXFALSE);
  }
  return 0;
}
