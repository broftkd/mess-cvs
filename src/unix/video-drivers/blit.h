/* this routine is the generic blit routine used in many cases, through a
   number of defines it can be customised for specific cases.
   Currently recognised defines:
   DEST		 ptr of type DEST_PIXEL to which should be blitted
   DEST_PIXEL	 type of the buffer to which is blitted
   DEST_WIDTH    Width of the destination buffer in pixels!
   SRC_PIXEL     type of the buffer from which is blitted
   INDIRECT      Define this if you want SRC_PIXELs to be an index to a lookup
                 table, the contents of this define is used as the lookuptable.
   CONVERT_PIXEL If defined then the blit core will call CONVERT_PIXEL on each
                 src_pixel (after looking it up in INDIRECT if defined).
                 This could be used to show for example a 32 bpp bitmap
                 (artwork) on a 16 bpp X-server by defining a CONVER_PIXEL
                 function which downgrades the direct RGB  pixels to 16 bpp.
                 When you use this you should also make sure that effect.c
                 renders in SRC_PIXEL color depth and format after lookup in
                 INDIRECT.
                 Also see the comment about using this with effects in the top
                 of blit_effect.h .
   PACK_BITS     If defined write to packed 24bit pixels, DEST_PIXEL must be
                 32bits and the contents of SRC_PIXEL after applying INDIRECT
                 and CONVERT_PIXEL should be 32 bits RGB 888.
   BLIT_HWSCALE_YUY2	Write to a 16 bits DEST in YUY2 format if INDIRECT is
                 set the contents of SRC_PIXEL after applying INDIRECT and
                 CONVERT_PIXEL should be a special pre calculated 32 bits
                 format, see x11_window.c for an example.
                 If indirect is not set the contents of SRC_PIXEL after
                 applying CONVERT_PIXEL should be 32 bits RGB 888.
   DFB           Direct Frame buffer access, avoid reading from DEST. This
                 define only has effect in scenarios where there could be reads
                 from the destination, in other scenarios it is ignored.

These routines assume dirty_params->min_x and DEST_WIDTH are a multiple off 4!
*/

#ifdef PACK_BITS
/* scale destptr delta's by 3/4 since we're using 32 bits ptr's for a 24 bits
   dest */
#define DEST_PIXEL_SIZE 3
#define CORRECTED_DEST_WIDTH ((DEST_WIDTH*3)/4)
#else
#define DEST_PIXEL_SIZE sizeof(DEST_PIXEL)
#define CORRECTED_DEST_WIDTH DEST_WIDTH
#endif

/* arbitrary Y-scaling (Adam D. Moss <adam@gimp.org>) */
#define REPS_FOR_Y(N,YV,YMAX) ((N)* ( (((YV)+1)*yarbsize)/(YMAX) - ((YV)*yarbsize)/(YMAX)))

#ifdef DFB
#define COPY_LINE_FOR_Y(YV, YMAX, SRC, END, DST) \
{ \
   int i = 0, reps = REPS_FOR_Y(1, YV, YMAX); \
   if (reps > 0) { \
     reps -= scanlines; \
     do { COPY_LINE2(SRC, END, (DST)+(i*(CORRECTED_DEST_WIDTH))); \
     } while (++i < reps); \
   } \
}
#else
#define COPY_LINE_FOR_Y(YV, YMAX, SRC, END, DST) \
{ \
   int i = 0, reps = REPS_FOR_Y(1, YV, YMAX); \
   if (reps > 0) { \
     reps -= scanlines; \
     COPY_LINE2(SRC, END, DST); \
     while (++i < reps) \
       memcpy((DST)+(i*(CORRECTED_DEST_WIDTH)), DST, ((END)-(SRC))*DEST_PIXEL_SIZE*sysdep_display_params.widthscale); \
  } \
}
#endif

#define LOOP() \
if (sysdep_display_params.orientation) { \
  line_src = (SRC_PIXEL *)rotate_dbbuf0; \
  line_end = (SRC_PIXEL *)rotate_dbbuf0 + bounds_width; \
  for (y = dirty_area->min_y; y <= dirty_area->max_y; line_dest+=REPS_FOR_Y(CORRECTED_DEST_WIDTH,y,sysdep_display_params.height), y++) { \
           rotate_func(rotate_dbbuf0, bitmap, y, dirty_area); \
           COPY_LINE_FOR_Y(y, sysdep_display_params.height, line_src, line_end, line_dest); \
  } \
} else { \
  for (y = dirty_area->min_y; line_src < line_end; \
        line_dest+=REPS_FOR_Y(CORRECTED_DEST_WIDTH,y,sysdep_display_params.height), \
                line_src+=src_width, y++) \
           COPY_LINE_FOR_Y(y,sysdep_display_params.height, \
                           line_src, line_src+bounds_width, line_dest); \
}

