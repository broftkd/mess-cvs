/***************************************************************************

  vidhrdw/odyssey2.c

  Routines to control the Adventurevision video hardware

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "includes/odyssey2.h"


/* character sprite colors
   dark grey, red, green, orange, blue, violet, light grey, white
   dark back / grid colors
   black, dark blue, dark green, light green, red, violet, orange, light grey
   light back / grid colors
   black, blue, green, light green, red, violet, orange, light grey */

UINT8 odyssey2_colors[24][3]={
  /* Background,Grid Dim */
  { 0x00,0x00,0x00 }, 
  { 0x00,0x00,0xFF },   /* Blue */
  { 0x00,0x80,0x00 },   /* DK Green */
  { 0xff,0x9b,0x60 },
  { 0xCC,0x00,0x00 },   /* Red */
  { 0xa9,0x80,0xff }, 
  { 0x82,0xfd,0xdb }, 
  { 0xFF,0xFF,0xFF },

  /* Background,Grid Bright */

  { 0x80,0x80,0x80 }, 
  { 0x50,0xAE,0xFF },   /* Blue */
  { 0x00,0xFF,0x00 },   /* Dk Green */
  { 0x82,0xfb,0xdb },   /* Lt Grey */
  { 0xEC,0x02,0x60 },   /* Red */
  { 0xa9,0x80,0xff },   /* Violet */
  { 0xff,0x9b,0x60 },   /* Orange */
  { 0xFF,0xFF,0xFF },

  /* Character,Sprite colors */
  { 0x71,0x71,0x71 },   /* Dark Grey */
  { 0xFF,0x80,0x80 },   /* Red */
  { 0x00,0xC0,0x00 },   /* Green */
  { 0xff,0x9b,0x60 },   /* Orange */
  { 0x50,0xAE,0xFF },   /* Blue */
  { 0xa9,0x80,0xff },   /* Violet */
  { 0x82,0xfb,0xdb },   /* Lt Grey */
  { 0xff,0xff,0xff }    /* White */
};

UINT8 o2_shape[0x40][8]={
    { 0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00 }, // 0
    { 0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00 },
    { 0x3C,0x66,0x0C,0x18,0x30,0x60,0x7E,0x00 },
    { 0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00 },
    { 0xCC,0xCC,0xCC,0xFE,0x0C,0x0C,0x0C,0x00 },
    { 0xFE,0xC0,0xC0,0x7C,0x06,0xC6,0x7C,0x00 },
    { 0x7C,0xC6,0xC0,0xFC,0xC6,0xC6,0x7C,0x00 },
    { 0xFE,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00 },
    { 0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00 },
    { 0x7C,0xC6,0xC6,0x7E,0x06,0xC6,0x7C,0x00 },
    { 0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00 },
    { 0x18,0x7E,0x58,0x7E,0x58,0x7E,0x18,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x3C,0x66,0x0C,0x18,0x18,0x00,0x18,0x00 },
    { 0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0x00 },
    { 0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0,0x00 },
    { 0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00 },
    { 0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00 },
    { 0xFE,0xC0,0xC0,0xF8,0xC0,0xC0,0xFE,0x00 },
    { 0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0x00 },
    { 0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00 },
    { 0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00 },
    { 0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00 },
    { 0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00 },
    { 0x7C,0xC6,0xC6,0xC6,0xDE,0xCC,0x76,0x00 },
    { 0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00 },
    { 0xFC,0xC6,0xC6,0xC6,0xC6,0xC6,0xFC,0x00 },
    { 0xFE,0xC0,0xC0,0xF8,0xC0,0xC0,0xC0,0x00 },
    { 0x7C,0xC6,0xC0,0xC0,0xCE,0xC6,0x7E,0x00 },
    { 0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00 },
    { 0x06,0x06,0x06,0x06,0x06,0xC6,0x7C,0x00 },
    { 0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00 },
    { 0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00 },
    { 0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00 },
    { 0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00 },
    { 0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00 },
    { 0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00 },
    { 0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00 },
    { 0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00 },
    { 0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00 },
    { 0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,0x00 },
    { 0x00,0x18,0x00,0x7E,0x00,0x18,0x00,0x00 },
    { 0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00 },
    { 0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00 },
    { 0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0x00 },
    { 0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00 },
    { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00 },
    { 0xCE,0xDB,0xDB,0xDB,0xDB,0xDB,0xCE,0x00 },
    { 0x00,0x00,0x3C,0x7E,0x7E,0x7E,0x3C,0x00 },
    { 0x1C,0x1C,0x18,0x1E,0x18,0x18,0x1C,0x00 },
    { 0x1C,0x1C,0x18,0x1E,0x18,0x34,0x26,0x00 },
    { 0x38,0x38,0x18,0x78,0x18,0x2C,0x64,0x00 },
    { 0x38,0x38,0x18,0x78,0x18,0x18,0x38,0x00 },
    { 0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00 },
    { 0x18,0x3C,0x7E,0xFF,0xFF,0x18,0x18,0x00 },
    { 0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF,0x00 },
    { 0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0x00 },
    { 0x38,0x38,0x12,0xFE,0xB8,0x28,0x6C,0x00 },
    { 0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x00 },
    { 0x00,0x00,0x0C,0x08,0x08,0x7F,0x3E,0x00 },
    { 0x00,0x03,0x63,0xFF,0xFF,0x18,0x08,0x00 },
    { 0x00,0x00,0x00,0x10,0x38,0xFF,0x7E,0x00 }
};


