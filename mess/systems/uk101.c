/**********************************************************************
UK101 Memory map

	CPU: 6502 @ 1.0Mhz

Interrupts:	None.

Video:		Memory mapped

Sound:		None

Hardware:	MC6850

		0000-0FFF	RAM (standard)
		1000-1FFF	RAM (expanded)
		2000-9FFF	RAM	(emulator only)
		A000-BFFF	ROM (basic)
		C000-CFFF	NOP
		D000-D3FF	RAM (video)
		D400-DEFF	NOP
		DF00-DF00	H/W (Keyboard)
		DF01-EFFF	NOP
		F000-F001	H/W (MC6850)
		F002-F7FF	NOP
		F800-FFFF	ROM (monitor)
**********************************************************************/
#include "driver.h"
#include "inputx.h"
#include "cpu/m6502/m6502.h"
#include "vidhrdw/generic.h"
#include "machine/mc6850.h"
#include "includes/uk101.h"

/* memory w/r functions */

ADDRESS_MAP_START( uk101_mem , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x0fff) AM_READWRITE( MRA8_BANK1, MWA8_BANK1 )
	AM_RANGE(0x1000, 0x1fff) AM_READWRITE( MRA8_BANK2, MWA8_BANK2 )
	AM_RANGE(0x2000, 0x9fff) AM_READWRITE( MRA8_BANK3, MWA8_BANK3 )
	AM_RANGE(0xa000, 0xbfff) AM_ROM
	AM_RANGE(0xc000, 0xcfff) AM_NOP
	AM_RANGE(0xd000, 0xd3ff) AM_READWRITE( videoram_r, videoram_w) AM_BASE(&videoram) AM_SIZE(&videoram_size)
	AM_RANGE(0xd400, 0xdeff) AM_NOP
	AM_RANGE(0xdf00, 0xdf00) AM_READWRITE( uk101_keyb_r, uk101_keyb_w )
	AM_RANGE(0xdf01, 0xefff) AM_NOP
	AM_RANGE(0xf000, 0xf001) AM_READWRITE( acia6850_0_r, acia6850_0_w )
	AM_RANGE(0xf002, 0xf7ff) AM_NOP
	AM_RANGE(0xf800, 0xffff) AM_ROM
ADDRESS_MAP_END



/*
 Relaxed decoding as implemented in the Superboard II

		0000-0FFF	RAM (standard)
		1000-1FFF	RAM (expanded)
		2000-9FFF	RAM	(emulator only)
		A000-BFFF	ROM (basic)
		C000-CFFF	NOP
		D000-D3FF	RAM (video)
		D400-DBFF	NOP
		DC00-DFFF	H/W (Keyboard)
		E000-EFFF	NOP
		F000-F0FF	H/W (MC6850)
		F100-F7FF	NOP
		F800-FFFF	ROM (monitor)
*/
ADDRESS_MAP_START( superbrd_mem , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x0fff) AM_READWRITE( MRA8_BANK1, MWA8_BANK1 )
	AM_RANGE(0x1000, 0x1fff) AM_READWRITE( MRA8_BANK2, MWA8_BANK2 )
	AM_RANGE(0x2000, 0x9fff) AM_READWRITE( MRA8_BANK3, MWA8_BANK3 )
	AM_RANGE(0xa000, 0xbfff) AM_ROM
	AM_RANGE(0xc000, 0xcfff) AM_NOP
	AM_RANGE(0xd000, 0xd7ff) AM_READWRITE( videoram_r, videoram_w) AM_BASE(&videoram) AM_SIZE(&videoram_size)
	AM_RANGE(0xd800, 0xdbff) AM_NOP
	AM_RANGE(0xdc00, 0xdfff) AM_READWRITE( uk101_keyb_r, superbrd_keyb_w )
	AM_RANGE(0xe000, 0xefff) AM_NOP
	AM_RANGE(0xf000, 0xf0ff) AM_READWRITE( acia6850_0_r, acia6850_0_w )
	AM_RANGE(0xf100, 0xf7ff) AM_NOP
	AM_RANGE(0xf800, 0xffff) AM_ROM
