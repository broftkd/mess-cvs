/*****************************************************************

  Generic glide routines

  Copyright 1998 by Mike Oliphant - oliphant@ling.ed.ac.uk
  Copyright 2004 Hans de Goede - j.w.r.degoede@hhs.nl

    http://www.ling.ed.ac.uk/~oliphant/glmame

  This code may be used and distributed under the terms of the
  Mame license
  
*****************************************************************/

#if defined xfx || defined svgafx
#include <stdio.h>
#include <math.h>
#include <signal.h>
#include "driver.h"
#include "usrintrf.h"
#include "fxcompat.h"
#include "xmame.h"
#include "vidhrdw/vector.h"
#include "pixel_convert.h"
#include "sysdep/sysdep_display.h"

void CalcPoint(GrVertex *vert,int x,int y);
int  InitTextures(void);
int  InitVScreen(void);
void CloseVScreen(void);
void UpdateTexture(struct mame_bitmap *bitmap);
void DrawFlatBitmap(void);
void UpdateFXDisplay(struct mame_bitmap *bitmap);
static int SetResolution(struct rc_option *option, const char *arg,
   int priority);
int fxvec_renderer(point *pt, int num_points);

int fxwidth = 640;
int fxheight = 480;
int vscrntlx;
int vscrntly;
int vscrnwidth;
int vscrnheight;

static GrScreenResolution_t Gr_resolution = GR_RESOLUTION_640x480;
static char version[80];
static GrTexInfo texinfo;
static int bilinear=1; /* Do binlinear filtering? */
static const int texsize=256;
static GrContext_t context=0;

/* The squares that are tiled to make up the game screen polygon */

struct TexSquare
{
  UINT16 *texture;
  unsigned int texobj;
  long texadd;
  GrVertex vtxA, vtxB, vtxC, vtxD;
  float xcov,ycov;
};

static struct TexSquare *texgrid=NULL;
static int texnumx;
static int texnumy;
static int texdestwidth;
static int texdestheight;
static int firsttexdestwidth;
static int firsttexdestheight;
static int vecgame=0;
static struct sigaction orig_sigaction[32];
static struct sigaction vscreen_sa;  
static int signals_to_catch[] = { SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT,
   SIGFPE, SIGKILL, SIGSEGV, SIGPIPE, SIGTERM, SIGBUS, -1 };
static int signals_caught = 0;

struct rc_option fx_opts[] = {
   /* name, shortname, type, dest, deflt, min, max, func, help */
   { "FX (Glide) Related", NULL,		rc_seperator,	NULL,
     NULL,		0,			0,		NULL,
     NULL },
   { "resolution",	"res",			rc_use_function, NULL,
     "640x480",		0,			0,		SetResolution,
     "Specify the resolution/ windowsize to use in the form of XRESxYRES" },
   { NULL,		NULL,			rc_end,		NULL,
     NULL,		0,			0,		NULL,
     NULL }
};

struct sysdep_display_prop_struct sysdep_display_properties = { fxvec_renderer, 1 };

void CalcPoint(GrVertex *vert,int x,int y)
{
  if(x)
  {
    vert->x=vscrntlx+firsttexdestwidth+(x-1)*texdestwidth;
    if(vert->x>vscrntlx+vscrnwidth) vert->x=vscrntlx+vscrnwidth;
  }
  else
    vert->x = vscrntlx;

  if(y)
  {
    vert->y=vscrntly+vscrnheight-(firsttexdestheight+(y-1)*texdestheight);
    if(vert->y<vscrntly) vert->y=vscrntly;
  }
  else
    vert->y = vscrntly+vscrnheight;
}

int InitGlide(void)
{
  int fd = open("/dev/3dfx", O_RDWR);
  if ((fd < 0) && geteuid())
  {
     fprintf(stderr, "Glide error: couldn't open /dev/3dfx and not running as root\n");
     return OSD_NOT_OK;
  }
  if (fd >= 0)
     close(fd);
  putenv("FX_GLIDE_NO_SPLASH=");
  grGlideInit();
  grSetupVertexLayout();

  blit_hardware_rotation = 1;
  return OSD_OK;
}

void ExitGlide(void)
{
  grGlideShutdown();
}

static void VScreenSignalHandler(int signo)
{
  grEnablePassThru();
  orig_sigaction[signo].sa_handler(signo);
}

void VScreenCatchSignals(void)
{
  int i;
  
  /* catch fatal signals and restore the vgapassthru before exiting */
  memset(&vscreen_sa, 0, sizeof(vscreen_sa));
  vscreen_sa.sa_handler = VScreenSignalHandler;
  for (i=0; signals_to_catch[i] != -1; i++)
  {
     sigaction(signals_to_catch[i], &vscreen_sa, &(orig_sigaction[signals_to_catch[i]]));
  }
  signals_caught = 1;
}

