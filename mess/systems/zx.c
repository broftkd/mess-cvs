/***************************************************************************
	zx.c

    Orginal driver by:
	Juergen Buchmueller <pullmoll@t-online.de>, Dec 1999

    Fixes and additions by Krzysztof Strzecha:
	07.06.2004 Tape loading added. Some cleanups of debug code.
		   Fixed stupid bug in timings (vblank duration).
		   GAME_NOT_WORKING flag removed.
    	29.05.2004 CPU clock, number of scanlines, vblank duration corrected.
		   Some cleanups. Two non-working TESTDRIVERS added.
    	14.05.2004 Finally fixed and readded.

    To do:
	Tape saving (needs changes in video hardware emulation).
	Some memory areas are not mirrored as they should.
	Video hardware is not fully emulated, so it does not support pseudo
	hi-res and hi-res modes.
	Some memory packs are unemulated.

****************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "cpu/z80/z80.h"
#include "includes/zx.h"
#include "devices/cassette.h"
#include "formats/zx81_p.h"

ADDRESS_MAP_START( readmem_zx80 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x0fff) AM_READ( MRA8_ROM)
	AM_RANGE(0x4000, 0x43ff) AM_READ( MRA8_RAM)
	AM_RANGE(0x8000, 0xffff) AM_READ( MRA8_NOP)
ADDRESS_MAP_END

ADDRESS_MAP_START( writemem_zx80 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x0fff) AM_WRITE( MWA8_ROM)
	AM_RANGE(0x4000, 0x43ff) AM_WRITE( MWA8_RAM)
	AM_RANGE(0x8000, 0xffff) AM_WRITE( MWA8_NOP)
ADDRESS_MAP_END

ADDRESS_MAP_START( readmem_zx81 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x1fff) AM_READ( MRA8_ROM)
	AM_RANGE(0x4000, 0x43ff) AM_READ( MRA8_RAM)
	AM_RANGE(0x8000, 0xffff) AM_READ( MRA8_NOP)
ADDRESS_MAP_END

ADDRESS_MAP_START( writemem_zx81 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x3fff) AM_WRITE( MWA8_ROM)
	AM_RANGE(0x4000, 0x43ff) AM_WRITE( MWA8_RAM)
	AM_RANGE(0x8000, 0xffff) AM_WRITE( MWA8_NOP)
ADDRESS_MAP_END

ADDRESS_MAP_START( readmem_pc8300 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x1fff) AM_READ( MRA8_ROM)
	AM_RANGE(0x4000, 0x7fff) AM_READ( MRA8_RAM)		   /* PC8300 comes with 16K RAM */
	AM_RANGE(0x8000, 0xffff) AM_READ( MRA8_NOP)
ADDRESS_MAP_END

ADDRESS_MAP_START( writemem_pc8300 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x3fff) AM_WRITE( MWA8_ROM)
	AM_RANGE(0x4000, 0x7fff) AM_WRITE( MWA8_RAM)		   /* PC8300 comes with 16K RAM */
	AM_RANGE(0x8000, 0xffff) AM_WRITE( MWA8_NOP)
ADDRESS_MAP_END

