/***************************************************************** 

  Generic OpenGL routines

  Copyright 1998 by Mike Oliphant - oliphant@ling.ed.ac.uk

    http://www.ling.ed.ac.uk/~oliphant/glmame

  Improved by Sven Goethel, http://www.jausoft.com, sgoethel@jausoft.com

  This code may be used and distributed under the terms of the
  Mame license

*****************************************************************/

#ifdef xgl

#include "xmame.h"
#include "driver.h"
#include "usrintrf.h"
#include "glmame.h"
#include <math.h>
#include "effect.h"
#include <GL/glext.h>

/* private functions */
static GLdouble CompareVec (GLdouble i, GLdouble j, GLdouble k,
	    GLdouble x, GLdouble y, GLdouble z);
static void TranslatePointInPlane   (
	      GLdouble vx_p1, GLdouble vy_p1, GLdouble vz_p1,
	      GLdouble vx_nw, GLdouble vy_nw, GLdouble vz_nw,
	      GLdouble vx_nh, GLdouble vy_nh, GLdouble vz_nh,
	      GLdouble x_off, GLdouble y_off,
	      GLdouble *vx_p, GLdouble *vy_p, GLdouble *vz_p );
static void ScaleThisVec (GLdouble i, GLdouble j, GLdouble k,
	      GLdouble * x, GLdouble * y, GLdouble * z);
static void AddToThisVec (GLdouble i, GLdouble j, GLdouble k,
	      GLdouble * x, GLdouble * y, GLdouble * z);
static GLdouble LengthOfVec (GLdouble x, GLdouble y, GLdouble z);
static void NormThisVec (GLdouble * x, GLdouble * y, GLdouble * z);
static void DeltaVec (GLdouble x1, GLdouble y1, GLdouble z1,
	       GLdouble x2, GLdouble y2, GLdouble z2,
	       GLdouble * dx, GLdouble * dy, GLdouble * dz);
static void CrossVec (GLdouble a1, GLdouble a2, GLdouble a3,
	  GLdouble b1, GLdouble b2, GLdouble b3,
	  GLdouble * c1, GLdouble * c2, GLdouble * c3);
static void CopyVec(GLdouble *ax,GLdouble *ay,GLdouble *az,            /* dest   */
	     const GLdouble bx,const GLdouble by,const GLdouble bz     /* source */
                  );
static void CalcFlatTexPoint( int x, int y, GLdouble texwpervw, GLdouble texhpervh, 
		       GLdouble * px, GLdouble * py);
static void SetupFrustum (void);
static void SetupOrtho (void);

static void WAvg (GLdouble perc, GLdouble x1, GLdouble y1, GLdouble z1,
	   GLdouble x2, GLdouble y2, GLdouble z2,
	   GLdouble * ax, GLdouble * ay, GLdouble * az);
static void UpdateCabDisplay (struct mame_bitmap *bitmap);
static void UpdateFlatDisplay (struct mame_bitmap *bitmap);
static void UpdateGLDisplay (struct mame_bitmap *bitmap);

/* cabloading stuff, FIXME */
static int cabspecified;
/* int cabview=0;  .. Do we want a cabinet view or not? */
static int cabload_err;

static GLint   gl_internal_format;
static GLenum  gl_bitmap_format;
static GLenum  gl_bitmap_type;
static GLsizei text_width;
static GLsizei text_height;


static GLdouble mxModel[16];
static GLdouble mxProjection[16];
static GLint    dimView[4];

/* The squares that are tiled to make up the game screen polygon */

struct TexSquare
{
  GLubyte *texture;
  GLuint texobj;
  int isTexture;
  GLdouble x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4;
  GLdouble fx1, fy1, fx2, fy2, fx3, fy3, fx4, fy4;
  GLdouble xcov, ycov;
};

static struct TexSquare *texgrid;

static int texnumx;
static int texnumy;

/**
 * cscr..: cabinet screen points:
 * 
 * are defined within <model>.cab in the following order:
 *	1.) left  - top
 *	2.) right - top
 *	3.) right - bottom
 *	4.) left  - bottom
 *
 * are read in reversed:
 *	1.) left  - bottom
 *	2.) right - bottom
 *	3.) right - top
 *	4.) left  - top
 *
 * so we do have a positiv (Q I) coord system ;-), of course:
 *	left   < right
 *	bottom < top
 */
GLdouble vx_cscr_p1, vy_cscr_p1, vz_cscr_p1, 
        vx_cscr_p2, vy_cscr_p2, vz_cscr_p2,
        vx_cscr_p3, vy_cscr_p3, vz_cscr_p3, 
	vx_cscr_p4, vy_cscr_p4, vz_cscr_p4;

/**
 * cscr..: cabinet screen dimension vectors
 *	
 * 	these are the cabinet-screen's width/height in the cabinet's world-coord !!
 */
static GLdouble  vx_cscr_dw, vy_cscr_dw, vz_cscr_dw; /* the width (world-coord) , p1-p2 */
static GLdouble  vx_cscr_dh, vy_cscr_dh, vz_cscr_dh; /* the height (world-coord), p1-p4 */

static GLdouble  s__cscr_w,  s__cscr_h;              /* scalar width/height (world-coord) */
GLdouble  s__cscr_w_view, s__cscr_h_view;     /* scalar width/height (view-coord) */

/* screen x-axis (world-coord), normalized v__cscr_dw */
static GLdouble vx_scr_nx, vy_scr_nx, vz_scr_nx; 

/* screen y-axis (world-coord), normalized v__cscr_dh */
static GLdouble vx_scr_ny, vy_scr_ny, vz_scr_ny; 

/* screen z-axis (world-coord), the normalized cross product of v__cscr_dw,v__cscr_dh */
static GLdouble vx_scr_nz, vy_scr_nz, vz_scr_nz; 

/* x/y-factor for view/world coord translation */
static GLdouble cab_vpw_fx; /* s__cscr_w_view / s__cscr_w */
static GLdouble cab_vpw_fy; /* s__cscr_h_view / s__cscr_h */

/**
 * gscr..: game screen dimension vectors
 *	
 * 	these are the game portion of the cabinet-screen's width/height 
 *      in the cabinet's world-coord !!
 *
 *	gscr[wh]d[xyz] <= cscr[wh]d[xyz]
 */

static GLdouble vx_gscr_dw, vy_gscr_dw, vz_gscr_dw; /* the width (world-coord) */
static GLdouble vx_gscr_dh, vy_gscr_dh, vz_gscr_dh; /* the height (world-coord) */

static GLdouble  s__gscr_w, s__gscr_h;              /* scalar width/height (world-coord) */
static GLdouble  s__gscr_w_view, s__gscr_h_view;    /* scalar width/height (view-coord) */

static GLdouble  s__gscr_offx, s__gscr_offy;          /* delta game start (world-coord) */

static GLdouble  s__gscr_offx_view, s__gscr_offy_view;/* delta game-start (view-coord) */

/**
*
* ALL GAME SCREEN VECTORS ARE IN FINAL ORIENTATION (e.g. if blit_swapxy)
*
* v__gscr_p1 = v__cscr_p1   + 
*              s__gscr_offx * v__scr_nx + s__gscr_offy * v__scr_ny ;
*
* v__gscr_p2  = v__gscr_p1  + 
*              s__gscr_w    * v__scr_nx + 0.0          * v__scr_ny ;
*
* v__gscr_p3  = v__gscr_p2  + 
*              0.0          * v__scr_nx + s__gscr_h    * v__scr_ny ;
*
* v__gscr_p4  = v__gscr_p3  - 
*              s__gscr_w    * v__scr_nx - 0.0          * v__scr_ny ;
*
* v__gscr_p4b = v__gscr_p1  + 
*              0.0          * v__scr_nx + s__gscr_h    * v__scr_ny ;
*
* v__gscr_p4a == v__gscr_p4b
*/
static GLdouble vx_gscr_p1, vy_gscr_p1, vz_gscr_p1; 
static GLdouble vx_gscr_p2, vy_gscr_p2, vz_gscr_p2; 
static GLdouble vx_gscr_p3, vy_gscr_p3, vz_gscr_p3; 
static GLdouble vx_gscr_p4, vy_gscr_p4, vz_gscr_p4; 

/* Camera panning variables */
static int currentpan = 0;
static int lastpan = 0;
static int panframe = 0;

/* Misc variables */
int gl_is_initialized;
static int vecgame = 0;
static int do_snapshot;
static unsigned short *colorBlittedMemory;
static int line_len=0;


