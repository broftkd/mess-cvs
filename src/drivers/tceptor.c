/*
 *	Thunder Ceptor board
 *	(C) 1986 Namco
 *
 *	Hardware analyzed by nono
 *	Driver by BUT
 *
 *	TODO:
 *	- support background layer (decode first)
 */

#include "driver.h"
#include "cpu/m6502/m6502.h"
#include "cpu/m6809/m6809.h"
#include "cpu/m6800/m6800.h"
#include "namcoic.h"

#ifdef MAME_DEBUG
extern int debug_key_pressed;
#endif

#ifdef TINY_COMPILE
// to avoid link error
int namcos2_gametype;
#endif


extern PALETTE_INIT( tceptor );
extern VIDEO_START( tceptor );
extern VIDEO_UPDATE( tceptor );

extern READ_HANDLER( tceptor_tile_ram_r );
extern WRITE_HANDLER( tceptor_tile_ram_w );
extern READ_HANDLER( tceptor_tile_attr_r );
extern WRITE_HANDLER( tceptor_tile_attr_w );

extern data8_t *tceptor_tile_ram;
extern data8_t *tceptor_tile_attr;
extern data16_t *tceptor_sprite_ram;

/*******************************************************************/

static data8_t *m6502_a_shared_ram;
static data8_t *m6502_b_shared_ram;
static data8_t *m68k_shared_ram;
static data8_t *mcu_shared_ram;


/*******************************************************************/

static READ_HANDLER( m6502_a_shared_r )
{
	return m6502_a_shared_ram[offset];
}

static WRITE_HANDLER( m6502_a_shared_w )
{
	m6502_a_shared_ram[offset] = data;
}

static READ_HANDLER( m6502_b_shared_r )
{
	return m6502_b_shared_ram[offset];
}

static WRITE_HANDLER( m6502_b_shared_w )
{
	m6502_b_shared_ram[offset] = data;
}


static READ_HANDLER( m68k_shared_r )
{
	return m68k_shared_ram[offset];
}

static WRITE_HANDLER( m68k_shared_w )
{
	m68k_shared_ram[offset] = data;
}

static READ16_HANDLER( m68k_shared_word_r )
{
	return m68k_shared_ram[offset];
}

static WRITE16_HANDLER( m68k_shared_word_w )
{
	if (ACCESSING_LSB16)
		m68k_shared_ram[offset] = data & 0xff;
}


static READ_HANDLER( mcu_shared_r )
{
	return mcu_shared_ram[offset];
}

static WRITE_HANDLER( mcu_shared_w )
{
	mcu_shared_ram[offset] = data;
}


/*******************************************************************/

static WRITE_HANDLER( voice_w )
{
	DAC_signed_data_16_w(0, data ? (data + 1) * 0x100 : 0x8000);
}

/* fix dsw/input data to memory mapped data */
static data8_t fix_input0(data8_t in1, data8_t in2)
{
	data8_t r = 0;

	r |= (in1 & 0x80) >> 7;
	r |= (in1 & 0x20) >> 4;
	r |= (in1 & 0x08) >> 1;
	r |= (in1 & 0x02) << 2;
	r |= (in2 & 0x80) >> 3;
	r |= (in2 & 0x20) >> 0;
	r |= (in2 & 0x08) << 3;
	r |= (in2 & 0x02) << 6;

	return r;
}

static data8_t fix_input1(data8_t in1, data8_t in2)
{
	data8_t r = 0;

	r |= (in1 & 0x40) >> 6;
	r |= (in1 & 0x10) >> 3;
	r |= (in1 & 0x04) >> 0;
	r |= (in1 & 0x01) << 3;
	r |= (in2 & 0x40) >> 2;
	r |= (in2 & 0x10) << 1;
	r |= (in2 & 0x04) << 4;
	r |= (in2 & 0x01) << 7;

	return r;
}

static READ_HANDLER( dsw0_r )
{
	return fix_input0(readinputport(0), readinputport(1));
}

static READ_HANDLER( dsw1_r )
{
	return fix_input1(readinputport(0), readinputport(1));
}

static READ_HANDLER( input0_r )
{
	return fix_input0(readinputport(2), readinputport(3));
}

static READ_HANDLER( input1_r )
{
	return fix_input1(readinputport(2), readinputport(3));
}

static READ_HANDLER( readFF )
{
	return 0xff;
}

/*******************************************************************/

#ifdef MAME_DEBUG
static data8_t *print_ram;