static ADDRESS_MAP_START (readport, ADDRESS_SPACE_IO, 8)
	AM_RANGE(0x0000, 0xffff) AM_READ( zx_io_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START (writeport, ADDRESS_SPACE_IO, 8)
	AM_RANGE(0x0000, 0xffff) AM_WRITE( zx_io_w)
ADDRESS_MAP_END

ADDRESS_MAP_START( readmem_pow3000 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x1fff) AM_READ( MRA8_ROM)
	AM_RANGE(0x4000, 0x7fff) AM_READ( MRA8_RAM)		   /* Power 3000 comes with 16K RAM */
	AM_RANGE(0x8000, 0xffff) AM_READ( MRA8_NOP)
ADDRESS_MAP_END

ADDRESS_MAP_START( writemem_pow3000 , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE(0x0000, 0x3fff) AM_WRITE( MWA8_ROM)
	AM_RANGE(0x4000, 0x7fff) AM_WRITE( MWA8_RAM)		   /* Power 3000 comes with 16K RAM */
	AM_RANGE(0x8000, 0xffff) AM_WRITE( MWA8_NOP)
ADDRESS_MAP_END

static ADDRESS_MAP_START (readport_pow3000, ADDRESS_SPACE_IO, 8)
	AM_RANGE(0x0000, 0xffff) AM_READ( pow3000_io_r)
ADDRESS_MAP_END

static ADDRESS_MAP_START (writeport_pow3000, ADDRESS_SPACE_IO, 8)
	AM_RANGE(0x0000, 0xffff) AM_WRITE( zx_io_w)
ADDRESS_MAP_END

INPUT_PORTS_START(zx80)
PORT_START							   /* IN0 */
PORT_DIPNAME(0x80, 0x00, "16K RAM module")
PORT_DIPSETTING(0x00, DEF_STR( No ) )
PORT_DIPSETTING(0x80, DEF_STR( Yes ) )
PORT_BIT(0x7e, 0x0f, IPT_UNUSED)
PORT_BIT(0x01, 0x00, IPT_KEYBOARD | IPF_RESETCPU) PORT_NAME("Reset") PORT_CODE(KEYCODE_F3)

PORT_START							   /* IN1 KEY ROW 0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z  ") PORT_CODE(KEYCODE_Z)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X  ") PORT_CODE(KEYCODE_X)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C  ") PORT_CODE(KEYCODE_C)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V  ") PORT_CODE(KEYCODE_V)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN2 KEY ROW 1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A  ") PORT_CODE(KEYCODE_A)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S  ") PORT_CODE(KEYCODE_S)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D  ") PORT_CODE(KEYCODE_D)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F  ") PORT_CODE(KEYCODE_F)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G  ") PORT_CODE(KEYCODE_G)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN3 KEY ROW 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q  ") PORT_CODE(KEYCODE_Q)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W  ") PORT_CODE(KEYCODE_W)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E  ") PORT_CODE(KEYCODE_E)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R  ") PORT_CODE(KEYCODE_R)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T  ") PORT_CODE(KEYCODE_T)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN4 KEY ROW 3 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1  EDIT") PORT_CODE(KEYCODE_1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2  AND") PORT_CODE(KEYCODE_2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3  THEN") PORT_CODE(KEYCODE_3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4  TO") PORT_CODE(KEYCODE_4)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5  LEFT") PORT_CODE(KEYCODE_5)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN5 KEY ROW 4 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0  RUBOUT") PORT_CODE(KEYCODE_0)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9  GRAPHICS") PORT_CODE(KEYCODE_9)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8  RIGHT") PORT_CODE(KEYCODE_8)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7  UP") PORT_CODE(KEYCODE_7)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6  DOWN") PORT_CODE(KEYCODE_6)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN6 KEY ROW 5 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P  PRINT") PORT_CODE(KEYCODE_P)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O  POKE") PORT_CODE(KEYCODE_O)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I  INPUT") PORT_CODE(KEYCODE_I)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U  IF") PORT_CODE(KEYCODE_U)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y  RETURN") PORT_CODE(KEYCODE_Y)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN7 KEY ROW 6 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ENTER") PORT_CODE(KEYCODE_ENTER)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L  ") PORT_CODE(KEYCODE_L)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K  ") PORT_CODE(KEYCODE_K)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J  ") PORT_CODE(KEYCODE_J)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H  ") PORT_CODE(KEYCODE_H)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN8 KEY ROW 7 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SPACE Pound") PORT_CODE(KEYCODE_SPACE)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".  ,") PORT_CODE(KEYCODE_STOP)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M  >") PORT_CODE(KEYCODE_M)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N  <") PORT_CODE(KEYCODE_N)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B  ") PORT_CODE(KEYCODE_B)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN9 special keys 1 */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(LEFT)") PORT_CODE(KEYCODE_LEFT)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN10 special keys 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(BACKSPACE)") PORT_CODE(KEYCODE_BACKSPACE)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(GRAPHICS)") PORT_CODE(KEYCODE_LALT)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(RIGHT)") PORT_CODE(KEYCODE_RIGHT)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(UP)") PORT_CODE(KEYCODE_UP)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(DOWN)") PORT_CODE(KEYCODE_DOWN)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

INPUT_PORTS_END

INPUT_PORTS_START(zx81)
PORT_START							   /* IN0 */
PORT_DIPNAME(0x80, 0x00, "16K RAM module")
PORT_DIPSETTING(0x00, DEF_STR(No))
PORT_DIPSETTING(0x80, DEF_STR(Yes))
PORT_BIT(0x7e, 0x0f, IPT_UNUSED)
PORT_BIT(0x01, 0x00, IPT_KEYBOARD | IPF_RESETCPU) PORT_NAME("Reset") PORT_CODE(KEYCODE_F3)