void VScreenRestoreSignals(void)
{
  int i;
  
  if (!signals_caught)
     return;
  
  /* restore signal handlers */
  for (i=0; signals_to_catch[i] != -1; i++)
  {
     sigaction(signals_to_catch[i], &(orig_sigaction[signals_to_catch[i]]), NULL);
  }
  signals_caught = 0;
}

int InitTextures(void)
{
  int i,j,x=0,y=0;
  struct TexSquare *tsq;
  long texmem,memaddr;
  float firsttexdestwidthfac=0.0, firsttexdestheightfac=0.0;
  float texpercx, texpercy;

  texinfo.smallLod=texinfo.largeLod=GR_LOD_256;
  texinfo.aspectRatio=GR_ASPECT_1x1;
  texinfo.format=GR_TEXFMT_ARGB_1555;

  texmem=grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH,&texinfo);

  if(vecgame)
  {
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                                       GR_COMBINE_FACTOR_LOCAL,
                                       GR_COMBINE_LOCAL_CONSTANT,
                                       GR_COMBINE_OTHER_NONE,
                                       FXFALSE);

    grAlphaBlendFunction(GR_BLEND_ALPHA_SATURATE,GR_BLEND_ONE,
						 GR_BLEND_ALPHA_SATURATE,GR_BLEND_ONE);
  }                                          
	
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
				   GR_COMBINE_FACTOR_ONE,
				   GR_COMBINE_LOCAL_NONE,
				   GR_COMBINE_OTHER_TEXTURE,
				   FXFALSE);

  grTexCombine(GR_TMU0,
			   GR_COMBINE_FUNCTION_LOCAL,GR_COMBINE_FACTOR_NONE,
			   GR_COMBINE_FUNCTION_NONE,GR_COMBINE_FACTOR_NONE,
			   FXFALSE, FXFALSE);

  grTexMipMapMode(GR_TMU0,
				  GR_MIPMAP_DISABLE,
				  FXFALSE);

  grTexClampMode(GR_TMU0,
				 GR_TEXTURECLAMP_CLAMP,
				 GR_TEXTURECLAMP_CLAMP);

  if(bilinear)
	grTexFilterMode(GR_TMU0,
					GR_TEXTUREFILTER_BILINEAR,
					GR_TEXTUREFILTER_BILINEAR);
  else
	grTexFilterMode(GR_TMU0,
					GR_TEXTUREFILTER_POINT_SAMPLED,
					GR_TEXTUREFILTER_POINT_SAMPLED);

  /* Allocate the texture memory */
  
  texnumx=visual_width/texsize;
  if(texnumx*texsize!=visual_width) texnumx++;
  texnumy=visual_height/texsize;
  if(texnumy*texsize!=visual_height) texnumy++;
  
  if (blit_swapxy)
  {
    texpercx=(float)texsize/(float)visual_height;
    texpercy=(float)texsize/(float)visual_width;
  }
  else
  {
    texpercx=(float)texsize/(float)visual_width;
    texpercy=(float)texsize/(float)visual_height;
  }

  if(texpercx>1.0) texpercx=1.0;
  if(texpercy>1.0) texpercy=1.0;

  texdestwidth=vscrnwidth*texpercx;
  texdestheight=vscrnheight*texpercy;

  texgrid=(struct TexSquare *)
	calloc(texnumx*texnumy, sizeof(struct TexSquare));
  memaddr=grTexMinAddress(GR_TMU0);
  
  for(i=0;i<texnumy;i++) {
	for(j=0;j<texnumx;j++) {
	  tsq=texgrid+i*texnumx+j;

	  tsq->texadd=memaddr;
	  memaddr+=texmem;

	  if(j==(texnumx-1) && visual_width%texsize)
		tsq->xcov=(float)((visual_width)%texsize)/(float)texsize;
	  else tsq->xcov=1.0;
	  
	  if(i==(texnumy-1) && visual_height%texsize)
		tsq->ycov=(float)((visual_height)%texsize)/(float)texsize;
	  else tsq->ycov=1.0;

	  tsq->vtxA.oow=1.0;
	  tsq->vtxB=tsq->vtxC=tsq->vtxD=tsq->vtxA;

          if(blit_flipy)
          {
            tsq->vtxA.tmuvtx[0].tow=256.0;
            tsq->vtxB.tmuvtx[0].tow=256.0;
            tsq->vtxC.tmuvtx[0].tow=0.0;
            tsq->vtxD.tmuvtx[0].tow=0.0;
            if (blit_swapxy)
            {
              y = (texnumx-1) - j;
              firsttexdestheightfac = (visual_width%texsize)/(float)texsize;
            }
            else
            {
              y = (texnumy-1) - i;
              firsttexdestheightfac = (visual_height%texsize)/(float)texsize;
            }
          }
          else
          {
            tsq->vtxA.tmuvtx[0].tow=0.0;
            tsq->vtxB.tmuvtx[0].tow=0.0;
            tsq->vtxC.tmuvtx[0].tow=256.0;
            tsq->vtxD.tmuvtx[0].tow=256.0;
            if (blit_swapxy)
            {
              y = j;
              firsttexdestheightfac = 1.0;
            }
            else
            {
              y = i;
              firsttexdestheightfac = 1.0;
            }
          }

          if(blit_flipx)
          {
            tsq->vtxA.tmuvtx[0].sow=256.0;
            tsq->vtxB.tmuvtx[0].sow=0.0;
            tsq->vtxC.tmuvtx[0].sow=0.0;
            tsq->vtxD.tmuvtx[0].sow=256.0;
            if (blit_swapxy)
            {
              x = (texnumy-1) - i;
              firsttexdestwidthfac = (visual_height%texsize)/(float)texsize;
            }
            else
            {
              x = (texnumx-1) - j;
              firsttexdestwidthfac = (visual_width%texsize)/(float)texsize;
            }
          }
          else
          {
            tsq->vtxA.tmuvtx[0].sow=0.0;
            tsq->vtxB.tmuvtx[0].sow=256.0;
            tsq->vtxC.tmuvtx[0].sow=256.0;
            tsq->vtxD.tmuvtx[0].sow=0.0;
            if (blit_swapxy)
            {
              x = i;
              firsttexdestwidthfac = 1.0;
            }
            else
            {
              x = j;
              firsttexdestwidthfac = 1.0;
            }
          }
          
          if(blit_swapxy)
          {
            float temp;
            
            temp=tsq->vtxA.tmuvtx[0].sow;
            tsq->vtxA.tmuvtx[0].sow=tsq->vtxA.tmuvtx[0].tow;
            tsq->vtxA.tmuvtx[0].tow=temp;

            temp=tsq->vtxB.tmuvtx[0].sow;
            tsq->vtxB.tmuvtx[0].sow=tsq->vtxB.tmuvtx[0].tow;
            tsq->vtxB.tmuvtx[0].tow=temp;

            temp=tsq->vtxC.tmuvtx[0].sow;
            tsq->vtxC.tmuvtx[0].sow=tsq->vtxC.tmuvtx[0].tow;
            tsq->vtxC.tmuvtx[0].tow=temp;

            temp=tsq->vtxD.tmuvtx[0].sow;
            tsq->vtxD.tmuvtx[0].sow=tsq->vtxD.tmuvtx[0].tow;
            tsq->vtxD.tmuvtx[0].tow=temp;
          }

          tsq->vtxA.tmuvtx[0].tow*=tsq->ycov;
          tsq->vtxB.tmuvtx[0].tow*=tsq->ycov;
          tsq->vtxC.tmuvtx[0].tow*=tsq->ycov;
          tsq->vtxD.tmuvtx[0].tow*=tsq->ycov;

          tsq->vtxA.tmuvtx[0].sow*=tsq->xcov;
          tsq->vtxB.tmuvtx[0].sow*=tsq->xcov;
          tsq->vtxC.tmuvtx[0].sow*=tsq->xcov;
          tsq->vtxD.tmuvtx[0].sow*=tsq->xcov;

          firsttexdestwidth=texdestwidth*firsttexdestwidthfac;
          firsttexdestheight=texdestheight*firsttexdestheightfac;

          CalcPoint(&(tsq->vtxA), x  , y  );
          CalcPoint(&(tsq->vtxB), x+1, y  );
          CalcPoint(&(tsq->vtxC), x+1, y+1);
          CalcPoint(&(tsq->vtxD), x  , y+1);
	
          /* Initialize the texture memory */
          if (!(tsq->texture=calloc(texsize*texsize, sizeof *(tsq->texture))))
             return 1;
        }
  }
  return 0;
}