static READ_HANDLER( print_r )
{
printf("%d: %04x: read from %04x\n", cpu_getcurrentframe(), activecpu_get_previouspc(), offset);
	return print_ram[offset];
}

static WRITE_HANDLER( print_w )
{
	if (print_ram[offset] == data) return;

printf("%d: %04x: write %02x to %04x\n", cpu_getcurrentframe(), activecpu_get_previouspc(), data, offset);
	print_ram[offset] = data;
}

static WRITE16_HANDLER( log16_w )
{
printf("%d: %04x: write %02x to %04x\n", cpu_getcurrentframe(), activecpu_get_previouspc(), data, offset);
}

//{ 0x0000, 0xffff, print_r },
//{ 0x0000, 0xffff, print_w, &print_ram },
#endif


/*******************************************************************/

static MEMORY_READ_START( m6809_readmem )
	{ 0x0000, 0x00ff, MRA_RAM },
	{ 0x0100, 0x16ff, MRA_RAM },
	{ 0x1700, 0x17ff, MRA_RAM },
	{ 0x1800, 0x1bff, tceptor_tile_ram_r },
	{ 0x1c00, 0x1fff, tceptor_tile_attr_r },
	{ 0x2800, 0x37ff, MRA_RAM },			// VIEW RAM
	{ 0x4000, 0x40ff, namcos1_wavedata_r },
	{ 0x4000, 0x43ff, mcu_shared_r },
	{ 0x4f00, 0x4f00, MRA_NOP },			// unknown
	{ 0x4f01, 0x4f01, input_port_4_r },		// analog input (accel)
	{ 0x4f02, 0x4f02, input_port_5_r },		// analog input (left/right)
	{ 0x4f03, 0x4f03, input_port_6_r },		// analog input (up/down)
	{ 0x6000, 0x7fff, m68k_shared_r },		// COM RAM
	{ 0x8000, 0xffff, MRA_ROM },
MEMORY_END

static MEMORY_WRITE_START( m6809_writemem )
	{ 0x0000, 0x00ff, MWA_RAM },
	{ 0x0100, 0x16ff, MWA_RAM },
	{ 0x1700, 0x17ff, MWA_RAM },
	{ 0x1800, 0x1bff, tceptor_tile_ram_w, &tceptor_tile_ram },
	{ 0x1c00, 0x1fff, tceptor_tile_attr_w, &tceptor_tile_attr },
	{ 0x2000, 0x27ff, MWA_RAM },
	{ 0x2800, 0x37ff, MWA_RAM },			// VIEW RAM
	{ 0x3800, 0x3dff, MWA_RAM },
	{ 0x4000, 0x40ff, namcos1_wavedata_w },
	{ 0x4000, 0x43ff, mcu_shared_w },
	{ 0x4800, 0x4800, MWA_RAM },
	{ 0x4f00, 0x4f03, MWA_NOP },			// analog input control?
	{ 0x5000, 0x5006, MWA_RAM },
	{ 0x6000, 0x7fff, m68k_shared_w, &m68k_shared_ram },
	{ 0x8000, 0x8000, MWA_NOP },			// write to ROM address!!
	{ 0x8800, 0x8800, MWA_NOP },			// write to ROM address!!
	{ 0x8000, 0xffff, MWA_ROM },
MEMORY_END


static MEMORY_READ_START( m6502_a_readmem )
	{ 0x0000, 0x00ff, m6502_b_shared_r },
	{ 0x0100, 0x01ff, MRA_RAM },
	{ 0x0200, 0x02ff, MRA_RAM },
	{ 0x0300, 0x030f, MRA_RAM },
	{ 0x2001, 0x2001, YM2151_status_port_0_r },
	{ 0x3000, 0x30ff, m6502_a_shared_r },
	{ 0x8000, 0xffff, MRA_ROM },
MEMORY_END

static MEMORY_WRITE_START( m6502_a_writemem )
	{ 0x0000, 0x00ff, m6502_b_shared_w },
	{ 0x0100, 0x01ff, MWA_RAM },
	{ 0x0200, 0x02ff, MWA_RAM },
	{ 0x0300, 0x030f, MWA_RAM },
	{ 0x2000, 0x2000, YM2151_register_port_0_w },
	{ 0x2001, 0x2001, YM2151_data_port_0_w },
	{ 0x3000, 0x30ff, m6502_a_shared_w, &m6502_a_shared_ram },
	{ 0x3c01, 0x3c01, MWA_RAM },
	{ 0x8000, 0xffff, MWA_ROM },
