/*
 * file devices.c
 *
 * Routines for Pointers device processing
 *
 * Joystick and Mouse
 *
 * original idea from Chris Sharp <sharp@uk.ibm.com>
 *
 */

/*
 * updates by sdevaux <sebastien.devaux@laposte.net>
 */

#define __DEVICES_C_

#ifdef UGCICOIN
#include <ugci.h>
#endif

#include "xmame.h"
#include "devices.h"
#include "keycodes.h"
#include "driver.h"
#include "ui_text.h"
#include "sysdep/rc.h"
#include "sysdep/fifo.h"


/*============================================================ */
/*	IMPORTS */
/*============================================================ */

extern int verbose;


/*============================================================ */
/*	MACROS */
/*============================================================ */

#define STRUCTSIZE(x)		((dinput_version == 0x0300) ? sizeof(x##_DX3) : sizeof(x))
#define ELEMENTS(x)		(sizeof(x) / sizeof((x)[0]))



/*============================================================ */
/*	GLOBAL VARIABLES */
/*============================================================ */

UINT8				trying_to_quit;



/*============================================================ */
/*	LOCAL VARIABLES */
/*============================================================ */

/* this will be filled in dynamically */
static struct OSCodeInfo	codelist[KEY_CODES + JOY_CODES];
static int			total_codes;

/* Controller override options */
static float			a2d_deadzone;
static int			steadykey;
static int			analogstick;
static int			ugcicoin;
static int			dummy;

/* additional key data */
static INT8			key[KEY_CODES];
static INT8			oldkey[KEY_CODES];
static INT8			currkey[KEY_CODES];
struct kbd_fifo_struct;
static struct kbd_fifo_struct *kbd_fifo = NULL;

/* joystick states */
static UINT8			joystick_digital[JOY_MAX][JOY_AXES];

/*============================================================ */
/*	OPTIONS */
/*============================================================ */

/* prototypes */
static int decode_digital(struct rc_option *option, const char *arg, int priority);

/* global input options */
struct rc_option input_opts[] =
{
	/* name, shortname, type, dest, deflt, min, max, func, help */
	{ "Input device options", NULL, rc_seperator, NULL, NULL, 0, 0, NULL, NULL },
	{ "joytype", "jt", rc_int, &joytype, "0", 0, 7, NULL, "Select type of joystick support to use:\n"
		"0 No joystick\n"
		"1 i386 style joystick driver (if compiled in)\n"
		"2 Fm Town Pad support (if compiled in)\n"
		"3 X11 input extension joystick (if compiled in)\n"
		"4 new i386 linux 1.x.x joystick driver(if compiled in)\n"
		"5 NetBSD USB joystick driver (if compiled in)\n"
		"6 PS2-Linux native pad (if compiled in)\n"
		"7 SDL joystick driver" },
	{ "analogstick", "as", rc_bool, &analogstick, "0", 0, 0, NULL, "Use Joystick as analog for analog controls" },
#ifdef I386_JOYSTICK
	{ NULL, NULL, rc_link, joy_i386_opts, NULL, 0, 0, NULL, NULL },
#endif
#ifdef LIN_FM_TOWNS
	{ NULL, NULL, rc_link, joy_pad_opts, NULL, 0, 0, NULL, NULL },
#endif
#ifdef X11_JOYSTICK
	{ NULL, NULL, rc_link, joy_x11_opts, NULL, 0, 0, NULL, NULL },
#endif
#ifdef USB_JOYSTICK
	{ NULL, NULL, rc_link, joy_usb_opts, NULL, 0, 0, NULL, NULL },
#endif
#ifdef PS2_JOYSTICK
	{ NULL, NULL, rc_link, joy_ps2_opts, NULL, 0, 0, NULL, NULL },
#endif
#ifdef USE_XINPUT_DEVICES
	{ NULL, NULL, rc_link, XInputDevices_opts, NULL, 0, 0, NULL, NULL },
#endif
	{ "mouse", NULL, rc_bool, &use_mouse, "0", 0, 0, NULL, "Enable mouse input" },
	{ "ugcicoin", NULL, rc_bool, &ugcicoin, "0", 0, 0, NULL, "Enable/disable UGCI(tm) Coin/Play support" },
#ifdef USE_LIGHTGUN_ABS_EVENT
	{ NULL, NULL, rc_link, lightgun_abs_event_opts, NULL, 0, 0, NULL, NULL },
#endif
	{ "steadykey", "steady", rc_bool, &steadykey, "0", 0, 0, NULL, "Enable steadykey support" },
	{ "a2d_deadzone", "a2d", rc_float, &a2d_deadzone, "0.3", 0.0, 1.0, NULL, "Minimal analog value for digital input" },
	{ "ctrlr", NULL, rc_string, &options.controller, 0, 0, 0, NULL, "Preconfigure for specified controller" },
	{ "digital", NULL, rc_string, &dummy, "none", 1, 0, decode_digital, "Mark certain joysticks or axes as digital (none|all|j<N>*|j<N>a<M>[,...])" },
	{ "usbpspad", "pspad", rc_bool, &is_usb_ps_gamepad, "0", 0, 0, NULL, "The joystick(s) are USB PS gamepads" },
	{ "rapidfire", "rapidf", rc_bool, &rapidfire_enable, "0", 0, 0, NULL, "Enable rapid-fire support for joysticks" },
	{ NULL,	NULL, rc_end, NULL, NULL, 0, 0,	NULL, NULL }
};

#ifdef UGCICOIN

#define PLAY_KEYCODE_BASE     KEY_1
#define COIN_KEYCODE_BASE     KEY_3
#define MAX_PLAYERS           2
#define MIN_COIN_WAIT         3

static int coin_pressed[MAX_PLAYERS];

static void ugci_callback(int id, enum ugci_event_type type, int value)
{
	struct xmame_keyboard_event event;

	if (id >= MAX_PLAYERS)
		return;

	switch (type)
	{
		case UGCI_EVENT_PLAY:
			event.press = value;
			event.scancode = PLAY_KEYCODE_BASE + id;
			xmame_keyboard_register_event(&event);
			break;

		case UGCI_EVENT_COIN:
			if (coin_pressed[id])
				return;
			event.press = coin_pressed[id] = 1;
			event.scancode = COIN_KEYCODE_BASE + id;
			xmame_keyboard_register_event(&event);
			break;

		default:
			break;
	}
}
#endif

void load_rapidfire_settings(void)
{
	FILE *fp;
	char name[BUF_SIZE];

	if (!rapidfire_enable)
		return;

	snprintf(name, BUF_SIZE, "%s/.%s/cfg/%s.rpf", home_dir, NAME, Machine->gamedrv->name);
	fp = fopen(name, "rb");
	if (fp)
	{
		int i,j;

		for (i=0; i<4; i++)
		{
			fread(&rapidfire_data[i].ctrl_button, sizeof(rapidfire_data[0].ctrl_button), 1, fp);
			for (j=0; j<10; j++)
			{
				fread(&rapidfire_data[i].setting[j], sizeof(rapidfire_data[0].setting[0]), 1, fp);
			}
		}
		fclose(fp);
	}
}

void save_rapidfire_settings(void)
{
	FILE *fp;
	char name[BUF_SIZE];

	if (!rapidfire_enable)
		return;

	snprintf(name, BUF_SIZE, "%s/.%s/cfg/%s.rpf", home_dir, NAME, Machine->gamedrv->name);
	fp = fopen(name, "wb");
	if (fp)
	{
		int i,j;
		for (i=0; i<4; i++)
		{
			fwrite(&rapidfire_data[i].ctrl_button, sizeof(rapidfire_data[0].ctrl_button), 1, fp);
			for (j=0; j<10; j++)
			{
				fwrite(&rapidfire_data[i].setting[j], sizeof(rapidfire_data[0].setting[0]), 1, fp);
			}
		}
		fclose(fp);
	}
}



/*============================================================ */
/*	PROTOTYPES */
/*============================================================ */

/* private methods */
FIFO(INLINE, kbd, struct xmame_keyboard_event)

static void updatekeyboard(void);
static void init_keycodes(void);
static void init_joycodes(void);

void process_ctrlr_file(struct rc_struct *iptrc, const char *ctype, const char *filename);


/*============================================================ */
/*	KEYBOARD/JOYSTICK LIST */
/*============================================================ */

/* macros for building/mapping joystick codes */
#define JOYCODE(joy, type, index)	((index) | ((type) << 8) | ((joy) << 12) | 0x80000000)
#define JOYINDEX(joycode)		((joycode) & 0xff)
#define CODETYPE(joycode)		(((joycode) >> 8) & 0xf)
#define JOYNUM(joycode)			(((joycode) >> 12) & 0xf)

/* macros for differentiating the two */
#define IS_KEYBOARD_CODE(code)		(((code) & 0x80000000) == 0)
#define IS_JOYSTICK_CODE(code)		(((code) & 0x80000000) != 0)

/* joystick types */
#define CODETYPE_KEYBOARD			0
#define CODETYPE_AXIS_NEG			1
#define CODETYPE_AXIS_POS			2
#define CODETYPE_POV_UP				3
#define CODETYPE_POV_DOWN			4
#define CODETYPE_POV_LEFT			5
#define CODETYPE_POV_RIGHT			6
#define CODETYPE_BUTTON				7
#define CODETYPE_JOYAXIS			8
#define CODETYPE_MOUSEAXIS			9
#define CODETYPE_MOUSEBUTTON			10
#define CODETYPE_GUNAXIS			11