PORT_START							   /* IN1 KEY ROW 0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z     :") PORT_CODE(KEYCODE_Z)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X     ;") PORT_CODE(KEYCODE_X)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C     ?") PORT_CODE(KEYCODE_C)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V     /") PORT_CODE(KEYCODE_V)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN2 KEY ROW 1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A     NEW     STOP") PORT_CODE(KEYCODE_A)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S     SAVE    LPRINT") PORT_CODE(KEYCODE_S)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D     DIM     SLOW") PORT_CODE(KEYCODE_D)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F     FOR     FAST") PORT_CODE(KEYCODE_F)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G     GOTO    LLIST") PORT_CODE(KEYCODE_G)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN3 KEY ROW 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q     PLOT    \"\"") PORT_CODE(KEYCODE_Q)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W     UNPLOT  OR") PORT_CODE(KEYCODE_W)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E     REM     STEP") PORT_CODE(KEYCODE_E)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R     RUN     <=") PORT_CODE(KEYCODE_R)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T     RAND    <>") PORT_CODE(KEYCODE_T)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN4 KEY ROW 3 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1     EDIT") PORT_CODE(KEYCODE_1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2     AND") PORT_CODE(KEYCODE_2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3     THEN") PORT_CODE(KEYCODE_3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4     TO") PORT_CODE(KEYCODE_4)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5     LEFT") PORT_CODE(KEYCODE_5)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN5 KEY ROW 4 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0     RUBOUT") PORT_CODE(KEYCODE_0)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9     GRAPHICS") PORT_CODE(KEYCODE_9)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8     RIGHT") PORT_CODE(KEYCODE_8)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7     UP") PORT_CODE(KEYCODE_7)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6     DOWN") PORT_CODE(KEYCODE_6)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN6 KEY ROW 5 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P     PRINT   \"") PORT_CODE(KEYCODE_P)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O     POKE    )") PORT_CODE(KEYCODE_O)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I     INPUT   (") PORT_CODE(KEYCODE_I)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U     IF      $") PORT_CODE(KEYCODE_U)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y     RETURN  >=") PORT_CODE(KEYCODE_Y)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN7 KEY ROW 6 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ENTER FUNCTION") PORT_CODE(KEYCODE_ENTER)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L     LET     =") PORT_CODE(KEYCODE_L)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K     LIST    +") PORT_CODE(KEYCODE_K)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J     LOAD    -") PORT_CODE(KEYCODE_J)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H     GOSUB   **") PORT_CODE(KEYCODE_H)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN8 KEY ROW 7 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SPACE Pound") PORT_CODE(KEYCODE_SPACE)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".     ,") PORT_CODE(KEYCODE_STOP)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M     PAUSE   >") PORT_CODE(KEYCODE_M)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N     NEXT    <") PORT_CODE(KEYCODE_N)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B     SCROLL  *") PORT_CODE(KEYCODE_B)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN9 special keys 1 */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(LEFT)") PORT_CODE(KEYCODE_LEFT)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN10 special keys 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(BACKSPACE)") PORT_CODE(KEYCODE_BACKSPACE)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(GRAPHICS)") PORT_CODE(KEYCODE_LALT)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(RIGHT)") PORT_CODE(KEYCODE_RIGHT)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(UP)") PORT_CODE(KEYCODE_UP)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(DOWN)") PORT_CODE(KEYCODE_DOWN)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

INPUT_PORTS_END

INPUT_PORTS_START(pow3000)
PORT_START							   /* IN0 */
PORT_BIT(0xfe, 0x0f, IPT_UNUSED)
PORT_BIT(0x01, 0x00, IPT_KEYBOARD | IPF_RESETCPU) PORT_NAME("Reset") PORT_CODE(KEYCODE_F3)

