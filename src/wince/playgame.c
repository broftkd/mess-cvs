#include <windows.h>
#include <stdio.h>
#include "mamece.h"
#include "driver.h"
#include "rc.h"
#include "..\windows\window.h"

/* ------------------------------------------------------------------------*/
#ifdef MESS
LPTSTR messages = NULL;

static int DECL_SPEC wince_mess_vprintf(char *fmt, va_list arg)
{
	int length;
	int old_length;
	LPTSTR new_messages;
	LPCTSTR tbuffer;
	char buffer[256];

	/* get into a buffer */
	length = vsnprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), fmt, arg);
	tbuffer = A2T(buffer);

	/* output to debugger */
	OutputDebugString(tbuffer);

	/* append the buffer to the rest of the messages */
	old_length = messages ? tcslen(messages) : 0;
	new_messages = realloc(messages, (old_length + tcslen(tbuffer) + 1) * sizeof(TCHAR));
	if (new_messages)
	{
		messages = new_messages;
		tcscpy(messages + old_length, tbuffer);
	}
	return length;
}

LPTSTR get_mess_output(void)
{
	LPTSTR m;
	m = messages;
	messages = NULL;
	return m;
}

#endif /* MESS */
/* ------------------------------------------------------------------------*/

char *rompath_extra = NULL;

#define localconcat(s1, s2)	((s1) ? ((s2) ? __localconcat(_alloca(strlen(s1) + strlen(s2) + 1), (s1), (s2)) : (s1)) : (s2))

static char *__localconcat(char *dest, const char *s1, const char *s2)
{
	strcpy(dest, s1);
	strcat(dest, s2);
	return dest;
}

static void get_mame_root(TCHAR *buffer, int buflen)
{
#ifdef _X86_
	// To make things simple when running under emulation
	sntprintf(buffer, buflen, TEXT("\\"));
#else
	GetModuleFileName(NULL, buffer, buflen);
#endif
	tcsrchr(buffer, '\\')[0] = '\0';
}

static void set_fileio_opt(const char *rootpath, const char *opt, const char *relpath)
{
	extern struct rc_option fileio_opts[1];
	rc_set_option2(fileio_opts, opt, localconcat(rootpath, relpath), MAXINT_PTR);
}

void setup_paths()
{
	TCHAR rootpath_buffer[MAX_PATH];
	char *rootpath;

	get_mame_root(rootpath_buffer, sizeof(rootpath_buffer) / sizeof(rootpath_buffer[0]));
	rootpath = T2A(rootpath_buffer);

#ifdef MESS
	set_fileio_opt(rootpath, "biospath", "\\Bios");
#else
	set_fileio_opt(rootpath, "rompath", "\\ROMs");
#endif
	set_fileio_opt(rootpath, "samplepath",			"\\Samples");
	set_fileio_opt(rootpath, "cfg_directory",			"\\Cfg");
	set_fileio_opt(rootpath, "nvram_directory",		"\\Nvram");
	set_fileio_opt(rootpath, "memcard_directory",		"\\Memcard");
	set_fileio_opt(rootpath, "input_directory",		"\\Inp");
	set_fileio_opt(rootpath, "hiscore_directory",		"\\Hi");
	set_fileio_opt(rootpath, "artwork_directory",		"\\Artwork");
	set_fileio_opt(rootpath, "snapshot_directory",	"\\Snap");
#ifdef MESS
	set_fileio_opt(rootpath, "cheat_file",			"\\cheat.cdb");
#else
	set_fileio_opt(rootpath, "cheat_file",			"\\cheat.dat");
#endif
	set_fileio_opt(rootpath, "history_file",			"\\history.dat");
	set_fileio_opt(rootpath, "mameinfo_file",			"\\mameinfo.dat");
}

int play_game(int game_index, struct ui_options *opts)
{
	extern int use_dirty;
	extern int throttle;
	extern float gamma_correct;
	extern int attenuation;
	int original_leds;
	int err;

	options.antialias = opts->enable_antialias;
	options.translucency = opts->enable_translucency;
	options.rol = opts->rotate_left;
	options.ror = opts->rotate_right;
	options.samplerate = opts->enable_sound ? 44100 : 0;
	options.use_samples = 1;
	options.use_filter = 1;
	options.vector_flicker = (float) (opts->enable_flicker ? 255 : 0);
	options.vector_width = win_gfx_width;
	options.vector_height = win_gfx_height;
	options.color_depth = 0;
	options.norotate = 0;
	options.flipx = 0;
	options.flipy = 0;
	options.beam = 0x00010000;
	options.use_artwork = 0;
	options.cheat = 0;
	options.mame_debug = 0;
	options.playback = NULL;
	options.record = NULL;
	options.gui_host = 1;

#ifdef MESS
	options.mess_printf_output = wince_mess_vprintf;
#endif

	gamma_correct = 1;
	attenuation = 0;
	use_dirty = opts->enable_dirtyline;
	throttle = !opts->disable_throttle;
	win_window_mode = 0;

	/* remember the initial LED states */
	original_leds = osd_get_leds();

	#ifdef MESS
	/* Build the CRC database filename */
/*	sprintf(crcfilename, "%s/%s.crc", crcdir, drivers[game_index]->name);
	if (drivers[game_index]->clone_of->name)
		sprintf (pcrcfilename, "%s/%s.crc", crcdir, drivers[game_index]->clone_of->name);
	else
		pcrcfilename[0] = 0;
  */  #endif

	err = run_game(game_index);

	/* restore the original LED state */
	osd_set_leds(original_leds);

	return err;
}

//============================================================
//	osd_init
//============================================================

int osd_init(void)
{
	extern int win_init_input(void);
	int result;

	result = win_init_window();
	if (result) {
		logerror("win_init_window() failed!\n");
		return result;
	}

	result = win_init_input();
	if (result) {
		logerror("win_init_input() failed!\n");
		return result;
	}

	return 0;
}



//============================================================
//	osd_exit
//============================================================

void osd_exit(void)
{
	extern void win_shutdown_input(void);
	extern void win_shutdown_window(void);

	win_shutdown_input();
	win_shutdown_window();
	osd_set_leds(0);
}


//============================================================
//	osd_display_loading_rom_message
//============================================================

int osd_display_loading_rom_message (const char *name, int current, int total)
{
	return 0;
}

//============================================================
//	logerror
//============================================================

void CLIB_DECL logerror(const char *text,...)
{
#ifdef _X86_
	va_list arg;

	/* standard vfprintf stuff here */
	va_start(arg, text);

	{
		char szBuffer[256];
		_vsnprintf(szBuffer, sizeof(szBuffer) / sizeof(szBuffer[0]), text, arg);
		OutputDebugString(A2T(szBuffer));
	}
	va_end(arg);
#endif
}