MEMORY_END


static MEMORY_READ_START( m6502_b_readmem )
	{ 0x0000, 0x00ff, m6502_b_shared_r },
	{ 0x0100, 0x01ff, MRA_RAM },
	{ 0x8000, 0xffff, MRA_ROM },
MEMORY_END

static MEMORY_WRITE_START( m6502_b_writemem )
	{ 0x0000, 0x00ff, m6502_b_shared_w, &m6502_b_shared_ram },
	{ 0x0100, 0x01ff, MWA_RAM },
	{ 0x4000, 0x4000, voice_w },			// voice data
	{ 0x5000, 0x5000, MWA_RAM },			// voice ctrl??
	{ 0x8000, 0xffff, MWA_ROM },
MEMORY_END


static MEMORY_READ16_START( m68k_readmem )
	{ 0x000000, 0x00ffff, MRA16_ROM },		// M68K ERROR 1
	{ 0x100000, 0x10ffff, MRA16_ROM },		// not sure
	{ 0x200000, 0x203fff, MRA16_RAM },		// M68K ERROR 0
	{ 0x700000, 0x703fff, m68k_shared_word_r },
MEMORY_END

static MEMORY_WRITE16_START( m68k_writemem )
	{ 0x000000, 0x00ffff, MWA16_ROM },
	{ 0x100000, 0x10ffff, MWA16_ROM },		// not sure
	{ 0x200000, 0x203fff, MWA16_RAM },
	{ 0x300000, 0x300001, MWA16_RAM },
	{ 0x400000, 0x4001ff, MWA16_RAM, &tceptor_sprite_ram },
	{ 0x500000, 0x51ffff, namco_road16_w },
	{ 0x600000, 0x600001, MWA16_RAM },
	{ 0x700000, 0x703fff, m68k_shared_word_w },
MEMORY_END


static MEMORY_READ_START( mcu_readmem )
	{ 0x0000, 0x001f, hd63701_internal_registers_r },
	{ 0x0080, 0x00ff, MRA_RAM },
	{ 0x1000, 0x10ff, namcos1_wavedata_r },
	{ 0x1000, 0x13ff, mcu_shared_r },
	{ 0x1400, 0x154d, MRA_RAM },
	{ 0x17c0, 0x17ff, MRA_RAM },
	{ 0x2000, 0x20ff, m6502_a_shared_r },
	{ 0x2100, 0x2100, dsw0_r },
	{ 0x2101, 0x2101, dsw1_r },
	{ 0x2200, 0x2200, input0_r },
	{ 0x2201, 0x2201, input1_r },
	{ 0x8000, 0xbfff, MRA_ROM },
	{ 0xc000, 0xc800, MRA_RAM },
	{ 0xc800, 0xdfff, MRA_RAM },			// Battery Backup
	{ 0xf000, 0xffff, MRA_ROM },
MEMORY_END

static MEMORY_WRITE_START( mcu_writemem )
	{ 0x0000, 0x001f, hd63701_internal_registers_w },
	{ 0x0080, 0x00ff, MWA_RAM },
	{ 0x1000, 0x10ff, namcos1_wavedata_w, &namco_wavedata },
	{ 0x1100, 0x113f, namcos1_sound_w, &namco_soundregs },
	{ 0x1000, 0x13ff, mcu_shared_w, &mcu_shared_ram },
	{ 0x1400, 0x154d, MWA_RAM },
	{ 0x17c0, 0x17ff, MWA_RAM },
	{ 0x2000, 0x20ff, m6502_a_shared_w },
	{ 0x8000, 0x8000, MWA_NOP },			// write to ROM address!!
	{ 0x8800, 0x8800, MWA_NOP },			// write to ROM address!!
	{ 0x8000, 0xbfff, MWA_ROM },
	{ 0xc000, 0xc7ff, MWA_RAM },
	{ 0xc800, 0xdfff, MWA_RAM, &generic_nvram, &generic_nvram_size },	// Battery Backup
	{ 0xf000, 0xffff, MWA_ROM },
MEMORY_END


static PORT_READ_START( mcu_readport )
	{ HD63701_PORT1, HD63701_PORT1, readFF },
	{ HD63701_PORT2, HD63701_PORT2, readFF },
PORT_END

static PORT_WRITE_START( mcu_writeport )
	{ HD63701_PORT1, HD63701_PORT1, MWA_NOP },
	{ HD63701_PORT2, HD63701_PORT2, MWA_NOP },