PORT_START							   /* IN1 KEY ROW 0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Z     CLS??") PORT_CODE(KEYCODE_Z)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("X     AUTO??") PORT_CODE(KEYCODE_X)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("C     ???") PORT_CODE(KEYCODE_C)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("V     LEFT") PORT_CODE(KEYCODE_V)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN2 KEY ROW 1 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("A     UP??") PORT_CODE(KEYCODE_A)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("S     DOWN??") PORT_CODE(KEYCODE_S)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("D     RIGHT") PORT_CODE(KEYCODE_D)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F     ") PORT_CODE(KEYCODE_F)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("G     ") PORT_CODE(KEYCODE_G)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN3 KEY ROW 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Q     ") PORT_CODE(KEYCODE_Q)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("W     ") PORT_CODE(KEYCODE_W)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("E     ") PORT_CODE(KEYCODE_E)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("R     ") PORT_CODE(KEYCODE_R)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("T     ") PORT_CODE(KEYCODE_T)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN4 KEY ROW 3 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1     ") PORT_CODE(KEYCODE_1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2     ") PORT_CODE(KEYCODE_2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3     ") PORT_CODE(KEYCODE_3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4     ") PORT_CODE(KEYCODE_4)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5     ") PORT_CODE(KEYCODE_5)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN5 KEY ROW 4 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0     ") PORT_CODE(KEYCODE_0)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9     ") PORT_CODE(KEYCODE_9)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8     ") PORT_CODE(KEYCODE_8)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7     ") PORT_CODE(KEYCODE_7)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6     ") PORT_CODE(KEYCODE_6)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN6 KEY ROW 5 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("P     ") PORT_CODE(KEYCODE_P)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("O     ") PORT_CODE(KEYCODE_O)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("I     ") PORT_CODE(KEYCODE_I)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("U     ") PORT_CODE(KEYCODE_U)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Y     ") PORT_CODE(KEYCODE_Y)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN7 KEY ROW 6 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ENTER ") PORT_CODE(KEYCODE_ENTER)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("L     ") PORT_CODE(KEYCODE_L)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("K     ") PORT_CODE(KEYCODE_K)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("J     ") PORT_CODE(KEYCODE_J)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("H     ") PORT_CODE(KEYCODE_H)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN8 KEY ROW 7 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SPACE ") PORT_CODE(KEYCODE_SPACE)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(".     ") PORT_CODE(KEYCODE_STOP)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("M     ") PORT_CODE(KEYCODE_M)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("N     ") PORT_CODE(KEYCODE_N)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("B     ") PORT_CODE(KEYCODE_B)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN9 special keys 1 */
PORT_BIT(0x0f, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(LEFT)") PORT_CODE(KEYCODE_LEFT)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START							   /* IN10 special keys 2 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(UP)") PORT_CODE(KEYCODE_UP)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(DOWN)") PORT_CODE(KEYCODE_DOWN)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(RIGHT)") PORT_CODE(KEYCODE_RIGHT)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(BACKSPACE)") PORT_CODE(KEYCODE_BACKSPACE)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("(GRAPHICS)") PORT_CODE(KEYCODE_LALT)
PORT_BIT(0xe0, IP_ACTIVE_LOW, IPT_UNUSED)

INPUT_PORTS_END

static struct GfxLayout zx_pixel_layout =
{
	8, 1,							   /* 8x1 pixels */
	256,							   /* 256 codes */
	1,								   /* 1 bit per pixel */
	{0},							   /* no bitplanes */
	/* x offsets */
	{0, 1, 2, 3, 4, 5, 6, 7},
	/* y offsets */
	{0},
	8								   /* one byte per code */
};

static struct GfxLayout zx_char_layout =
{
	8, 8,							   /* 8x8 pixels */
	64,								   /* 64 codes */
	1,								   /* 1 bit per pixel */
	{0},							   /* no bitplanes */
	/* x offsets */
	{0, 1, 2, 3, 4, 5, 6, 7},
	/* y offsets */
	{0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8},
	8 * 8							   /* eight bytes per code */
};

static struct GfxDecodeInfo zx80_gfxdecodeinfo[] =
{
	{REGION_GFX1, 0x0000, &zx_pixel_layout, 0, 2},
	{REGION_CPU1, 0x0e00, &zx_char_layout, 0, 2},
	{-1}							   /* end of array */
};

static struct GfxDecodeInfo zx81_gfxdecodeinfo[] =
{
	{REGION_GFX1, 0x0000, &zx_pixel_layout, 0, 2},
	{REGION_CPU1, 0x1e00, &zx_char_layout, 0, 2},
	{-1}							   /* end of array */
};