void gl_bootstrap_resources()
{
  #ifndef NDEBUG
    printf("GLINFO: gl_bootstrap_resources\n");
  #endif

  blit_hardware_rotation=1; /* no rotation by the blitter macro, or else !! */

  cabspecified = 0;
  if(cabname!=NULL) cabname[0]=0;

  gl_bitmap_type=0;
  gl_bitmap_format=0;
  text_width=0;
  text_height=0;
  force_text_width_height = 0;
  cabload_err=0;

  texnumx=0;
  texnumy=0;
  
  colorBlittedMemory=NULL;
  
  vx_cscr_p1=0; vy_cscr_p1=0; vz_cscr_p1=0; vx_cscr_p2=0; vy_cscr_p2=0; vz_cscr_p2=0;
  vx_cscr_p3=0; vy_cscr_p3=0; vz_cscr_p3=0; vx_cscr_p4=0; vy_cscr_p4=0; vz_cscr_p4=0;
  
  cpan=0;
  numpans=0;
  currentpan = 0;
  lastpan = 0;
  panframe = 0;
  
  vecgame = 0;
  veclist=0;

  gl_is_initialized = 0;

  do_snapshot=0;
}

void gl_reset_resources()
{
  #ifndef NDEBUG
    printf("GLINFO: gl_reset_resources\n");
  #endif

  if(texgrid) free(texgrid);
  if(cpan) free(cpan);

  texgrid = 0;
  cpan=0;

  gl_bitmap_type=0;
  gl_bitmap_format=0;
  cabload_err=0;

  texnumx=0;
  texnumy=0;
  
  colorBlittedMemory=NULL;
  
  vx_cscr_p1=0; vy_cscr_p1=0; vz_cscr_p1=0; vx_cscr_p2=0; vy_cscr_p2=0; vz_cscr_p2=0;
  vx_cscr_p3=0; vy_cscr_p3=0; vz_cscr_p3=0; vx_cscr_p4=0; vy_cscr_p4=0; vz_cscr_p4=0;
  
  cpan=0;
  numpans=0;
  currentpan = 0;
  lastpan = 0;
  panframe = 0;
  
  vecgame = 0;
  veclist=0;

  if(cabname!=NULL && strlen(cabname)>0)
  	cabspecified = 1;

  gl_is_initialized = 0;
  
  do_snapshot=0;
}

/* ---------------------------------------------------------------------- */
/* ------------ New OpenGL Specials ------------------------------------- */
/* ---------------------------------------------------------------------- */

void gl_set_bilinear (int new_value)
{
  int x, y;
  bilinear = new_value;

  if (gl_is_initialized == 0)
    return;

  if (bilinear)
  {
    if (cabtex)
    {
	    disp__glBindTexture (GL_TEXTURE_2D, cabtex[0]);

	    disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	    disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	    disp__glPixelStorei (GL_UNPACK_ROW_LENGTH, line_len /* ?? */); 
	    disp__glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
	    CHECK_GL_ERROR ();
    }

    for (y = 0; y < texnumy; y++)
    {
      for (x = 0; x < texnumx; x++)
      {
        disp__glBindTexture (GL_TEXTURE_2D, texgrid[y * texnumx + x].texobj);

        disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        disp__glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
        disp__glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        CHECK_GL_ERROR ();
      }
    }
  }
  else
  {
    if (cabtex)
    {
	    disp__glBindTexture (GL_TEXTURE_2D, cabtex[0]);

	    disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	    disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	    disp__glPixelStorei (GL_UNPACK_ROW_LENGTH, line_len /* ?? */);
	    disp__glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
	    CHECK_GL_ERROR ();
    }

    for (y = 0; y < texnumy; y++)
    {
      for (x = 0; x < texnumx; x++)
      {
        disp__glBindTexture (GL_TEXTURE_2D, texgrid[y * texnumx + x].texobj);

        disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        disp__glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
        disp__glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        CHECK_GL_ERROR ();
      }
    }
  }
}

void gl_init_cabview ()
{
  if (glContext == 0)
    return;

        currentpan=1;
	lastpan = 0;
	panframe = 0;

	#ifdef WIN32
	  __try
	  {
	#endif
	    if (!cabspecified || !LoadCabinet (cabname))
	    {
	      if (cabspecified)
		printf ("GLERROR: Unable to load cabinet %s\n", cabname);
	      strcpy (cabname, "glmamejau");
	      cabspecified = 1;
	      LoadCabinet (cabname);
	      cabload_err = 0;
	    }

	#ifdef WIN32
	  }
	  __except (GetExceptionCode ())
	  {
	    fprintf (stderr, "\nGLERROR: Cabinet loading is still buggy - sorry !\n");
	    cabload_err = 1;
	  }
	#endif
}

void gl_set_cabview (int new_value)
{
  if (glContext == 0)
    return;

  cabview = new_value;

  if (cabview)
  {
        currentpan=1;
	lastpan = 0;
	panframe = 0;

	SetupFrustum ();
  } else {
	SetupOrtho ();
  }
}

int gl_stream_antialias (int aa)
{
  if (gl_is_initialized == 0)
    return aa;

  if (aa)
  {
    disp__glShadeModel (GL_SMOOTH);
    disp__glEnable (GL_POLYGON_SMOOTH);
    disp__glEnable (GL_LINE_SMOOTH);
    disp__glEnable (GL_POINT_SMOOTH);
  }
  else
  {
    disp__glShadeModel (GL_FLAT);
    disp__glDisable (GL_POLYGON_SMOOTH);
    disp__glDisable (GL_LINE_SMOOTH);
    disp__glDisable (GL_POINT_SMOOTH);
  }

  return aa;
}

void gl_set_antialias (int new_value)
{
  antialias = gl_stream_antialias(new_value);
}