/* master joystick translation table */
static int joy_trans_table[][2] =
{
	/* internal code			MAME code */
	{ JOYCODE(0, CODETYPE_AXIS_NEG, 0),	JOYCODE_1_LEFT },
	{ JOYCODE(0, CODETYPE_AXIS_POS, 0),	JOYCODE_1_RIGHT },
	{ JOYCODE(0, CODETYPE_AXIS_NEG, 1),	JOYCODE_1_UP },
	{ JOYCODE(0, CODETYPE_AXIS_POS, 1),	JOYCODE_1_DOWN },
	{ JOYCODE(0, CODETYPE_BUTTON, 0),	JOYCODE_1_BUTTON1 },
	{ JOYCODE(0, CODETYPE_BUTTON, 1),	JOYCODE_1_BUTTON2 },
	{ JOYCODE(0, CODETYPE_BUTTON, 2),	JOYCODE_1_BUTTON3 },
	{ JOYCODE(0, CODETYPE_BUTTON, 3),	JOYCODE_1_BUTTON4 },
	{ JOYCODE(0, CODETYPE_BUTTON, 4),	JOYCODE_1_BUTTON5 },
	{ JOYCODE(0, CODETYPE_BUTTON, 5),	JOYCODE_1_BUTTON6 },
	{ JOYCODE(0, CODETYPE_BUTTON, 6),	JOYCODE_1_BUTTON7 },
	{ JOYCODE(0, CODETYPE_BUTTON, 7),	JOYCODE_1_BUTTON8 },
	{ JOYCODE(0, CODETYPE_BUTTON, 8),	JOYCODE_1_BUTTON9 },
	{ JOYCODE(0, CODETYPE_BUTTON, 9),	JOYCODE_1_BUTTON10 },
	{ JOYCODE(0, CODETYPE_JOYAXIS, 0),	JOYCODE_1_ANALOG_X },
	{ JOYCODE(0, CODETYPE_JOYAXIS, 1),	JOYCODE_1_ANALOG_Y },
	{ JOYCODE(0, CODETYPE_JOYAXIS, 2),	JOYCODE_1_ANALOG_Z },

	{ JOYCODE(1, CODETYPE_AXIS_NEG, 0),	JOYCODE_2_LEFT },
	{ JOYCODE(1, CODETYPE_AXIS_POS, 0),	JOYCODE_2_RIGHT },
	{ JOYCODE(1, CODETYPE_AXIS_NEG, 1),	JOYCODE_2_UP },
	{ JOYCODE(1, CODETYPE_AXIS_POS, 1),	JOYCODE_2_DOWN },
	{ JOYCODE(1, CODETYPE_BUTTON, 0),	JOYCODE_2_BUTTON1 },
	{ JOYCODE(1, CODETYPE_BUTTON, 1),	JOYCODE_2_BUTTON2 },
	{ JOYCODE(1, CODETYPE_BUTTON, 2),	JOYCODE_2_BUTTON3 },
	{ JOYCODE(1, CODETYPE_BUTTON, 3),	JOYCODE_2_BUTTON4 },
	{ JOYCODE(1, CODETYPE_BUTTON, 4),	JOYCODE_2_BUTTON5 },
	{ JOYCODE(1, CODETYPE_BUTTON, 5),	JOYCODE_2_BUTTON6 },
	{ JOYCODE(1, CODETYPE_BUTTON, 6),	JOYCODE_2_BUTTON7 },
	{ JOYCODE(1, CODETYPE_BUTTON, 7),	JOYCODE_2_BUTTON8 },
	{ JOYCODE(1, CODETYPE_BUTTON, 8),	JOYCODE_2_BUTTON9 },
	{ JOYCODE(1, CODETYPE_BUTTON, 9),	JOYCODE_2_BUTTON10 },
	{ JOYCODE(1, CODETYPE_JOYAXIS, 0),	JOYCODE_2_ANALOG_X },
	{ JOYCODE(1, CODETYPE_JOYAXIS, 1),	JOYCODE_2_ANALOG_Y },
	{ JOYCODE(1, CODETYPE_JOYAXIS, 2),	JOYCODE_2_ANALOG_Z },

	{ JOYCODE(2, CODETYPE_AXIS_NEG, 0),	JOYCODE_3_LEFT },
	{ JOYCODE(2, CODETYPE_AXIS_POS, 0),	JOYCODE_3_RIGHT },
	{ JOYCODE(2, CODETYPE_AXIS_NEG, 1),	JOYCODE_3_UP },
	{ JOYCODE(2, CODETYPE_AXIS_POS, 1),	JOYCODE_3_DOWN },
	{ JOYCODE(2, CODETYPE_BUTTON, 0),	JOYCODE_3_BUTTON1 },
	{ JOYCODE(2, CODETYPE_BUTTON, 1),	JOYCODE_3_BUTTON2 },
	{ JOYCODE(2, CODETYPE_BUTTON, 2),	JOYCODE_3_BUTTON3 },
	{ JOYCODE(2, CODETYPE_BUTTON, 3),	JOYCODE_3_BUTTON4 },
	{ JOYCODE(2, CODETYPE_BUTTON, 4),	JOYCODE_3_BUTTON5 },
	{ JOYCODE(2, CODETYPE_BUTTON, 5),	JOYCODE_3_BUTTON6 },
	{ JOYCODE(2, CODETYPE_BUTTON, 6),	JOYCODE_3_BUTTON7 },
	{ JOYCODE(2, CODETYPE_BUTTON, 7),	JOYCODE_3_BUTTON8 },
	{ JOYCODE(2, CODETYPE_BUTTON, 8),	JOYCODE_3_BUTTON9 },
	{ JOYCODE(2, CODETYPE_BUTTON, 9),	JOYCODE_3_BUTTON10 },
	{ JOYCODE(2, CODETYPE_JOYAXIS, 0),	JOYCODE_3_ANALOG_X },
	{ JOYCODE(2, CODETYPE_JOYAXIS, 1),	JOYCODE_3_ANALOG_Y },
	{ JOYCODE(2, CODETYPE_JOYAXIS, 2),	JOYCODE_3_ANALOG_Z },

	{ JOYCODE(3, CODETYPE_AXIS_NEG, 0),	JOYCODE_4_LEFT },
	{ JOYCODE(3, CODETYPE_AXIS_POS, 0),	JOYCODE_4_RIGHT },
	{ JOYCODE(3, CODETYPE_AXIS_NEG, 1),	JOYCODE_4_UP },
	{ JOYCODE(3, CODETYPE_AXIS_POS, 1),	JOYCODE_4_DOWN },
	{ JOYCODE(3, CODETYPE_BUTTON, 0),	JOYCODE_4_BUTTON1 },
	{ JOYCODE(3, CODETYPE_BUTTON, 1),	JOYCODE_4_BUTTON2 },
	{ JOYCODE(3, CODETYPE_BUTTON, 2),	JOYCODE_4_BUTTON3 },
	{ JOYCODE(3, CODETYPE_BUTTON, 3),	JOYCODE_4_BUTTON4 },
	{ JOYCODE(3, CODETYPE_BUTTON, 4),	JOYCODE_4_BUTTON5 },
	{ JOYCODE(3, CODETYPE_BUTTON, 5),	JOYCODE_4_BUTTON6 },
	{ JOYCODE(3, CODETYPE_BUTTON, 6),	JOYCODE_4_BUTTON7 },
	{ JOYCODE(3, CODETYPE_BUTTON, 7),	JOYCODE_4_BUTTON8 },
	{ JOYCODE(3, CODETYPE_BUTTON, 8),	JOYCODE_4_BUTTON9 },
	{ JOYCODE(3, CODETYPE_BUTTON, 9),	JOYCODE_4_BUTTON10 },
	{ JOYCODE(3, CODETYPE_JOYAXIS, 0),	JOYCODE_4_ANALOG_X },
	{ JOYCODE(3, CODETYPE_JOYAXIS, 1),	JOYCODE_4_ANALOG_Y },
	{ JOYCODE(3, CODETYPE_JOYAXIS, 2),	JOYCODE_4_ANALOG_Z },

	{ JOYCODE(4, CODETYPE_AXIS_NEG, 0),	JOYCODE_5_LEFT },
	{ JOYCODE(4, CODETYPE_AXIS_POS, 0),	JOYCODE_5_RIGHT },
	{ JOYCODE(4, CODETYPE_AXIS_NEG, 1),	JOYCODE_5_UP },
	{ JOYCODE(4, CODETYPE_AXIS_POS, 1),	JOYCODE_5_DOWN },
	{ JOYCODE(4, CODETYPE_BUTTON, 0),	JOYCODE_5_BUTTON1 },
	{ JOYCODE(4, CODETYPE_BUTTON, 1),	JOYCODE_5_BUTTON2 },
	{ JOYCODE(4, CODETYPE_BUTTON, 2),	JOYCODE_5_BUTTON3 },
	{ JOYCODE(4, CODETYPE_BUTTON, 3),	JOYCODE_5_BUTTON4 },
	{ JOYCODE(4, CODETYPE_BUTTON, 4),	JOYCODE_5_BUTTON5 },
	{ JOYCODE(4, CODETYPE_BUTTON, 5),	JOYCODE_5_BUTTON6 },
	{ JOYCODE(4, CODETYPE_BUTTON, 6),	JOYCODE_5_BUTTON7 },
	{ JOYCODE(4, CODETYPE_BUTTON, 7),	JOYCODE_5_BUTTON8 },
	{ JOYCODE(4, CODETYPE_BUTTON, 8),	JOYCODE_5_BUTTON9 },
	{ JOYCODE(4, CODETYPE_BUTTON, 9),	JOYCODE_5_BUTTON10 },
	{ JOYCODE(4, CODETYPE_JOYAXIS, 0),	JOYCODE_5_ANALOG_X },
	{ JOYCODE(4, CODETYPE_JOYAXIS, 1), 	JOYCODE_5_ANALOG_Y },
	{ JOYCODE(4, CODETYPE_JOYAXIS, 2),	JOYCODE_5_ANALOG_Z },

	{ JOYCODE(5, CODETYPE_AXIS_NEG, 0),	JOYCODE_6_LEFT },
	{ JOYCODE(5, CODETYPE_AXIS_POS, 0),	JOYCODE_6_RIGHT },
	{ JOYCODE(5, CODETYPE_AXIS_NEG, 1),	JOYCODE_6_UP },
	{ JOYCODE(5, CODETYPE_AXIS_POS, 1),	JOYCODE_6_DOWN },
	{ JOYCODE(5, CODETYPE_BUTTON, 0),	JOYCODE_6_BUTTON1 },
	{ JOYCODE(5, CODETYPE_BUTTON, 1),	JOYCODE_6_BUTTON2 },
	{ JOYCODE(5, CODETYPE_BUTTON, 2),	JOYCODE_6_BUTTON3 },
	{ JOYCODE(5, CODETYPE_BUTTON, 3),	JOYCODE_6_BUTTON4 },
	{ JOYCODE(5, CODETYPE_BUTTON, 4),	JOYCODE_6_BUTTON5 },
	{ JOYCODE(5, CODETYPE_BUTTON, 5),	JOYCODE_6_BUTTON6 },
	{ JOYCODE(5, CODETYPE_BUTTON, 6),	JOYCODE_6_BUTTON7 },
	{ JOYCODE(5, CODETYPE_BUTTON, 7),	JOYCODE_6_BUTTON8 },
	{ JOYCODE(5, CODETYPE_BUTTON, 8),	JOYCODE_6_BUTTON9 },
	{ JOYCODE(5, CODETYPE_BUTTON, 9),	JOYCODE_6_BUTTON10 },
	{ JOYCODE(5, CODETYPE_JOYAXIS, 0),	JOYCODE_6_ANALOG_X },
	{ JOYCODE(5, CODETYPE_JOYAXIS, 1),	JOYCODE_6_ANALOG_Y },
	{ JOYCODE(5, CODETYPE_JOYAXIS, 2),	JOYCODE_6_ANALOG_Z },

	{ JOYCODE(6, CODETYPE_AXIS_NEG, 0),	JOYCODE_7_LEFT },
	{ JOYCODE(6, CODETYPE_AXIS_POS, 0),	JOYCODE_7_RIGHT },
	{ JOYCODE(6, CODETYPE_AXIS_NEG, 1),	JOYCODE_7_UP },
	{ JOYCODE(6, CODETYPE_AXIS_POS, 1),	JOYCODE_7_DOWN },
	{ JOYCODE(6, CODETYPE_BUTTON, 0),	JOYCODE_7_BUTTON1 },
	{ JOYCODE(6, CODETYPE_BUTTON, 1),	JOYCODE_7_BUTTON2 },
	{ JOYCODE(6, CODETYPE_BUTTON, 2),	JOYCODE_7_BUTTON3 },
	{ JOYCODE(6, CODETYPE_BUTTON, 3),	JOYCODE_7_BUTTON4 },
	{ JOYCODE(6, CODETYPE_BUTTON, 4),	JOYCODE_7_BUTTON5 },
	{ JOYCODE(6, CODETYPE_BUTTON, 5),	JOYCODE_7_BUTTON6 },
	{ JOYCODE(6, CODETYPE_BUTTON, 6),	JOYCODE_7_BUTTON7 },
	{ JOYCODE(6, CODETYPE_BUTTON, 7),	JOYCODE_7_BUTTON8 },
	{ JOYCODE(6, CODETYPE_BUTTON, 8),	JOYCODE_7_BUTTON9 },
	{ JOYCODE(6, CODETYPE_BUTTON, 9),	JOYCODE_7_BUTTON10 },
	{ JOYCODE(6, CODETYPE_JOYAXIS, 0),	JOYCODE_7_ANALOG_X },
	{ JOYCODE(6, CODETYPE_JOYAXIS, 1),	JOYCODE_7_ANALOG_Y },
	{ JOYCODE(6, CODETYPE_JOYAXIS, 2),	JOYCODE_7_ANALOG_Z },

	{ JOYCODE(7, CODETYPE_AXIS_NEG, 0),	JOYCODE_8_LEFT },
	{ JOYCODE(7, CODETYPE_AXIS_POS, 0),	JOYCODE_8_RIGHT },
	{ JOYCODE(7, CODETYPE_AXIS_NEG, 1),	JOYCODE_8_UP },
	{ JOYCODE(7, CODETYPE_AXIS_POS, 1),	JOYCODE_8_DOWN },
	{ JOYCODE(7, CODETYPE_BUTTON, 0),	JOYCODE_8_BUTTON1 },
	{ JOYCODE(7, CODETYPE_BUTTON, 1),	JOYCODE_8_BUTTON2 },
	{ JOYCODE(7, CODETYPE_BUTTON, 2),	JOYCODE_8_BUTTON3 },
	{ JOYCODE(7, CODETYPE_BUTTON, 3),	JOYCODE_8_BUTTON4 },
	{ JOYCODE(7, CODETYPE_BUTTON, 4),	JOYCODE_8_BUTTON5 },
	{ JOYCODE(7, CODETYPE_BUTTON, 5),	JOYCODE_8_BUTTON6 },
	{ JOYCODE(7, CODETYPE_BUTTON, 6),	JOYCODE_8_BUTTON7 },
	{ JOYCODE(7, CODETYPE_BUTTON, 7),	JOYCODE_8_BUTTON8 },
	{ JOYCODE(7, CODETYPE_BUTTON, 8),	JOYCODE_8_BUTTON9 },
	{ JOYCODE(7, CODETYPE_BUTTON, 9),	JOYCODE_8_BUTTON10 },
	{ JOYCODE(7, CODETYPE_JOYAXIS, 0),	JOYCODE_8_ANALOG_X },
	{ JOYCODE(7, CODETYPE_JOYAXIS, 1),	JOYCODE_8_ANALOG_Y },
	{ JOYCODE(7, CODETYPE_JOYAXIS, 2),	JOYCODE_8_ANALOG_Z },

	{ JOYCODE(0, CODETYPE_MOUSEBUTTON, 0), 	MOUSECODE_1_BUTTON1 },
	{ JOYCODE(0, CODETYPE_MOUSEBUTTON, 1), 	MOUSECODE_1_BUTTON2 },
	{ JOYCODE(0, CODETYPE_MOUSEBUTTON, 2), 	MOUSECODE_1_BUTTON3 },
	{ JOYCODE(0, CODETYPE_MOUSEBUTTON, 3), 	MOUSECODE_1_BUTTON4 },
	{ JOYCODE(0, CODETYPE_MOUSEBUTTON, 4), 	MOUSECODE_1_BUTTON5 },
	{ JOYCODE(0, CODETYPE_MOUSEAXIS, 0),	MOUSECODE_1_ANALOG_X },
	{ JOYCODE(0, CODETYPE_MOUSEAXIS, 1),	MOUSECODE_1_ANALOG_Y },
	{ JOYCODE(0, CODETYPE_MOUSEAXIS, 2),	MOUSECODE_1_ANALOG_Z },

	{ JOYCODE(1, CODETYPE_MOUSEBUTTON, 0), 	MOUSECODE_2_BUTTON1 },
	{ JOYCODE(1, CODETYPE_MOUSEBUTTON, 1), 	MOUSECODE_2_BUTTON2 },
	{ JOYCODE(1, CODETYPE_MOUSEBUTTON, 2), 	MOUSECODE_2_BUTTON3 },
	{ JOYCODE(1, CODETYPE_MOUSEBUTTON, 3), 	MOUSECODE_2_BUTTON4 },
	{ JOYCODE(1, CODETYPE_MOUSEBUTTON, 4), 	MOUSECODE_2_BUTTON5 },
	{ JOYCODE(1, CODETYPE_MOUSEAXIS, 0),	MOUSECODE_2_ANALOG_X },
	{ JOYCODE(1, CODETYPE_MOUSEAXIS, 1),	MOUSECODE_2_ANALOG_Y },
	{ JOYCODE(1, CODETYPE_MOUSEAXIS, 2),	MOUSECODE_2_ANALOG_Z },

	{ JOYCODE(2, CODETYPE_MOUSEBUTTON, 0), 	MOUSECODE_3_BUTTON1 },
	{ JOYCODE(2, CODETYPE_MOUSEBUTTON, 1), 	MOUSECODE_3_BUTTON2 },
	{ JOYCODE(2, CODETYPE_MOUSEBUTTON, 2), 	MOUSECODE_3_BUTTON3 },
	{ JOYCODE(2, CODETYPE_MOUSEBUTTON, 3), 	MOUSECODE_3_BUTTON4 },
	{ JOYCODE(2, CODETYPE_MOUSEAXIS, 0),	MOUSECODE_3_ANALOG_X },
	{ JOYCODE(2, CODETYPE_MOUSEAXIS, 1),	MOUSECODE_3_ANALOG_Y },
	{ JOYCODE(2, CODETYPE_MOUSEAXIS, 2),	MOUSECODE_3_ANALOG_Z },

	{ JOYCODE(3, CODETYPE_MOUSEBUTTON, 0), 	MOUSECODE_4_BUTTON1 },
	{ JOYCODE(3, CODETYPE_MOUSEBUTTON, 1), 	MOUSECODE_4_BUTTON2 },
	{ JOYCODE(3, CODETYPE_MOUSEBUTTON, 2), 	MOUSECODE_4_BUTTON3 },
	{ JOYCODE(3, CODETYPE_MOUSEBUTTON, 3), 	MOUSECODE_4_BUTTON4 },
	{ JOYCODE(3, CODETYPE_MOUSEAXIS, 0),	MOUSECODE_4_ANALOG_X },
	{ JOYCODE(3, CODETYPE_MOUSEAXIS, 1),	MOUSECODE_4_ANALOG_Y },
	{ JOYCODE(3, CODETYPE_MOUSEAXIS, 2),	MOUSECODE_4_ANALOG_Z },

	{ JOYCODE(0, CODETYPE_GUNAXIS, 0),	GUNCODE_1_ANALOG_X },
	{ JOYCODE(0, CODETYPE_GUNAXIS, 1),	GUNCODE_1_ANALOG_Y },

	{ JOYCODE(1, CODETYPE_GUNAXIS, 0),	GUNCODE_2_ANALOG_X },
	{ JOYCODE(1, CODETYPE_GUNAXIS, 1),	GUNCODE_2_ANALOG_Y },
};