static struct GfxDecodeInfo pc8300_gfxdecodeinfo[] =
{
	{REGION_GFX1, 0x0000, &zx_pixel_layout, 0, 2},
	{REGION_GFX2, 0x0000, &zx_char_layout, 0, 2},
	{-1}							   /* end of array */
};

static struct GfxDecodeInfo pow3000_gfxdecodeinfo[] =
{
	{REGION_GFX1, 0x0000, &zx_pixel_layout, 0, 2},
	{REGION_GFX2, 0x0000, &zx_char_layout, 0, 2},
	{-1}							   /* end of array */
};

static unsigned char zx80_palette[] =
{
	255,255,255,	/* white */
	  0,  0,  0,	/* black */
};

static unsigned char zx81_palette[] =
{
	255,255,255,	/* white */
	  0,  0,  0,	/* black */
};

static unsigned char ts1000_palette[] =
{
	 64,244,244,	/* cyan */
	  0,  0,  0,	/* black */
};

static unsigned short zx_colortable[] =
{
	0, 1,							   /* white on black */
	1, 0							   /* black on white */
};


/* Initialise the palette */
static PALETTE_INIT( zx80 )
{
	palette_set_colors(0, zx80_palette, sizeof(zx80_palette) / 3);
	memcpy(colortable, zx_colortable, sizeof (zx_colortable));
}

static PALETTE_INIT( zx81 )
{
	palette_set_colors(0, zx81_palette, sizeof(zx81_palette) / 3);
	memcpy(colortable, zx_colortable, sizeof (zx_colortable));
}

static PALETTE_INIT( ts1000 )
{
	palette_set_colors(0, ts1000_palette, sizeof(ts1000_palette) / 3);
	memcpy(colortable, zx_colortable, sizeof (zx_colortable));
}

static struct Wave_interface zx81_wave_interface = {
	1,		/* 1 cassette recorder */
	{50}		/* mixing levels in percent */
};

static struct DACinterface zx81_dac_interface =
{
	1,		/* number of DACs */
	{50}		/* volume */
};

#define ZX81_CPU_CLOCK			3250000
#define ZX81_CYCLES_PER_SCANLINE	207
#define ZX81_PIXELS_PER_SCANLINE	256
#define ZX81_CYCLES_PER_VBLANK		1235
#define ZX81_VBLANK_DURATION		(1.0*ZX81_CYCLES_PER_VBLANK/ZX81_CPU_CLOCK*1000*1000)

#define ZX81_PAL_SCANLINES		304
#define ZX81_PAL_FRAMES_PER_SECOND	(1.0*ZX81_CPU_CLOCK/(ZX81_PAL_SCANLINES*ZX81_CYCLES_PER_SCANLINE+ZX81_CYCLES_PER_VBLANK))

#define ZX81_NTSC_SCANLINES		256
#define ZX81_NTSC_FRAMES_PER_SECOND	(1.0*ZX81_CPU_CLOCK/(ZX81_NTSC_SCANLINES*ZX81_CYCLES_PER_SCANLINE+ZX81_CYCLES_PER_VBLANK))