int gl_stream_alphablending (int alpha)
{
  if (gl_is_initialized == 0)
    return alpha;

  if (alpha)
  {
    disp__glEnable (GL_BLEND);
    disp__glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  else
  {
    disp__glDisable (GL_BLEND);
  }

  return alpha;
}

void gl_set_alphablending (int new_value)
{
  alphablending = gl_stream_alphablending(new_value);
}

int InitVScreen (int vw, int vh)
{
  const unsigned char * glVersion;
  double game_aspect ;
  double cabn_aspect ;
  GLdouble vx_gscr_p4b, vy_gscr_p4b, vz_gscr_p4b; 
  GLdouble t1;
  int visual_orientated_width;
  int visual_orientated_height;

  gl_reset_resources();

  if (glContext == 0)
    return 1;

  if( ! blit_swapxy )
  {
            visual_orientated_width  = visual_width;
            visual_orientated_height = visual_height;
  } else {
            visual_orientated_width  = visual_height;
            visual_orientated_height = visual_width;
  }

  fetch_GL_FUNCS (libGLName, libGLUName, 0);

  CHECK_GL_BEGINEND();

  glVersion = disp__glGetString(GL_VERSION);

  printf("\nGLINFO: OpenGL Driver Information:\n");
  printf("\tvendor: %s,\n\trenderer %s,\n\tversion %s\n", 
  	disp__glGetString(GL_VENDOR), 
	disp__glGetString(GL_RENDERER),
	glVersion);

  if(!(glVersion[0]>'1' ||
       (glVersion[0]=='1' && glVersion[2]>='2') ) )
  {
       printf("error: an OpenGL >= 1.2 capable driver is required!\n");
       return 1;
  }

  printf("GLINFO: swapxy=%d, flipx=%d, flipy=%d\n",
  	blit_swapxy, blit_flipx, blit_flipy);

  disp__glClearColor (0, 0, 0, 1);
  disp__glClear (GL_COLOR_BUFFER_BIT);
  disp__glFlush ();

  gl_init_cabview ();

  gl_resize(winwidth, winheight, vw, vh);

  disp__glDepthFunc (GL_LEQUAL);

  /* Calulate delta vectors for screen height and width */

  DeltaVec (vx_cscr_p1, vy_cscr_p1, vz_cscr_p1, vx_cscr_p2, vy_cscr_p2, vz_cscr_p2,
	    &vx_cscr_dw, &vy_cscr_dw, &vz_cscr_dw);
  DeltaVec (vx_cscr_p1, vy_cscr_p1, vz_cscr_p1, vx_cscr_p4, vy_cscr_p4, vz_cscr_p4,
	    &vx_cscr_dh, &vy_cscr_dh, &vz_cscr_dh);

  s__cscr_w = LengthOfVec (vx_cscr_dw, vy_cscr_dw, vz_cscr_dw);
  s__cscr_h = LengthOfVec (vx_cscr_dh, vy_cscr_dh, vz_cscr_dh);


	  /*	  
	  ScaleThisVec ( -1.0,  1.0,  1.0, &vx_cscr_dh, &vy_cscr_dh, &vz_cscr_dh);
	  ScaleThisVec ( -1.0,  1.0,  1.0, &vx_cscr_dw, &vy_cscr_dw, &vz_cscr_dw);
	  */

  CopyVec( &vx_scr_nx, &vy_scr_nx, &vz_scr_nx,
	    vx_cscr_dw,  vy_cscr_dw,  vz_cscr_dw);
  NormThisVec (&vx_scr_nx, &vy_scr_nx, &vz_scr_nx);

  CopyVec( &vx_scr_ny, &vy_scr_ny, &vz_scr_ny,
	    vx_cscr_dh,  vy_cscr_dh,  vz_cscr_dh);
  NormThisVec (&vx_scr_ny, &vy_scr_ny, &vz_scr_ny);

  CrossVec (vx_cscr_dw, vy_cscr_dw, vz_cscr_dw,
  	    vx_cscr_dh, vy_cscr_dh, vz_cscr_dh,
	    &vx_scr_nz, &vy_scr_nz, &vz_scr_nz);
  NormThisVec (&vx_scr_nz, &vy_scr_nz, &vz_scr_nz);

#ifndef NDEBUG
  /**
   * assertions ...
   */
  CopyVec( &t1x, &t1y, &t1z,
	   vx_scr_nx, vy_scr_nx, vz_scr_nx);
  ScaleThisVec (s__cscr_w,s__cscr_w,s__cscr_w,
                &t1x, &t1y, &t1z);
  t1 =  CompareVec (t1x, t1y, t1z, vx_cscr_dw,  vy_cscr_dw,  vz_cscr_dw);

  printf("GLINFO: test v__cscr_dw - ( v__scr_nx * s__cscr_w ) = %f\n", t1);
  printf("\t v__cscr_dw = %f / %f / %f\n", vx_cscr_dw,  vy_cscr_dw,  vz_cscr_dw);
  printf("\t v__scr_nx = %f / %f / %f\n", vx_scr_nx, vy_scr_nx, vz_scr_nx);
  printf("\t s__cscr_w  = %f \n", s__cscr_w);

  CopyVec( &t1x, &t1y, &t1z,
	   vx_scr_ny, vy_scr_ny, vz_scr_ny);
  ScaleThisVec (s__cscr_h,s__cscr_h,s__cscr_h,
                &t1x, &t1y, &t1z);
  t1 =  CompareVec (t1x, t1y, t1z, vx_cscr_dh,  vy_cscr_dh,  vz_cscr_dh);

  printf("GLINFO: test v__cscr_dh - ( v__scr_ny * s__cscr_h ) = %f\n", t1);
  printf("\t v__cscr_dh = %f / %f / %f\n", vx_cscr_dh,  vy_cscr_dh,  vz_cscr_dh);
  printf("\t v__scr_ny  = %f / %f / %f\n", vx_scr_ny, vy_scr_ny, vz_scr_ny);
  printf("\t s__cscr_h   = %f \n", s__cscr_h);
#endif

  /**
   *
   * Cabinet-Screen Ratio:
   */
   game_aspect = (double)visual_orientated_width / (double)visual_orientated_height;

   cabn_aspect = (double)s__cscr_w        / (double)s__cscr_h;

   if( game_aspect <= cabn_aspect )
   {
   	/**
	 * cabinet_width  >  game_width
	 * cabinet_height == game_height 
	 *
	 * cabinet_height(view-coord) := visual_orientated_height(view-coord) 
	 */
	s__cscr_h_view  = (GLdouble) visual_orientated_height;
	s__cscr_w_view  = s__cscr_h_view * cabn_aspect;

	s__gscr_h_view  = s__cscr_h_view;
	s__gscr_w_view  = s__gscr_h_view * game_aspect; 

	s__gscr_offy_view = 0.0;
	s__gscr_offx_view = ( s__cscr_w_view - s__gscr_w_view ) / 2.0;

   } else {
   	/**
	 * cabinet_width  <  game_width
	 * cabinet_width  == game_width 
	 *
	 * cabinet_width(view-coord) := visual_orientated_width(view-coord) 
	 */
	s__cscr_w_view  = (GLdouble) visual_orientated_width;
	s__cscr_h_view  = s__cscr_w_view / cabn_aspect;

	s__gscr_w_view  = s__cscr_w_view;
	s__gscr_h_view  = s__gscr_w_view / game_aspect; 

	s__gscr_offx_view = 0.0;
	s__gscr_offy_view = ( s__cscr_h_view - s__gscr_h_view ) / 2.0;

   }
   cab_vpw_fx = (GLdouble)s__cscr_w_view / (GLdouble)s__cscr_w ;
   cab_vpw_fy = (GLdouble)s__cscr_h_view / (GLdouble)s__cscr_h ;

   s__gscr_w   = (GLdouble)s__gscr_w_view  / cab_vpw_fx ;
   s__gscr_h   = (GLdouble)s__gscr_h_view  / cab_vpw_fy ;
   s__gscr_offx = (GLdouble)s__gscr_offx_view / cab_vpw_fx ;
   s__gscr_offy = (GLdouble)s__gscr_offy_view / cab_vpw_fy ;


   /**
    * ALL GAME SCREEN VECTORS ARE IN FINAL ORIENTATION (e.g. if blit_swapxy)
    *
    * v__gscr_p1 = v__cscr_p1   + 
    *              s__gscr_offx * v__scr_nx + s__gscr_offy * v__scr_ny ;
    *
    * v__gscr_p2  = v__gscr_p1  + 
    *              s__gscr_w    * v__scr_nx + 0.0          * v__scr_ny ;
    *
    * v__gscr_p3  = v__gscr_p2  + 
    *              0.0          * v__scr_nx + s__gscr_h    * v__scr_ny ;
    *
    * v__gscr_p4  = v__gscr_p3  - 
    *              s__gscr_w    * v__scr_nx - 0.0          * v__scr_ny ;
    *
    * v__gscr_p4b = v__gscr_p1  + 
    *              0.0          * v__scr_nx + s__gscr_h    * v__scr_ny ;
    *
    * v__gscr_p4  == v__gscr_p4b
    */
   TranslatePointInPlane ( vx_cscr_p1, vy_cscr_p1, vz_cscr_p1,
                           vx_scr_nx, vy_scr_nx, vz_scr_nx,
			   vx_scr_ny, vy_scr_ny, vz_scr_ny,
			   s__gscr_offx, s__gscr_offy,
			   &vx_gscr_p1, &vy_gscr_p1, &vz_gscr_p1);

   TranslatePointInPlane ( vx_gscr_p1, vy_gscr_p1, vz_gscr_p1,
                           vx_scr_nx, vy_scr_nx, vz_scr_nx,
			   vx_scr_ny, vy_scr_ny, vz_scr_ny,
			   s__gscr_w, 0.0,
			   &vx_gscr_p2, &vy_gscr_p2, &vz_gscr_p2);

   TranslatePointInPlane ( vx_gscr_p2, vy_gscr_p2, vz_gscr_p2,
                           vx_scr_nx, vy_scr_nx, vz_scr_nx,
			   vx_scr_ny, vy_scr_ny, vz_scr_ny,
			   0.0, s__gscr_h,
			   &vx_gscr_p3, &vy_gscr_p3, &vz_gscr_p3);

   TranslatePointInPlane ( vx_gscr_p3, vy_gscr_p3, vz_gscr_p3,
                           vx_scr_nx, vy_scr_nx, vz_scr_nx,
			   vx_scr_ny, vy_scr_ny, vz_scr_ny,
			   -s__gscr_w, 0.0,
			   &vx_gscr_p4, &vy_gscr_p4, &vz_gscr_p4);

   TranslatePointInPlane ( vx_gscr_p1, vy_gscr_p1, vz_gscr_p1,
                           vx_scr_nx, vy_scr_nx, vz_scr_nx,
			   vx_scr_ny, vy_scr_ny, vz_scr_ny,
			   0.0, s__gscr_h,
			   &vx_gscr_p4b, &vy_gscr_p4b, &vz_gscr_p4b);

   t1 =  CompareVec (vx_gscr_p4,  vy_gscr_p4,  vz_gscr_p4,
   		     vx_gscr_p4b, vy_gscr_p4b, vz_gscr_p4b);

  DeltaVec (vx_gscr_p1, vy_gscr_p1, vz_gscr_p1, vx_gscr_p2, vy_gscr_p2, vz_gscr_p2,
	    &vx_gscr_dw, &vy_gscr_dw, &vz_gscr_dw);
  DeltaVec (vx_gscr_p1, vy_gscr_p1, vz_gscr_p1, vx_gscr_p4, vy_gscr_p4, vz_gscr_p4,
	    &vx_gscr_dh, &vy_gscr_dh, &vz_gscr_dh);

#ifndef NDEBUG
  printf("GLINFO: test v__cscr_dh - ( v__scr_ny * s__cscr_h ) = %f\n", t1);
  printf("GLINFO: cabinet vectors\n");
  printf("\t cab p1     : %f / %f / %f \n", vx_cscr_p1, vy_cscr_p1, vz_cscr_p1);
  printf("\t cab p2     : %f / %f / %f \n", vx_cscr_p2, vy_cscr_p2, vz_cscr_p2);
  printf("\t cab p3     : %f / %f / %f \n", vx_cscr_p3, vy_cscr_p3, vz_cscr_p3);
  printf("\t cab p4     : %f / %f / %f \n", vx_cscr_p4, vy_cscr_p4, vz_cscr_p4);
  printf("\n") ;
  printf("\t cab width  : %f / %f / %f \n", vx_cscr_dw, vy_cscr_dw, vz_cscr_dw);
  printf("\t cab height : %f / %f / %f \n", vx_cscr_dh, vy_cscr_dh, vz_cscr_dh);
  printf("\n");
  printf("\t x axis : %f / %f / %f \n", vx_scr_nx, vy_scr_nx, vz_scr_nx);
  printf("\t y axis : %f / %f / %f \n", vx_scr_ny, vy_scr_ny, vz_scr_ny);
  printf("\t z axis : %f / %f / %f \n", vx_scr_nz, vy_scr_nz, vz_scr_nz);
  printf("\n");
  printf("\n");
  printf("\t cab wxh scal wd: %f x %f \n", s__cscr_w, s__cscr_h);
  printf("\t cab wxh scal vw: %f x %f \n", s__cscr_w_view, s__cscr_h_view);
  printf("\n");
  printf("\t gam p1     : %f / %f / %f \n", vx_gscr_p1, vy_gscr_p1, vz_gscr_p1);
  printf("\t gam p2     : %f / %f / %f \n", vx_gscr_p2, vy_gscr_p2, vz_gscr_p2);
  printf("\t gam p3     : %f / %f / %f \n", vx_gscr_p3, vy_gscr_p3, vz_gscr_p3);
  printf("\t gam p4     : %f / %f / %f \n", vx_gscr_p4, vy_gscr_p4, vz_gscr_p4);
  printf("\t gam p4b    : %f / %f / %f \n", vx_gscr_p4b, vy_gscr_p4b, vz_gscr_p4b);
  printf("\t gam p4-p4b : %f\n", t1);
  printf("\n");
  printf("\t gam width  : %f / %f / %f \n", vx_gscr_dw, vy_gscr_dw, vz_gscr_dw);
  printf("\t gam height : %f / %f / %f \n", vx_gscr_dh, vy_gscr_dh, vz_gscr_dh);
  printf("\n");
  printf("\t gam wxh scal wd: %f x %f \n", s__gscr_w, s__gscr_h);
  printf("\t gam wxh scal vw: %f x %f \n", s__gscr_w_view, s__gscr_h_view);
  printf("\n");
  printf("\t gam off  wd: %f / %f\n", s__gscr_offx, s__gscr_offy);
  printf("\t gam off  vw: %f / %f\n", s__gscr_offx_view, s__gscr_offy_view);
  printf("\n");
#endif

  if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
  {
    glvec_init();
    vecgame = 1;
  }
  else
    vecgame = 0;

  /* fill the display_palette_info struct */
  memset (&display_palette_info, 0, sizeof (struct sysdep_palette_info));

  /* no alpha .. important, because mame has no alpha set ! */
  gl_internal_format=GL_RGB;

  switch(video_real_depth)
  {
	case 15:
		    /* ARGB1555 */
		    display_palette_info.red_mask   = 0x00007C00;
		    display_palette_info.green_mask = 0x000003E0;
		    display_palette_info.blue_mask  = 0x0000001F;
		    gl_bitmap_format = GL_BGRA;
		    /*                                   A R G B */
		    gl_bitmap_type   = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		    break;
	case 16:
		    /* RGBA5551 */
		    display_palette_info.red_mask   = 0x0000F800;
		    display_palette_info.green_mask = 0x000007C0;
		    display_palette_info.blue_mask  = 0x0000003E;
		    gl_bitmap_format = GL_RGBA;
		    /*                                   R G B A */
		    gl_bitmap_type   = GL_UNSIGNED_SHORT_5_5_5_1;
		    break;
	case 32:
		 /**
		  * skip the D of DRGB 
		  */
		  /* ABGR8888 */
		  display_palette_info.blue_mask   = 0x00FF0000;
		  display_palette_info.green_mask  = 0x0000FF00;
		  display_palette_info.red_mask    = 0x000000FF;
		  gl_bitmap_format = GL_BGRA;
			/*  gl_bitmap_format = GL_RGBA; */
		  /*                                 A B G R */
		  gl_bitmap_type   = GL_UNSIGNED_INT_8_8_8_8_REV;
		  break;
  }

  printf("GLINFO: depth=%d, rgb 0x%X, 0x%X, 0x%X (true color mode)\n",
		video_real_depth, 
		display_palette_info.red_mask, display_palette_info.green_mask, 
		display_palette_info.blue_mask);

  if(video_real_depth == 16) {
	printf("GLINFO: Using bit blit to map color indices !!\n");
  } else {
	printf("GLINFO: Using true color mode (no color indices, but direct color)!!\n");
  }
  
  return 0;
}

/* Close down the virtual screen */

void CloseVScreen (void)
{
  #ifndef NDEBUG
    printf("GLINFO: CloseVScreen (gl_is_initialized=%d)\n", gl_is_initialized);
  #endif

  if (vecgame)
    glvec_exit();

  if(colorBlittedMemory!=NULL)
  {
  	free(colorBlittedMemory);
        colorBlittedMemory = NULL;
  }
  
  if (gl_is_initialized != 0)
  {
    CHECK_GL_BEGINEND();
  }

  gl_reset_resources();
}

/* Not needed under GL */
#ifndef WIN32
void sysdep_clear_screen (void)
{
}
#endif

static int texture_init = 0;

/**
 * the given bitmap MUST be the original mame core bitmap !!!
 *    - no swapxy, flipx or flipy and no resize !
 *    - shall be Machine->scrbitmap
 */
void InitTextures (struct mame_bitmap *bitmap)
{
  int e_x, e_y, s, i=0;
  int x=0, y=0;
  GLint format=0;
  GLint tidxsize=0;
  GLenum err;
  unsigned char *line_1=0;
  struct TexSquare *tsq=0;
  GLdouble texwpervw=0.0;
  GLdouble texhpervh=0.0;
  unsigned char *empty_text;
  int bytes_per_pixel = (video_real_depth + 7) / 8;

  if (glContext == 0 || texture_init == 1)
    return;

  texture_init = 1;

  text_width  = visual_width;
  text_height = visual_height;

  CHECK_GL_BEGINEND();

  texnumx = 1;
  texnumy = 1;

  if(force_text_width_height>0)
  {
  	text_height=force_text_width_height;
  	text_width=force_text_width_height;
	fprintf (stderr, "GLINFO: force_text_width_height := %d x %d\n",
		text_height, text_width);
  }
  
  /* achieve the 2**e_x:=text_width, 2**e_y:=text_height */
  e_x=0; s=1;
  while (s<text_width)
  { s*=2; e_x++; }
  text_width=s;

  e_y=0; s=1;
  while (s<text_height)
  { s*=2; e_y++; }
  text_height=s;

  CHECK_GL_BEGINEND();

#ifndef NDEBUG
  fprintf (stderr, "GLINFO: gl_internal_format= 0x%X,\n\tgl_bitmap_format =  0x%X,\n\tgl_bitmap_type = 0x%X\n", gl_internal_format, gl_bitmap_format, gl_bitmap_type);
#endif

  /* Test the max texture size */
  do
  {
    disp__glTexImage2D (GL_PROXY_TEXTURE_2D, 0,
		  gl_internal_format,
		  text_width, text_height,
		  0, gl_bitmap_format, gl_bitmap_type, 0);

    CHECK_GL_ERROR ();

    disp__glGetTexLevelParameteriv
      (GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);

#ifndef NOTEXIDXSIZE
    disp__glGetTexLevelParameteriv
      (GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_INDEX_SIZE_EXT, &tidxsize);
#else
    tidxsize = -1;
#endif
    CHECK_GL_ERROR ();

    if (format == gl_internal_format &&
	force_text_width_height > 0 &&
	(force_text_width_height < text_width ||
	 force_text_width_height < text_height))
    {
      format = 0;
    }

    if (format != gl_internal_format)
    {
      fprintf (stderr, "GLINFO: Needed texture [%dx%d] too big (format=0x%X,idxsize=%d), trying ",
		text_height, text_width, format, tidxsize);

      if (text_width > text_height)
      {
	e_x--;
	text_width = 1;
	for (i = e_x; i > 0; i--)
	  text_width *= 2;
      }
      else
      {
	e_y--;
	text_height = 1;
	for (i = e_y; i > 0; i--)
	  text_height *= 2;
      }

      fprintf (stderr, "[%dx%d] !\n", text_height, text_width);

      if(text_width < 64 || text_height < 64)
      {
      	fprintf (stderr, "GLERROR: Give up .. usable texture size not available, or texture config error !\n");
	exit(1);
      }
    }
  }
  while (format != gl_internal_format && text_width > 1 && text_height > 1);

  texnumx = visual_width / text_width;
  if ((visual_width % text_width) > 0)
    texnumx++;

  texnumy = visual_height / text_height;
  if ((visual_height % text_height) > 0)
    texnumy++;

  /* Allocate the texture memory */

  /**
   * texwpervw, texhpervh:
   * 	how much the texture covers the visual,
   * 	for both components (width/height) (percent).
   */
  texwpervw = (GLdouble) text_width / (GLdouble) visual_width;
  if (texwpervw > 1.0)
    texwpervw = 1.0;

  texhpervh = (GLdouble) text_height / (GLdouble) visual_height;
  if (texhpervh > 1.0)
    texhpervh = 1.0;

  texgrid = (struct TexSquare *)
    calloc (texnumx * texnumy, sizeof (struct TexSquare));

  fprintf (stderr, "GLINFO: texture-usage %d*width=%d, %d*height=%d\n",
		 (int) texnumx, (int) text_width, (int) texnumy,
		 (int) text_height);

  if(video_real_depth == 16) 
  {
    colorBlittedMemory = malloc( visual_width * visual_height * bytes_per_pixel);
    line_1   = (unsigned char *)colorBlittedMemory;
    line_len = visual_width;
  }
  else
  {
    unsigned char *line_2;
    line_1   = (unsigned char *) bitmap->line[visual.min_y];
    line_2   = (unsigned char *) bitmap->line[visual.min_y + 1];
    line_len = (line_2 - line_1) / bytes_per_pixel;
    line_1  += visual.min_x * bytes_per_pixel;
  }
  
  empty_text = calloc(text_width*text_height, bytes_per_pixel);

  for (y = 0; y < texnumy; y++)
  {
    for (x = 0; x < texnumx; x++)
    {
      tsq = texgrid + y * texnumx + x;

      if (x == texnumx - 1 && visual_width % text_width)
	tsq->xcov =
	  (GLdouble) (visual_width % text_width) / (GLdouble) text_width;
      else
	tsq->xcov = 1.0;

      if (y == texnumy - 1 && visual_height % text_height)
	tsq->ycov =
	  (GLdouble) (visual_height % text_height) / (GLdouble) text_height;
      else
	tsq->ycov = 1.0;

      CalcFlatTexPoint (x, y, texwpervw, texhpervh, &(tsq->fx1), &(tsq->fy1));
      CalcFlatTexPoint (x + 1, y, texwpervw, texhpervh, &(tsq->fx2), &(tsq->fy2));
      CalcFlatTexPoint (x + 1, y + 1, texwpervw, texhpervh, &(tsq->fx3), &(tsq->fy3));
      CalcFlatTexPoint (x, y + 1, texwpervw, texhpervh, &(tsq->fx4), &(tsq->fy4));

      CalcCabPointbyViewpoint( x*(GLdouble)text_width,
      		    y*(GLdouble)text_height,
                   &(tsq->x1), &(tsq->y1), &(tsq->z1));

      CalcCabPointbyViewpoint( x*(GLdouble)text_width  + tsq->xcov*(GLdouble)text_width,
      		    y*(GLdouble)text_height,
                   &(tsq->x2), &(tsq->y2), &(tsq->z2));

      CalcCabPointbyViewpoint( x*(GLdouble)text_width  + tsq->xcov*(GLdouble)text_width,
      		    y*(GLdouble)text_height + tsq->ycov*(GLdouble)text_height,
                   &(tsq->x3), &(tsq->y3), &(tsq->z3));

      CalcCabPointbyViewpoint( x*(GLdouble)text_width,
      		    y*(GLdouble)text_height + tsq->ycov*(GLdouble)text_height,
                   &(tsq->x4), &(tsq->y4), &(tsq->z4));

      /* calculate the pixel store data,
         to use the machine-bitmap for our texture 
      */
      tsq->texture = line_1 +
        ( (y * text_height * line_len) + (x * text_width)) * bytes_per_pixel;

      tsq->isTexture=GL_FALSE;
      tsq->texobj=0;

      disp__glGenTextures (1, &(tsq->texobj));

      disp__glBindTexture (GL_TEXTURE_2D, tsq->texobj);
      err = disp__glGetError ();
      if(err==GL_INVALID_ENUM)
      {
	fprintf (stderr, "GLERROR glBindTexture (glGenText) := GL_INVALID_ENUM, texnum x=%d, y=%d, texture=%d\n", x, y, tsq->texobj);
      } 
      #ifndef NDEBUG
	      else if(err==GL_INVALID_OPERATION) {
		fprintf (stderr, "GLERROR glBindTexture (glGenText) := GL_INVALID_OPERATION, texnum x=%d, y=%d, texture=%d\n", x, y, tsq->texobj);
	      }
      #endif

      if(disp__glIsTexture(tsq->texobj) == GL_FALSE)
      {
	fprintf (stderr, "GLERROR ain't a texture (glGenText): texnum x=%d, y=%d, texture=%d\n",
		x, y, tsq->texobj);
      } else {
        tsq->isTexture=GL_TRUE;
      }

      CHECK_GL_ERROR ();

      disp__glTexImage2D (GL_TEXTURE_2D, 0,
		    gl_internal_format,
		    text_width, text_height,
		    0, gl_bitmap_format, gl_bitmap_type, empty_text);

      CHECK_GL_ERROR ();

      disp__glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 1.0);

      CHECK_GL_ERROR ();

      disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
      disp__glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

      disp__glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

      CHECK_GL_ERROR ();
    }	/* for all texnumx */
  }  /* for all texnumy */

  gl_is_initialized = 1;

  CHECK_GL_BEGINEND();

  /* lets init the rest of the custumizings ... */
  gl_set_bilinear (bilinear);
  gl_set_antialias (antialias);
  gl_set_alphablending (alphablending);
  gl_set_cabview (cabview);

  CHECK_GL_BEGINEND();

  CHECK_GL_ERROR ();
}