ADDRESS_MAP_END


/* graphics output */

struct GfxLayout uk101_charlayout =
{
	8, 16,
	256,
	1,
	{ 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 0*8, 1*8, 1*8, 2*8, 2*8, 3*8, 3*8,
	  4*8, 4*8, 5*8, 5*8, 6*8, 6*8, 7*8, 7*8 },
	8 * 8
};

struct GfxLayout superbrd_charlayout =
{
	8, 8,
	256,
	1,
	{ 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8 * 8
};

static struct	GfxDecodeInfo uk101_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &uk101_charlayout, 0, 1},
	{-1}
};

static struct	GfxDecodeInfo superbrd_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &superbrd_charlayout, 0, 1},
	{-1}
};

static unsigned char uk101_palette[2 * 3] =
{
	0x00, 0x00, 0x00,	/* Black */
	0xff, 0xff, 0xff	/* White */
};

static unsigned short uk101_colortable[] =
{
	0,1
};

static PALETTE_INIT( uk101 )
{
	palette_set_colors(0, uk101_palette, sizeof(uk101_palette) / 3);
	memcpy(colortable, uk101_colortable, sizeof (uk101_colortable));
}

/* keyboard input */
INPUT_PORTS_START( uk101 )
	PORT_START	/* 0: DF00 & 0x80 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7") PORT_CODE(KEYCODE_7)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6") PORT_CODE(KEYCODE_6)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5") PORT_CODE(KEYCODE_5)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4") PORT_CODE(KEYCODE_4)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3") PORT_CODE(KEYCODE_3)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2") PORT_CODE(KEYCODE_2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1") PORT_CODE(KEYCODE_1)
	PORT_START /* 1: DF00 & 0x40 */
	PORT_BIT( 0x03, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Backspace") PORT_CODE(KEYCODE_BACKSPACE)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(":") PORT_CODE(KEYCODE_COLON)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8") PORT_CODE(KEYCODE_8)
	PORT_START /* 2: DF00 & 0x20 */
	PORT_BIT( 0x07, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("\\") PORT_CODE(KEYCODE_BACKSLASH)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O") PORT_CODE(KEYCODE_O)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".") PORT_CODE(KEYCODE_STOP)
	PORT_START /* 3: DF00 & 0x10 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W)
	PORT_START /* 4: DF00 & 0x08 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S)
	PORT_START /* 5: DF00 & 0x04 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(",") PORT_CODE(KEYCODE_COMMA)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X)
	PORT_START /* 6: DF00 & 0x02 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P") PORT_CODE(KEYCODE_P)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("'") PORT_CODE(KEYCODE_QUOTE)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("/") PORT_CODE(KEYCODE_SLASH)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Space") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q)
	PORT_START /* 7: DF00 & 0x01 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Caps Lock") PORT_CODE(KEYCODE_CAPSLOCK) PORT_TOGGLE
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Right Shift") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Left Shift") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT( 0x18, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Escape") PORT_CODE(KEYCODE_ESC)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Control") PORT_CODE(KEYCODE_LCONTROL)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Control") PORT_CODE(KEYCODE_RCONTROL)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("~") PORT_CODE(KEYCODE_TILDE)
INPUT_PORTS_END

/* Superboard keyboard

Matrix (see http://www.technology.niagarac.on.ca/people/mcsele/images/OSI600SchematicsPg11-12.jpg)

    C7 C6 C5 C4 C3 C2 C1 C0
R7   1  2  3  4  5  6  7
R6   8  9  0  :  - Ro
R5   .  L  O Lf Cr
R4   W  E  R  T  Y  U  I
R3   S  D  F  G  H  J  K
R2   X  C  V  B  N  M  ,
R1   Q  A  Z Sp  /  ;  P
R0  Rp Ct Ec       Ls Rs Sl  

Cr = RETURN, Ct = CTRL, Ec = ESC, Lf= LINE FEED, Ls = left SHIFT, Ro = RUB OUT
Rp = REPEAT, Rs = right SHIFT, Sl = SHIFT LOCK, Sp = space bar 

Layout (see http://www.technology.niagarac.on.ca/people/mcsele/images/OSISuperboardII.jpg)

! " # $ % & ' ( )   * = RUB
1 2 3 4 5 6 7 8 9 0 : - OUT

                        LINE
ESC Q W E R T Y U I O P FEED RETURN

                       + SHIFT
CTRL A S D F G H J K L ; LOCK  REPEAT BREAK

                    < > ?
SHIFT Z X C V B N M , . / SHIFT

     ---SPACE BAR---
*/
INPUT_PORTS_START( superbrd )
	PORT_START	/* 0: DF00 & 0x80 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7  '") PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('\'')	/* 7 ' */
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6  &") PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('&')	/* 6 & */
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5  %") PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')	/* 5 % */
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4  $") PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('$')	/* 4 $ */
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3  #") PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')	/* 3 # */
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2  \"") PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('\"')	/* 2 " */
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1  !") PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')	/* 1 ! */
	PORT_START /* 1: DF00 & 0x40 */
	PORT_BIT( 0x03, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("RUBOUT") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)	/* RUBOUT */
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("-  =") PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('-') PORT_CHAR('=') /* - = */
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(":  *") PORT_CODE(KEYCODE_MINUS) PORT_CHAR(':') PORT_CHAR('*') /* : * */
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0   ") PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9  )") PORT_CODE(KEYCODE_9) PORT_CHAR('9') PORT_CHAR(')')	/* 9 ) */
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8  (") PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('(')	/* 8 ( */
	PORT_START /* 2: DF00 & 0x20 */
	PORT_BIT( 0x07, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ENTER") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("LINE FEED") PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR(10) /* LINE FEED */
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O") PORT_CODE(KEYCODE_O) PORT_CHAR('O')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L) PORT_CHAR('L')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".  >") PORT_CODE(KEYCODE_STOP) PORT_CHAR('.') PORT_CHAR('>') /* . > */
	PORT_START /* 3: DF00 & 0x10 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I) PORT_CHAR('I')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U) PORT_CHAR('U')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y) PORT_CHAR('Y')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T) PORT_CHAR('T')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R) PORT_CHAR('R')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E) PORT_CHAR('E')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W) PORT_CHAR('W')
	PORT_START /* 4: DF00 & 0x08 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K) PORT_CHAR('K')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J) PORT_CHAR('J')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G) PORT_CHAR('G')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_START /* 5: DF00 & 0x04 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(",  <") PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',') PORT_CHAR('<') /* , < */
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M) PORT_CHAR('M')
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N) PORT_CHAR('N')
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B) PORT_CHAR('B')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V) PORT_CHAR('V')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C) PORT_CHAR('C')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X) PORT_CHAR('X')
	PORT_START /* 6: DF00 & 0x02 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P") PORT_CODE(KEYCODE_P) PORT_CHAR('P')
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(";  +") PORT_CODE(KEYCODE_COLON) PORT_CHAR(';') PORT_CHAR('+') /* ; + */
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("/  ?") PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?') /* / ? */
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SPACE") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z) PORT_CHAR('Z')
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
	PORT_START /* 7: DF00 & 0x01 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SHIFT LOCK") PORT_CODE(KEYCODE_CAPSLOCK) PORT_CHAR(UCHAR_MAMEKEY(CAPSLOCK)) PORT_TOGGLE
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R-SHIFT") PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L-SHIFT") PORT_CODE(KEYCODE_LSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0x18, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ESC") PORT_CODE(KEYCODE_TAB) PORT_CHAR(27)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("CTRL") PORT_CODE(KEYCODE_CAPSLOCK) PORT_CHAR(UCHAR_SHIFT_2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("REPEAT") PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\') /* REPEAT */
INPUT_PORTS_END