static MACHINE_DRIVER_START( zx80 )
	/* basic machine hardware */
	MDRV_CPU_ADD_TAG("main", Z80, ZX81_CPU_CLOCK)
	MDRV_CPU_FLAGS(CPU_16BIT_PORT)
	MDRV_CPU_PROGRAM_MAP(readmem_zx80, writemem_zx80)
	MDRV_CPU_IO_MAP(readport, writeport)
	MDRV_FRAMES_PER_SECOND(ZX81_PAL_FRAMES_PER_SECOND)
	MDRV_VBLANK_DURATION(ZX81_VBLANK_DURATION)
	MDRV_INTERLEAVE(1)

	MDRV_MACHINE_INIT( zx80 )

    /* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(ZX81_PIXELS_PER_SCANLINE, ZX81_PAL_SCANLINES)
	MDRV_VISIBLE_AREA(0, ZX81_PIXELS_PER_SCANLINE-1, 0, ZX81_PAL_SCANLINES-1)
	MDRV_GFXDECODE( zx80_gfxdecodeinfo )
	MDRV_PALETTE_LENGTH(6)
	MDRV_COLORTABLE_LENGTH(4)
	MDRV_PALETTE_INIT( zx80 )

	MDRV_VIDEO_START( zx )
	MDRV_VIDEO_UPDATE( zx )

	/* sound hardware */
	MDRV_SOUND_ADD(WAVE, zx81_wave_interface)
	MDRV_SOUND_ADD(DAC, zx81_dac_interface)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( zx81 )
	MDRV_IMPORT_FROM( zx80 )
	MDRV_CPU_MODIFY( "main" )
	MDRV_CPU_PROGRAM_MAP( readmem_zx81, writemem_zx81 )

	MDRV_MACHINE_INIT( zx81 )
	MDRV_GFXDECODE( zx81_gfxdecodeinfo )
	MDRV_PALETTE_INIT( zx81 )
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( ts1000 )
	MDRV_IMPORT_FROM( zx81 )
	MDRV_FRAMES_PER_SECOND(ZX81_NTSC_FRAMES_PER_SECOND)

	MDRV_SCREEN_SIZE(ZX81_PIXELS_PER_SCANLINE, ZX81_NTSC_SCANLINES)
	MDRV_VISIBLE_AREA(0, ZX81_PIXELS_PER_SCANLINE-1, 0, ZX81_NTSC_SCANLINES-1)
	MDRV_PALETTE_INIT( ts1000 )
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( pc8300 )
	MDRV_IMPORT_FROM( ts1000 )
	MDRV_CPU_MODIFY( "main" )
	MDRV_CPU_PROGRAM_MAP( readmem_pc8300, writemem_pc8300 )

	MDRV_MACHINE_INIT( pc8300 )
	MDRV_GFXDECODE( pc8300_gfxdecodeinfo )
	MDRV_PALETTE_INIT( zx81 )
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( pow3000 )
	MDRV_IMPORT_FROM( ts1000 )
	MDRV_CPU_MODIFY( "main" )
	MDRV_CPU_PROGRAM_MAP( readmem_pow3000, writemem_pow3000 )

	MDRV_MACHINE_INIT( pc8300 )
	MDRV_GFXDECODE( pow3000_gfxdecodeinfo )
	MDRV_PALETTE_INIT( zx81 )
MACHINE_DRIVER_END


ROM_START(zx80)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("zx80.rom",    0x0000, 0x1000, CRC(4c7fc597))
    ROM_REGION(0x00100, REGION_GFX1,0)
	/* filled in by zx_init_driver */
ROM_END


ROM_START(aszmic)
    ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("aszmic.rom",  0x0000, 0x1000, CRC(6c123536))
    ROM_REGION(0x00100, REGION_GFX1,0)
    /* filled in by zx_init_driver */
ROM_END

ROM_START(zx81)
        ROM_REGION(0x10000, REGION_CPU1,0)
        ROM_LOAD("zx81.rom",    0x0000, 0x2000, CRC(fcbbd617))
        ROM_REGION(0x00100, REGION_GFX1,0)
        /* filled in by zx_init_driver */
ROM_END                                                                                                                                       

ROM_START(zx81a)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("zx81a.rom",    0x0000, 0x2000, CRC(4b1dd6eb))
	ROM_REGION(0x00100, REGION_GFX1,0)
	/* filled in by zx_init_driver */
ROM_END

ROM_START(zx81b)
        ROM_REGION(0x10000, REGION_CPU1,0)
        ROM_LOAD("zx81b.rom",    0x0000, 0x2000, CRC(522c37b8))
        ROM_REGION(0x00100, REGION_GFX1,0)
        /* filled in by zx_init_driver */
ROM_END                                                                                                                                       

ROM_START(h4th)
        ROM_REGION(0x10000, REGION_CPU1,0)
        ROM_LOAD("h4th.rom",    0x0000, 0x2000, CRC(257d5a32))
        ROM_REGION(0x00100, REGION_GFX1,0)
        /* filled in by zx_init_driver */
ROM_END                                                                                                                                       

ROM_START(tree4th)
        ROM_REGION(0x10000, REGION_CPU1,0)
        ROM_LOAD("tree4th.rom",    0x0000, 0x2000, CRC(71616238))
        ROM_REGION(0x00100, REGION_GFX1,0)
        /* filled in by zx_init_driver */
ROM_END                                                                                                                                       

ROM_START(ts1000)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("zx81a.rom",  0x0000, 0x2000, CRC(4b1dd6eb))
	ROM_REGION(0x00100, REGION_GFX1,0)
	/* filled in by zx_init_driver */