/**
 * returns the length of the |(x,y,z)-(i,j,k)|
 */
GLdouble
CompareVec (GLdouble i, GLdouble j, GLdouble k,
	    GLdouble x, GLdouble y, GLdouble z)
{
  GLdouble dx = x-i;
  GLdouble dy = y-j;
  GLdouble dz = z-k;

  return LengthOfVec(dx, dy, dz);
}

void
AddToThisVec (GLdouble i, GLdouble j, GLdouble k,
	      GLdouble * x, GLdouble * y, GLdouble * z)
{
  *x += i ;
  *y += j ;
  *z += k ;
}

/**
 * TranslatePointInPlane:
 *
 * v__p = v__p1 + 
 *        x_off * v__nw +
 *        y_off * v__nh;
 */
void
TranslatePointInPlane   (
	      GLdouble vx_p1, GLdouble vy_p1, GLdouble vz_p1,
	      GLdouble vx_nw, GLdouble vy_nw, GLdouble vz_nw,
	      GLdouble vx_nh, GLdouble vy_nh, GLdouble vz_nh,
	      GLdouble x_off, GLdouble y_off,
	      GLdouble *vx_p, GLdouble *vy_p, GLdouble *vz_p )
{
   GLdouble tx, ty, tz;

   CopyVec( vx_p, vy_p, vz_p,
            vx_p1, vy_p1, vz_p1); 

   CopyVec( &tx, &ty, &tz,
	    vx_nw, vy_nw, vz_nw);

   ScaleThisVec (x_off, x_off, x_off,
                 &tx, &ty, &tz);

   AddToThisVec (tx, ty, tz,
                 vx_p, vy_p, vz_p);

   CopyVec( &tx, &ty, &tz,
	    vx_nh, vy_nh, vz_nh);

   ScaleThisVec (y_off, y_off, y_off,
                 &tx, &ty, &tz);

   AddToThisVec (tx, ty, tz,
                 vx_p, vy_p, vz_p);
}