/* 2 init routines one for creating the display and one after that, since some
   (most) init stuff needs a display */

int osd_input_initpre(void)
{
	int i, j;
	int stick, axis;

	joy_poll_func = NULL;

	memset(joy_data,   0, sizeof(joy_data));
	memset(mouse_data, 0, sizeof(mouse_data));

	if(rapidfire_enable)
	{
		memset(rapidfire_data, 0, sizeof(rapidfire_data));

		for(i=0; i<4; i++)
		{
			rapidfire_data[i].enable = 1;
			rapidfire_data[i].ctrl_button = -1;
			rapidfire_data[i].ctrl_prev_status = 0;
			for(j=0; j<10; j++)
			{
				rapidfire_data[i].setting[j] = 64;
				rapidfire_data[i].status[j] = 0;
			}
		}
		load_rapidfire_settings();
	}

	for (stick = 0; stick < JOY_MAX; stick++)
	{
		joy_data[stick].fd = -1;
		for (axis = 0; axis < JOY_AXES; axis++)
		{
			joy_data[stick].axis[axis].min = -10;
			joy_data[stick].axis[axis].max =  10;
		}
	}

	/* joysticks */
	switch (joytype)
	{
		case JOY_NONE:
			break;
#ifdef I386_JOYSTICK
		case JOY_I386NEW:
		case JOY_I386:
			joy_i386_init();
			break;
#endif
#ifdef LIN_FM_TOWNS
		case JOY_PAD:
			joy_pad_init ();
			break;
#endif
#ifdef X11_JOYSTICK
		case JOY_X11:
			joy_x11_init();
			break;
#endif
#ifdef USB_JOYSTICK
		case JOY_USB:
			joy_usb_init();
			break;
#endif
#ifdef PS2_JOYSTICK
		case JOY_PS2:
			joy_ps2_init();
			break;
#endif
#if defined SDL || defined SDL_JOYSTICK
		case JOY_SDL:
			joy_SDL_init();
			break;
#endif
		default:
			fprintf (stderr_file, "OSD: Warning: unknown joytype: %d, or joytype not compiled in.\n"
					"   Disabling joystick support.\n", joytype);
			joytype = JOY_NONE;
	}

	total_codes = 0;

	/* init the keyboard list */
	init_keycodes();

	/* init the joystick list */
	init_joycodes();

	/* terminate array */
	memset(&codelist[total_codes], 0, sizeof(codelist[total_codes]));

	if (joytype != JOY_NONE)
	{
		int found = FALSE;

		for (stick = 0; stick < JOY_MAX; stick++)
		{
			if (joy_data[stick].num_axes || joy_data[stick].num_buttons)
			{
				fprintf(stderr_file, "OSD: Info: Joystick %d, %d axis, %d buttons\n",
						stick, joy_data[stick].num_axes, joy_data[stick].num_buttons);
				found = TRUE;
			}
		}

		if (!found)
		{
			fprintf(stderr_file, "OSD: Warning: No joysticks found disabling joystick support\n");
			joytype = JOY_NONE;
		}
	}

	if (use_mouse)
		fprintf (stderr_file, "Mouse/Trakball selected.\n");

#ifdef UGCICOIN
	if (ugcicoin)
	{
		if (ugci_init(ugci_callback, UGCI_EVENT_MASK_COIN | UGCI_EVENT_MASK_PLAY, 1) <= 0)
			ugcicoin = 0;
	}
#endif
#ifdef USE_LIGHTGUN_ABS_EVENT
	lightgun_event_abs_init();
#endif

#ifdef JOY_PS2
	/* Special mapping for PlayStation2 -- to be removed when 0.60 patch done */
	/* Add mappings for P1 SELECT, START, P2 SELECT, START */
	add_joylist_entry("SELECT1", JOYCODE(0, CODETYPE_BUTTON, 6), JOYCODE_1_SELECT);
	add_joylist_entry("START1", JOYCODE(0, CODETYPE_BUTTON, 7), JOYCODE_1_START);
	add_joylist_entry("SELECT2", JOYCODE(1, CODETYPE_BUTTON, 6), JOYCODE_2_SELECT);
	add_joylist_entry("START2", JOYCODE(1, CODETYPE_BUTTON, 7), JOYCODE_2_START);
	/* For now, L2 is equivalent of TAB, and R2 is equivalent of ESC */
	add_joylist_entry("L2", JOYCODE(0, CODETYPE_BUTTON, 8), KEYCODE_TAB);
	add_joylist_entry("R2", JOYCODE(0, CODETYPE_BUTTON, 9), KEYCODE_ESC);
	/* Remap L3 and R3 to BUTTON7 and BUTTON8 */
	add_joylist_entry("L3", JOYCODE(0, CODETYPE_BUTTON, 10), JOYCODE_1_BUTTON7);
	add_joylist_entry("R3", JOYCODE(0, CODETYPE_BUTTON, 11), JOYCODE_1_BUTTON8);
	add_joylist_entry("L4", JOYCODE(1, CODETYPE_BUTTON, 10), JOYCODE_2_BUTTON7);
	add_joylist_entry("R4", JOYCODE(1, CODETYPE_BUTTON, 11), JOYCODE_2_BUTTON8);
	/* Map the 4 directional buttons to the four axes. */
	add_joylist_entry("LEFT1", JOYCODE(0, CODETYPE_BUTTON, 12), JOYCODE_1_LEFT);
	add_joylist_entry("RIGHT1", JOYCODE(0, CODETYPE_BUTTON, 13), JOYCODE_1_RIGHT);
	add_joylist_entry("UP1", JOYCODE(0, CODETYPE_BUTTON, 14), JOYCODE_1_UP);
	add_joylist_entry("DOWN1", JOYCODE(0, CODETYPE_BUTTON, 15), JOYCODE_1_DOWN);
	add_joylist_entry("LEFT2", JOYCODE(1, CODETYPE_BUTTON, 12), JOYCODE_2_LEFT);
	add_joylist_entry("RIGHT2", JOYCODE(1, CODETYPE_BUTTON, 13), JOYCODE_2_RIGHT);
	add_joylist_entry("UP2", JOYCODE(1, CODETYPE_BUTTON, 14), JOYCODE_2_UP);
	add_joylist_entry("DOWN2", JOYCODE(1, CODETYPE_BUTTON, 15), JOYCODE_2_DOWN);
#endif

	return OSD_OK;
}