static INTERRUPT_GEN( uk101_interrupt )
{
	cpunum_set_input_line(0, 0, PULSE_LINE);
}

/* machine definition */
static MACHINE_DRIVER_START( uk101 )
	/* basic machine hardware */
	MDRV_CPU_ADD_TAG("main", M6502, 1000000)
	MDRV_CPU_PROGRAM_MAP( uk101_mem, 0 )
	MDRV_CPU_VBLANK_INT(uk101_interrupt, 1)
	MDRV_FRAMES_PER_SECOND(50)
	MDRV_VBLANK_DURATION(2500)
	MDRV_INTERLEAVE(1)

    /* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(32 * 8, 25 * 16)
	MDRV_VISIBLE_AREA(0, 32 * 8 - 1, 0, 25 * 16 - 1)
	MDRV_GFXDECODE( uk101_gfxdecodeinfo )
	MDRV_PALETTE_LENGTH(2)
	MDRV_COLORTABLE_LENGTH(2)
	MDRV_PALETTE_INIT( uk101 )

	MDRV_VIDEO_START( generic )
	MDRV_VIDEO_UPDATE( uk101 )
MACHINE_DRIVER_END



static MACHINE_DRIVER_START( superbrd )
	MDRV_IMPORT_FROM( uk101 )
	MDRV_CPU_MODIFY( "main" )
	MDRV_GFXDECODE( superbrd_gfxdecodeinfo )
	MDRV_CPU_PROGRAM_MAP( superbrd_mem, 0 )
	MDRV_SCREEN_SIZE(32 * 8, 32 * 8)
	MDRV_VISIBLE_AREA(0, 32 * 8 - 1, 0, 32 * 8 - 1)
	MDRV_VIDEO_UPDATE( superbrd )
	MDRV_INTERLEAVE(0)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")

	MDRV_SOUND_ADD(DAC, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)
MACHINE_DRIVER_END

ROM_START(uk101)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("basuk01.rom", 0xa000, 0x0800, CRC(9d3caa92))
	ROM_LOAD("basuk02.rom", 0xa800, 0x0800, CRC(0039ef6a))
	ROM_LOAD("basuk03.rom", 0xb000, 0x0800, CRC(0d011242))
	ROM_LOAD("basuk04.rom", 0xb800, 0x0800, CRC(667223e8))
	ROM_LOAD("monuk02.rom", 0xf800, 0x0800, CRC(04ac5822))
	ROM_REGION(0x800, REGION_GFX1,0)
	ROM_LOAD("chguk101.rom", 0x0000, 0x0800, CRC(fce2c84a))
ROM_END

ROM_START(superbrd)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("basus01.rom", 0xa000, 0x0800, CRC(f4f5dec0))
	ROM_LOAD("basuk02.rom", 0xa800, 0x0800, CRC(0039ef6a))
	ROM_LOAD("basus03.rom", 0xb000, 0x0800, CRC(ca25f8c1))
	ROM_LOAD("basus04.rom", 0xb800, 0x0800, CRC(8ee6030e))
	ROM_LOAD("monde01.rom", 0xf800, 0x0800, CRC(95a44d2e))
	ROM_REGION(0x800, REGION_GFX1,0)
	ROM_LOAD("chgsup2.rom", 0x0000, 0x0800, CRC(735f5e0a))
ROM_END

static void uk101_cassette_getinfo(struct IODevice *dev)
{
	/* cassette */
	dev->type = IO_CASSETTE;
	dev->count = 1;
	dev->file_extensions = "bas\0";
	dev->readable = 1;
	dev->writeable = 0;
	dev->creatable = 0;
	dev->load = device_load_uk101_cassette;
	dev->unload = device_unload_uk101_cassette;
}

SYSTEM_CONFIG_START(uk101)
	CONFIG_DEVICE(uk101_cassette_getinfo)
	CONFIG_RAM_DEFAULT(4 * 1024)
	CONFIG_RAM(8 * 1024)
	CONFIG_RAM(40 * 1024)
SYSTEM_CONFIG_END

/*    YEAR	NAME		PARENT	COMPAT	MACHINE		INPUT	INIT	CONFIG  COMPANY				FULLNAME */
COMP( 1979,	uk101,		0,		0,		uk101,		uk101,	uk101,	uk101,	"Compukit",			"UK101" )
COMP( 1979, superbrd,	uk101,	0,		superbrd,	superbrd,	uk101,	uk101,	"Ohio Scientific",	"Superboard II" )