void
ScaleThisVec (GLdouble i, GLdouble j, GLdouble k,
	      GLdouble * x, GLdouble * y, GLdouble * z)
{
  *x *= i ;
  *y *= j ;
  *z *= k ;
}

GLdouble
LengthOfVec (GLdouble x, GLdouble y, GLdouble z)
{
  return sqrt(x*x+y*y+z*z);
}

void
NormThisVec (GLdouble * x, GLdouble * y, GLdouble * z)
{
  double len = LengthOfVec (*x, *y, *z);

  *x /= len ;
  *y /= len ;
  *z /= len ;
}

/* Compute a delta vector between two points */

void
DeltaVec (GLdouble x1, GLdouble y1, GLdouble z1,
	  GLdouble x2, GLdouble y2, GLdouble z2,
	  GLdouble * dx, GLdouble * dy, GLdouble * dz)
{
  *dx = x2 - x1;
  *dy = y2 - y1;
  *dz = z2 - z1;
}

/* Compute a crossproduct vector of two vectors ( plane ) */

void
CrossVec (GLdouble a1, GLdouble a2, GLdouble a3,
	  GLdouble b1, GLdouble b2, GLdouble b3,
	  GLdouble * c1, GLdouble * c2, GLdouble * c3)
{
  *c1 = a2*b3 - a3*b2; 
  *c2 = a3*b1 - a1*b3;
  *c3 = a1*b2 - a1*b1;
}