int osd_input_initpost(void)
{
#ifdef USE_XINPUT_DEVICES
	XInputDevices_init();
#endif

	/* init the keyboard */
	if (xmame_keyboard_init())
		return OSD_NOT_OK;

	return OSD_OK;
}


void osd_input_close(void)
{
	int stick;

	xmame_keyboard_exit();

	switch(joytype)
	{
#ifdef PS2_JOYSTICK
		case JOY_PS2:
			joy_ps2_exit();
			break;
#endif
		default:
			break;
	}

	for(stick = 0; stick < JOY_MAX; stick++)
		if (joy_data[stick].fd >= 0)
			close(joy_data[stick].fd);

	if (rapidfire_enable)
		save_rapidfire_settings();
}



/*============================================================ */
/*	updatekeyboard */
/*============================================================ */

/* since the keyboard controller is slow, it is not capable of reporting multiple */
/* key presses fast enough. We have to delay them in order not to lose special moves */
/* tied to simultaneous button presses. */

static void updatekeyboard(void)
{
	int i, changed = 0;

	/* see if any keys have changed state */
	for (i = 0; i < KEY_CODES; i++)
		if (key[i] != oldkey[i])
		{
			changed = 1;

			/* keypress was missed, turn it on for one frame */
			if (key[i] == 0 && currkey[i] == 0)
				currkey[i] = -1;
		}

	/* if keyboard state is stable, copy it over */
	if (!changed)
		memcpy(currkey, key, sizeof(currkey));

	/* remember the previous state */
	memcpy(oldkey, key, sizeof(oldkey));
}