static UINT8 *odyssey2_display;
int odyssey2_vh_hpos;

union {
    UINT8 reg[0x100];
    struct {
	struct {
	    UINT8 y,x,color,res;
	} sprites[4];
	struct {
	    UINT8 y,x,ptr,color;
	} foreground[12];
	struct {
	    struct {
		UINT8 y,x,ptr,color;
	    } single[4];
	} quad[4];
	UINT8 shape[4][8];
	UINT8 control;
	UINT8 status;
	UINT8 collision;
	UINT8 color;
	UINT8 y;
	UINT8 x;
	UINT8 res;
	UINT8 shift1,shift2,shift3;
	UINT8 sound;
	UINT8 res2[5+0x10];
	UINT8 hgrid[2][0x10];
	UINT8 vgrid[0x10];
    } s;
} o2_vdc= { { 0 } };


/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/
int odyssey2_vh_start(void)
{
    odyssey2_vh_hpos = 0;
	odyssey2_display = (UINT8 *)malloc(8 * 8 * 256);
	if( !odyssey2_display )
		return 1;
	memset(odyssey2_display, 0, 8 * 8 * 256);
    return 0;
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/

void odyssey2_vh_init_palette(unsigned char *game_palette, unsigned short *game_colortable,const unsigned char *color_prom)
{
    game_colortable[0] = 0;
    game_colortable[1] = 1;
    memcpy (game_palette, odyssey2_colors, sizeof(odyssey2_colors));
}

void odyssey2_vh_stop(void)
{
	if( odyssey2_display )
		free(odyssey2_display);
	odyssey2_display = NULL;
}

extern READ_HANDLER ( odyssey2_video_r )
{
    switch (offset) {
    case 0xa1: return 8;
    }
    return o2_vdc.reg[offset];
}

extern WRITE_HANDLER ( odyssey2_video_w )
{
    o2_vdc.reg[offset]=data;
}

extern READ_HANDLER ( odyssey2_t1_r )
{
    static bool t=FALSE;
    t=!t;
    return t;
}


void odyssey2_draw_grid(struct osd_bitmap *bitmap)
{
}

/***************************************************************************

  Refresh the video screen

***************************************************************************/

void odyssey2_vh_screenrefresh(struct osd_bitmap *bitmap, int full_refresh)
{
    int i, j, k, offset, x, y;
    int color;

    plot_box(bitmap, 0, 0, bitmap->width, bitmap->height, Machine->pens[(o2_vdc.s.color>>3)&0xf]);
    if (o2_vdc.s.control&8) {
#define WIDTH 16
#define HEIGHT 28
#define THICK 4
	int w=THICK;
	color=o2_vdc.s.color&7;
	color|=(o2_vdc.s.color>>3)&8;
	for (i=0, x=0; x<WIDTH*9; x+=WIDTH, i++) {
	    for (j=1, y=0; y<HEIGHT*9; y+=HEIGHT, j<<=1) {
	    if ( (j<=0x80)&&(o2_vdc.s.hgrid[0][i]&j)
		 ||(j>0x80)&&(o2_vdc.s.hgrid[1][i]&1) )
		plot_box(bitmap,x,y,WIDTH+THICK,w,Machine->pens[color]);
	    }
	}
	if (o2_vdc.s.control&0x80) w=WIDTH;
	for (i=0, x=0; x<WIDTH*10; x+=WIDTH, i++) {
	    for (j=1, y=0; y<HEIGHT*8; y+=HEIGHT, j<<=1) {
		if (o2_vdc.s.vgrid[i]&j)
		    plot_box(bitmap,x,/*12+*/y,w,HEIGHT+1,Machine->pens[color]);
	    }
	}
    }
    if (o2_vdc.s.control&0x20) {
#if 1
	for (i=0; i<ARRAY_LENGTH(o2_vdc.s.foreground); i++) {
	    offset=o2_vdc.s.foreground[i].ptr|((o2_vdc.s.foreground[i].color&1)<<8);
	    offset=(offset+(o2_vdc.s.foreground[i].y>>1))&0x1ff;
	    Machine->gfx[0]->colortable[1]=Machine->pens[16+((o2_vdc.s.foreground[i].color&0xe)>>1)];
	    for (j=0; j<8; j++) {
		drawgfx(bitmap, Machine->gfx[0], ((char*)o2_shape)[offset],0,
			0,0,o2_vdc.s.foreground[i].x,o2_vdc.s.foreground[i].y+j,
			0, TRANSPARENCY_PEN,0);	
		offset=(offset+1)&0x1ff;
	    }
	}
#endif
#if 1
	for (i=0; i<ARRAY_LENGTH(o2_vdc.s.quad); i++) {
	    x=o2_vdc.s.quad[i].single[0].x;
	    y=o2_vdc.s.quad[i].single[0].y;
	    for (j=0; j<ARRAY_LENGTH(o2_vdc.s.quad[0].single); j++, x+=2*8) {
		offset=o2_vdc.s.quad[i].single[j].ptr|((o2_vdc.s.quad[i].single[j].color&1)<<8);
		offset=(offset+(o2_vdc.s.quad[i].single[0].y>>1))&0x1ff;
		Machine->gfx[0]->colortable[1]=Machine->pens[16+((o2_vdc.s.quad[i].single[j].color&0xe)>>1)];
		for (k=0; k<8; k++) {
		    drawgfx(bitmap, Machine->gfx[0], ((char*)o2_shape)[offset],0,
			    0,0,x,y+k,
			    0, TRANSPARENCY_PEN,0);	
		    offset=(offset+1)&0x1ff;
		}
	    }
	}
#endif
#if 1
	for (i=0; i<ARRAY_LENGTH(o2_vdc.s.sprites); i++) {
	    Machine->gfx[0]->colortable[1]=Machine->pens[16+((o2_vdc.s.sprites[i].color>>3)&7)];
	    y=o2_vdc.s.sprites[i].y;
	    for (j=0; j<8; j++) {		
		if (o2_vdc.s.sprites[i].color&4) {
		    if (y+2*j>=bitmap->height) break;
		    drawgfxzoom(bitmap, Machine->gfx[0], o2_vdc.s.shape[i][j],0,
				0,0,o2_vdc.s.sprites[i].x,y+j*2,
				0, TRANSPARENCY_PEN,0,0x20000, 0x20000);
		} else {
		    if (y+j>=bitmap->height) break;
		    drawgfx(bitmap, Machine->gfx[0], o2_vdc.s.shape[i][j],0,
			    0,0,o2_vdc.s.sprites[i].x,y+j,
			    0, TRANSPARENCY_PEN,0);
		}
	    }
	}
#endif
    }
#if 0
#if 1
    Machine->gfx[0]->colortable[1]=Machine->pens[23];
    for (i=0; i<0x20; i++) {
	drawgfx(bitmap, Machine->gfx[0], o2_vdc.s.shape[i/8][i&7],0,
		0,0,240,i,
		0, TRANSPARENCY_PEN,0);
    }
#endif

    char str[0x40];
    for (i=0; i<4; i++) {
	snprintf(str, sizeof(str), "%.2x:%.2x %.2x:%.2x",
		 o2_vdc.s.sprites[i].x,
		 o2_vdc.s.sprites[i].y,
		 o2_vdc.s.sprites[i].color,
		 o2_vdc.s.sprites[i].color);
	ui_text(bitmap, str, 160, i*8);
    }


#endif
    return;
}