typedef struct {
    int         res;
    int       width;
    int       height;
} ResToRes;
		
static ResToRes resTable[] = {
    { GR_RESOLUTION_320x200,   320,  200 },  /* 0x0 */
    { GR_RESOLUTION_320x240,   320,  240 },  /* 0x1 */
    { GR_RESOLUTION_400x256,   400,  256 },  /* 0x2 */
    { GR_RESOLUTION_512x384,   512,  384 },  /* 0x3 */
    { GR_RESOLUTION_640x200,   640,  200 },  /* 0x4 */
    { GR_RESOLUTION_640x350,   640,  350 },  /* 0x5 */
    { GR_RESOLUTION_640x400,   640,  400 },  /* 0x6 */
    { GR_RESOLUTION_640x480,   640,  480 },  /* 0x7 */
    { GR_RESOLUTION_800x600,   800,  600 },  /* 0x8 */
    { GR_RESOLUTION_960x720,   960,  720 },  /* 0x9 */
    { GR_RESOLUTION_856x480,   856,  480 },  /* 0xA */
    { GR_RESOLUTION_512x256,   512,  256 },  /* 0xB */
    { GR_RESOLUTION_1024x768,  1024, 768 },  /* 0xC */
    { GR_RESOLUTION_1280x1024, 1280, 1024 }, /* 0xD */
    { GR_RESOLUTION_1600x1200, 1600, 1200 }, /* 0xE */
    { GR_RESOLUTION_400x300,   400,  300 }   /* 0xF */
};
			