/*============================================================ */
/*	is_key_pressed */
/*============================================================ */

static int is_key_pressed(int keycode)
{
	/* blames to the dos-people who want to check key states before
	   the display (and under X thus the keyboard) is initialised */
	if (!kbd_fifo)
		return 0;

	if (keycode >= KEY_CODES)
		return 0;

	/* special case: if we're trying to quit, fake up/down/up/down */
	if (keycode == KEY_ESC && trying_to_quit)
	{
		static int dummy_state = 1;
		return dummy_state ^= 1;
	}

	sysdep_update_keyboard();
	updatekeyboard();

	if (steadykey)
		return currkey[keycode];
	else
		return key[keycode];
}



/*============================================================ */
/*	osd_readkey_unicode */
/*============================================================ */

int osd_readkey_unicode(int flush)
{
	struct xmame_keyboard_event event;

	/* blames to the dos-people who want to check key states before
	   the display (and under X thus the keyboard) is initialised */
	if (!kbd_fifo)
		return 0;

	if (flush)
		xmame_keyboard_clear();

	sysdep_update_keyboard();
	updatekeyboard();

	if (!kbd_fifo_get(kbd_fifo, &event) && event.press)
		return event.unicode;
	else
		return 0;
}



static void add_joylist_entry(const char *name, os_code_t code,
		input_code_t standardcode)
{
	/* copy the name */
	char *namecopy = malloc(strlen(name) + 1);
	if (namecopy)
	{
		int entry;

		/* find the table entry, if there is one */
		for (entry = 0; entry < ELEMENTS(joy_trans_table); entry++)
			if (joy_trans_table[entry][0] == code)
				break;

		/* fill in the joy description */
		codelist[total_codes].name = strcpy(namecopy, name);
		codelist[total_codes].oscode = code;
		if (entry < ELEMENTS(joy_trans_table))
			standardcode = joy_trans_table[entry][1];
		codelist[total_codes].inputcode = standardcode;
		total_codes++;
	}
}


static void add_keylist_entry(const char *name, os_code_t code,
		input_code_t standardcode)
{
	/* copy the name */
	char *namecopy = malloc(strlen(name) + 1);
	if (namecopy)
	{
		/* fill in the joy description */
		codelist[total_codes].name = strcpy(namecopy, name);
		codelist[total_codes].oscode = code;
		codelist[total_codes].inputcode = standardcode;
		total_codes++;
	}
}



/*============================================================ */
/*	init_keycodes */
/*============================================================ */

static void init_keycodes(void)
{
	add_keylist_entry("A", KEY_A, KEYCODE_A);
	add_keylist_entry("B", KEY_B, KEYCODE_B);
	add_keylist_entry("C", KEY_C, KEYCODE_C);
	add_keylist_entry("D", KEY_D, KEYCODE_D);
	add_keylist_entry("E", KEY_E, KEYCODE_E);
	add_keylist_entry("F", KEY_F, KEYCODE_F);
	add_keylist_entry("G", KEY_G, KEYCODE_G);
	add_keylist_entry("H", KEY_H, KEYCODE_H);
	add_keylist_entry("I", KEY_I, KEYCODE_I);
	add_keylist_entry("J", KEY_J, KEYCODE_J);
	add_keylist_entry("K", KEY_K, KEYCODE_K);
	add_keylist_entry("L", KEY_L, KEYCODE_L);
	add_keylist_entry("M", KEY_M, KEYCODE_M);
	add_keylist_entry("N", KEY_N, KEYCODE_N);
	add_keylist_entry("O", KEY_O, KEYCODE_O);
	add_keylist_entry("P", KEY_P, KEYCODE_P);
	add_keylist_entry("Q", KEY_Q, KEYCODE_Q);
	add_keylist_entry("R", KEY_R, KEYCODE_R);
	add_keylist_entry("S", KEY_S, KEYCODE_S);
	add_keylist_entry("T", KEY_T, KEYCODE_T);
	add_keylist_entry("U", KEY_U, KEYCODE_U);
	add_keylist_entry("V", KEY_V, KEYCODE_V);
	add_keylist_entry("W", KEY_W, KEYCODE_W);
	add_keylist_entry("X", KEY_X, KEYCODE_X);
	add_keylist_entry("Y", KEY_Y, KEYCODE_Y);
	add_keylist_entry("Z", KEY_Z, KEYCODE_Z);
	add_keylist_entry("0", KEY_0, KEYCODE_0);
	add_keylist_entry("1", KEY_1, KEYCODE_1);
	add_keylist_entry("2", KEY_2, KEYCODE_2);
	add_keylist_entry("3", KEY_3, KEYCODE_3);
	add_keylist_entry("4", KEY_4, KEYCODE_4);
	add_keylist_entry("5", KEY_5, KEYCODE_5);
	add_keylist_entry("6", KEY_6, KEYCODE_6);
	add_keylist_entry("7", KEY_7, KEYCODE_7);
	add_keylist_entry("8", KEY_8, KEYCODE_8);
	add_keylist_entry("9", KEY_9, KEYCODE_9);
	add_keylist_entry("0 PAD", KEY_0_PAD, KEYCODE_0_PAD);
	add_keylist_entry("1 PAD", KEY_1_PAD, KEYCODE_1_PAD);
	add_keylist_entry("2 PAD", KEY_2_PAD, KEYCODE_2_PAD);
	add_keylist_entry("3 PAD", KEY_3_PAD, KEYCODE_3_PAD);
	add_keylist_entry("4 PAD", KEY_4_PAD, KEYCODE_4_PAD);
	add_keylist_entry("5 PAD", KEY_5_PAD, KEYCODE_5_PAD);
	add_keylist_entry("6 PAD", KEY_6_PAD, KEYCODE_6_PAD);
	add_keylist_entry("7 PAD", KEY_7_PAD, KEYCODE_7_PAD);
	add_keylist_entry("8 PAD", KEY_8_PAD, KEYCODE_8_PAD);
	add_keylist_entry("9 PAD", KEY_9_PAD, KEYCODE_9_PAD);
	add_keylist_entry("F1", KEY_F1, KEYCODE_F1);
	add_keylist_entry("F2", KEY_F2, KEYCODE_F2);
	add_keylist_entry("F3", KEY_F3, KEYCODE_F3);
	add_keylist_entry("F4", KEY_F4, KEYCODE_F4);
	add_keylist_entry("F5", KEY_F5, KEYCODE_F5);
	add_keylist_entry("F6", KEY_F6, KEYCODE_F6);
	add_keylist_entry("F7", KEY_F7, KEYCODE_F7);
	add_keylist_entry("F8", KEY_F8, KEYCODE_F8);
	add_keylist_entry("F9", KEY_F9, KEYCODE_F9);
	add_keylist_entry("F10", KEY_F10, KEYCODE_F10);
	add_keylist_entry("F11", KEY_F11, KEYCODE_F11);
	add_keylist_entry("F12", KEY_F12, KEYCODE_F12);
	add_keylist_entry("ESC", KEY_ESC, KEYCODE_ESC);
	add_keylist_entry("~", KEY_TILDE, KEYCODE_TILDE);
	add_keylist_entry("-", KEY_MINUS, KEYCODE_MINUS);
	add_keylist_entry("=", KEY_EQUALS, KEYCODE_EQUALS);
	add_keylist_entry("BKSPACE", KEY_BACKSPACE, KEYCODE_BACKSPACE);
	add_keylist_entry("TAB", KEY_TAB, KEYCODE_TAB);
	add_keylist_entry("[", KEY_OPENBRACE, KEYCODE_OPENBRACE);
	add_keylist_entry("]", KEY_CLOSEBRACE, KEYCODE_CLOSEBRACE);
	add_keylist_entry("ENTE ", KEY_ENTER, KEYCODE_ENTER);
	add_keylist_entry(";", KEY_COLON, KEYCODE_COLON);
	add_keylist_entry(":", KEY_QUOTE, KEYCODE_QUOTE);
	add_keylist_entry("\\", KEY_BACKSLASH, KEYCODE_BACKSLASH);
	add_keylist_entry("<", KEY_BACKSLASH2, KEYCODE_BACKSLASH2);
	add_keylist_entry(",", KEY_COMMA, KEYCODE_COMMA);
	add_keylist_entry(".", KEY_STOP, KEYCODE_STOP);
	add_keylist_entry("/", KEY_SLASH, KEYCODE_SLASH);
	add_keylist_entry("SPACE", KEY_SPACE, KEYCODE_SPACE);
	add_keylist_entry("INS", KEY_INSERT, KEYCODE_INSERT);
	add_keylist_entry("DEL", KEY_DEL, KEYCODE_DEL);
	add_keylist_entry("HOME", KEY_HOME, KEYCODE_HOME);
	add_keylist_entry("END", KEY_END, KEYCODE_END);
	add_keylist_entry("PGUP", KEY_PGUP, KEYCODE_PGUP);
	add_keylist_entry("PGDN", KEY_PGDN, KEYCODE_PGDN);
	add_keylist_entry("LEFT", KEY_LEFT, KEYCODE_LEFT);
	add_keylist_entry("RIGHT", KEY_RIGHT, KEYCODE_RIGHT);
	add_keylist_entry("UP", KEY_UP, KEYCODE_UP);
	add_keylist_entry("DOWN", KEY_DOWN, KEYCODE_DOWN);
	add_keylist_entry("/ PAD", KEY_SLASH_PAD, KEYCODE_SLASH_PAD);
	add_keylist_entry("* PAD", KEY_ASTERISK, KEYCODE_ASTERISK);
	add_keylist_entry("- PAD", KEY_MINUS_PAD, KEYCODE_MINUS_PAD);
	add_keylist_entry("+ PAD", KEY_PLUS_PAD, KEYCODE_PLUS_PAD);
	add_keylist_entry(". PAD", KEY_DEL_PAD, KEYCODE_DEL_PAD);
	add_keylist_entry("ENTER PAD", KEY_ENTER_PAD, KEYCODE_ENTER_PAD);
	add_keylist_entry("PRTSCR", KEY_PRTSCR, KEYCODE_PRTSCR);
	add_keylist_entry("PAUSE", KEY_PAUSE, KEYCODE_PAUSE);
	add_keylist_entry("PAUSE", KEY_PAUSE_ALT, KEYCODE_PAUSE);
	add_keylist_entry("LSHIFT", KEY_LSHIFT, KEYCODE_LSHIFT);
	add_keylist_entry("RSHIFT", KEY_RSHIFT, KEYCODE_RSHIFT);
	add_keylist_entry("LCTRL", KEY_LCONTROL, KEYCODE_LCONTROL);
	add_keylist_entry("RCTRL", KEY_RCONTROL, KEYCODE_RCONTROL);
	add_keylist_entry("ALT", KEY_ALT, KEYCODE_LALT);
	add_keylist_entry("ALTGR", KEY_ALTGR, KEYCODE_RALT);
	add_keylist_entry("LWIN", KEY_LWIN, KEYCODE_LWIN);
	add_keylist_entry("RWIN", KEY_RWIN, KEYCODE_RWIN);
	add_keylist_entry("MENU", KEY_MENU, KEYCODE_MENU);
	add_keylist_entry("SCRLOCK", KEY_SCRLOCK, KEYCODE_SCRLOCK);
	add_keylist_entry("NUMLOCK", KEY_NUMLOCK, KEYCODE_NUMLOCK);
	add_keylist_entry("CAPSLOCK", KEY_CAPSLOCK, KEYCODE_CAPSLOCK);
}