#ifdef CONVERT_PIXEL
#  ifdef INDIRECT
#    define GETPIXEL(src) CONVERT_PIXEL(INDIRECT[src])
#  else
#    define GETPIXEL(src) CONVERT_PIXEL((src))
#  endif
#else
#  ifdef INDIRECT
#    define GETPIXEL(src) INDIRECT[src]
#  else
#    ifndef PACK_BITS
#      define GETPIXEL(src) (src)
#    else
/*     when we're using direct blitting the bitmap might have cruft in the msb
       of the sparse 32 bpp pixels (artwork), so in this case we need to mask
       out the msb */
#      define GETPIXEL(src) ((src)&0x00FFFFFF)
#    endif
#  endif
#endif

#define RMASK 0xff0000
#define GMASK 0x00ff00
#define BMASK 0x0000ff

  /* begin actual blit code */
  int y, src_width, bounds_width;
  SRC_PIXEL *line_src, *line_end;
  DEST_PIXEL *line_dest;
  int scanlines = 0;
  int yarbsize  = sysdep_display_params.yarbsize?
    sysdep_display_params.yarbsize:
    sysdep_display_params.height*sysdep_display_params.heightscale;

  sysdep_display_check_bounds(bitmap, vis_in_dest_out, dirty_area, 3);

  src_width    = (((SRC_PIXEL *)bitmap->line[1]) - ((SRC_PIXEL *)bitmap->line[0]));
  bounds_width = (dirty_area->max_x+1) - dirty_area->min_x;
  line_src     = (SRC_PIXEL *)bitmap->line[dirty_area->min_y] + dirty_area->min_x;
  line_end     = (SRC_PIXEL *)bitmap->line[dirty_area->max_y+1] + dirty_area->min_x;
  line_dest    = (DEST_PIXEL *)((unsigned char *)(DEST) + (vis_in_dest_out->min_y*DEST_WIDTH + vis_in_dest_out->min_x)*DEST_PIXEL_SIZE);

  switch(sysdep_display_params.effect)
  {
    case SYSDEP_DISPLAY_EFFECT_FAKESCAN:
      scanlines = 1;
    case SYSDEP_DISPLAY_EFFECT_NONE:
      switch(sysdep_display_params.widthscale)
      {

case 1:
#ifdef PACK_BITS
#define COPY_LINE2(SRC, END, DST) \
{\
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   for(;src<end;dst+=3,src+=4) \
   { \
      *(dst  ) = (GETPIXEL(*(src  ))    ) | (GETPIXEL(*(src+1))<<24); \
      *(dst+1) = (GETPIXEL(*(src+1))>> 8) | (GETPIXEL(*(src+2))<<16); \
      *(dst+2) = (GETPIXEL(*(src+2))>>16) | (GETPIXEL(*(src+3))<< 8); \
   }\
}
LOOP()
#elif defined BLIT_HWSCALE_YUY2
/* HWSCALE_YUY2 has seperate code for direct / indirect, see above */
#ifdef INDIRECT
#ifdef LSB_FIRST /* x86 etc */
#define COPY_LINE2(SRC, END, DST) \
   {\
      SRC_PIXEL  *src = SRC; \
      SRC_PIXEL  *end = END; \
      unsigned int *dst = (unsigned int *)DST; \
      unsigned int r,y,y2,uv1,uv2; \
      for(;src<end;) \
      { \
         r=GETPIXEL(*src++); \
         y=r&255; \
         uv1=(r&0xff00ff00)>>1; \
         r=GETPIXEL(*src++); \
         uv2=(r&0xff00ff00)>>1; \
         y2=r&0xff0000; \
         *dst++=y|y2|((uv1+uv2)&0xff00ff00);\
      } \
   }
#else /* ppc etc */
#define COPY_LINE2(SRC, END, DST) \
   {\
      SRC_PIXEL  *src = SRC; \
      SRC_PIXEL  *end = END; \
      unsigned int *dst = (unsigned int *)DST; \
      unsigned int r,y,y2,uv1,uv2; \
      for(;src<end;) \
      { \
         r=GETPIXEL(*src++); \
         y=r&0xff000000 ; \
         uv1=(r&0x00ff00ff); \
         r=GETPIXEL(*src++); \
         uv2=(uv1+(r&0x00ff00ff))>>1; \
         y2=r&0xff00; \
         *dst++=y|y2|(uv2&0x00ff00ff); \
      } \
   }
#endif /* ifdef LSB_FIRST */
#else  /* HWSCALE_YUY2, not indirect (for 32 bpp direct color) */
#define COPY_LINE2(SRC, END, DST) \
   {\
      SRC_PIXEL  *src = SRC; \
      SRC_PIXEL  *end = END; \
      unsigned char *dst = (unsigned char *)DST; \
      int r,g,b,r2,g2,b2,y,y2,u,v; \
      for(;src<end;) \
      { \
         r=g=b=GETPIXEL(*src++); \
         r&=RMASK;  r>>=16; \
         g&=GMASK;  g>>=8; \
         b&=BMASK;  b>>=0; \
         r2=g2=b2=GETPIXEL(*src++); \
         r2&=RMASK;  r2>>=16; \
         g2&=GMASK;  g2>>=8; \
         b2&=BMASK;  b2>>=0; \
         y = (( 9836*r + 19310*g + 3750*b ) >> 15); \
         y2 = (( 9836*r2 + 19310*g2 + 3750*b2 ) >> 15); \
         r+=r2; g+=g2; b+=b2; \
         *dst++=y; \
         u = (( -5527*r - 10921*g + 16448*b ) >> 16) + 128; \
         *dst++=u; \
         v = (( 16448*r - 13783*g - 2665*b ) >> 16 ) + 128; \
         *dst++=y2; \
         *dst++=v; \
      }\
   }
#endif /* HWSCALE_YUY2 direct or indirect */
LOOP()
#else  /* normal */
/* speedup hack for GETPIXEL(src) == (src) */
#if !defined INDIRECT && !defined CONVERT_PIXEL
if (sysdep_display_params.orientation)
{
# define COPY_LINE2(SRC, END, DST) \
    rotate_func(DST, bitmap, y, dirty_area);
  for (y = dirty_area->min_y; y <= dirty_area->max_y; line_dest+=REPS_FOR_Y(CORRECTED_DEST_WIDTH,y,sysdep_display_params.height), y++)
           COPY_LINE_FOR_Y(y, sysdep_display_params.height, 0, bounds_width, line_dest);
}
else
{
# undef COPY_LINE2
# define COPY_LINE2(SRC, END, DST) \
    memcpy(DST, SRC, bounds_width*DEST_PIXEL_SIZE);
  for (y = dirty_area->min_y; line_src < line_end;
    line_dest+=REPS_FOR_Y(CORRECTED_DEST_WIDTH,y,sysdep_display_params.height),
    line_src+=src_width, y++)
      COPY_LINE_FOR_Y(y,sysdep_display_params.height,
                           line_src, line_src+bounds_width, line_dest);
}
#else /* really normal */
#define COPY_LINE2(SRC, END, DST) \
{\
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   for(;src<end;src+=4,dst+=4) \
   { \
      *(dst  ) = GETPIXEL(*(src  )); \
      *(dst+1) = GETPIXEL(*(src+1)); \
      *(dst+2) = GETPIXEL(*(src+2)); \
      *(dst+3) = GETPIXEL(*(src+3)); \
   }\
}
LOOP()
#endif /* speedup hack for GETPIXEL(src) == (src) */
#endif /* packed / yuv / normal */


#undef COPY_LINE2
break;


case 2:
#ifdef PACK_BITS
#define COPY_LINE2(SRC, END, DST) \
{ \
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   for(;src<end; src+=2, dst+=3) \
   { \
      *(dst  ) = (GETPIXEL(*(src  ))    ) | (GETPIXEL(*(src  ))<<24); \
      *(dst+1) = (GETPIXEL(*(src  ))>> 8) | (GETPIXEL(*(src+1))<<16); \
      *(dst+2) = (GETPIXEL(*(src+1))>>16) | (GETPIXEL(*(src+1))<<8); \
   } \
}
#elif defined BLIT_HWSCALE_YUY2
/* HWSCALE_YUY2 has seperate code for direct / indirect, see above */
#ifdef INDIRECT
#define COPY_LINE2(SRC, END, DST) \
   {\
      SRC_PIXEL  *src = SRC; \
      SRC_PIXEL  *end = END; \
      unsigned int *dst = (unsigned int *)DST; \
      for(;src<end;) \
      { \
         *dst++=GETPIXEL(*src++); \
      } \
   }
#else  /* HWSCALE_YUY2, not indirect (for 32 bpp direct color) */
#define COPY_LINE2(SRC, END, DST) \
   {\
      SRC_PIXEL  *src = SRC; \
      SRC_PIXEL  *end = END; \
      unsigned char *dst = (unsigned char *)DST; \
      int r,g,b,y,u,v; \
      for(;src<end;) \
      { \
         r=g=b=*src++; \
         r&=RMASK;  r>>=16; \
         g&=GMASK;  g>>=8; \
         b&=BMASK;  b>>=0; \
         y = (( 9836*r + 19310*g + 3750*b ) >> 15); \
         *dst++=y; \
         u = (( -5527*r - 10921*g + 16448*b ) >> 15) + 128; \
         *dst++=u; \
         v = (( 16448*r - 13783*g - 2665*b ) >> 15 ) + 128; \
         *dst++=y; \
         *dst++=v; \
      }\
   }
#endif /* HWSCALE_YUY2 direct or indirect */
#else /* normal */
#define COPY_LINE2(SRC, END, DST) \
{ \
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   for(;src<end; src+=4, dst+=8) \
   { \
      *(dst+ 1) = *(dst   ) = GETPIXEL(*(src  )); \
      *(dst+ 3) = *(dst+ 2) = GETPIXEL(*(src+1)); \
      *(dst+ 5) = *(dst+ 4) = GETPIXEL(*(src+2)); \
      *(dst+ 7) = *(dst+ 6) = GETPIXEL(*(src+3)); \
   } \
}
#endif /* pack bits */

LOOP()

#undef COPY_LINE2
break;


/* HWSCALE always uses widthscale <=2 for non effect blits */
#ifndef BLIT_HWSCALE_YUY2 
#ifndef PACK_BITS /* no optimised 3x COPY_LINE2 for PACK_BITS */
case 3:
#define COPY_LINE2(SRC, END, DST) \
{ \
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   for(;src<end; src+=4, dst+=12) \
   { \
      *(dst+ 2) = *(dst+ 1) = *(dst   ) = GETPIXEL(*(src  )); \
      *(dst+ 5) = *(dst+ 4) = *(dst+ 3) = GETPIXEL(*(src+1)); \
      *(dst+ 8) = *(dst+ 7) = *(dst+ 6) = GETPIXEL(*(src+2)); \
      *(dst+11) = *(dst+10) = *(dst+ 9) = GETPIXEL(*(src+3)); \
   } \
}

LOOP()

#undef COPY_LINE2
break;
#endif /* PACK_BITS (no optimised 3x COPY_LINE2 for PACK_BITS) */


default:
#ifdef PACK_BITS
#define COPY_LINE2(SRC, END, DST) \
{ \
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   DEST_PIXEL pixel; \
   int i, step=0; \
   for(;src<end;src++) \
   { \
      pixel = GETPIXEL(*src); \
      for(i=0; i<sysdep_display_params.widthscale; i++,step=(step+1)&3) \
      { \
         switch(step) \
         { \
            case 0: \
               *(dst  )  = pixel; \
               break; \
            case 1: \
               *(dst  ) |= pixel << 24; \
               *(dst+1)  = pixel >> 8; \
               break; \
            case 2: \
               *(dst+1) |= pixel << 16; \
               *(dst+2)  = pixel >> 16; \
               break; \
            case 3: \
               *(dst+2) |= pixel << 8; \
               dst+=3; \
               break; \
         } \
      } \
   } \
}
#else
#define COPY_LINE2(SRC, END, DST) \
{ \
   SRC_PIXEL  *src = SRC; \
   SRC_PIXEL  *end = END; \
   DEST_PIXEL *dst = DST; \
   int i; \
   for(;src<end;src++) \
   { \
      const DEST_PIXEL v = GETPIXEL(*(src)); \
      i=(sysdep_display_params.widthscale+7)/8; \
      dst+=sysdep_display_params.widthscale&7; \
      switch (sysdep_display_params.widthscale&7) \
      { \
         case 0: do{  dst+=8; \
                      *(dst-8) = v; \
         case 7:      *(dst-7) = v; \
         case 6:      *(dst-6) = v; \
         case 5:      *(dst-5) = v; \
         case 4:      *(dst-4) = v; \
         case 3:      *(dst-3) = v; \
         case 2:      *(dst-2) = v; \
         case 1:      *(dst-1) = v; \
                 } while(--i>0); \
      } \
   } \
}
#endif

LOOP()

#undef COPY_LINE2
break;
#endif /* ifndef BLIT_HWSCALE_YUY2  */


    } /* end switch(widtscale) */
    break;

/* these aren't used by the effect code, so undef them now */
#undef REPS_FOR_Y
#undef COPY_LINE_FOR_Y
#undef LOOP
#undef GETPIXEL
#undef DEST_PIXEL_SIZE

#include "blit_effect.h"

  } /* end switch(effect) */

/* clean up */
#undef CORRECTED_DEST_WIDTH