ROM_END

ROM_START(pc8300)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("8300_org.rom",0x0000, 0x2000, CRC(a350f2b1))
	ROM_REGION(0x00100, REGION_GFX1,0)
	/* filled in by zx_init_driver */
	ROM_REGION(0x00200, REGION_GFX2,0)
	ROM_LOAD("8300_fnt.bin",0x0000, 0x0200, CRC(6bd0408c))
ROM_END

ROM_START(pow3000)
	ROM_REGION(0x10000, REGION_CPU1,0)
	ROM_LOAD("pow3000.rom", 0x0000, 0x2000, CRC(8a49b2c3))
	ROM_REGION(0x00100, REGION_GFX1,0)
	/* filled in by zx_init_driver */
	ROM_REGION(0x00200, REGION_GFX2,0)
	ROM_LOAD("pow3000.chr", 0x0000, 0x0200, CRC(1c42fe46))
ROM_END

ROM_START(lambda)
        ROM_REGION(0x10000, REGION_CPU1,0)
        ROM_LOAD("lambda.rom",0x0000, 0x2000, CRC(8a49b2c3))
        ROM_REGION(0x00100, REGION_GFX1,0)
        /* filled in by zx_init_driver */
        ROM_REGION(0x00200, REGION_GFX2,0)
        ROM_LOAD("8300_fnt.bin",0x0000, 0x0200, CRC(6bd0408c))
ROM_END                                                                                                                                       

static struct CassetteOptions zx81_cassette_options = {
	1,		/* channels */
	16,		/* bits per sample */
	44100		/* sample frequency */
};

SYSTEM_CONFIG_START(zx80)
	CONFIG_DEVICE_CASSETTEX(1, zx80_o_format, &zx81_cassette_options, CASSETTE_STOPPED | CASSETTE_SPEAKER_ENABLED)
SYSTEM_CONFIG_END

SYSTEM_CONFIG_START(zx81)
	CONFIG_DEVICE_CASSETTEX(1, zx81_p_format, &zx81_cassette_options, CASSETTE_STOPPED | CASSETTE_SPEAKER_ENABLED)
SYSTEM_CONFIG_END

/***************************************************************************

  Game driver(s)

***************************************************************************/
/*	YEAR	NAME	 PARENT	COMPAT	MACHINE	INPUT	INIT	CONFIG	COMPANY				FULLNAME */
COMP (	1980,	zx80,    0,	0,	zx80,	zx80,	zx,	zx80,	"Sinclair Research",		"ZX-80" )
COMP (	1981,	aszmic,  zx80,	0,	zx80,	zx80,	zx,	zx80,	"Sinclair Research",		"ZX.Aszmic" )
COMP (	1981,	zx81,    0,	0,	zx81,	zx81,   zx,	zx81,	"Sinclair Research",		"ZX-81" )
COMP (	198?,	zx81a,   zx81,	0,	zx81,	zx81,	zx,	zx81,	"Sinclair Research",		"ZX-81 (2nd rev)" )
COMP (	198?,	zx81b,   zx81,	0,	zx81,	zx81,	zx,	zx81,	"Sinclair Research",		"ZX-81 (3rd rev)" )
COMPX(	198?,	h4th,	zx81,	0,	zx81,	zx81,	zx,	zx81,	"Sinclair Research",		"Sinclair ZX-81 Forth by David Husband",	GAME_NOT_WORKING)
COMPX(	198?,	tree4th,zx81,	0,	zx81,	zx81,	zx,	zx81,	"Sinclair Research",		"Sinclair ZX-81 Tree-Forth by Tree Systems",	GAME_NOT_WORKING)
COMP (	1982,	ts1000,  zx81,	0,	ts1000,	zx81,	zx,	zx81,	"Timex Sinclair",		"Timex Sinclair 1000" )
COMP (	1984,	pc8300,  zx81,	0,	pc8300,	pow3000,zx,	zx81,	"Your Computer",		"PC8300" )
COMP (	1983,	pow3000, zx81,	0,	pow3000,pow3000,zx,	zx81,	"Creon Enterprises",		"Power 3000" )
COMP (	1982,	lambda,  zx81,	0,	pc8300,	pow3000,zx,	zx81,	"Lambda Electronics Ltd",	"Lambda 8300" )
