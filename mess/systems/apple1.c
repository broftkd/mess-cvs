/**********************************************************************

Apple I

CPU:		6502 @ 1.023 MHz
			(Effective speed with RAM refresh waits is 0.960 MHz.)

Memory:		4-8 KB on main board (4 KB standard)

			Additional memory could be added via the expansion
			connector, but the user was responsible for making sure
			the extra memory was properly interfaced.

			Some users replaced the onboard 4-kilobit RAM chips with
			16-kilobit RAM chips, increasing on-board memory to 32 KB,
			but this required modifying the RAM interface circuitry.

Interrupts:	None.
			(The system board had jumpers to allow interrupts, but
			these were not connected in a standard system.)

Video:		Dumb terminal, based on 7 1K-bit shift registers

Sound:		None

Hardware:	Motorola 6820 PIA for keyboard and display interface

Memory map:

$0000-$1FFF:	RAM address space
	$0000-$00FF:	6502 zero page
		$0024-$002B:	Zero page locations used by the Monitor
	$0100-$01FF:	6502 processor stack
	$0200-$027F:	Keyboard input buffer storage used by the Monitor
	$0280-$0FFF:	RAM space available for a program in a 4 KB system
	$1000-$1FFF:	Extra RAM space available for a program in an 8 KB system
					not using cassette BASIC

$2000-$BFFF:	Unused address space, available for RAM in systems larger
				than 8 KB.

$C000-$CFFF:	Address space for optional cassette interface
	$C028:			Cassette interface output port
	$C100-$C1FF:	Cassette interface ROM

$D000-$DFFF:	I/O address space
	$D010-$D013:	Motorola 6820 PIA address space
		$D010:  		Keyboard input port
		$D011:  		Control register for keyboard input port, with
						key-available flag.
		$D012:  		Display output port (bit 7 is a status input)
		$D013:  		Control register for display output port

$E000-$EFFF:	Extra RAM space available for a program in an 8 KB system
				modified to use cassette BASIC

$F000-$FFFF:	ROM address space
	$FF00-$FFFF:	Apple Monitor ROM

**********************************************************************/

#include "driver.h"
#include "cpu/m6502/m6502.h"
#include "machine/6821pia.h"
#include "vidhrdw/generic.h"
#include "includes/apple1.h"
#include "devices/snapquik.h"
#include "inputx.h"

/* port i/o functions */

/* memory w/r functions */

static ADDRESS_MAP_START( apple1_map, ADDRESS_SPACE_PROGRAM, 8 )
	AM_RANGE(0x0000, 0xcfff) AM_NOP
	AM_RANGE(0xd000, 0xd00f) AM_NOP
	AM_RANGE(0xd010, 0xd013) AM_READWRITE(pia_0_r, pia_0_w)
	AM_RANGE(0xd014, 0xfeff) AM_NOP
	AM_RANGE(0xff00, 0xffff) AM_ROM
ADDRESS_MAP_END

/* graphics output */

struct GfxLayout apple1_charlayout =
{
	7, 8,				/* character cell is 7 pixels wide by 8 pixels high */
	64,					/* 64 characters in 2513 character generator ROM */
	1,					/* 1 bitplane */
	{ 0 },
	/* 5 visible pixels per row, starting at bit 3, with MSB being 0: */
	{ 3, 4, 5, 6, 7 },
	/* pixel rows stored from top to bottom: */
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8 * 8				/* 8 8-bit pixel rows per character */
};

static struct	GfxDecodeInfo apple1_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x0000, &apple1_charlayout, 0, 1},
	{ -1 }
};

/* Monochrome monitors were not easy to get when the Apple I was
   introduced, so most systems used a television display with an RF
   modulator.  Thus white seems like a more accurate foreground color
   than green. */

static unsigned char apple1_palette[] =
{
	0x00, 0x00, 0x00,	/* Black */
	0xff, 0xff, 0xff	/* White */
};

static unsigned short apple1_colortable[] =
{
	0, 1
};

