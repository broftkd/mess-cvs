/***************************************************************************

    M.A.M.E.32  -  Multiple Arcade Machine Emulator for Win32
    Win32 Portions Copyright (C) 1997-98 Michael Soderstrom and Chris Kirmse
    
    This file is part of MAME32, and may only be used, modified and
    distributed under the terms of the MAME license, in "readme.txt".
    By continuing to use, modify or distribute this file you indicate
    that you have read the license and understand and accept it fully.

 ***************************************************************************/

#ifndef __DIRECTORIES_H__
#define __DIRECTORIES_H__

#ifdef MESS
#define HASDIR_SAMPLE	0
#ifdef MESS_PICKER
#define HASDIR_SOFTWARE	1
#else
#define HASDIR_SOFTWARE	0
#endif
#else
#define HASDIR_SAMPLE	1
#define HASDIR_SOFTWARE	0
#endif

/* Dialog return codes */
#define DIRDLG_ROMS         0x0010
#define DIRDLG_SAMPLES      0x0020
#define DIRDLG_CFG          0x0040
#define DIRDLG_HI           0x0080
#define DIRDLG_IMG          0x0100
#define DIRDLG_INP          0x0200

#if HASDIR_SOFTWARE
#define DIRDLG_SOFTWARE		0x0400
#endif

INT_PTR CALLBACK DirectoriesDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);

#endif
