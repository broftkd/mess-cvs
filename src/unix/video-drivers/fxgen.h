#ifndef __FXGEN_H
#define __FXGEN_H

#include "sysdep/sysdep_display.h"

int  InitGlide(void);
void ExitGlide(void);
int  InitParams(void);
int  InitVScreen(int reopen);
void CloseVScreen(void);
void VScreenCatchSignals(void);
void VScreenRestoreSignals(void);
const char * xfx_update_display(struct mame_bitmap *bitmap,
	  struct rectangle *vis_area,  struct rectangle *dirty_area,
	  struct sysdep_palette_struct *palette,
	  int flags);

extern struct rc_option	fx_opts[];

#endif