void CopyVec(GLdouble *ax,GLdouble *ay,GLdouble *az,                   /* dest   */
	          const GLdouble bx,const GLdouble by,const GLdouble bz     /* source */
                  )
{
	*ax=bx;
	*ay=by;
	*az=bz;
}


/**
 * Calculate texture points (world) for flat screen 
 *
 * x,y: 
 * 	texture index (scalar)
 *	
 * texwpervw, texhpervh:
 * 	how much the texture covers the visual,
 * 	for both components (width/height) (percent).
 *
 * px,py,pz:
 * 	the resulting cabinet point
 *
 */
void CalcFlatTexPoint( int x, int y, GLdouble texwpervw, GLdouble texhpervh, 
		       GLdouble *px,GLdouble *py)
{
  *px=(double)x*texwpervw;
  if(*px>1.0) *px=1.0;
  *py=(double)y*texhpervh;
  if(*py>1.0) *py=1.0;
}

/**
 * vx_gscr_view,vy_gscr_view:
 * 	view-corrd within game-screen
 *
 * vx_p,vy_p,vz_p: 
 * 	world-coord within cab-screen
 */
void CalcCabPointbyViewpoint( 
		   GLdouble vx_gscr_view, GLdouble vy_gscr_view, 
                   GLdouble *vx_p, GLdouble *vy_p, GLdouble *vz_p
		 )
{
  GLdouble vx_gscr = (GLdouble)vx_gscr_view/cab_vpw_fx;
  GLdouble vy_gscr = (GLdouble)vy_gscr_view/cab_vpw_fy;

   /**
    * v__p  = v__gscr_p1  + vx_gscr * v__scr_nx + vy_gscr * v__scr_ny ;
    */

   TranslatePointInPlane ( vx_gscr_p1, vy_gscr_p1, vz_gscr_p1,
                           vx_scr_nx, vy_scr_nx, vz_scr_nx,
			   vx_scr_ny, vy_scr_ny, vz_scr_ny,
			   vx_gscr, vy_gscr,
			   vx_p, vy_p, vz_p);
}

/* Set up a frustum projection */

void
SetupFrustum (void)
{
  double vscrnaspect = (double) winwidth / (double) winheight;

  CHECK_GL_BEGINEND();

  disp__glMatrixMode (GL_PROJECTION);
  disp__glLoadIdentity ();

  if(!do_snapshot)
	  disp__glFrustum (-vscrnaspect, vscrnaspect, -1.0, 1.0, 5.0, 100.0);
  else
	  disp__glFrustum (-vscrnaspect, vscrnaspect, 1.0, -1.0, 5.0, 100.0);

  CHECK_GL_ERROR ();

  disp__glGetDoublev(GL_PROJECTION_MATRIX, mxProjection);
  CHECK_GL_ERROR ();

  disp__glMatrixMode (GL_MODELVIEW);
  disp__glLoadIdentity ();
  disp__glTranslatef (0.0, 0.0, -20.0);

  disp__glGetDoublev(GL_MODELVIEW_MATRIX, mxModel);
  CHECK_GL_ERROR ();
}


/* Set up an orthographic projection */

void SetupOrtho (void)
{
  CHECK_GL_BEGINEND();

  disp__glMatrixMode (GL_PROJECTION);
  disp__glLoadIdentity ();

  if(!do_snapshot)
  {
	  disp__glOrtho (-0.5,  0.5, -0.5,  0.5,  1.0,  -1.0); /* normal display ! */
  } else {
	  disp__glOrtho (-0.5,  0.5,  0.5, -0.5,  1.0,  -1.0); /* normal display ! */
  }

  disp__glMatrixMode (GL_MODELVIEW);
  disp__glLoadIdentity ();

  disp__glRotated ( 180.0 , 1.0, 0.0, 0.0);

  if ( blit_flipx )
	disp__glRotated (  180.0 , 0.0, 1.0, 0.0);

  if ( blit_flipy )
	disp__glRotated ( -180.0 , 1.0, 0.0, 0.0);

  if( blit_swapxy ) {
	disp__glRotated ( 180.0 , 0.0, 1.0, 0.0);
  	disp__glRotated (  90.0 , 0.0, 0.0, 1.0 );
  }

  disp__glTranslated ( -0.5 , -0.5, 0.0 );
  	
}

void gl_resize(int w, int h, int vw, int vh)
{
  int vscrndx;
  int vscrndy;

  winheight = h;
  winwidth  = w;

  vscrndx = (winwidth  - vw) / 2;
  vscrndy = (winheight - vh) / 2;

  CHECK_GL_BEGINEND();

  if (glContext!=NULL)
  {
	if (cabview)
		disp__glViewport (0, 0, winwidth, winheight);
	else
		disp__glViewport (vscrndx, vscrndy, vw, vh);

/*
		disp__glViewport (0, 0, winwidth, winheight);
			    */

        disp__glGetIntegerv(GL_VIEWPORT, dimView);

	if (cabview)
		SetupFrustum ();
	else
		SetupOrtho ();

/*	fprintf(stderr, "GLINFO: xgl_resize to %dx%d\n", winwidth, winheight);
	fflush(stderr); */
  }
}

/* Compute an average between two sets of 3D coordinates */

void
WAvg (GLdouble perc, GLdouble x1, GLdouble y1, GLdouble z1,
      GLdouble x2, GLdouble y2, GLdouble z2,
      GLdouble * ax, GLdouble * ay, GLdouble * az)
{
  *ax = (1.0 - perc) * x1 + perc * x2;
  *ay = (1.0 - perc) * y1 + perc * y2;
  *az = (1.0 - perc) * z1 + perc * z2;
}