PORT_END


/*******************************************************************/

INPUT_PORTS_START( tceptor )
	PORT_START      /* DSW 1 */
	PORT_DIPNAME( 0x07, 0x07, DEF_STR( Coin_A ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 3C_2C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 4C_3C ) )
	PORT_DIPSETTING(    0x07, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x06, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x05, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_4C ) )
	PORT_DIPNAME( 0x18, 0x18, DEF_STR( Coin_B ) )
	PORT_DIPSETTING(    0x10, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 3C_2C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 4C_3C ) )
	PORT_DIPSETTING(    0x18, DEF_STR( 1C_1C ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x20, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, "Freeze" )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START      /* DSW 2 */
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x02, "A" )
	PORT_DIPSETTING(    0x03, "B" )
	PORT_DIPSETTING(    0x01, "C" )
	PORT_DIPSETTING(    0x00, "D" )
	PORT_DIPNAME( 0x04, 0x00, "MODE" )		// set default as 2D mode
	PORT_DIPSETTING(    0x00, "2D" )
	PORT_DIPSETTING(    0x04, "3D" )
	PORT_BIT( 0xf8, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START      /* Memory Mapped Port */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )	// shot
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 )	// bomb
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_UNKNOWN )	// shot
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN )	// bomb
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START      /* Memory Mapped Port */
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_SERVICE( 0x04, IP_ACTIVE_LOW )		// TEST SW
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_SERVICE1 )	// service?
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START	/* ADC0809 - 8 CHANNEL ANALOG - CHANNEL 1 */
	PORT_ANALOGX( 0xff, 0x00, IPT_PEDAL, 100, 16, 0x00, 0xd6, KEYCODE_SPACE, IP_JOY_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT )

	PORT_START	/* ADC0809 - 8 CHANNEL ANALOG - CHANNEL 2 */
	PORT_ANALOG(  0xff, 0x7f, IPT_AD_STICK_X | IPF_CENTER, 100, 16, 0x00, 0xfe )

	PORT_START	/* ADC08090 - 8 CHANNEL ANALOG - CHANNEL 3 */
	PORT_ANALOG(  0xff, 0x7f, IPT_AD_STICK_Y | IPF_CENTER, 100, 16, 0x00, 0xfe )
INPUT_PORTS_END


/*******************************************************************/

static struct GfxLayout tile_layout =
{
	8, 8,
	512,
	2,
	{ 0x0000, 0x0004 }, //,  0x8000, 0x8004 },
	{ 8*8, 8*8+1, 8*8+2, 8*8+3, 0*8+0, 0*8+1, 0*8+2, 0*8+3 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	2*8*8
};

// not decoded yet
static struct GfxLayout bg_layout =
{
	16, 16,
	768,
	4,
	{ 0x00000, 0x10000, 0x20000, 0x30000 },
	{ 0, 1, 2, 3, 8, 9, 10, 11, 4, 5, 6, 7, 12, 13, 14, 15 },
	{ 0, 16, 32, 48, 64, 80, 96, 112,  },
	16*16
};

static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &tile_layout,    0, 256 },
	{ REGION_GFX2, 0, &bg_layout,   2048, 512 },
	//{ REGION_GFX3, 0, &spr16_layout,  1024,  64 },
	//{ REGION_GFX4, 0, &spr32_layout,  1024,  64 },
	{ -1 }
};


/*******************************************************************/

static struct YM2151interface ym2151_interface =
{
	1,			/* 1 chip */
	14318180/4,		/* 3.579545 MHz */
	{ YM3012_VOL(100,MIXER_PAN_CENTER,100,MIXER_PAN_CENTER) },
	{ 0 },
	{ 0 }
};

static struct namco_interface namco_interface =
{
	49152000/2048,		/* 24000Hz */
	8,			/* number of voices */
	50,			/* playback volume */
	-1,			/* memory region */
	0			/* stereo */
};

static struct DACinterface dac_interface =
{
	1,			/* 1 channel */
	{ 50 }			/* mixing level */
};


/*******************************************************************/