static PALETTE_INIT( apple1 )
{
	palette_set_colors(0, apple1_palette, sizeof(apple1_palette) / 3);
	memcpy(colortable, apple1_colortable, sizeof (apple1_colortable));
}

/* keyboard input */

INPUT_PORTS_START( apple1 )
	PORT_START	/* 0: first sixteen keys */
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("1") PORT_CODE(KEYCODE_1) PORT_CHAR('1')
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("2") PORT_CODE(KEYCODE_2) PORT_CHAR('2')
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("3") PORT_CODE(KEYCODE_3) PORT_CHAR('3')
	PORT_BIT( 0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("4") PORT_CODE(KEYCODE_4) PORT_CHAR('4')
	PORT_BIT( 0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("5") PORT_CODE(KEYCODE_5) PORT_CHAR('5')
	PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("6") PORT_CODE(KEYCODE_6) PORT_CHAR('6')
	PORT_BIT( 0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("7") PORT_CODE(KEYCODE_7) PORT_CHAR('7')
	PORT_BIT( 0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("8") PORT_CODE(KEYCODE_8) PORT_CHAR('8')
	PORT_BIT( 0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9) PORT_CHAR('9')
	PORT_BIT( 0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-')
	PORT_BIT( 0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("=") PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('=')
	PORT_BIT( 0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("[") PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[')
	PORT_BIT( 0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("]") PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']')
	PORT_BIT( 0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME(";") PORT_CODE(KEYCODE_COLON) PORT_CHAR(';')
	PORT_BIT( 0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("'") PORT_CODE(KEYCODE_QUOTE) PORT_CHAR('\'')
	PORT_START	/* 1: second sixteen keys */
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME(",") PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',')
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME(".") PORT_CODE(KEYCODE_STOP) PORT_CHAR('.')
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("/") PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/')
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("\\") PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\')
	PORT_BIT( 0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("A") PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT( 0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("B") PORT_CODE(KEYCODE_B) PORT_CHAR('B')
	PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("C") PORT_CODE(KEYCODE_C) PORT_CHAR('C')
	PORT_BIT( 0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("D") PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT( 0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("E") PORT_CODE(KEYCODE_E) PORT_CHAR('E')
	PORT_BIT( 0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F") PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT( 0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("G") PORT_CODE(KEYCODE_G) PORT_CHAR('G')
	PORT_BIT( 0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("H") PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT( 0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("I") PORT_CODE(KEYCODE_I) PORT_CHAR('I')
	PORT_BIT( 0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("J") PORT_CODE(KEYCODE_J) PORT_CHAR('J')
	PORT_BIT( 0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("K") PORT_CODE(KEYCODE_K) PORT_CHAR('K')
	PORT_BIT( 0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("L") PORT_CODE(KEYCODE_L) PORT_CHAR('L')
	PORT_START	/* 2: third sixteen keys */
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("M") PORT_CODE(KEYCODE_M) PORT_CHAR('M')
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("N") PORT_CODE(KEYCODE_N) PORT_CHAR('N')
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("O") PORT_CODE(KEYCODE_O) PORT_CHAR('O')
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("P") PORT_CODE(KEYCODE_P) PORT_CHAR('P')
	PORT_BIT( 0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Q") PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
	PORT_BIT( 0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("R") PORT_CODE(KEYCODE_R) PORT_CHAR('R')
	PORT_BIT( 0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("S") PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_BIT( 0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("T") PORT_CODE(KEYCODE_T) PORT_CHAR('T')
	PORT_BIT( 0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("U") PORT_CODE(KEYCODE_U) PORT_CHAR('U')
	PORT_BIT( 0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("V") PORT_CODE(KEYCODE_V) PORT_CHAR('V')
	PORT_BIT( 0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("W") PORT_CODE(KEYCODE_W) PORT_CHAR('W')
	PORT_BIT( 0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("X") PORT_CODE(KEYCODE_X) PORT_CHAR('X')
	PORT_BIT( 0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Y") PORT_CODE(KEYCODE_Y) PORT_CHAR('Y')
	PORT_BIT( 0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Z") PORT_CODE(KEYCODE_Z) PORT_CHAR('Z')
	PORT_BIT( 0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER) PORT_CHAR('\r')
	PORT_BIT( 0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Backarrow") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR('_')
	PORT_START	/* 3: fourth sixteen keys */
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Space") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Escape") PORT_CODE(KEYCODE_ESC) PORT_CHAR('\x1b')
	PORT_START	/* 4: shift keys */
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Shift") PORT_CODE(KEYCODE_LSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Shift") PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT( 0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Control") PORT_CODE(KEYCODE_LCONTROL) PORT_CHAR(UCHAR_SHIFT_2)
	PORT_BIT( 0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Control") PORT_CODE(KEYCODE_RCONTROL) PORT_CHAR(UCHAR_SHIFT_2)
	PORT_START	/* 5: RESET and CLEAR SCREEN pushbutton switches */
	PORT_BIT( 0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Reset") PORT_CODE(KEYCODE_F12)
	PORT_BIT( 0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Clear") PORT_CODE(KEYCODE_F2)
INPUT_PORTS_END

/* sound output */

/* machine definition */
static MACHINE_DRIVER_START( apple1 )
	/* basic machine hardware */
	/* Actual CPU speed is 1.023 MHz, but RAM refresh effectively
	   slows it to 960 kHz. */
	MDRV_CPU_ADD_TAG("main", M6502, 960000)        /* 1.023 Mhz */
	MDRV_CPU_PROGRAM_MAP(apple1_map, 0)
	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_REAL_60HZ_VBLANK_DURATION)
	MDRV_INTERLEAVE(1)

	MDRV_MACHINE_INIT( apple1 )

	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	/* It would be nice if we could implement some sort of display
	   overscan here. */
	MDRV_SCREEN_SIZE(40 * 7, 24 * 8)
	MDRV_VISIBLE_AREA(0, 40 * 7 - 1, 0, 24 * 8 - 1)
	MDRV_GFXDECODE(apple1_gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(sizeof (apple1_palette) / 3)
	MDRV_COLORTABLE_LENGTH(sizeof(apple1_colortable)/sizeof(unsigned short))
	MDRV_PALETTE_INIT(apple1)

	MDRV_VIDEO_START(apple1)
	MDRV_VIDEO_UPDATE(apple1)
MACHINE_DRIVER_END


ROM_START(apple1)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("apple1.rom", 0xff00, 0x0100, CRC(a30b6af5) SHA1(224767aa499dc98767e042f375ced1359be8a35f))
	/* 512-byte Signetics 2513 character generator ROM: */
	ROM_REGION(0x0200, REGION_GFX1,0)
	ROM_LOAD("apple1.vid", 0x0000, 0x0200, CRC(a7e567fc) SHA1(b18aae0a2d4f92f5a7e22640719bbc4652f3f4ee))
ROM_END

static void apple1_snapshot_getinfo(struct IODevice *dev)
{
	/* snapshot */
	snapshot_device_getinfo(dev, snapshot_load_apple1, 0.0);
	dev->file_extensions = "snp\0";
}

SYSTEM_CONFIG_START(apple1)
	CONFIG_DEVICE(apple1_snapshot_getinfo)
	CONFIG_RAM_DEFAULT	(0x1000)
	CONFIG_RAM			(0x2000)
	CONFIG_RAM			(0x3000)
	CONFIG_RAM			(0x4000)
	CONFIG_RAM			(0x5000)
	CONFIG_RAM			(0x6000)
	CONFIG_RAM			(0x7000)
	CONFIG_RAM			(0x8000)
	CONFIG_RAM			(0x9000)
	CONFIG_RAM			(0xA000)
	CONFIG_RAM			(0xB000)
	CONFIG_RAM			(0xC000)
	CONFIG_RAM			(0xD000)
SYSTEM_CONFIG_END

/*    YEAR	NAME	PARENT	COMPAT	MACHINE		INPUT		INIT	CONFIG	COMPANY				FULLNAME */
COMP( 1976,	apple1,	0,		0,		apple1,		apple1,		apple1,	apple1,	"Apple Computer",	"Apple I" )