void drawGameAxis ()
{
  GLdouble tx, ty, tz;

	disp__glPushMatrix ();

	disp__glLineWidth(1.5f);
	disp__glBegin(GL_LINES);

	/** x-axis **/
	disp__glColor3d (1.0,1.0,1.0);
	disp__glVertex3d(0,0,0);

	disp__glColor3d (1.0,0.0,0.0);
        CopyVec( &tx, &ty, &tz,
	         vx_scr_nx, vy_scr_nx, vz_scr_nx);
        ScaleThisVec ( 5.0, 5.0, 5.0, &tx, &ty, &tz);
	disp__glVertex3d( tx, ty, tz );


	/** y-axis **/
	disp__glColor3d (1.0,1.0,1.0);
	disp__glVertex3d(0,0,0);

	disp__glColor3d (0.0,1.0,0.0);
        CopyVec( &tx, &ty, &tz,
	         vx_scr_ny, vy_scr_ny, vz_scr_ny);
        ScaleThisVec ( 5.0, 5.0, 5.0, &tx, &ty, &tz);
	disp__glVertex3d( tx, ty, tz );


	/** z-axis **/
	disp__glColor3d (1.0,1.0,1.0);
	disp__glVertex3d(0,0,0);

	disp__glColor3d (0.0,0.0,1.0);
        CopyVec( &tx, &ty, &tz,
	         vx_scr_nz, vy_scr_nz, vz_scr_nz);
        ScaleThisVec ( 5.0, 5.0, 5.0, &tx, &ty, &tz);
	disp__glVertex3d( tx, ty, tz );

	disp__glEnd();

	disp__glPopMatrix ();
	disp__glColor3d (1.0,1.0,1.0);
        CHECK_GL_ERROR ();
}


void cabinetTextureRotationTranslation ()
{
	/**
	 * Be aware, this matrix is written in reverse logical
	 * order of GL commands !
	 *
	 * This is the way OpenGL stacks does work !
	 *
	 * So if you interprete this code,
	 * you have to start from the bottom from this block !!
	 *
	 * !!!!!!!!!!!!!!!!!!!!!!
	 */

	/** END  READING ... TRANSLATION / ROTATION **/

	/* go back on screen */
	disp__glTranslated ( vx_gscr_p1, vy_gscr_p1, vz_gscr_p1); 

	/* x-border -> I. Q */
	disp__glTranslated ( vx_gscr_dw/2.0, vy_gscr_dw/2.0, vz_gscr_dw/2.0);

	/* y-border -> I. Q */
	disp__glTranslated ( vx_gscr_dh/2.0, vy_gscr_dh/2.0, vz_gscr_dh/2.0);

	/********* CENTERED AT ORIGIN END  ****************/

	if ( blit_flipx )
		disp__glRotated ( -180.0 , vx_scr_ny, vy_scr_ny, vz_scr_ny);

	if ( blit_flipy )
		disp__glRotated ( -180.0 , vx_scr_nx, vy_scr_nx, vz_scr_nx);



	/********* CENTERED AT ORIGIN BEGIN ****************/

	if( blit_swapxy )
	{
		disp__glRotated ( -180.0 , vx_scr_ny, vy_scr_ny, vz_scr_ny);

		/* x-center */
		disp__glTranslated ( vx_gscr_dw/2.0, vy_gscr_dw/2.0, vz_gscr_dw/2.0);

		/* y-center */
		disp__glTranslated ( vx_gscr_dh/2.0, vy_gscr_dh/2.0, vz_gscr_dh/2.0);

		/* swap -> III. Q */
		disp__glRotated ( -90.0 , vx_scr_nz, vy_scr_nz, vz_scr_nz);
	} else {
		/* x-center */
		disp__glTranslated ( -vx_gscr_dw/2.0, -vy_gscr_dw/2.0, -vz_gscr_dw/2.0);

		/* y-center */
		disp__glTranslated ( vx_gscr_dh/2.0, vy_gscr_dh/2.0, vz_gscr_dh/2.0);
	}

	/* re-flip -> IV. Q     (normal) */
	disp__glRotated ( 180.0 , vx_scr_nx, vy_scr_nx, vz_scr_nx);

	/* go to origin -> I. Q (flipx) */
	disp__glTranslated ( -vx_gscr_p1, -vy_gscr_p1, -vz_gscr_p1); 

	/** START READING ... TRANSLATION / ROTATION **/
}


void
drawTextureDisplay (struct mame_bitmap *bitmap, int useCabinet)
{
  struct TexSquare *square;
  int x = 0, y = 0;
  GLenum err;
  static const double z_pos = 0.9f;
  int updateTexture = 1;
  static int ui_was_dirty=0;

  if(gl_is_initialized == 0)
  	return;
  	
  if(vecgame && !ui_dirty && !ui_was_dirty)
    updateTexture = 0;

  CHECK_GL_BEGINEND();

  if (updateTexture)
  {
    disp__glPixelStorei (GL_UNPACK_ROW_LENGTH, line_len);
    disp__glPixelStorei (GL_UNPACK_ALIGNMENT, 8);

    if(video_real_depth == 16)
    {
    	unsigned short *dest=colorBlittedMemory;
    	int y,x;
    	for (y=visual.min_y;y<=visual.max_y;y++)
    	{
    	   for (x=visual.min_x; x<=visual.max_x; x++, dest++)
    	   {
    	      *dest=current_palette->lookup[((UINT16*)(bitmap->line[y]))[x]];
    	   }
    	}
    }
  }

  disp__glEnable (GL_TEXTURE_2D);

  for (y = 0; y < texnumy; y++)
  {
    for (x = 0; x < texnumx; x++)
    {
      int width,height;
      
      square = texgrid + y * texnumx + x;
      
      if(x<(texnumx-1) || !(visual_width%text_width))
		width=text_width;
      else width=visual_width%text_width;

      if(y<(texnumy-1) || !(visual_height%text_height))
		height=text_height;
      else height=visual_height%text_height;

      if(square->isTexture==GL_FALSE)
      {
	#ifndef NDEBUG
	  fprintf (stderr, "GLINFO ain't a texture(update): texnum x=%d, y=%d, texture=%d\n",
	  	x, y, square->texobj);
	#endif
        continue;
      }

      disp__glBindTexture (GL_TEXTURE_2D, square->texobj);
      err = disp__glGetError ();
      if(err==GL_INVALID_ENUM)
      {
	fprintf (stderr, "GLERROR glBindTexture := GL_INVALID_ENUM, texnum x=%d, y=%d, texture=%d\n", x, y, square->texobj);
      }
      #ifndef NDEBUG
	      else if(err==GL_INVALID_OPERATION) {
		fprintf (stderr, "GLERROR glBindTexture := GL_INVALID_OPERATION, texnum x=%d, y=%d, texture=%d\n", x, y, square->texobj);
	      }
      #endif

      if(disp__glIsTexture(square->texobj) == GL_FALSE)
      {
        square->isTexture=GL_FALSE;
	fprintf (stderr, "GLERROR ain't a texture(update): texnum x=%d, y=%d, texture=%d\n",
		x, y, square->texobj);
      }

      /* This is the quickest way I know of to update the texture */
      if (updateTexture)
      {
	disp__glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0,
		width, height,
		gl_bitmap_format, gl_bitmap_type, square->texture);
      }

      if (useCabinet)
      {
  	GL_BEGIN(GL_QUADS);
	disp__glTexCoord2d (0, 0);
	disp__glVertex3d (square->x1, square->y1, square->z1);
	disp__glTexCoord2d (square->xcov, 0);
	disp__glVertex3d (square->x2, square->y2, square->z2);
	disp__glTexCoord2d (square->xcov, square->ycov);
	disp__glVertex3d (square->x3, square->y3, square->z3);
	disp__glTexCoord2d (0, square->ycov);
	disp__glVertex3d (square->x4, square->y4, square->z4);
	GL_END();
      }
      else
      {
	GL_BEGIN(GL_QUADS);
	disp__glTexCoord2d (0, 0);
	disp__glVertex3d (square->fx1, square->fy1, z_pos);
	disp__glTexCoord2d (square->xcov, 0);
	disp__glVertex3d (square->fx2, square->fy2, z_pos);
	disp__glTexCoord2d (square->xcov, square->ycov);
	disp__glVertex3d (square->fx3, square->fy3, z_pos);
	disp__glTexCoord2d (0, square->ycov);
	disp__glVertex3d (square->fx4, square->fy4, z_pos);
	GL_END();
      }
    } /* for all texnumx */
  } /* for all texnumy */

  if (updateTexture)
  {
    disp__glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
    disp__glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
    disp__glPixelTransferi (GL_MAP_COLOR, GL_FALSE);
  }

  disp__glDisable (GL_TEXTURE_2D);

  CHECK_GL_BEGINEND();

  /* YES - lets catch this error ... 
   */
  (void) disp__glGetError ();
  
  ui_was_dirty = ui_dirty;
}