static MACHINE_DRIVER_START( tceptor )

	/* basic machine hardware */
	MDRV_CPU_ADD(M6809, 49152000/32)
	MDRV_CPU_MEMORY(m6809_readmem,m6809_writemem)
	MDRV_CPU_VBLANK_INT(irq0_line_hold,1)

	MDRV_CPU_ADD(M65C02, 49152000/24)
	MDRV_CPU_MEMORY(m6502_a_readmem,m6502_a_writemem)

	MDRV_CPU_ADD(M65C02, 49152000/24)
	MDRV_CPU_MEMORY(m6502_b_readmem,m6502_b_writemem)

	MDRV_CPU_ADD(M68000, 49152000/4)
	MDRV_CPU_MEMORY(m68k_readmem,m68k_writemem)
	MDRV_CPU_VBLANK_INT(irq1_line_hold,1)

	MDRV_CPU_ADD(HD63701, 49152000/32)	/* or compatible 6808 with extra instructions */
	MDRV_CPU_MEMORY(mcu_readmem,mcu_writemem)
	MDRV_CPU_PORTS(mcu_readport,mcu_writeport)
	MDRV_CPU_VBLANK_INT(irq0_line_hold,1)

	MDRV_FRAMES_PER_SECOND(60.606060)
	MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)
	MDRV_INTERLEAVE(100)
	MDRV_NVRAM_HANDLER(generic_1fill)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(38*8, 32*8)
	MDRV_VISIBLE_AREA(2*8, 34*8-1 + 2*8, 0*8, 28*8-1 + 0)
	MDRV_GFXDECODE(gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(1024)
	MDRV_COLORTABLE_LENGTH(4096)

	MDRV_PALETTE_INIT(tceptor)
	MDRV_VIDEO_START(tceptor)
	MDRV_VIDEO_UPDATE(tceptor)

	/* sound hardware */
	MDRV_SOUND_ADD(YM2151, ym2151_interface)
	MDRV_SOUND_ADD(NAMCO, namco_interface)
	MDRV_SOUND_ADD(DAC, dac_interface)
MACHINE_DRIVER_END


/***************************************************************************

  Game driver(s)

***************************************************************************/

ROM_START( tceptor2 )
	ROM_REGION( 0x10000, REGION_CPU1, 0 )			// 68A09EP
	ROM_LOAD( "tc2-1.10f",  0x08000, 0x08000, CRC(F953F153) SHA1(f4cd0a133d23b4bf3c24c70c28c4ecf8ad4daf6f) )

	ROM_REGION( 0x10000, REGION_CPU2, 0 )			// RC65C02
	ROM_LOAD( "tc1-21.1m",  0x08000, 0x08000, CRC(2D0B2FA8) SHA1(16ecd70954e52a8661642b15a5cf1db51783e444) )

	ROM_REGION( 0x10000, REGION_CPU3, 0 )			// RC65C02
	ROM_LOAD( "tc1-22.3m",  0x08000, 0x08000, CRC(9F5A3E98) SHA1(2b2ffe39fe647a3039b92721817bddc9e9a92d82) )

	ROM_REGION( 0x110000, REGION_CPU4, 0 )			// MC68000-12
	ROM_LOAD16_BYTE( "tc2-4.8c",     0x000000, 0x08000, CRC(6C2EFC04) SHA1(3a91f5b8bbf7040083e2da2bd0fb2ab3c51ec45c) )
	ROM_LOAD16_BYTE( "tc2-3.10c",    0x000001, 0x08000, CRC(312B781A) SHA1(37bf3ced16b765d78bf8de7a4916c2b518b702ed) )
	ROM_LOAD16_BYTE( "tc2-6.8d",     0x100000, 0x08000, CRC(20711F14) SHA1(39623592bb4be3b3be2bff4b3219ac16ba612761) )
	ROM_LOAD16_BYTE( "tc2-5.10d",    0x100001, 0x08000, CRC(925F2560) SHA1(81fcef6a9c7e9dfb6884043cf2266854bc87cd69) )

	ROM_REGION( 0x10000, REGION_CPU5, 0 )			// Custom 60A1
	ROM_LOAD( "tc1-2.3a",    0x08000, 0x4000, CRC(B6DEF610) SHA1(d0eada92a25d0243206fb8239374f5757caaea47) )
	ROM_LOAD( "pl1-mcu.bin", 0x0f000, 0x1000, CRC(6ef08fb3) SHA1(4842590d60035a0059b0899eb2d5f58ae72c2529) ) /* microcontroller */

	ROM_REGION( 0x02000, REGION_GFX1, ROMREGION_DISPOSE )	// text tilemap
	ROM_LOAD( "tc1-18.6b",  0x00000, 0x02000, CRC(662B5650) SHA1(ba82fe5efd1011854a6d0d7d87075475b65c0601) )

	ROM_REGION( 0x0c000, REGION_GFX2, ROMREGION_DISPOSE )	// background??
	ROM_LOAD( "tc2-20.10e", 0x00000, 0x08000, CRC(E72738FC) SHA1(53664400f343acdc1d8cf7e00e261ae42b857a5f) )
	ROM_LOAD( "tc2-19.10d", 0x08000, 0x04000, CRC(9C221E21) SHA1(58bcbb998dcf2190cf46dd3d22b116ac673285a6) )

	ROM_REGION( 0x10000, REGION_GFX3, ROMREGION_DISPOSE )	// 16x16 sprites
	ROM_LOAD( "tc2-16.8t",  0x00000, 0x08000, CRC(DCF4DA96) SHA1(e953cb46d60171271128b3e0ef4e958d1fab1d04) )
	ROM_LOAD( "tc2-15.10t", 0x08000, 0x08000, CRC(FB0A9F89) SHA1(cc9be6ff542b5d5e6ad3baca7a355b9bd31b3dd1) )

	ROM_REGION( 0x80000, REGION_GFX4, ROMREGION_DISPOSE )	// 32x32 sprites
	ROM_LOAD( "tc2-8.8m",   0x00000, 0x10000, CRC(03528D79) SHA1(237810fa55c36b6d87c7e02e02f19feb64e5a11f) )
	ROM_LOAD( "tc2-10.8p",  0x10000, 0x10000, CRC(561105EB) SHA1(101a0e48a740ce4acc34a7d1a50191bb857e7371) )
	ROM_LOAD( "tc2-12.8r",  0x20000, 0x10000, CRC(626CA8FB) SHA1(0b51ced00b3de1f672f6f8c7cc5dd9e2ea2e4f8d) )
	ROM_LOAD( "tc2-14.8s",  0x30000, 0x10000, CRC(B9EEC79D) SHA1(ae69033d6f80be0be883f919544c167e8f91db27) )
	ROM_LOAD( "tc2-7.10m",  0x40000, 0x10000, CRC(0E3523E0) SHA1(eb4670333ad383099fafda1c930f42e48e82f5c5) )
	ROM_LOAD( "tc2-9.10p",  0x50000, 0x10000, CRC(CCFD9FF6) SHA1(2934e098aa5231af18dbfb888fe05faab9576a7d) )
	ROM_LOAD( "tc2-11.10r", 0x60000, 0x10000, CRC(40724380) SHA1(57549094fc8403f1f528e57fe3fa64844bf89e22) )
	ROM_LOAD( "tc2-13.10s", 0x70000, 0x10000, CRC(519EC7C1) SHA1(c4abe279d7cf6f626dcbb6f6c4dc2a138b818f51) )

	ROM_REGION( 0x3600, REGION_PROMS, 0 )
	ROM_LOAD( "tc2-3.1k",   0x00000, 0x00400, CRC(E3504F1A) SHA1(1ac3968e993030a6b2f4719702ff870267ab6918) )	// red components
	ROM_LOAD( "tc2-1.1h",   0x00400, 0x00400, CRC(E8A96FDA) SHA1(42e5d2b351000ac0705b01ab484c5fe8e294a08b) )	// green components
	ROM_LOAD( "tc2-2.1j",   0x00800, 0x00400, CRC(C65EDA61) SHA1(c316b748daa6be68eebbb480557637efc9f44781) )	// blue components
	ROM_LOAD( "tc1-5.6a",   0x00c00, 0x00400, CRC(AFA8EDA8) SHA1(783efbcbf0bb7e4cf2e2618ddd0ef3b52a4518cc) )	// tiles color table
	ROM_LOAD( "tc2-4.2e",   0x01000, 0x00200, CRC(2C43CB6E) SHA1(00d11bb510e52f7923fdbe44dbec9b03cefc487e) )	// road color table
	ROM_LOAD( "tc2-6.7s",   0x01200, 0x00400, CRC(BADCDA76) SHA1(726e0019241d31716f3af9ebe900089bce771477) )	// sprite color table
	ROM_LOAD( "tc1-17.7k",  0x01600, 0x02000, CRC(90DB1BF6) SHA1(dbb9e50a8efc3b4012fcf587cc87da9ef42a1b80) )	// sprite related
ROM_END


GAME( 1986, tceptor2, 0,        tceptor,  tceptor,  0,        ROT0,   "Namco", "Thunder Ceptor II" )