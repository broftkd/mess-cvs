/***************************************************************************

	vidhrdw/cirrus.c

	Cirrus SVGA card emulation (preliminary)

	Cirrus has the following additional registers that are not present in
	conventional VGA:

	SEQ 06h:		Unlock Cirrus registers; write 12h to unlock registers,
					and	read 12h back to confirm Cirrus presence.

	SEQ 07h
		bit 3-1:	Pixel depth
						0x00	8 bpp
						0x02	16 bpp (double vert clock)
						0x04	24 bpp
						0x06	16 bpp
						0x08	32 bpp
		bit 0:		VGA/SVGA (0=VGA, 1=SVGA)
					
	GC 09h:			Set 64k bank (bits 3-0 only)

***************************************************************************/

#include "cirrus.h"

static void cirrus_update_8bpp(struct mame_bitmap *bitmap, struct crtc6845 *crtc)
{
	UINT16 *line;
	const UINT8 *vram;
	int y, x;

	vram = (const UINT8 *) pc_vga_memory();

	for (y = 0; y < 480; y++)
	{
		line = (UINT16 *) bitmap->line[y];

		for (x = 0; x < 640; x++)
			*line++ = *vram++;
	}
}



static void cirrus_update_16bpp(struct mame_bitmap *bitmap, struct crtc6845 *crtc)
{
	osd_die("NYI");
}



static void cirrus_update_24bpp(struct mame_bitmap *bitmap, struct crtc6845 *crtc)
{
	osd_die("NYI");
}



static void cirrus_update_32bpp(struct mame_bitmap *bitmap, struct crtc6845 *crtc)
{
	osd_die("NYI");
}



static pc_video_update_proc cirrus_choosevideomode(const UINT8 *sequencer,
	const UINT8 *crtc, const UINT8 *gc, int *width, int *height)
{
	pc_video_update_proc proc = NULL;

	if ((sequencer[0x06] == 0x12) && (sequencer[0x07] & 0x01))
	{
		*width = 640;
		*height = 480;

		switch(sequencer[0x07] & 0x0E)
		{
			case 0x00:	proc = cirrus_update_8bpp; break;
			case 0x02:	proc = cirrus_update_16bpp; break;
			case 0x04:	proc = cirrus_update_24bpp; break;
			case 0x06:	proc = cirrus_update_16bpp; break;
			case 0x08:	proc = cirrus_update_32bpp; break;
		}
	}
	return proc;
}



const struct pc_svga_interface cirrus_svga_interface =
{
	2 * 1024 * 1024,	/* 2 megs vram */
	8,					/* 8 sequencer registers */
	10,					/* 10 gc registers */
	25,					/* 25 crtc registers */
	cirrus_choosevideomode
};



/*************************************
 *
 *	PCI card interface
 *
 *************************************/

static data32_t cirrus5430_pci_read(int function, int offset)
{
	data32_t result;

	if (function != 0)
		return 0;

	switch(offset)
	{
		case 0x00:	/* vendor/device ID */
			result = 0x00A01013;
			break;

		case 0x08:
			result = 0x03000000;
			break;

		case 0x10:
			result = 0xD0000000;
			break;

		default:
			result = 0;
			break;
	}
	return result;
}



static void cirrus5430_pci_write(int function, int offset, data32_t data)
{
}



const struct pci_device_info cirrus5430_callbacks =
{
	cirrus5430_pci_read,
	cirrus5430_pci_write
};