/* public methods (in keyboard.h / osdepend.h) */
int xmame_keyboard_init(void)
{
	memset(key, 0, KEY_CODES);

	kbd_fifo = kbd_fifo_create(256);
	if(!kbd_fifo)
		return -1;

	return 0;
}

void xmame_keyboard_exit()
{
	if(kbd_fifo)
		kbd_fifo_destroy(kbd_fifo);
}

void xmame_keyboard_register_event(struct xmame_keyboard_event *event)
{
	/* register the event in our event fifo */
	kbd_fifo_put(kbd_fifo, *event);

	/* and update the key array */
	key[event->scancode] = event->press;
}

void xmame_keyboard_clear(void)
{
	kbd_fifo_empty(kbd_fifo);
	memset(key, 0, KEY_CODES);
}



/*============================================================ */
/*	init_joycodes */
/*============================================================ */

static void init_joycodes(void)
{
	int mouse, gun, stick, axis, button;
	char tempname[JOY_NAME_LEN + 1];

	/* map mice first */
	if (use_mouse)
	{
		for (mouse = 0; mouse < MOUSE_MAX; mouse++)
		{
			/* add analog axes (fix me -- should enumerate these) */
			snprintf(tempname, JOY_NAME_LEN, "Mouse %d X", mouse + 1);
			add_joylist_entry(tempname, JOYCODE(mouse, CODETYPE_MOUSEAXIS, 0), CODE_OTHER_ANALOG_RELATIVE);
			snprintf(tempname, JOY_NAME_LEN, "Mouse %d Y", mouse + 1);
			add_joylist_entry(tempname, JOYCODE(mouse, CODETYPE_MOUSEAXIS, 1), CODE_OTHER_ANALOG_RELATIVE);

			/* add mouse buttons */
			for (button = 0; button < MOUSE_BUTTONS; button++)
			{
				snprintf(tempname, JOY_NAME_LEN, "Mouse %d button %d", mouse + 1, button + 1);
				add_joylist_entry(tempname, JOYCODE(mouse, CODETYPE_MOUSEBUTTON, button), CODE_OTHER_DIGITAL);
			}
		}
	}

	/* map lightguns second */
	for (gun = 0; gun < GUN_MAX; gun++)
	{
		/* add lightgun axes (fix me -- should enumerate these) */
		snprintf(tempname, JOY_NAME_LEN, "Lightgun %d X", gun + 1);
		add_joylist_entry(tempname, JOYCODE(gun, CODETYPE_GUNAXIS, 0), CODE_OTHER_ANALOG_ABSOLUTE);
		snprintf(tempname, JOY_NAME_LEN, "Lightgun %d Y", gun + 1);
		add_joylist_entry(tempname, JOYCODE(gun, CODETYPE_GUNAXIS, 1), CODE_OTHER_ANALOG_ABSOLUTE);
	}

	/* now map joysticks */
	for (stick = 0; stick < JOY_MAX; stick++)
	{
		if (joy_data[stick].fd > 0)
		{
			for (axis = 0; axis < JOY_AXES; axis++)
			{
				snprintf(tempname, JOY_NAME_LEN, "Joy %d axis %d %s", stick + 1, axis + 1, "neg");
				add_joylist_entry(tempname, JOYCODE(stick, CODETYPE_JOYAXIS, axis), CODE_OTHER_ANALOG_ABSOLUTE);

				snprintf(tempname, JOY_NAME_LEN, "Joy %d axis %d %s", stick + 1, axis + 1, "neg");
				add_joylist_entry(tempname, JOYCODE(stick, CODETYPE_AXIS_NEG, axis), CODE_OTHER_DIGITAL);

				snprintf(tempname, JOY_NAME_LEN, "Joy %d axis %d %s", stick + 1, axis + 1, "pos");
				add_joylist_entry(tempname, JOYCODE(stick, CODETYPE_AXIS_POS, axis), CODE_OTHER_DIGITAL);
			}
			for (button = 0; button < JOY_BUTTONS; button++)
			{
				snprintf(tempname, JOY_NAME_LEN, "Joy %d button %d", stick + 1 , axis + 1);
				add_joylist_entry(tempname, JOYCODE(stick, CODETYPE_BUTTON, button), CODE_OTHER_DIGITAL);
			}
		}
	}
}


/*
 * given a new x an y joystick axis value convert it to a move definition
 */

void joy_evaluate_moves(void)
{
	int stick, axis, threshold;

	if (is_usb_ps_gamepad)
	{
		for (stick = 0; stick < JOY_MAX; stick++)
		{
			joy_data[stick].axis[0].dirs[0] = joy_data[stick].buttons[15];
			joy_data[stick].axis[0].dirs[1] = joy_data[stick].buttons[13];
			joy_data[stick].axis[1].dirs[0] = joy_data[stick].buttons[12];
			joy_data[stick].axis[1].dirs[1] = joy_data[stick].buttons[14];
		}
	} 
	else 
	{
		for (stick = 0; stick < JOY_MAX; stick++)
		{
			for (axis = 0; axis < joy_data[stick].num_axes; axis++)
			{
				memset(joy_data[stick].axis[axis].dirs, FALSE, JOY_DIRS*sizeof(int));

				/* auto calibrate */
				/* sdevaux 04/2003 : update middle when autocalibrate */
				if (joy_data[stick].axis[axis].val > joy_data[stick].axis[axis].max)
				{
					joy_data[stick].axis[axis].max = joy_data[stick].axis[axis].val;
					joy_data[stick].axis[axis].center = (joy_data[stick].axis[axis].max + joy_data[stick].axis[axis].min)/2;
				}
				else if (joy_data[stick].axis[axis].val < joy_data[stick].axis[axis].min)
				{
					joy_data[stick].axis[axis].min = joy_data[stick].axis[axis].val;
					joy_data[stick].axis[axis].center = (joy_data[stick].axis[axis].max + joy_data[stick].axis[axis].min)/2;
				}

				threshold = (joy_data[stick].axis[axis].max - joy_data[stick].axis[axis].center) >> 1;

				if (joy_data[stick].axis[axis].val < (joy_data[stick].axis[axis].center - threshold))
					joy_data[stick].axis[axis].dirs[0] = TRUE;
				else if (joy_data[stick].axis[axis].val > (joy_data[stick].axis[axis].center + threshold))
					joy_data[stick].axis[axis].dirs[1] = TRUE;
			}
		}
	}
}



/*============================================================ */
/*	get_joycode_value */
/*============================================================ */