/* Draw a frame in Cabinet mode */

void UpdateCabDisplay (struct mame_bitmap *bitmap)
{
  int glerrid;
  int shadeModel;
  GLdouble camx, camy, camz;
  GLdouble dirx, diry, dirz;
  GLdouble normx, normy, normz;
  GLdouble perc;
  struct CameraPan *pan, *lpan;

  if (gl_is_initialized == 0)
    return;

  CHECK_GL_BEGINEND();

  disp__glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  CHECK_GL_ERROR ();

  disp__glPushMatrix ();


  /* Do the camera panning */

  if (cpan)
  {
    pan = cpan + currentpan;

/*
    printf("GLINFO (glcab): pan %d/%d panframe %d/%d\n", 
    	currentpan, numpans, panframe, pan->frames);
*/

    if (0 >= panframe || panframe >= pan->frames)
    {
      lastpan = currentpan;
      currentpan += 1;
      if(currentpan>=numpans) currentpan=1;
      panframe = 0;
/*
      printf("GLINFO (glcab): finished pan %d/%d\n", currentpan, numpans);
*/
    }

    switch (pan->type)
    {
    case pan_goto:
      camx = pan->lx;
      camy = pan->ly;
      camz = pan->lz;
      dirx = pan->px;
      diry = pan->px;
      dirz = pan->pz;
      normx = pan->nx;
      normy = pan->ny;
      normz = pan->nz;
      break;
    case pan_moveto:
      lpan = cpan + lastpan;
      perc = (GLdouble) panframe / (GLdouble) pan->frames;
      WAvg (perc, lpan->lx, lpan->ly, lpan->lz,
	    pan->lx, pan->ly, pan->lz, &camx, &camy, &camz);
      WAvg (perc, lpan->px, lpan->py, lpan->pz,
	    pan->px, pan->py, pan->pz, &dirx, &diry, &dirz);
      WAvg (perc, lpan->nx, lpan->ny, lpan->nz,
	    pan->nx, pan->ny, pan->nz, &normx, &normy, &normz);
      break;
    default:
      break;
    }

    disp__gluLookAt (camx, camy, camz, dirx, diry, dirz, normx, normy, normz);

    panframe++;
  }
  else
    disp__gluLookAt (-5.0, 0.0, 5.0, 0.0, 0.0, -5.0, 0.0, 1.0, 0.0);

  disp__glEnable (GL_DEPTH_TEST);

  /* Draw the cabinet */
  disp__glCallList (cablist);

  /* YES - lets catch this error ... */
  glerrid = disp__glGetError ();
  if (0x502 != glerrid)
  {
    CHECK_GL_ERROR ();
  }

  /* Draw the screen if in vector mode */

  cabinetTextureRotationTranslation ();

  if (vecgame)
  {
    disp__glColor4d (1.0, 1.0, 1.0, 1.0);

    drawTextureDisplay (bitmap, 1 /*cabinet */ );

    disp__glDisable (GL_TEXTURE_2D);
    disp__glGetIntegerv(GL_SHADE_MODEL, &shadeModel);
    disp__glShadeModel (GL_FLAT);

    /* always enable antialias for vectors */
    disp__glEnable (GL_LINE_SMOOTH);
    disp__glEnable (GL_POINT_SMOOTH);

    disp__glCallList (veclist);

    if (!antialias)
    {
      disp__glDisable (GL_LINE_SMOOTH);
      disp__glDisable (GL_POINT_SMOOTH);
    }

    disp__glShadeModel (shadeModel);
  }
  else
  {				/* Draw the screen of a bitmapped game */

    drawTextureDisplay (bitmap, 1 /*cabinet */ );

  }

  disp__glDisable (GL_DEPTH_TEST);

  disp__glPopMatrix ();

  if (do_snapshot)
  {
    gl_save_screen_snapshot();
    do_snapshot = 0;
    /* reset upside down .. */
    if (cabview)
            SetupFrustum ();
    else
            SetupOrtho ();
  }

  if (doublebuffer)
  {
#ifdef WIN32
    BOOL ret = SwapBuffers (glHDC);
    if (ret != TRUE)
    {
      CHECK_WGL_ERROR (glWnd, __FILE__, __LINE__);
      doublebuffer = FALSE;
    }
#else
    SwapBuffers ();
#endif
  }
  else
    disp__glFlush ();

  CHECK_GL_BEGINEND();

  CHECK_GL_ERROR ();

}

void
UpdateFlatDisplay (struct mame_bitmap *bitmap)
{
  int shadeModel;

  if (gl_is_initialized == 0)
    return;

  CHECK_GL_BEGINEND();

  disp__glClear (GL_COLOR_BUFFER_BIT);

  disp__glPushMatrix ();

  CHECK_GL_BEGINEND();

  CHECK_GL_ERROR ();

  disp__glColor4d (1.0, 1.0, 1.0, 1.0);

  drawTextureDisplay (bitmap, 0 );

  if (vecgame)
  {
    disp__glDisable (GL_TEXTURE_2D);
    disp__glGetIntegerv(GL_SHADE_MODEL, &shadeModel);
    disp__glShadeModel (GL_FLAT);

    /* always enable antialias for vectors */
    disp__glEnable (GL_LINE_SMOOTH);
    disp__glEnable (GL_POINT_SMOOTH);

    disp__glCallList (veclist);

    if (!antialias)
    {
      disp__glDisable (GL_LINE_SMOOTH);
      disp__glDisable (GL_POINT_SMOOTH);
    }

    disp__glShadeModel (shadeModel);

  }

  CHECK_GL_BEGINEND();

  /* YES - lets catch this error ... 
   */
  (void) disp__glGetError ();

  disp__glPopMatrix ();

  CHECK_GL_ERROR ();

  if (do_snapshot)
  {
    gl_save_screen_snapshot();
    do_snapshot = 0;
    /* reset upside down .. */
    if (cabview)
            SetupFrustum ();
    else
            SetupOrtho ();
  }

  if (doublebuffer)
  {
#ifdef WIN32
    BOOL ret = SwapBuffers (glHDC);
    if (ret != TRUE)
    {
      CHECK_WGL_ERROR (glWnd, __FILE__, __LINE__);
      doublebuffer = FALSE;
    }
#else
    SwapBuffers ();
#endif
  }
  else
    disp__glFlush ();
}

/**
 * the given bitmap MUST be the original mame core bitmap !!!
 *    - no swapxy, flipx or flipy and no resize !
 *    - shall be Machine->scrbitmap
 */

void UpdateGLDisplay (struct mame_bitmap *bitmap)
{
  if ( ! texture_init ) InitTextures (bitmap);

  if (gl_is_initialized == 0)
    return;

  /* upside down .. to make a good snapshot ;-) */
  if (do_snapshot)
  {
    if (cabview)
            SetupFrustum ();
    else
            SetupOrtho ();
  }

  if (cabview && !cabload_err)
    UpdateCabDisplay (bitmap);
  else
    UpdateFlatDisplay (bitmap);

  CHECK_GL_BEGINEND();
  CHECK_GL_ERROR ();
}

/**
 * the given bitmap MUST be the original mame core bitmap !!!
 *    - no swapxy, flipx or flipy and no resize !
 *    - shall be Machine->scrbitmap
 */
void UpdateVScreen(struct mame_bitmap *bitmap)
{
  UpdateGLDisplay (bitmap);

  if (code_pressed (KEYCODE_RALT))
  {
    if (code_pressed_memory (KEYCODE_A))
    {
	  gl_set_antialias (1-antialias);
	  printf("GLINFO: switched antialias := %d\n", antialias);
    }
    else if (code_pressed_memory (KEYCODE_B))
    {
      gl_set_bilinear (1 - bilinear);
      printf("GLINFO: switched bilinear := %d\n", bilinear);
    }
    else if (code_pressed_memory (KEYCODE_C))
    {
      gl_set_cabview (1-cabview);
      printf("GLINFO: switched cabinet := %d\n", cabview);
    }
    else if (code_pressed_memory (KEYCODE_PLUS_PAD))
    {
	set_gl_beam(get_gl_beam()+0.5);
    }
    else if (code_pressed_memory (KEYCODE_MINUS_PAD))
    {
	set_gl_beam(get_gl_beam()-0.5);
    }
  }
}

struct mame_bitmap *osd_override_snapshot(struct mame_bitmap *bitmap,
		struct rectangle *bounds)
{
	do_snapshot = 1;
	return NULL;
}


#endif /* ifdef xgl */