static long resTableSize = sizeof( resTable ) / sizeof( ResToRes );

/* Get screen resolution */
static int SetResolution(struct rc_option *option, const char *arg,
   int priority)
{
  int width, height, match;
  if (sscanf(arg, "%dx%d", &width, &height) == 2)
  {
     for( match = 0; match < resTableSize; match++ )
        if( width==resTable[match].width && height==resTable[match].height)
        {
           Gr_resolution = resTable[match].res;
           fxwidth = width;
           fxheight = height;
           option->priority = priority;
           return 0;
        }
  }
  fprintf(stderr,
      "error: invalid resolution or unknown resolution: \"%s\".\n"
      "   Valid resolutions are:\n", arg);
  for( match = 0; match < resTableSize; match++ )
  {
     fprintf(stderr_file, "   \"%dx%d\"", resTable[match].width,
        resTable[match].height);
     if (match && (match % 5) == 0)
        fprintf(stderr_file, "\n");
  }
  return -1;
}

/* Set up the virtual screen */

int InitVScreen(void)
{
  grGlideGetVersion(version);

  if(Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	vecgame=1;

  fprintf(stderr_file, "info: using Glide version %s\n", version);
  
  grSstSelect(0);

  if(!(context = grSstWinOpen(0,Gr_resolution,GR_REFRESH_60Hz,GR_COLORFORMAT_ABGR,
     GR_ORIGIN_LOWER_LEFT,2,1)))
  {
     fprintf(stderr_file, "error opening Glide window, do you have enough memory on your 3dfx for the selected mode?\n");
     return OSD_NOT_OK;
  }
  fprintf(stderr_file,
     "info: screen resolution set to %dx%d\n", fxwidth, fxheight);

  /* clear the buffer */
  grBufferClear(0,0,0);
  
  /* calculate the vscreen boundaries */
  mode_clip_aspect(fxwidth, fxheight, &vscrnwidth, &vscrnheight,
     (double)fxwidth/fxheight);
  vscrntlx=(fxwidth -vscrnwidth )/2;
  vscrntly=(fxheight-vscrnheight)/2;
  
  /* fill the display_palette_info struct */
  memset(&display_palette_info, 0, sizeof(struct sysdep_palette_info));
  switch(video_real_depth) {
    case 15:
    case 16:
      display_palette_info.red_mask   = 0x7C00;
      display_palette_info.green_mask = 0x03E0;
      display_palette_info.blue_mask  = 0x001F;
      break;
    case 32:
      display_palette_info.red_mask   = 0xFF0000;
      display_palette_info.green_mask = 0x00FF00;
      display_palette_info.blue_mask  = 0x0000FF;
      break;
  }
   
  return InitTextures();
}

/* Close down the virtual screen */

void CloseVScreen(void)
{
  int x,y;

  /* Free Texture stuff */

  if(texgrid) {
	for(y=0;y<texnumy;y++) {
	  for(x=0;x<texnumx;x++) {
            if (texgrid[y*texnumx+x].texture)
	      free(texgrid[y*texnumx+x].texture);
	  }
	}
	
	free(texgrid);
	texgrid = NULL;
  }

  if (context)
  {
    grSstWinClose(context);
    context = 0;
  }
}


/* Update the texture with the contents of the game screen */

void UpdateTexture(struct mame_bitmap *bitmap)
{
	int y,rline,texline,xsquare,ysquare,ofs,width, i;
	struct TexSquare *square;

	if(visual_width<=texsize) width=visual_width;
	else width=texsize;

	switch (video_real_depth) {


	case 15:
		for(y=visual.min_y;y<=visual.max_y;y++) {
			rline=y-visual.min_y;
			ysquare=rline/texsize;
			texline=rline%texsize;
			
			for(xsquare=0;xsquare<texnumx;xsquare++) {
				ofs=xsquare*texsize;
				
				if(xsquare<(texnumx-1) || !(visual_width%texsize))
					width=texsize;
				else width=visual_width%texsize;
				
				square=texgrid+(ysquare*texnumx)+xsquare;

                                memcpy(square->texture+texline*texsize,
                                       (UINT16*)(bitmap->line[y])+visual.min_x+ofs,
                                       width*2);
			}
		} 
		break;

	case 16:
		
		for(y=visual.min_y;y<=visual.max_y;y++) {
			rline=y-visual.min_y;
			ysquare=rline/texsize;
			texline=rline%texsize;
			
			for(xsquare=0;xsquare<texnumx;xsquare++) {
				ofs=xsquare*texsize;
				
				if(xsquare<(texnumx-1) || !(visual_width%texsize))
					width=texsize;
				else width=visual_width%texsize;
				
				square=texgrid+(ysquare*texnumx)+xsquare;
				for(i = 0;i < width;i++) {
					square->texture[texline*texsize+i] = 
						current_palette->lookup[(((UINT16*)(bitmap->line[y]))[visual.min_x+ofs+i])];
				}
			}
		}
		break;

	case 24:
	case 32:
		for(y=visual.min_y;y<=visual.max_y;y++) {
			rline=y-visual.min_y;
			ysquare=rline/texsize;
			texline=rline%texsize;
			
			for(xsquare=0;xsquare<texnumx;xsquare++) {
				ofs=xsquare*texsize;
				
				if(xsquare<(texnumx-1) || !(visual_width%texsize))
					width=texsize;
				else width=visual_width%texsize;
				
				square=texgrid+(ysquare*texnumx)+xsquare;
					
				for(i = 0;i < width;i++) {
					square->texture[texline*texsize+i] = 
						_32TO16_RGB_555((((UINT32*)(bitmap->line[y]))[visual.min_x+ofs+i]));
				}
			}
		}
		break;
	}

        for(ysquare=0;ysquare<texnumy;ysquare++) {
              for(xsquare=0;xsquare<texnumx;xsquare++) {
		square=texgrid+(ysquare*texnumx)+xsquare;
                texinfo.data=(void *)square->texture;
                grTexDownloadMipMapLevel(GR_TMU0,square->texadd,
                         GR_LOD_256,GR_LOD_256,GR_ASPECT_1x1,
                         GR_TEXFMT_ARGB_1555,
                         GR_MIPMAPLEVELMASK_BOTH,texinfo.data);
              }
        }
}

void DrawFlatBitmap(void)
{
  struct TexSquare *square;
  int x,y;

  for(y=0;y<texnumy;y++) {
	for(x=0;x<texnumx;x++) {
	  square=texgrid+y*texnumx+x;

	  grTexSource(GR_TMU0,square->texadd,
				  GR_MIPMAPLEVELMASK_BOTH,&texinfo);

	  grDrawTriangle(&(square->vtxA),&(square->vtxD),&(square->vtxC));
	  grDrawTriangle(&(square->vtxA),&(square->vtxB),&(square->vtxC));
	}
  }
}

void UpdateFXDisplay(struct mame_bitmap *bitmap)
{
  static int ui_was_dirty=0;
  
  if(bitmap && (!vecgame || ui_dirty || ui_was_dirty))
    UpdateTexture(bitmap);

  DrawFlatBitmap();
  grBufferSwap(1);
  grBufferClear(0,0,0);

  ui_was_dirty=ui_dirty;
}


/* invoked by main tree code to update bitmap into screen */

void sysdep_update_display(struct mame_bitmap *bitmap)
{
  if(code_pressed(KEYCODE_RCONTROL)) {
	if(code_pressed_memory(KEYCODE_B)) {
	  bilinear=1-bilinear;

	  if(bilinear)
		grTexFilterMode(GR_TMU0,
						GR_TEXTUREFILTER_BILINEAR,
						GR_TEXTUREFILTER_BILINEAR);
	  else
		grTexFilterMode(GR_TMU0,
						GR_TEXTUREFILTER_POINT_SAMPLED,
						GR_TEXTUREFILTER_POINT_SAMPLED);

	}
  }

  UpdateFXDisplay(bitmap);
}

#endif /* if defined xfx || defined svgafx */