static INT32 get_joycode_value(os_code_t joycode)
{
	int joyindex = JOYINDEX(joycode);
	int codetype = CODETYPE(joycode);
	int joynum = JOYNUM(joycode);

	/* switch off the type */
	switch (codetype)
	{
		case CODETYPE_MOUSEBUTTON:
			return mouse_data[joynum].buttons[joyindex];

		case CODETYPE_BUTTON:
			return joy_data[joynum].buttons[joyindex];

		case CODETYPE_AXIS_POS:
		case CODETYPE_AXIS_NEG:
		{
			int val = joy_data[joynum].axis[joyindex].val;	
			int top = joy_data[joynum].axis[joyindex].max;	
			int bottom = joy_data[joynum].axis[joyindex].min;	
			int middle = joy_data[joynum].axis[joyindex].center;

			/* watch for movement greater "a2d_deadzone" along either axis */
			/* FIXME in the two-axis joystick case, we need to find out */
			/* the angle. Anything else is unprecise. */
			if (codetype == CODETYPE_AXIS_POS)
				return (val > middle + ((top - middle) * a2d_deadzone));
			else
				return (val < middle - ((middle - bottom) * a2d_deadzone));
		}

		/* analog joystick axis */
		case CODETYPE_JOYAXIS:
		{
			int val = joy_data[joynum].axis[joyindex].val;	
			int top = joy_data[joynum].axis[joyindex].max;	
			int bottom = joy_data[joynum].axis[joyindex].min;	

			if (joytype == JOY_NONE)
				return 0;
			val = (INT64)val * (INT64)(ANALOG_VALUE_MAX - ANALOG_VALUE_MIN) / (INT64)(top - bottom) + ANALOG_VALUE_MIN;
			if (val < ANALOG_VALUE_MIN)
				val = ANALOG_VALUE_MIN;
			if (val > ANALOG_VALUE_MAX)
				val = ANALOG_VALUE_MAX;
			return val;
		}

		/* analog mouse axis */
		case CODETYPE_MOUSEAXIS:
		{
			int delta = 0;
#ifdef USE_XINPUT_DEVICES
			if (joynum < XINPUT_JOYSTICK_1)
				XInputPollDevices(joynum, joyindex, &delta);
#else
			if (joynum < MOUSE_MAX && joyindex < 2)
				delta = mouse_data[joynum].deltas[joyindex];
#endif
			/* return the latest mouse info */
			return delta * 512;
		}

		/* analog gun axis */
		case CODETYPE_GUNAXIS:
		{
#ifdef USE_LIGHTGUN_ABS_EVENT
			int delta = 0;

			/* return the latest gun info */
			if (lightgun_event_abs_read(joynum, joyindex, &delta))
				return delta;
#endif
			return 0;
		}
	}

	/* keep the compiler happy */
	return 0;
}

int get_rapidfire_speed(int joy_num, int button_num)
{
	if (joy_num < 0 || 3 < joy_num)
		return 0;
	if (button_num < 0 || 9 < button_num)
		return 0;

	return rapidfire_data[joy_num].setting[button_num];
}

void set_rapidfire_speed(int joy_num, int button_num, int speed)
{
	if (joy_num < 0 || 3 < joy_num)
		return;
	if (button_num < 0 || 9 < button_num)
		return;

	rapidfire_data[joy_num].setting[button_num] = speed;
	rapidfire_data[joy_num].status[button_num] = 0;
}

int is_rapidfire_ctrl_button(int joy_num, int button_num)
{
	if (joy_num < 0 || 3 < joy_num)
		return 0;
	if (button_num < 0 || 9 < button_num)
		return 0;

	if (button_num == rapidfire_data[joy_num].ctrl_button)
		return 1;
	else
		return 0;
}

int no_rapidfire_ctrl_button(int joy_num)
{
	if (joy_num < 0 || 3 < joy_num)
		return 0;

	if (rapidfire_data[joy_num].ctrl_button < 0 ||
			rapidfire_data[joy_num].ctrl_button > 9)
		return 1;
	else
		return 0;
}

void set_rapidfire_ctrl_button(int joy_num, int button_num)
{
	if (joy_num < 0 || 3 < joy_num)
		return;
	if (button_num < 0 || 9 < button_num)
		return;

	rapidfire_data[joy_num].ctrl_button = button_num;
}

void unset_rapidfire_ctrl_button(int joy_num)
{
	if (joy_num < 0 || 3 < joy_num)
		return;

	rapidfire_data[joy_num].ctrl_button = -1;
}

int setrapidfire(struct mame_bitmap *bitmap, int selected)
{
	const char *menu_item[42] = {
		"Joy1Button 1",
		"Joy1Button 2",
		"Joy1Button 3",
		"Joy1Button 4",
		"Joy1Button 5",
		"Joy1Button 6",
		"Joy1Button 7",
		"Joy1Button 8",
		"Joy1Button 9",
		"Joy1Button10",
		"Joy2Button 1",
		"Joy2Button 2",
		"Joy2Button 3",
		"Joy2Button 4",
		"Joy2Button 5",
		"Joy2Button 6",
		"Joy2Button 7",
		"Joy2Button 8",
		"Joy2Button 9",
		"Joy2Button10",
		"Joy3Button 1",
		"Joy3Button 2",
		"Joy3Button 3",
		"Joy3Button 4",
		"Joy3Button 5",
		"Joy3Button 6",
		"Joy3Button 7",
		"Joy3Button 8",
		"Joy3Button 9",
		"Joy3Button10",
		"Joy4Button 1",
		"Joy4Button 2",
		"Joy4Button 3",
		"Joy4Button 4",
		"Joy4Button 5",
		"Joy4Button 6",
		"Joy4Button 7",
		"Joy4Button 8",
		"Joy4Button 9",
		"Joy4Button10",
		0,
		0              /* terminate array */
	};
	const char *menu_subitem[42];
	char sub[42][16];
	int sel;
	int arrowize;
	int items = 41;
	int joy;
	int button;
	int d0,flag;

	menu_subitem[40] = 0;
	menu_item[40] = ui_getstring (UI_returntomain);

	sel = selected - 1;

	for (joy = 0; joy < 4; joy += 1)
	{
		for (button = 0; button < 10; button += 1)
		{
			if (is_rapidfire_ctrl_button(joy, button))
			{
				sprintf(&sub[ (joy*10) + button ][0], "    SWITCH");
			}
			else
			{
				int speed = get_rapidfire_speed(joy, button);
				if (speed & 0x0100)
					sprintf(&sub[ (joy*10) + button ][0], "RAPID %3d", speed & 0xFF);
				else if(speed & 0x0200)
					sprintf(&sub[ (joy*10) + button ][0], "CHARGE %3d", speed & 0xFF);
				else
					sprintf(&sub[ (joy*10) + button ][0], "     OFF");
			}
			menu_subitem[ (joy*10) + button ] = &sub[ (joy*10) + button ][0];
		}
	}
	arrowize = 0;
	ui_displaymenu(bitmap, menu_item, menu_subitem, 0, sel, arrowize);

	if (input_ui_pressed_repeat(IPT_UI_DOWN,8))
		sel = (sel + 1) % items;

	if (input_ui_pressed_repeat(IPT_UI_UP,8))
		sel = (sel + items - 1) % items;

	if (input_ui_pressed_repeat(IPT_UI_LEFT,8))
	{
		if (sel < items - 1)
		{
			joy    = sel / 10;
			button = sel % 10;
			d0 = get_rapidfire_speed(joy, button);
			flag = d0 & 0xff00;
			if((flag & 0x300)&&(!is_rapidfire_ctrl_button(joy, button)))
			{
				d0 &= 0xff;
				if              (d0 <= 32){
					d0 = 128;
				}else if(d0 <= 64){
					d0 = 32;
				}else if(d0 <= 96){
					d0 = 64;
				}else if(d0 <= 128){
					d0 = 96;
				}else{
					d0 = 128;
				}
				d0 |= flag;
				set_rapidfire_speed(joy, button, d0);
				/* tell updatescreen() to clean after us (in case the window changes size) */
				schedule_full_refresh();
			}
		}
	}

	if (input_ui_pressed_repeat(IPT_UI_RIGHT,8))
	{
		if (sel < items - 1)
		{
			joy    = sel / 10;
			button = sel % 10;
			d0 = get_rapidfire_speed(joy, button);
			flag = d0 & 0xff00;
			if((flag & 0x300)&&(is_rapidfire_ctrl_button(joy, button)))
			{
				d0 &= 0xff;
				if (d0 <= 32)
					d0 = 64;
				else if (d0 <= 64)
					d0 = 96;
				else if (d0 <= 96)
					d0 = 128;
				else if (d0 <= 128)
					d0 = 32;
				else
					d0 = 128;

				d0 |= flag;
				set_rapidfire_speed(joy, button, d0);
				/* tell updatescreen() to clean after us (in case the window changes size) */
				schedule_full_refresh();
			}
		}
	}

	if (input_ui_pressed(IPT_UI_SELECT))
	{
		if (sel == items - 1) sel = -1;         /* cancel */
		else if (sel < items - 1)
		{
			joy    = sel / 10;
			button = sel % 10;
			d0 = get_rapidfire_speed(joy, button);
			flag = d0 & 0xff00;
			d0 &= 0xff;
			if (is_rapidfire_ctrl_button(joy, button))
			{
				/* off */
				flag = 0;
				unset_rapidfire_ctrl_button(joy);
			}
			else if(flag & 0x100)
			{
				flag = 0x200; /* rapid */
			}
			else if(flag & 0x200)
			{
				/* switch */
				if(no_rapidfire_ctrl_button(joy))
				{
					set_rapidfire_ctrl_button(joy, button);
				}
				flag = 0;
			}
			else
			{
				flag = 0x100; /* charge */
			}
			d0 |= flag;
			set_rapidfire_speed(joy, button, d0);
			/* tell updatescreen() to clean after us (in case the window changes size) */
			schedule_full_refresh();
		}
	}

	if (input_ui_pressed(IPT_UI_CANCEL))
		sel = -1;                                                       
	/* cancel */

	if (input_ui_pressed(IPT_UI_CONFIGURE))
		sel = -2;

	if (sel == -1 || sel == -2)
	{
		schedule_full_refresh();
	}

	return sel + 1;
}

void joystick_rapidfire(void)
{
	int joy_num;
	int button_num;
	int ctrl;

	if (setup_active())
		return;

	for (joy_num=0; joy_num<4 && joy_data[joy_num].fd>=0; joy_num++)
	{
		ctrl = rapidfire_data[joy_num].ctrl_button;
		if (ctrl >= 0 && ctrl < 10 && ctrl < joy_data[joy_num].num_buttons)
		{
			if (joy_data[joy_num].buttons[ctrl] &&
					rapidfire_data[joy_num].ctrl_prev_status == 0)
			{
				rapidfire_data[joy_num].enable = 1 - rapidfire_data[joy_num].enable;
			}
			rapidfire_data[joy_num].ctrl_prev_status = joy_data[joy_num].buttons[ctrl];
		}
		if (rapidfire_data[joy_num].enable)
		{
			for (button_num = 0;
					button_num < 10 && button_num < joy_data[joy_num].num_buttons;
					button_num++)
			{
				if (rapidfire_data[joy_num].setting[button_num] & 0xff &&
						rapidfire_data[joy_num].setting[button_num] & 0x300)
				{
					if (joy_data[joy_num].buttons[button_num] != 0)
					{
						joy_data[joy_num].buttons[button_num] &=
							(rapidfire_data[joy_num].status[button_num] >> 7) ? 0xFFFF : 0;
						rapidfire_data[joy_num].status[button_num] +=
							rapidfire_data[joy_num].setting[button_num];
						rapidfire_data[joy_num].status[button_num] &= 0xFF;
					}else{
						rapidfire_data[joy_num].status[button_num] = 0xFF;

						/* charge */
						if ((rapidfire_data[joy_num].setting[button_num] & 0x300) == 0x200)
							joy_data[joy_num].buttons[button_num] = 0xFFFF;
					}
				}
			}
		}
	}
}

static struct joydata_struct prev_joy_data[JOY_MAX];

void store_button_state(void)
{
	int stick;

	for (stick = 0; stick < JOY_MAX; stick++)
		prev_joy_data[stick] = joy_data[stick];
}

void restore_button_state(void)
{
	int stick, axis;

	for (stick = 0; stick < JOY_MAX; stick++)
		for (axis = 0; axis < JOY_BUTTONS; axis++)
			joy_data[stick].buttons[axis] = prev_joy_data[stick].buttons[axis];
}

void osd_poll_joysticks(void)
{
	if (use_mouse)
		sysdep_mouse_poll();

	if (joy_poll_func)
	{
		if (rapidfire_enable)
			restore_button_state();

		(*joy_poll_func) ();

		/* evaluate joystick movements */
		joy_evaluate_moves();

		if (rapidfire_enable)
		{
			store_button_state();
			joystick_rapidfire();
		}
	}

#ifdef UGCICOIN
	if (ugcicoin)
	{
		int id;

		ugci_poll(0);

		/* Check coin-pressed. Simulate a release event */
		for (id = 0; id < MAX_PLAYERS; id++)
		{
			if (coin_pressed[id] && coin_pressed[id]++ > MIN_COIN_WAIT)
			{
				struct xmame_keyboard_event event;

				event.press = coin_pressed[id] = 0;
				event.scancode = COIN_KEYCODE_BASE + id;
				xmame_keyboard_register_event(&event);
			}
		}
	}
#endif

#ifdef USE_LIGHTGUN_ABS_EVENT
	lightgun_event_abs_poll();
#endif
}



/*============================================================ */
/*	osd_is_code_pressed */
/*============================================================ */

INT32 osd_get_code_value(os_code_t code)
{
	if (IS_KEYBOARD_CODE(code))
		return is_key_pressed(code);
	else
		return get_joycode_value(code);
}



/*============================================================ */
/*	osd_get_code_list */
/*============================================================ */

const struct OSCodeInfo *osd_get_code_list(void)
{
	return codelist;
}



/*============================================================ */
/*	osd_joystick_needs_calibration */
/*============================================================ */

int osd_joystick_needs_calibration(void)
{
	return 0;
}



/*============================================================ */
/*	osd_joystick_start_calibration */
/*============================================================ */

void osd_joystick_start_calibration(void)
{
}



/*============================================================ */
/*	osd_joystick_calibrate_next */
/*============================================================ */

const char *osd_joystick_calibrate_next(void)
{
	return 0;
}



/*============================================================ */
/*	osd_joystick_calibrate */
/*============================================================ */

void osd_joystick_calibrate(void)
{
}



/*============================================================ */
/*	osd_joystick_end_calibration */
/*============================================================ */

void osd_joystick_end_calibration(void)
{
}


/*============================================================ */
/*	osd_customize_inputport_list */
/*============================================================ */

void osd_customize_inputport_list(struct InputPortDefinition *defaults)
{
	static input_seq_t no_alt_tab_seq = SEQ_DEF_5(KEYCODE_TAB, CODE_NOT, KEYCODE_LALT, CODE_NOT, KEYCODE_RALT);
	struct InputPortDefinition *idef = defaults;

	/* loop over all the defaults */
	while (idef->type != IPT_END)
	{
		/* map in some OSD-specific keys */
		switch (idef->type)
		{
			/* alt-enter for fullscreen */
			case IPT_OSD_1:
				idef->token = "TOGGLE_FULLSCREEN";
				idef->name = "Toggle fullscreen";
				seq_set_2(&idef->defaultseq, KEYCODE_LALT, KEYCODE_ENTER);
				break;

#ifdef MESS
			case IPT_OSD_2:
				if (options.disable_normal_ui)
				{
					idef->token = "TOGGLE_MENUBAR";
					idef->name = "Toggle menubar";
					seq_set_1 (&idef->defaultseq, KEYCODE_SCRLOCK);
				}
				break;
#endif /* MESS */
		}

		/* disable the config menu if the ALT key is down */
		/* (allows ALT-TAB to switch between windows apps) */
		if (idef->type == IPT_UI_CONFIGURE)
			seq_copy(&idef->defaultseq, &no_alt_tab_seq);

#ifdef MESS
		if (idef->type == IPT_UI_THROTTLE)
			seq_set_0(&idef->defaultseq);
#endif /* MESS */

		/* find the next one */
		idef++;
	}
}



/*============================================================ */
/*	decode_digital */
/*============================================================ */

static int decode_digital(struct rc_option *option, const char *arg, int priority)
{
	if (strcmp(arg, "none") == 0)
		memset(joystick_digital, 0, sizeof(joystick_digital));
	else if (strcmp(arg, "all") == 0)
		memset(joystick_digital, 1, sizeof(joystick_digital));
	else
	{
		/* scan the string */
		while (1)
		{
			int joynum = 0;
			int axisnum = 0;
			
			/* stop if we hit the end */
			if (arg[0] == 0)
				break;
			
			/* we require the next bits to be j<N> */
			if (tolower(arg[0]) != 'j' || sscanf(&arg[1], "%d", &joynum) != 1)
				goto usage;
			arg++;
			while (arg[0] != 0 && isdigit(arg[0]))
				arg++;
			
			/* if we are followed by a comma or an end, mark all the axes digital */
			if (arg[0] == 0 || arg[0] == ',')
			{
				if (joynum != 0 && joynum - 1 < JOY_MAX)
					memset(&joystick_digital[joynum - 1], 1, sizeof(joystick_digital[joynum - 1]));
				if (arg[0] == 0)
					break;
				arg++;
				continue;
			}

			/* loop over axes */
			while (1)
			{
				/* stop if we hit the end */
				if (arg[0] == 0)
					break;
				
				/* if we hit a comma, skip it and break out */
				if (arg[0] == ',')
				{
					arg++;
					break;
				}
				
				/* we require the next bits to be a<N> */
				if (tolower(arg[0]) != 'a' || sscanf(&arg[1], "%d", &axisnum) != 1)
					goto usage;
				arg++;
				while (arg[0] != 0 && isdigit(arg[0]))
					arg++;
				
				/* set that axis to digital */
				if (joynum != 0 && joynum - 1 < JOY_MAX && axisnum < JOY_AXES)
					joystick_digital[joynum - 1][axisnum] = 1;
			}
		}
	}
	option->priority = priority;
	return 0;

usage:
	fprintf(stderr, "error: invalid value for digital: %s -- valid values are:\n", arg);
	fprintf(stderr, "         none -- no axes on any joysticks are digital\n");
	fprintf(stderr, "         all -- all axes on all joysticks are digital\n");
	fprintf(stderr, "         j<N> -- all axes on joystick <N> are digital\n");
	fprintf(stderr, "         j<N>a<M> -- axis <M> on joystick <N> is digital\n");
	fprintf(stderr, "    Multiple axes can be specified for one joystick:\n");
	fprintf(stderr, "         j1a5a6 -- axes 5 and 6 on joystick 1 are digital\n");
	fprintf(stderr, "    Multiple joysticks can be specified separated by commas:\n");
	fprintf(stderr, "         j1,j2a2 -- all joystick 1 axes and axis 2 on joystick 2 are digital\n");
	return -1;
}



/*============================================================ */
/*	osd_get_leds */
/*============================================================ */

int osd_get_leds(void)
{
	return 0;
}



/*============================================================ */
/*	osd_set_leds */
/*============================================================ */

void osd_set_leds(int state)
{
}



/*============================================================ */
/*	osd_keyboard_disabled */
/*============================================================ */

int osd_keyboard_disabled()
{
	return 0;
}



/*============================================================ */
/*	osd_trying_to_quit */
/*============================================================ */

int osd_trying_to_quit()
{
	return 0;
}
