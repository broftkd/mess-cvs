/*
	Machine code for TI99/4, TI-99/4A, and SNUG SGCPU (a.k.a. 99/4P).
	Raphael Nabet, 1999-2002.
	Some code was originally derived from Ed Swartz's V9T9.

	References:
	* The TI-99/4A Tech Pages.  Great site with complete technical description
	  of the TI99/4a.
		<http://www.nouspikel.com/ti99/titech.htm>
	* ftp.whtech.com has schematics and hardware documentations.  The documents
	  of most general interest are:
		<ftp://ftp.whtech.com//datasheets/Hardware manuals/99-4  console specification and schematic.pdf>
		<ftp://ftp.whtech.com//datasheets/Hardware manuals/99-4A Console and peripheral Expansion System Technical Data.pdf>
	  Schematics for various extension cards are available in the same directory.
	* V9T9 source code.  This older emulator was often fairly accurate.
	* Harald Glaab's site has software and documentation for the various SNUG
	  cards: 99/4P (nicknamed "SGCPU"), EVPC, BwG.
		<http://home.t-online.de/home/harald.glaab/snug/>
	* The TI99/4 boot ROM is the only source of information for the IR remote
	handset controller protocol

Emulated:
	* All TI99 basic console hardware, except a few tricks in TMS9901 emulation.
	* Cartridges with ROM (either non-paged or paged), RAM (minimemory) and
	  GROM (GRAM or extra GPL ports are possible, but not effectively supported yet).
	* Speech Synthesizer, with standard speech ROM (no speech ROM expansion).
	* Disk emulation (SSSD disk images with TI fdc, both SSSD and DSDD with
	  SNUG's BwG fdc).

	Compatibility looks quite good.

Issues:
	* disk images in MESS format are not quite the same a images in V9T9
	  format: they are identical for single-sided floppies, but use a different
	  track order for double-sided floppies.
		DS image (V9T9): side0 Trk0, side0 Trk1,... side0 Trk39, side1 Trk0,... side1 Trk39
		DS image (MESS): side0 Trk0, side1 Trk0, side0 Trk1,... side0 Trk39, side1 Trk39
	* floppy disk timings are not emulated (general issue)

TODO:
	* support for other peripherals and DSRs as documentation permits
	* find programs that use super AMS or any other extended memory card
	* find specs for the EVPC palette chip
	* finish 99/4p support: ROM6, HSGPL
	* save minimemory contents
*/

#include <math.h>
#include "driver.h"
#include "tms9901.h"
#include "vidhrdw/tms9928a.h"
#include "vidhrdw/v9938.h"
#include "sndhrdw/spchroms.h"
#include "devices/cassette.h"
#include "ti99_4x.h"
#include "99_peb.h"
#include "994x_ser.h"
#include "99_dsk.h"
#include "99_ide.h"


/* prototypes */
static READ16_HANDLER ( ti99_rw_rspeech );
static WRITE16_HANDLER ( ti99_ww_wspeech );

static void tms9901_set_int1(int state);
static void tms9901_interrupt_callback(int intreq, int ic);
static int ti99_R9901_0(int offset);
static int ti99_R9901_1(int offset);
static int ti99_R9901_2(int offset);
static int ti99_R9901_3(int offset);

static void ti99_handset_set_ack(int offset, int data);
static void ti99_handset_task(void);
static void ti99_KeyC2(int offset, int data);
static void ti99_KeyC1(int offset, int data);
static void ti99_KeyC0(int offset, int data);
static void ti99_AlphaW(int offset, int data);
static void ti99_CS1_motor(int offset, int data);
static void ti99_CS2_motor(int offset, int data);
static void ti99_audio_gate(int offset, int data);
static void ti99_CS_output(int offset, int data);

static void ti99_4p_internal_dsr_init(void);
static void ti99_TIxram_init(void);
static void ti99_sAMSxram_init(void);
static void ti99_4p_mapper_init(void);
static void ti99_myarcxram_init(void);
static void ti99_evpc_init(void);

/*
	pointers to RAM areas
*/
/* pointer to scratch RAM */
static UINT16 *sRAM_ptr;
/* pointer to extended RAM */
static UINT16 *xRAM_ptr;

/*
	Configuration
*/
/* model of ti99 in emulation */
static enum
{
	model_99_4,
	model_99_4a,
	model_99_4p
} ti99_model;
/* memory extension type */
static xRAM_kind_t xRAM_kind;
/* TRUE if speech synthesizer present */
static char has_speech;
/* floppy disk controller type */
static fdc_kind_t fdc_kind;
/* TRUE if ide card present */
static char has_ide;
/* TRUE if evpc card present */
static char has_evpc;
/* TRUE if rs232 card present */
static char has_rs232;
/* TRUE if ti99/4 IR remote handset present */
static char has_handset;


/* tms9901 setup */
static const tms9901reset_param tms9901reset_param_ti99 =
{
	TMS9901_INT1 | TMS9901_INT2 | TMS9901_INTC,	/* only input pins whose state is always known */

	{	/* read handlers */
		ti99_R9901_0,
		ti99_R9901_1,
		ti99_R9901_2,
		ti99_R9901_3
	},

	{	/* write handlers */
		ti99_handset_set_ack,
		NULL,
		ti99_KeyC2,
		ti99_KeyC1,
		ti99_KeyC0,
		ti99_AlphaW,
		ti99_CS1_motor,
		ti99_CS2_motor,
		ti99_audio_gate,
		ti99_CS_output,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},

	/* interrupt handler */
	tms9901_interrupt_callback,

	/* clock rate = 3MHz */
	3000000.
};

/* handset interface */
static int handset_buf;
static int handset_buflen;
static int handset_clock;
static int handset_ack;
enum
{
	max_handsets = 4
};

/* keyboard interface */
static int KeyCol;
static int AlphaLockLine;

/*
	GROM support.

In short:

	TI99/4x hardware supports GROMs.  GROMs are slow ROM devices, which are
	interfaced via a 8-bit data bus, and include an internal address pointer
	which is incremented after each read.  This implies that accesses are
	faster when reading consecutive bytes, although the address pointer can be
	read and written at any time.

	They are generally used to store programs in GPL (Graphic Programming
	Language: a proprietary, interpreted language - the interpreter takes most
	of a TI99/4(a) CPU ROMs).  They can used to store large pieces of data,
	too.

	Both TI-99/4 and TI-99/4a include three GROMs, with some start-up code,
	system routines and TI-Basic.  TI99/4 includes an additional Equation
	Editor.  According to the preliminary schematics found on ftp.whtech.com,
	TI-99/8 includes the three standard GROMs and 16 GROMs for the UCSD
	p-system.  TI99/2 does not include GROMs at all, and was not designed to
	support any, although it should be relatively easy to create an expansion
	card with the GPL interpreter and a /4a cartridge port.

The simple way:

	Each GROM is logically 8kb long.

	Communication with GROM is done with 4 memory-mapped locations.  You can read or write
	a 16-bit address pointer, and read data from GROMs.  One register allows to write data, too,
	which would support some GRAMs, but AFAIK TI never built such GRAMs (although docs from TI
	do refer to this possibility).

	Since address are 16-bit long, you can have up to 8 GROMs.  So, a cartridge may
	include up to 5 GROMs.  (Actually, there is a way more GROMs can be used - see below...)

	The address pointer is incremented after each GROM operation, but it will always remain
	within the bounds of the currently selected GROM (e.g. after 0x3fff comes 0x2000).

Some details:

	Original TI-built GROM are 6kb long, but take 8kb in address space.  The extra 2kb can be
	read, and follow the following formula:
		GROM[0x1800+offset] = GROM[0x0800+offset] | GROM[0x1000+offset];
	(sounds like address decoding is incomplete - we are lucky we don't burn any silicon when
	doing so...)

	Needless to say, some hackers simulated 8kb GRAMs and GROMs with normal RAM/PROM chips and
	glue logic.

GPL ports:

	When accessing the GROMs registers, 8 address bits (cpu_addr & 0x03FC) may
	be used as a port number, which permits the use of up to 256 independant
	GROM ports, with 64kb of address space in each.  TI99/4(a) ROMs can take
	advantage of the first 16 ports: it will look for GPL programs in every
	GROM of the 16 first ports.  Additionally, while the other 240 ports cannot
	contain standard GROMs with GPL code, they may still contain custom GROMs
	with data.

	Note, however, that the TI99/4(a) console does not decode the page number, so console GROMs
	occupy the first 24kb of EVERY port, and cartridge GROMs occupy the next 40kb of EVERY port.
	(Note that the TI99/8 console does have the required decoder.)  Fortunately, GROM drivers
	have a relatively high impedance, and therefore extension cards can use TTL drivers to impose
	another value on the data bus with no risk of burning the console GROMs.  This hack permits
	the addition of additionnal GROMs in other ports, with the only side effect that whenever
	the address pointer in port N is altered, the address pointer of console GROMs in port 0
	is altered, too.  Overriding the system GROMs with custom code is possible, too (some hackers
	have done so), but I would not recommended it, as connecting such a device to a TI99/8 might
	burn some drivers.

	The p-code card (-> UCSD Pascal system) contains 8 GROMs, all in port 16.  This port is not
	in the range recognized by the TI ROMs (0-15), and therefore it is used by the p-code DSR ROMs
	as custom data.  Additionally, some hackers used the extra ports to implement "GRAM" devices.
*/
/* defines for GROM_flags */
enum
{
	gf_has_look_ahead = 0x01,	/* has a look-ahead buffer - true with original GROMs */
	gf_is_GRAM = 0x02			/* is a GRAM - false with original GROMs */
};

/* GROM_port_t: descriptor for a port of 8 GROMs */
typedef struct GROM_port_t
{
	/* pointer to GROM data */
	UINT8 *data_ptr;
	/* active GROM in port (3 bits: 0-7) */
	//int active_GROM;
	/* current address pointer for the active GROM in port (16 bits) */
	unsigned int addr;
	/* GROM data buffer */
	UINT8 buf;
	/* internal flip-flops that are set after the first access to the GROM
	address so that next access is mapped to the LSB, and cleared after each
	data access */
	char raddr_LSB, waddr_LSB;
	/* flags for active GROM */
	char cur_flags;
	/* flags for each GROM chip */
	char GROM_flags[8];
} GROM_port_t;

/* GPL_port_lookup[n]: points to specific GROM_port_t structure when console GROMs are overriden
	by another GROM in port n, NULL otherwise */
static GROM_port_t *GPL_port_lookup[256];

/* descriptor for console GROMs */
GROM_port_t console_GROMs;

/*
	Cartridge support
*/
/* pointer on two 8kb cartridge pages */
static UINT16 *cartridge_pages[2] = {NULL, NULL};
/* flag: TRUE if the cartridge is minimemory (4kb of battery-backed SRAM in 0x5000-0x5fff) */
static char cartridge_minimemory = FALSE;
/* flag: TRUE if the cartridge is paged */
static char cartridge_paged = FALSE;
/* flag on the data for the current page (cartridge_pages[0] if cartridge is not paged) */
static UINT16 *current_page_ptr;
/* keep track of cart file types - required for cleanup... */
typedef enum slot_type_t { SLOT_EMPTY = -1, SLOT_GROM = 0, SLOT_CROM = 1, SLOT_DROM = 2, SLOT_MINIMEM = 3 } slot_type_t;
static slot_type_t slot_type[3] = { SLOT_EMPTY, SLOT_EMPTY, SLOT_EMPTY};


/* tms9900_ICount: used to implement memory waitstates (hack) */
extern int tms9900_ICount;



/*===========================================================================*/
/*
	General purpose code:
	initialization, cart loading, etc.
*/

void init_ti99_4(void)
{
	int i, j;


	ti99_model = model_99_4;
	has_evpc = FALSE;

	/* set up RAM pointers */
	sRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_sram);
	xRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_xram);
	cartridge_pages[0] = (UINT16 *) (memory_region(REGION_CPU1)+offset_cart);
	cartridge_pages[1] = (UINT16 *) (memory_region(REGION_CPU1)+offset_cart + 0x2000);
	console_GROMs.data_ptr = memory_region(region_grom);

	/* Generate missing chunk of each console GROMs */
	for (i=0; i<2; i++)
		for (j=0; j<0x800; j++)
		{
			console_GROMs.data_ptr[0x2000*i+0x1800+j] = console_GROMs.data_ptr[0x2000*i+0x0800+j]
															| console_GROMs.data_ptr[0x2000*i+0x1000+j];
		}
}

void init_ti99_4a(void)
{
	ti99_model = model_99_4a;
	has_evpc = FALSE;

	/* set up RAM pointers */
	sRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_sram);
	xRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_xram);
	cartridge_pages[0] = (UINT16 *) (memory_region(REGION_CPU1)+offset_cart);
	cartridge_pages[1] = (UINT16 *) (memory_region(REGION_CPU1)+offset_cart + 0x2000);
	console_GROMs.data_ptr = memory_region(region_grom);
}

void init_ti99_4ev(void)
{
	ti99_model = model_99_4a;
	has_evpc = TRUE;

	/* set up RAM pointers */
	sRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_sram);
	xRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_xram);
	cartridge_pages[0] = (UINT16 *) (memory_region(REGION_CPU1)+offset_cart);
	cartridge_pages[1] = (UINT16 *) (memory_region(REGION_CPU1)+offset_cart + 0x2000);
	console_GROMs.data_ptr = memory_region(region_grom);
}

void init_ti99_4p(void)
{
	ti99_model = model_99_4p;
	has_evpc = TRUE;

	/* set up RAM pointers */
	sRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_sram_4p);
	xRAM_ptr = (UINT16 *) (memory_region(REGION_CPU1) + offset_xram_4p);
	console_GROMs.data_ptr = memory_region(region_grom);
}

DEVICE_LOAD( ti99_cassette )
{
	struct cassette_args args;
	memset(&args, 0, sizeof(args));
	args.create_smpfreq = 22050;	/* maybe 11025 Hz would be sufficient? */
	return cassette_init(image, file, open_mode, &args);
}

/*
	Load ROM.  All files are in raw binary format.
	1st ROM: GROM (up to 40kb)
	2nd ROM: CPU ROM (8kb)
	3rd ROM: CPU ROM, 2nd page (8kb)

	We don't need to support 99/4p, as it has no cartridge port.
*/
DEVICE_LOAD( ti99_cart )
{
	/* Trick - we identify file types according to their extension */
	/* Note that if we do not recognize the extension, we revert to the slot location <-> type
	scheme.  I do this because the extension concept is quite unfamiliar to mac people
	(I am dead serious). */
	/* Original idea by Norberto Bensa */
	const char *name = image_filename(image);
	const char *ch, *ch2;
	int id = image_index_in_device(image);
	slot_type_t type = (slot_type_t) id;

	/* There is a circuitry in TI99/4(a) that resets the console when a
	cartridge is inserted or removed.  We emulate this instead of resetting the
	emulator (which is the default in MESS). */
	/*cpu_set_reset_line(0, PULSE_LINE);
	tms9901_reset(0);
	if (! has_evpc)
		TMS9928A_reset();
	if (has_evpc)
		v9938_reset();*/

	ch = strrchr(name, '.');
	ch2 = (ch-1 >= name) ? ch-1 : "";

	if (ch)
	{
		if ((! stricmp(ch2, "g.bin")) || (! stricmp(ch, ".grom")) || (! stricmp(ch, ".g")))
		{
			/* grom */
			type = SLOT_GROM;
		}
		else if ((! stricmp(ch2, "c.bin")) || (! stricmp(ch, ".crom")) || (! stricmp(ch, ".c")))
		{
			/* rom first page */
			type = SLOT_CROM;
		}
		else if ((! stricmp(ch2, "d.bin")) || (! stricmp(ch, ".drom")) || (! stricmp(ch, ".d")))
		{
			/* rom second page */
			type = SLOT_DROM;
		}
		else if ((! stricmp(ch2, "m.bin")) || (! stricmp(ch, ".mrom")) || (! stricmp(ch, ".m")))
		{
			/* rom minimemory  */
			type = SLOT_MINIMEM;
		}
	}

	slot_type[id] = type;

	switch (type)
	{
	case SLOT_EMPTY:
		break;

	case SLOT_GROM:
		mame_fread(file, memory_region(region_grom) + 0x6000, 0xA000);
		break;

	case SLOT_MINIMEM:
		cartridge_minimemory = TRUE;
	case SLOT_CROM:
		mame_fread_msbfirst(file, cartridge_pages[0], 0x2000);
		current_page_ptr = cartridge_pages[0];
		break;

	case SLOT_DROM:
		cartridge_paged = TRUE;
		mame_fread_msbfirst(file, cartridge_pages[1], 0x2000);
		current_page_ptr = cartridge_pages[0];
		break;
	}

	return INIT_PASS;
}

DEVICE_UNLOAD( ti99_cart )
{
	int id = image_index_in_device(image);

	/* There is a circuitry in TI99/4(a) that resets the console when a
	cartridge is inserted or removed.  We emulate this instead of resetting the
	emulator (which is the default in MESS). */
	/*cpu_set_reset_line(0, PULSE_LINE);
	tms9901_reset(0);
	if (! has_evpc)
		TMS9928A_reset();
	if (has_evpc)
		v9938_reset();*/

	switch (slot_type[id])
	{
	case SLOT_EMPTY:
		break;

	case SLOT_GROM:
		memset(memory_region(region_grom) + 0x6000, 0, 0xA000);
		break;

	case SLOT_MINIMEM:
		cartridge_minimemory = FALSE;
		/* we should insert some code to save the minimem contents... */
	case SLOT_CROM:
		memset(cartridge_pages[0], 0, 0x2000);
		break;

	case SLOT_DROM:
		cartridge_paged = FALSE;
		current_page_ptr = cartridge_pages[0];
		break;
	}

	slot_type[id] = SLOT_EMPTY;
}


/*
	ti99_init_machine(); called before ti99_load_rom...
*/
void machine_init_ti99(void)
{
	int i;


	/* erase GPL_port_lookup, so that only console_GROMs are installed by default */
	memset(GPL_port_lookup, 0, sizeof(GPL_port_lookup));

	for (i=0; i<8;i++)
		console_GROMs.GROM_flags[i] = gf_has_look_ahead;

	console_GROMs.data_ptr = memory_region(region_grom);
	console_GROMs.addr = 0;
	console_GROMs.cur_flags = console_GROMs.GROM_flags[0];

	if (ti99_model == model_99_4p)
	{
		/* set up system ROM and scratch pad pointers */
		cpu_setbank(1, memory_region(REGION_CPU1) + offset_rom0_4p);
		cpu_setbank(2, sRAM_ptr);
	}
	else
	{
		/* set up 4 mirrors of scratch pad to the same address */
		cpu_setbank(1, sRAM_ptr);
		cpu_setbank(2, sRAM_ptr);
		cpu_setbank(3, sRAM_ptr);
		cpu_setbank(4, sRAM_ptr);
	}

	/* reset cartridge mapper */
	current_page_ptr = cartridge_pages[0];

	/* init tms9901 */
	tms9901_init(0, & tms9901reset_param_ti99);

	if (! has_evpc)
		TMS9928A_reset();
	if (has_evpc)
		v9938_reset();

	/* clear keyboard interface state (probably overkill, but can't harm) */
	KeyCol = 0;
	AlphaLockLine = 0;

	/* reset handset */
	handset_buf = 0;
	handset_buflen = 0;
	handset_clock = 0;
	handset_ack = 0;
	tms9901_set_single_int(0, 12, 0);

	/* read config */
	if (ti99_model == model_99_4p)
		xRAM_kind = xRAM_kind_99_4p_1Mb;	/* hack */
	else
		xRAM_kind = (readinputport(input_port_config) >> config_xRAM_bit) & config_xRAM_mask;
	has_speech = (readinputport(input_port_config) >> config_speech_bit) & config_speech_mask;
	fdc_kind = (readinputport(input_port_config) >> config_fdc_bit) & config_fdc_mask;
	has_ide = (readinputport(input_port_config) >> config_ide_bit) & config_ide_mask;
	has_rs232 = (readinputport(input_port_config) >> config_rs232_bit) & config_rs232_mask;
	has_handset = (ti99_model == model_99_4) && ((readinputport(input_port_config) >> config_handsets_bit) & config_handsets_mask);

	/* set up optional expansion hardware */
	ti99_peb_init(ti99_model == model_99_4p, tms9901_set_int1, NULL);

	if (ti99_model == model_99_4p)
		ti99_4p_internal_dsr_init();

	if (has_speech)
	{
		spchroms_interface speech_intf = { region_speech_rom };

		spchroms_config(& speech_intf);

		install_mem_read16_handler(0, 0x9000, 0x93ff, ti99_rw_rspeech);
		install_mem_write16_handler(0, 0x9400, 0x97ff, ti99_ww_wspeech);
	}
	else
	{
		install_mem_read16_handler(0, 0x9000, 0x93ff, ti99_rw_null8bits);
		install_mem_write16_handler(0, 0x9400, 0x97ff, ti99_ww_null8bits);
	}

	switch (xRAM_kind)
	{
	case xRAM_kind_none:
	default:
		/* reset mem handler to none */
		install_mem_read16_handler(0, 0x2000, 0x3fff, ti99_rw_null8bits);
		install_mem_write16_handler(0, 0x2000, 0x3fff, ti99_ww_null8bits);
		install_mem_read16_handler(0, 0xa000, 0xffff, ti99_rw_null8bits);
		install_mem_write16_handler(0, 0xa000, 0xffff, ti99_ww_null8bits);
		break;
	case xRAM_kind_TI:
		ti99_TIxram_init();
		break;
	case xRAM_kind_super_AMS:
		ti99_sAMSxram_init();
		break;
	case xRAM_kind_99_4p_1Mb:
		ti99_4p_mapper_init();
		break;
	case xRAM_kind_foundation_128k:
	case xRAM_kind_foundation_512k:
	case xRAM_kind_myarc_128k:
	case xRAM_kind_myarc_512k:
		ti99_myarcxram_init();
		break;
	}

	switch (fdc_kind)
	{
	case fdc_kind_TI:
		ti99_fdc_init();
		break;
	case fdc_kind_BwG:
		ti99_bwg_init();
		break;
	case fdc_kind_hfdc:
		ti99_hfdc_init();
		break;
	case fdc_kind_none:
		break;
	}

	if (has_ide)
		ti99_ide_init();

	if (has_rs232)
		ti99_rs232_init();

	if (has_evpc)
		ti99_evpc_init();
}

void machine_stop_ti99(void)
{
	if (has_rs232)
		ti99_rs232_cleanup();

	tms9901_cleanup(0);
}


/*
	video initialization.
*/
int video_start_ti99_4ev(void)
{
	return v9938_init(MODEL_V9938, 0x20000, tms9901_set_int2);	/* v38 with 128 kb of video RAM */
}

/*
	VBL interrupt  (mmm...  actually, it happens when the beam enters the lower
	border, so it is not a genuine VBI, but who cares ?)
*/
void ti99_vblank_interrupt(void)
{
	TMS9928A_interrupt();
	if (has_handset)
		ti99_handset_task();
}

void ti99_4ev_hblank_interrupt(void)
{
	v9938_interrupt();
}


/*===========================================================================*/
#if 0
#pragma mark -
#pragma mark MEMORY HANDLERS
#endif
/*
	Memory handlers.

	TODO:
	* save minimem RAM when quitting
	* actually implement GRAM support and GPL port support
*/

/*
	Same as MRA_NOP, but with an additionnal delay.
*/
READ16_HANDLER ( ti99_rw_null8bits )
{
	tms9900_ICount -= 4;

	return (0);
}

WRITE16_HANDLER ( ti99_ww_null8bits )
{
	tms9900_ICount -= 4;
}

/*
	Cartridge read: same as MRA_ROM, but with an additionnal delay.
*/
READ16_HANDLER ( ti99_rw_cartmem )
{
	tms9900_ICount -= 4;

	return current_page_ptr[offset];
}

/*
	this handler handles ROM switching in cartridges
*/
WRITE16_HANDLER ( ti99_ww_cartmem )
{
	tms9900_ICount -= 4;

	if (cartridge_minimemory && offset >= 0x800)
		/* handle minimem RAM */
		COMBINE_DATA(current_page_ptr+offset);
	else if (cartridge_paged)
		/* handle pager */
		current_page_ptr = cartridge_pages[offset & 1];
}


/*---------------------------------------------------------------------------*/
/*
	memory-mapped register handlers.

Theory:

	These are registers to communicate with several peripherals.
	These registers are all 8-bit-wide, and are located at even adresses between
	0x8400 and 0x9FFE.
	The registers are identified by (addr & 1C00), and, for VDP and GPL access,
	by (addr & 2).  These registers are either read-only or write-only.  (Or,
	more accurately, (addr & 4000) disables register read, whereas
	!(addr & 4000) disables register write.)

	Memory mapped registers list:
	- write sound chip. (8400-87FE)
	- read VDP memory. (8800-8BFC), (addr&2) == 0
	- read VDP status. (8802-8BFE), (addr&2) == 2
	- write VDP memory. (8C00-8FFC), (addr&2) == 0
	- write VDP address and VDP register. (8C02-8FFE), (addr&2) == 2
	- read speech synthesis chip. (9000-93FE)
	- write speech synthesis chip. (9400-97FE)
	- read GPL memory. (9800-9BFC), (addr&2) == 0 (1)
	- read GPL adress. (9802-9BFE), (addr&2) == 2 (1)
	- write GPL memory. (9C00-9FFC), (addr&2) == 0 (1)
	- write GPL adress. (9C02-9FFE), (addr&2) == 2 (1)

(1) on some hardware, (addr & 0x3FC) provides a GPL page number.
*/

/*
	TMS9919 sound chip write
*/
WRITE16_HANDLER ( ti99_ww_wsnd )
{
	tms9900_ICount -= 4;

	SN76496_0_w(offset, (data >> 8) & 0xff);
}

/*
	TMS9918(A)/9928(A)/9929(A) VDP read
*/
READ16_HANDLER ( ti99_rw_rvdp )
{
	tms9900_ICount -= 4;

	if (offset & 1)
	{	/* read VDP status */
		return ((int) TMS9928A_register_r(0)) << 8;
	}
	else
	{	/* read VDP RAM */
		return ((int) TMS9928A_vram_r(0)) << 8;
	}
}

/*
	TMS9918(A)/9928(A)/9929(A) vdp write
*/
WRITE16_HANDLER ( ti99_ww_wvdp )
{
	tms9900_ICount -= 4;

	if (offset & 1)
	{	/* write VDP address */
		TMS9928A_register_w(0, (data >> 8) & 0xff);
	}
	else
	{	/* write VDP data */
		TMS9928A_vram_w(0, (data >> 8) & 0xff);
	}
}

/*
	V38 VDP read
*/
READ16_HANDLER ( ti99_rw_rv38 )
{
	tms9900_ICount -= 4;

	if (offset & 1)
	{	/* read VDP status */
		return ((int) v9938_status_r(0)) << 8;
	}
	else
	{	/* read VDP RAM */
		return ((int) v9938_vram_r(0)) << 8;
	}
}

/*
	V38 vdp write
*/
WRITE16_HANDLER ( ti99_ww_wv38 )
{
	tms9900_ICount -= 4;

	switch (offset & /*3*/1)
	{
	case 0:
		/* write VDP data */
		v9938_vram_w(0, (data >> 8) & 0xff);
		break;
	case 1:
		/* write VDP address */
		v9938_command_w(0, (data >> 8) & 0xff);
		break;
#if 0
	case 2:
		/* write VDP palette */
		v9938_palette_w(0, data);
		break;
	case 3:
		/* write VDP register */
		v9938_register_w(0, data);
		break;
#endif
	}
}

/*
	TMS5200 speech chip read
*/
static READ16_HANDLER ( ti99_rw_rspeech )
{
	tms9900_ICount -= 4;		/* this is just a minimum, it can be more */

	return ((int) tms5220_status_r(offset)) << 8;
}

#if 0

static void speech_kludge_callback(int dummy)
{
	if (! tms5220_ready_r())
	{
		/* Weirdly enough, we are always seeing some problems even though
		everything is working fine. */
		/*double time_to_ready = tms5220_time_to_ready();
		logerror("ti99/4a speech says aaargh!\n");
		logerror("(time to ready: %f -> %d)\n", time_to_ready, (int) ceil(3000000*time_to_ready));*/
	}
}

#endif

/*
	TMS5200 speech chip write
*/
static WRITE16_HANDLER ( ti99_ww_wspeech )
{
	tms9900_ICount -= 30;		/* this is just an approx. minimum, it can be much more */

#if 1
	/* the stupid design of the tms5220 core means that ready is cleared when
	there are 15 bytes in FIFO.  It should be 16.  Of course, if it were the
	case, we would need to store the value on the bus, which would be more
	complex. */
	if (! tms5220_ready_r())
	{
		double time_to_ready = tms5220_time_to_ready();
		int cycles_to_ready = ceil(TIME_TO_CYCLES(0, time_to_ready));

		logerror("time to ready: %f -> %d\n", time_to_ready, (int) cycles_to_ready);

		tms9900_ICount -= cycles_to_ready;
		timer_set(TIME_NOW, 0, /*speech_kludge_callback*/NULL);
	}
#endif

	tms5220_data_w(offset, (data >> 8) & 0xff);
}

/*
	GPL read
*/
READ16_HANDLER ( ti99_rw_rgpl )
{
	GROM_port_t *port = GPL_port_lookup[(offset & 0x1FE) >> 1];	/* GROM/GRAM can be paged */
	int reply;


	tms9900_ICount -= 4/*16*/;		/* from 4 to 16? */

	if (offset & 1)
	{	/* read GPL address */
		/* the console GROMs are always affected */
		console_GROMs.waddr_LSB = FALSE;	/* right??? */

		if (console_GROMs.raddr_LSB)
		{
			reply = (console_GROMs.addr << 8) & 0xff00;
			console_GROMs.raddr_LSB = FALSE;
		}
		else
		{
			reply = console_GROMs.addr & 0xff00;
			console_GROMs.raddr_LSB = TRUE;
		}

		if (port)
		{
			port->waddr_LSB = FALSE;

			if (port->raddr_LSB)
			{
				reply = (port->addr << 8) & 0xff00;
				port->raddr_LSB = FALSE;
			}
			else
			{
				reply = port->addr & 0xff00;
				port->raddr_LSB = TRUE;
			}
		}
	}
	else
	{	/* read GPL data */
		/* the console GROMs are always affected */
		reply = ((int) console_GROMs.buf) << 8;	/* retreive buffer */

		/* read ahead */
		console_GROMs.buf = console_GROMs.data_ptr[console_GROMs.addr];
		console_GROMs.addr = ((console_GROMs.addr + 1) & 0x1FFF) | (console_GROMs.addr & 0xE000);
		console_GROMs.raddr_LSB = console_GROMs.waddr_LSB = FALSE;

		if (port)
		{
			reply = ((int) port->buf) << 8;	/* retreive buffer */

			/* read ahead */
			port->buf = port->data_ptr[port->addr];
			port->addr = ((port->addr + 1) & 0x1FFF) | (port->addr & 0xE000);
			port->raddr_LSB = port->waddr_LSB = FALSE;
		}
	}

	return reply;
}

/*
	GPL write
*/
WRITE16_HANDLER ( ti99_ww_wgpl )
{
	GROM_port_t *port = GPL_port_lookup[(offset & 0x1FE) >> 1];	/* GROM/GRAM can be paged */


	tms9900_ICount -= 4/*16*/;		/* from 4 to 16? */

	if (offset & 1)
	{	/* write GPL adress */
		/* the console GROMs are always affected */
		console_GROMs.raddr_LSB = FALSE;

		if (console_GROMs.waddr_LSB)
		{
			console_GROMs.addr = (console_GROMs.addr & 0xFF00) | ((data >> 8) & 0xFF);

			/* read ahead */
			console_GROMs.buf = console_GROMs.data_ptr[console_GROMs.addr];
			console_GROMs.addr = ((console_GROMs.addr + 1) & 0x1FFF) | (console_GROMs.addr & 0xE000);

			console_GROMs.waddr_LSB = FALSE;
		}
		else
		{
			console_GROMs.addr = (data & 0xFF00) | (console_GROMs.addr & 0xFF);
			console_GROMs.cur_flags = console_GROMs.GROM_flags[data >> 13];

			console_GROMs.waddr_LSB = TRUE;
		}

		if (port)
		{
			port->raddr_LSB = FALSE;

			if (port->waddr_LSB)
			{
				port->addr = (port->addr & 0xFF00) | ((data >> 8) & 0xFF);

				/* read ahead */
				port->buf = port->data_ptr[port->addr];
				port->addr = ((port->addr + 1) & 0x1FFF) | (port->addr & 0xE000);

				port->waddr_LSB = FALSE;
			}
			else
			{
				port->addr = (data & 0xFF00) | (port->addr & 0xFF);
				port->cur_flags = port->GROM_flags[data >> 13];

				port->waddr_LSB = TRUE;
			}
		}
	}
	else
	{	/* write GPL byte */
		/* the console GROMs are always affected */
		/* BTW, console GROMs are never GRAMs, therefore there is no need to check */
		/* read ahead */
		console_GROMs.buf = console_GROMs.data_ptr[console_GROMs.addr];
		console_GROMs.addr = ((console_GROMs.addr + 1) & 0x1FFF) | (console_GROMs.addr & 0xE000);
		console_GROMs.raddr_LSB = console_GROMs.waddr_LSB = FALSE;

		if (port)
		{
			/* GRAM support is almost ready, but we need to support imitation "GROMs" with no read-ahead */
			if (port->GROM_flags[port->addr >> 13])
				port->data_ptr[port->addr] = ((data >> 8) & 0xFF);

			/* read ahead */
			port->buf = port->data_ptr[port->addr];
			port->addr = ((port->addr + 1) & 0x1FFF) | (port->addr & 0xE000);
			port->raddr_LSB = port->waddr_LSB = FALSE;
		}
	}
}


/*===========================================================================*/
#if 0
#pragma mark -
#pragma mark TI-99/4 HANDSETS
#endif
/*
	Handset support (TI99/4 only)

	The ti99/4 was intended to support some so-called "IR remote handsets".
	This feature was canceled at the last minute (reportedly ten minutes before
	the introductory press conference in June 1979), but the first thousands of
	99/4 units had the required port, and the support code was seemingly not
	deleted from ROMs until the introduction of the ti99/4a in 1981.  You could
	connect up to 4 20-key keypads, and up to 4 joysticks with a maximum
	resolution of 15 levels on each axis.

	The keyboard DSR was able to couple two 20-key keypads together to emulate
	a full 40-key keyboard.  Keyboard modes 0, 1 and 2 would select either the
	console keyboard with its two wired remote controllers (i.e. joysticks), or
	remote handsets 1 and 2 with their associated IR remote controllers (i.e.
	joysticks), according to which was currently active.
*/

/*
	ti99_handset_poll_bus()

	Poll the current state of the 4-bit data bus that goes from the I/R
	receiver to the tms9901.
*/
static int ti99_handset_poll_bus(void)
{
	return (has_handset) ? (handset_buf & 0xf) : 0;
}

/*
	ti99_handset_ack_callback()

	Handle data acknowledge sent by the ti-99/4 handset ISR (through tms9901
	line P0).  This function is called by a delayed timer 30ms after the state
	of P0 is changed, because, in one occasion, the ISR asserts the line before
	it reads the data, so we need to delay the acknowledge process.
*/
static void ti99_handset_ack_callback(int dummy)
{
	handset_clock = ! handset_clock;
	handset_buf >>= 4;
	handset_buflen--;
	tms9901_set_single_int(0, 12, 0);

	if (handset_buflen == 1)
	{
		/* Unless I am missing something, the third and last nybble of the
		message is not acknowledged by the DSR in any way, and the first nybble
		of next message is not requested for either, so we need to decide on
		our own when we can post a new event.  Currently, we wait for 1000us
		after the DSR acknowledges the second nybble. */
		timer_set(TIME_IN_USEC(1000), 0, ti99_handset_ack_callback);
	}

	if (handset_buflen == 0)
		/* See if we need to post a new event */
		ti99_handset_task();
}

/*
	ti99_handset_set_ack()

	Handler for tms9901 P0 pin (handset data acknowledge)
*/
static void ti99_handset_set_ack(int offset, int data)
{
	if (has_handset && handset_buflen && (data != handset_ack))
	{
		handset_ack = data;
		if (data == handset_clock)
			/* I don't know what the real delay is, but 30us apears to be enough */
			timer_set(TIME_IN_USEC(30), 0, ti99_handset_ack_callback);
	}
}

/*
	ti99_handset_post_message()

	Post a 12-bit message: trigger an interrupt on the tms9901, and store the
	message in the I/R receiver buffer so that the handset ISR will read this
	message.

	message: 12-bit message to post (only the 12 LSBits are meaningful)
*/
static void ti99_handset_post_message(int message)
{
	/* post message and assert interrupt */
	handset_clock = 1;
	handset_buf = ~ message;
	handset_buflen = 3;
	tms9901_set_single_int(0, 12, 1);
}

/*
	ti99_handset_poll_keyboard()

	Poll the current state of one given handset keypad.

	num: number of the keypad to poll (0-3)

	Returns TRUE if the handset state has changed and a message was posted.
*/
static int ti99_handset_poll_keyboard(int num)
{
	static UINT8 previous_key[max_handsets];

	UINT32 key_buf;
	UINT8 current_key;
	int i;


	/* read current key state */
	key_buf = ( readinputport(input_port_IR_keypads+num)
					| (readinputport(input_port_IR_keypads+num+1) << 16) ) >> (4*num);

	/* If a key was previously pressed, this key was not shift, and this key is
	still down, then don't change the current key press. */
	if (previous_key[num] && (previous_key[num] != 0x24)
			&& (key_buf & (1 << (previous_key[num] & 0x1f))))
	{
		/* check the shift modifier state */
		if (((previous_key[num] & 0x20) != 0) == ((key_buf & 0x0008) != 0))
			/* the shift modifier state has not changed */
			return FALSE;
		else
		{
			/* the shift modifier state has changed: we need to update the
			keyboard state */
			if (key_buf & 0x0008)
			{	/* shift has been pressed down */
				previous_key[num] = current_key = previous_key[num] | 0x20;
			}
			else
			{	/* shift has been pressed down */
				previous_key[num] = current_key = previous_key[num] & ~0x20;
			}
			/* post message */
			ti99_handset_post_message((((unsigned) current_key) << 4) | (num << 1));

			return TRUE;
		}
				
	}

	current_key = 0;	/* default value if no key is down */
	if (key_buf & 0x0008)
		current_key |= 0x20;	/* set shift flag */
	for (i=0; i<20; i++)
	{
		if (key_buf & (1 << i))
		{
			current_key = i + 1;

			if (current_key != 0x24)
				/* If this is the shift key, any other key we may find will
				have higher priority; otherwise, we may exit the loop and keep
				the key we have just found. */
				break;
		}
	}

	if (current_key != previous_key[num])
	{
		previous_key[num] = current_key;

		/* post message */
		ti99_handset_post_message((((unsigned) current_key) << 4) | (num << 1));

		return TRUE;
	}
		
	return FALSE;
}

/*
	ti99_handset_poll_joystick()

	Poll the current state of one given handset joystick.

	num: number of the joystick to poll (0-3)

	Returns TRUE if the handset state has changed and a message was posted.
*/
static int ti99_handset_poll_joystick(int num)
{
	static UINT8 previous_joy[max_handsets];
	UINT8 current_joy;
	int current_joy_x, current_joy_y;
	int message;

	/* read joystick position */
	current_joy_x = readinputport(input_port_IR_joysticks+2*num);
	current_joy_y = readinputport(input_port_IR_joysticks+2*num+1);
	/* compare with last saved position */
	current_joy = current_joy_x | (current_joy_y << 4);
	if (current_joy != previous_joy[num])
	{
		/* save position */
		previous_joy[num] = current_joy;

		/* transform position to signed quantity */
		current_joy_x -= 7;
		current_joy_y -= 7;

		message = 0;

		/* set sign */
		/* note that we set the sign if the joystick position is 0 to work
		around a bug in the ti99/4 ROMs */
		if (current_joy_x <= 0)
		{
			message |= 0x040;
			current_joy_x = - current_joy_x;
		}

		if (current_joy_y <= 0)
		{
			message |= 0x400;
			current_joy_y = - current_joy_y;
		}

		/* convert absolute values to Gray code and insert in message */
		if (current_joy_x & 4)
			current_joy_x ^= 3;
		if (current_joy_x & 2)
			current_joy_x ^= 1;
		message |= current_joy_x << 3;

		if (current_joy_y & 4)
			current_joy_y ^= 3;
		if (current_joy_y & 2)
			current_joy_y ^= 1;
		message |= current_joy_y << 7;

		/* set joystick address */
		message |= (num << 1) | 0x1;

		/* post message */
		ti99_handset_post_message(message);

		return TRUE;
	}

	return FALSE;
}

/*
	ti99_handset_task()

	Manage handsets, posting an event if the state of any handset has changed.
*/
static void ti99_handset_task(void)
{
	int i;

	if (handset_buflen == 0)
	{	/* poll every handset */
		for (i=0; i<max_handsets; i++)
			if (ti99_handset_poll_joystick(i))
				return;
		for (i=0; i<max_handsets; i++)
			if (ti99_handset_poll_keyboard(i))
				return;
	}
	else if (handset_buflen == 3)
	{	/* update messages after they have been posted */
		if (handset_buf & 1)
		{	/* keyboard */
			ti99_handset_poll_keyboard((~ (handset_buf >> 1)) & 0x3);
		}
		else
		{	/* joystick */
			ti99_handset_poll_joystick((~ (handset_buf >> 1)) & 0x3);
		}
	}
}


/*===========================================================================*/
#if 0
#pragma mark -
#pragma mark TMS9901 INTERFACE
#endif
/*
	TI99/4x-specific tms9901 I/O handlers

	See mess/machine/tms9901.c for generic tms9901 CRU handlers.

	BTW, although TMS9900 is generally big-endian, it is little endian as far as CRU is
	concerned. (i.e. bit 0 is the least significant)

KNOWN PROBLEMS:
	* a read or write to bits 16-31 causes TMS9901 to quit timer mode.  The problem is:
	  on a TI99/4A, any memory access causes a dummy CRU read.  Therefore, TMS9901 can quit
	  timer mode even though the program did not explicitely ask... and THIS is impossible
	  to emulate efficiently (we would have to check every memory operation).
*/
/*
	TMS9901 interrupt handling on a TI99/4(a).

	TI99/4(a) uses the following interrupts:
	INT1: external interrupt (used by RS232 controller, for instance)
	INT2: VDP interrupt
	TMS9901 timer interrupt (overrides INT3)
	INT12: handset interrupt (only on a TI-99/4 with the handset prototypes)

	Three (occasionally four) interrupts are used by the system (INT1, INT2,
	timer, and INT12 on a TI-99/4 with remote handset prototypes), out of 15/16
	possible interrupts.  Keyboard pins can be used as interrupt pins, too, but
	this is not emulated (it's a trick, anyway, and I don't know any program
	which uses it).

	When an interrupt line is set (and the corresponding bit in the interrupt mask is set),
	a level 1 interrupt is requested from the TMS9900.  This interrupt request lasts as long as
	the interrupt pin and the revelant bit in the interrupt mask are set.

	TIMER interrupts are kind of an exception, since they are not associated with an external
	interrupt pin, and I guess it takes a write to the 9901 CRU bit 3 ("SBO 3") to clear
	the pending interrupt (or am I wrong once again ?).

nota:
	All interrupt routines notify (by software) the TMS9901 of interrupt recognition ("SBO n").
	However, AFAIK, this has strictly no consequence in the TMS9901, and interrupt routines
	would work fine without this (except probably TIMER interrupt).  All this is quite weird.
	Maybe the interrupt recognition notification is needed on TMS9985, or any other weird
	variant of TMS9900 (TI990/10 with TI99 development system?).
*/

/*
	set the state of int1 (called by the peb core)
*/
static void tms9901_set_int1(int state)
{
	tms9901_set_single_int(0, 1, state);
}

/*
	set the state of int2 (called by the tms9928 core)
*/
void tms9901_set_int2(int state)
{
	tms9901_set_single_int(0, 2, state);
}

/*
	Called by the 9901 core whenever the state of INTREQ and IC0-3 changes
*/
static void tms9901_interrupt_callback(int intreq, int ic)
{
	if (intreq)
	{
		/* On TI99, TMS9900 IC0-3 lines are not connected to TMS9901,
		 * but hard-wired to force level 1 interrupts */
		cpu_set_irq_line_and_vector(0, 0, ASSERT_LINE, 1);	/* interrupt it, baby */
	}
	else
	{
		cpu_set_irq_line(0, 0, CLEAR_LINE);
	}
}

/*
	Read pins INT3*-INT7* of TI99's 9901.

	signification:
	 (bit 1: INT1 status)
	 (bit 2: INT2 status)
	 bit 3-7: keyboard status bits 0 to 4
*/
static int ti99_R9901_0(int offset)
{
	int answer;


	if ((ti99_model == model_99_4) && (KeyCol == 7))
		answer = (ti99_handset_poll_bus() << 3) | 0x80;
	else
	{
		answer = ((readinputport(input_port_keyboard + (KeyCol >> 1)) >> ((KeyCol & 1) * 8)) << 3) & 0xF8;

		if ((ti99_model == model_99_4a) || (ti99_model == model_99_4p))
		{
			if (! AlphaLockLine)
				answer &= ~ (readinputport(input_port_caps_lock) << 3);
		}
	}

	return (answer);
}

/*
	Read pins INT8*-INT15* of TI99's 9901.

	signification:
	 bit 0-2: keyboard status bits 5 to 7
	 bit 3: weird, not emulated
	 (bit 4: IR remote handset interrupt)
	 bit 5-7: weird, not emulated
*/
static int ti99_R9901_1(int offset)
{
	int answer;


	if ((ti99_model == model_99_4) && (KeyCol == 7))
		answer = 0x07;
	else
		answer = ((readinputport(input_port_keyboard + (KeyCol >> 1)) >> ((KeyCol & 1) * 8)) >> 5) & 0x07;

	return answer;
}

/*
	Read pins P0-P7 of TI99's 9901.

	 bit 1: handset data clock pin
*/
static int ti99_R9901_2(int offset)
{
	return (has_handset && handset_clock) ? 2 : 0;
}

/*
	Read pins P8-P15 of TI99's 9901.

	 bit 26: IR handset interrupt
	 bit 27: tape input
*/
static int ti99_R9901_3(int offset)
{
	int answer;

	if (has_handset && (handset_buflen == 3))
		answer = 0;
	else
		answer = 4;	/* on systems without handset, the pin is pulled up to avoid spurious interrupts */

	/* we don't take CS2 into account, as CS2 is a write-only unit */
	if (device_input(image_from_devtype_and_index(IO_CASSETTE, 0)) > 0)
		answer |= 8;

	return answer;
}


/*
	WRITE column number bit 2 (P2)
*/
static void ti99_KeyC2(int offset, int data)
{
	if (data)
		KeyCol |= 1;
	else
		KeyCol &= (~1);
}

/*
	WRITE column number bit 1 (P3)
*/
static void ti99_KeyC1(int offset, int data)
{
	if (data)
		KeyCol |= 2;
	else
		KeyCol &= (~2);
}

/*
	WRITE column number bit 0 (P4)
*/
static void ti99_KeyC0(int offset, int data)
{
	if (data)
		KeyCol |= 4;
	else
		KeyCol &= (~4);
}

/*
	WRITE alpha lock line - TI99/4a only (P5)
*/
static void ti99_AlphaW(int offset, int data)
{
	if ((ti99_model == model_99_4a) || (ti99_model == model_99_4p))
		AlphaLockLine = data;
}

static void ti99_CSx_motor(int data, int cassette_id)
{
	mess_image *img = image_from_devtype_and_index(IO_CASSETTE, cassette_id);
	if (data)
		device_status(img, device_status(img, -1) & ~ WAVE_STATUS_MOTOR_INHIBIT);
	else
		device_status(img, device_status(img, -1) | WAVE_STATUS_MOTOR_INHIBIT);
}

/*
	command CS1 tape unit motor (P6)
*/
static void ti99_CS1_motor(int offset, int data)
{
	ti99_CSx_motor(data, 0);
}

/*
	command CS2 tape unit motor (P7)
*/
static void ti99_CS2_motor(int offset, int data)
{
	ti99_CSx_motor(data, 1);
}

/*
	audio gate (P8)

	connected to the AUDIO IN pin of TMS9919

	set to 1 before using tape (in order not to burn the TMS9901 ??)
	Must actually control the mixing of tape sound with computer sound.

	I am not sure about polarity.
*/
static void ti99_audio_gate(int offset, int data)
{
	if (data)
		DAC_data_w(0, 0xFF);
	else
		DAC_data_w(0, 0);
}

/*
	tape output (P9)
	I think polarity is correct, but don't take my word for it.
*/
static void ti99_CS_output(int offset, int data)
{
	device_output(image_from_devtype_and_index(IO_CASSETTE, 0), data ? 32767 : -32767);
	device_output(image_from_devtype_and_index(IO_CASSETTE, 1), data ? 32767 : -32767);
}



/*===========================================================================*/
#if 0
#pragma mark -
#pragma mark SGCPU INTERNAL DSR
#endif
/*
	SNUG SGCPU (a.k.a. 99/4p) internal DSR support.

	Includes a few specific signals, and an extra ROM.
*/

/* prototypes */
static void ti99_4p_internal_dsr_cru_w(int offset, int data);
static READ16_HANDLER(ti99_4p_rw_internal_dsr);


static const ti99_peb_16bit_card_handlers_t ti99_4p_internal_dsr_handlers =
{
	NULL,
	ti99_4p_internal_dsr_cru_w,
	ti99_4p_rw_internal_dsr,
	NULL
};

/* pointer to the internal DSR ROM data */
static UINT16 *ti99_4p_internal_DSR;

static int internal_rom6_enable;


/* set up handlers, and set initial state */
static void ti99_4p_internal_dsr_init(void)
{
	ti99_4p_internal_DSR = (UINT16 *) (memory_region(REGION_CPU1) + offset_rom4_4p);

	ti99_peb_set_16bit_card_handlers(0x0f00, & ti99_4p_internal_dsr_handlers);

	internal_rom6_enable = 0;
	ti99_4p_peb_set_senila(0);
	ti99_4p_peb_set_senilb(0);
}

/* write CRU bit:
	bit0: enable/disable internal DSR ROM,
	bit1: enable/disable internal cartridge ROM
	bit2: set/clear senila
	bit3: set/clear senilb*/
static void ti99_4p_internal_dsr_cru_w(int offset, int data)
{
	switch (offset)
	{
	case 1:
		internal_rom6_enable = data;
		/* ... */
		break;
	case 2:
		ti99_4p_peb_set_senila(data);
		break;
	case 3:
		ti99_4p_peb_set_senilb(data);
		break;
	case 4:
		/* 0: 16-bit (fast) memory timings */
		/* 1: 8-bit memory timings */
		break;
	case 5:
		/* if 0: "KBENA" mode (enable keyboard?) */
		/* if 1: "KBINH" mode (inhibit keyboard?) */
		break;
	}
}

/* read internal DSR ROM */
static READ16_HANDLER(ti99_4p_rw_internal_dsr)
{
	return ti99_4p_internal_DSR[offset];
}



#if 0
#pragma mark -
#pragma mark MEMORY EXPANSION CARDS
#endif
/*===========================================================================*/
/*
	TI memory extension support.

	Simple 8-bit DRAM: 32kb in two chunks of 8kb and 24kb repectively.
	Since the RAM is on the 8-bit bus, there is an additionnal delay.
*/

static READ16_HANDLER ( ti99_rw_TIxramlow );
static WRITE16_HANDLER ( ti99_ww_TIxramlow );
static READ16_HANDLER ( ti99_rw_TIxramhigh );
static WRITE16_HANDLER ( ti99_ww_TIxramhigh );

static void ti99_TIxram_init(void)
{
	install_mem_read16_handler(0, 0x2000, 0x3fff, ti99_rw_TIxramlow);
	install_mem_write16_handler(0, 0x2000, 0x3fff, ti99_ww_TIxramlow);
	install_mem_read16_handler(0, 0xa000, 0xffff, ti99_rw_TIxramhigh);
	install_mem_write16_handler(0, 0xa000, 0xffff, ti99_ww_TIxramhigh);
}

/* low 8 kb: 0x2000-0x3fff */
static READ16_HANDLER ( ti99_rw_TIxramlow )
{
	tms9900_ICount -= 4;

	return xRAM_ptr[offset];
}

static WRITE16_HANDLER ( ti99_ww_TIxramlow )
{
	tms9900_ICount -= 4;

	COMBINE_DATA(xRAM_ptr + offset);
}

/* high 24 kb: 0xa000-0xffff */
static READ16_HANDLER ( ti99_rw_TIxramhigh )
{
	tms9900_ICount -= 4;

	return xRAM_ptr[offset+0x1000];
}

static WRITE16_HANDLER ( ti99_ww_TIxramhigh )
{
	tms9900_ICount -= 4;

	COMBINE_DATA(xRAM_ptr + offset+0x1000);
}


/*===========================================================================*/
/*
	Super AMS memory extension support.

	Up to 1Mb of SRAM.  Straightforward mapper, works with 4kb chunks.  The mapper was designed
	to be extendable with other RAM areas.
*/

/* prototypes */
static void sAMS_cru_w(int offset, int data);
static READ_HANDLER(sAMS_mapper_r);
static WRITE_HANDLER(sAMS_mapper_w);

static READ16_HANDLER ( ti99_rw_sAMSxramlow );
static WRITE16_HANDLER ( ti99_ww_sAMSxramlow );
static READ16_HANDLER ( ti99_rw_sAMSxramhigh );
static WRITE16_HANDLER ( ti99_ww_sAMSxramhigh );


static const ti99_peb_card_handlers_t sAMS_expansion_handlers =
{
	NULL,
	sAMS_cru_w,
	sAMS_mapper_r,
	sAMS_mapper_w
};


static int sAMS_mapper_on;
static int sAMSlookup[16];


/* set up super AMS handlers, and set initial state */
static void ti99_sAMSxram_init(void)
{
	int i;


	install_mem_read16_handler(0, 0x2000, 0x3fff, ti99_rw_sAMSxramlow);
	install_mem_write16_handler(0, 0x2000, 0x3fff, ti99_ww_sAMSxramlow);
	install_mem_read16_handler(0, 0xa000, 0xffff, ti99_rw_sAMSxramhigh);
	install_mem_write16_handler(0, 0xa000, 0xffff, ti99_ww_sAMSxramhigh);

	ti99_peb_set_card_handlers(0x1e00, & sAMS_expansion_handlers);

	sAMS_mapper_on = 0;

	for (i=0; i<16; i++)
		sAMSlookup[i] = i << 11;
}

/* write CRU bit:
	bit0: enable/disable mapper registers in DSR space,
	bit1: enable/disable address mapping */
static void sAMS_cru_w(int offset, int data)
{
	if (offset == 1)
		sAMS_mapper_on = data;
}

/* read a mapper register */
static READ_HANDLER(sAMS_mapper_r)
{
	return (sAMSlookup[(offset >> 1) & 0xf] >> 11);
}

/* write a mapper register */
static WRITE_HANDLER(sAMS_mapper_w)
{
	sAMSlookup[(offset >> 1) & 0xf] = ((int) data) << 11;
}

/* low 8 kb: 0x2000-0x3fff */
static READ16_HANDLER ( ti99_rw_sAMSxramlow )
{
	tms9900_ICount -= 4;

	if (sAMS_mapper_on)
		return xRAM_ptr[(offset&0x7ff)+sAMSlookup[(0x1000+offset)>>11]];
	else
		return xRAM_ptr[offset+0x1000];
}

static WRITE16_HANDLER ( ti99_ww_sAMSxramlow )
{
	tms9900_ICount -= 4;

	if (sAMS_mapper_on)
		COMBINE_DATA(xRAM_ptr + (offset&0x7ff)+sAMSlookup[(0x1000+offset)>>11]);
	else
		COMBINE_DATA(xRAM_ptr + offset+0x1000);
}

/* high 24 kb: 0xa000-0xffff */
static READ16_HANDLER ( ti99_rw_sAMSxramhigh )
{
	tms9900_ICount -= 4;

	if (sAMS_mapper_on)
		return xRAM_ptr[(offset&0x7ff)+sAMSlookup[(0x5000+offset)>>11]];
	else
		return xRAM_ptr[offset+0x5000];
}

static WRITE16_HANDLER ( ti99_ww_sAMSxramhigh )
{
	tms9900_ICount -= 4;

	if (sAMS_mapper_on)
		COMBINE_DATA(xRAM_ptr + (offset&0x7ff)+sAMSlookup[(0x5000+offset)>>11]);
	else
		COMBINE_DATA(xRAM_ptr + offset+0x5000);
}


/*===========================================================================*/
/*
	SNUG SGCPU (a.k.a. 99/4p) Super AMS clone support.
	Compatible with Super AMS, but uses a 16-bit bus.

	Up to 1Mb of SRAM.  Straightforward mapper, works with 4kb chunks.
*/

/* prototypes */
static void ti99_4p_mapper_cru_w(int offset, int data);
static READ16_HANDLER(ti99_4p_rw_mapper);
static WRITE16_HANDLER(ti99_4p_ww_mapper);


static const ti99_peb_16bit_card_handlers_t ti99_4p_mapper_handlers =
{
	NULL,
	ti99_4p_mapper_cru_w,
	ti99_4p_rw_mapper,
	ti99_4p_ww_mapper
};


static int ti99_4p_mapper_on;
static int ti99_4p_mapper_lookup[16];


/* set up handlers, and set initial state */
static void ti99_4p_mapper_init(void)
{
	int i;

	/* Not required at run-time */
	/*install_mem_read16_handler(0, 0x2000, 0x2fff, MRA16_BANK3);
	install_mem_write16_handler(0, 0x2000, 0x2fff, MWA16_BANK3);
	install_mem_read16_handler(0, 0x3000, 0x3fff, MRA16_BANK4);
	install_mem_write16_handler(0, 0x3000, 0x3fff, MWA16_BANK4);
	install_mem_read16_handler(0, 0xa000, 0xafff, MRA16_BANK5);
	install_mem_write16_handler(0, 0xa000, 0xafff, MWA16_BANK5);
	install_mem_read16_handler(0, 0xb000, 0xbfff, MRA16_BANK6);
	install_mem_write16_handler(0, 0xb000, 0xbfff, MWA16_BANK6);
	install_mem_read16_handler(0, 0xc000, 0xcfff, MRA16_BANK7);
	install_mem_write16_handler(0, 0xc000, 0xcfff, MWA16_BANK7);
	install_mem_read16_handler(0, 0xd000, 0xdfff, MRA16_BANK8);
	install_mem_write16_handler(0, 0xd000, 0xdfff, MWA16_BANK8);
	install_mem_read16_handler(0, 0xe000, 0xefff, MRA16_BANK9);
	install_mem_write16_handler(0, 0xe000, 0xefff, MWA16_BANK9);
	install_mem_read16_handler(0, 0xf000, 0xffff, MRA16_BANK10);
	install_mem_write16_handler(0, 0xf000, 0xffff, MWA16_BANK10);*/

	ti99_peb_set_16bit_card_handlers(0x1e00, & ti99_4p_mapper_handlers);

	ti99_4p_mapper_on = 0;

	for (i=0; i<16; i++)
	{
		ti99_4p_mapper_lookup[i] = i << 11;

		/* update bank base */
		switch (i)
		{
		case 2:
		case 3:
			cpu_setbank(3+(i-2), xRAM_ptr + (i<<11));
			break;

		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
			cpu_setbank(5+(i-10), xRAM_ptr + (i<<11));
			break;
		}
	}
}

/* write CRU bit:
	bit0: enable/disable mapper registers in DSR space,
	bit1: enable/disable address mapping */
static void ti99_4p_mapper_cru_w(int offset, int data)
{
	int i;

	if (offset == 1)
	{
		if (ti99_4p_mapper_on != data)
		{
			ti99_4p_mapper_on = data;

			for (i=0; i<16; i++)
			{
				/* update bank base */
				switch (i)
				{
				case 2:
				case 3:
					cpu_setbank(3+(i-2), xRAM_ptr + (ti99_4p_mapper_on ? (ti99_4p_mapper_lookup[i]) : (i<<11)));
					break;

				case 10:
				case 11:
				case 12:
				case 13:
				case 14:
				case 15:
					cpu_setbank(5+(i-10), xRAM_ptr + (ti99_4p_mapper_on ? (ti99_4p_mapper_lookup[i]) : (i<<11)));
					break;
				}
			}
		}
	}

}

/* read a mapper register */
static READ16_HANDLER(ti99_4p_rw_mapper)
{
	return (ti99_4p_mapper_lookup[offset & 0xf] >> 3);
}

/* write a mapper register */
static WRITE16_HANDLER(ti99_4p_ww_mapper)
{
	int page = offset & 0xf;

	ti99_4p_mapper_lookup[page] = (data & 0xff00) << 3;

	if (ti99_4p_mapper_on)
	{
		/* update bank base */
		switch (page)
		{
		case 2:
		case 3:
			cpu_setbank(3+(page-2), xRAM_ptr+ti99_4p_mapper_lookup[page]);
			break;

		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
			cpu_setbank(5+(page-10), xRAM_ptr+ti99_4p_mapper_lookup[page]);
			break;
		}
	}
}


/*===========================================================================*/
/*
	myarc-ish memory extension support (foundation, and myarc clones).

	Up to 512kb of RAM.  Straightforward mapper, works with 32kb chunks.
*/

static int myarc_cru_r(int offset);
static void myarc_cru_w(int offset, int data);

static READ16_HANDLER ( ti99_rw_myarcxramlow );
static WRITE16_HANDLER ( ti99_ww_myarcxramlow );
static READ16_HANDLER ( ti99_rw_myarcxramhigh );
static WRITE16_HANDLER ( ti99_ww_myarcxramhigh );


static const ti99_peb_card_handlers_t myarc_expansion_handlers =
{
	myarc_cru_r,
	myarc_cru_w,
	NULL,
	NULL
};


static int myarc_cur_page_offset;
static int myarc_page_offset_mask;


/* set up myarc handlers, and set initial state */
static void ti99_myarcxram_init(void)
{
	install_mem_read16_handler(0, 0x2000, 0x3fff, ti99_rw_myarcxramlow);
	install_mem_write16_handler(0, 0x2000, 0x3fff, ti99_ww_myarcxramlow);
	install_mem_read16_handler(0, 0xa000, 0xffff, ti99_rw_myarcxramhigh);
	install_mem_write16_handler(0, 0xa000, 0xffff, ti99_ww_myarcxramhigh);

	switch (xRAM_kind)
	{
	case xRAM_kind_foundation_128k:	/* 128kb foundation */
	case xRAM_kind_myarc_128k:		/* 128kb myarc clone */
		myarc_page_offset_mask = 0x0c000;
		break;
	case xRAM_kind_foundation_512k:	/* 512kb foundation */
	case xRAM_kind_myarc_512k:		/* 512kb myarc clone */
		myarc_page_offset_mask = 0x3c000;
		break;
	default:
		break;	/* let's just keep GCC's big mouth shut */
	}

	switch (xRAM_kind)
	{
	case xRAM_kind_foundation_128k:	/* 128kb foundation */
	case xRAM_kind_foundation_512k:	/* 512kb foundation */
		ti99_peb_set_card_handlers(0x1e00, & myarc_expansion_handlers);
		break;
	case xRAM_kind_myarc_128k:		/* 128kb myarc clone */
	case xRAM_kind_myarc_512k:		/* 512kb myarc clone */
		ti99_peb_set_card_handlers(0x1000, & myarc_expansion_handlers);
		ti99_peb_set_card_handlers(0x1900, & myarc_expansion_handlers);
		break;
	default:
		break;	/* let's just keep GCC's big mouth shut */
	}

	myarc_cur_page_offset = 0;
}

/* read CRU bit:
	bit 1-2 (128kb) or 1-4 (512kb): read current map offset */
static int myarc_cru_r(int offset)
{
	/*if (offset == 0)*/	/* right??? */
	{
		return (myarc_cur_page_offset >> 14);
	}
}

/* write CRU bit:
	bit 1-2 (128kb) or 1-4 (512kb): write map offset */
static void myarc_cru_w(int offset, int data)
{
	offset &= 0x7;	/* right??? */
	if (offset >= 1)
	{
		int mask = 1 << (offset-1+14);

		if (data)
			myarc_cur_page_offset |= mask;
		else
			myarc_cur_page_offset &= ~mask;

		myarc_cur_page_offset &= myarc_page_offset_mask;
	}
}

/* low 8 kb: 0x2000-0x3fff */
static READ16_HANDLER ( ti99_rw_myarcxramlow )
{
	tms9900_ICount -= 4;

	return xRAM_ptr[myarc_cur_page_offset + offset];
}

static WRITE16_HANDLER ( ti99_ww_myarcxramlow )
{
	tms9900_ICount -= 4;

	COMBINE_DATA(xRAM_ptr + myarc_cur_page_offset + offset);
}

/* high 24 kb: 0xa000-0xffff */
static READ16_HANDLER ( ti99_rw_myarcxramhigh )
{
	tms9900_ICount -= 4;

	return xRAM_ptr[myarc_cur_page_offset + offset+0x1000];
}

static WRITE16_HANDLER ( ti99_ww_myarcxramhigh )
{
	tms9900_ICount -= 4;

	COMBINE_DATA(xRAM_ptr + myarc_cur_page_offset + offset+0x1000);
}


/*===========================================================================*/
#if 0
#pragma mark -
#pragma mark EVPC VIDEO CARD
#endif
/*
	SNUG's EVPC emulation
*/

/* prototypes */
static int evpc_cru_r(int offset);
static void evpc_cru_w(int offset, int data);
static READ_HANDLER(evpc_mem_r);
static WRITE_HANDLER(evpc_mem_w);

/* pointer to the evpc DSR area */
static UINT8 *ti99_evpc_DSR;

/* pointer to the evpc novram area */
/*static UINT8 *ti99_evpc_novram;*/

static int RAMEN;

static int evpc_dsr_page;

static const ti99_peb_card_handlers_t evpc_handlers =
{
	evpc_cru_r,
	evpc_cru_w,
	evpc_mem_r,
	evpc_mem_w
};


/*
	Reset evpc card, set up handlers
*/
static void ti99_evpc_init(void)
{
	ti99_evpc_DSR = memory_region(region_dsr) + offset_evpc_dsr;

	RAMEN = 0;
	evpc_dsr_page = 0;

	ti99_peb_set_card_handlers(0x1400, & evpc_handlers);
}

/*
	Read evpc CRU interface
*/
static int evpc_cru_r(int offset)
{
	return 0;	/* dip-switch value */
}

/*
	Write evpc CRU interface
*/
static void evpc_cru_w(int offset, int data)
{
	switch (offset)
	{
	case 0:
		break;

	case 1:
		if (data)
			evpc_dsr_page |= 1;
		else
			evpc_dsr_page &= ~1;
		break;

	case 2:
		break;

	case 3:
		RAMEN = data;
		break;

	case 4:
		if (data)
			evpc_dsr_page |= 4;
		else
			evpc_dsr_page &= ~4;
		break;

	case 5:
		if (data)
			evpc_dsr_page |= 2;
		else
			evpc_dsr_page &= ~2;
		break;

	case 6:
		break;

	case 7:
		break;
	}
}


static struct
{
	UINT8 read_index, write_index, mask;
	bool read;
	int state;
	struct { UINT8 red, green, blue; } color[0x100];
	//int dirty;
} evpc_palette;

/*
	read a byte in evpc DSR space
*/
static READ_HANDLER(evpc_mem_r)
{
	UINT8 reply = 0;


	if (offset < 0x1f00)
	{
		reply = ti99_evpc_DSR[offset+evpc_dsr_page*0x2000];
	}
	else if (offset < 0x1ff0)
	{
		if (RAMEN)
		{	/* NOVRAM */
			reply = 0;
		}
		else
		{
			reply = ti99_evpc_DSR[offset+evpc_dsr_page*0x2000];
		}
	}
	else
	{	/* PALETTE */
		logerror("palette read, offset=%d\n", offset-0x1ff0);
		switch (offset - 0x1ff0)
		{
		case 0:
			/* Palette Read Address Register */
			logerror("EVPC palette address read\n");
			reply = evpc_palette.write_index;
			break;

		case 2:
			/* Palette Read Color Value */
			logerror("EVPC palette color read\n");
			if (evpc_palette.read)
			{
				switch (evpc_palette.state)
				{
				case 0:
					reply = evpc_palette.color[evpc_palette.read_index].red;
					break;
				case 1:
					reply = evpc_palette.color[evpc_palette.read_index].green;
					break;
				case 2:
					reply = evpc_palette.color[evpc_palette.read_index].blue;
					break;
				}
				evpc_palette.state++;
				if (evpc_palette.state == 3)
				{
					evpc_palette.state = 0;
					evpc_palette.read_index++;
				}
			}
			break;

		case 4:
			/* Palette Read Pixel Mask */
			logerror("EVPC palette mask read\n");
			reply = evpc_palette.mask;
			break;
		case 6:
			/* Palette Read Address Register for Color Value */
			logerror("EVPC palette status read\n");
			if (evpc_palette.read)
				reply = 0;
			else
				reply = 3;
			break;
		}
	}

	return reply;
}

/*
	write a byte in evpc DSR space
*/
static WRITE_HANDLER(evpc_mem_w)
{
	if ((offset >= 0x1f00) && (offset < 0x1ff0) && RAMEN)
	{	/* NOVRAM */
	}
	else if (offset >= 0x1ff0)
	{	/* PALETTE */
		logerror("palette write, offset=%d\n, data=%d", offset-0x1ff0, data);
		switch (offset - 0x1ff0)
		{
		case 8:
			/* Palette Write Address Register */
			logerror("EVPC palette address write (for write access)\n");
			evpc_palette.write_index = data;
			evpc_palette.state = 0;
			evpc_palette.read = 0;
			break;

		case 10:
			/* Palette Write Color Value */
			logerror("EVPC palette color write\n");
			if (! evpc_palette.read)
			{
				switch (evpc_palette.state)
				{
				case 0:
					evpc_palette.color[evpc_palette.write_index].red = data;
					break;
				case 1:
					evpc_palette.color[evpc_palette.write_index].green = data;
					break;
				case 2:
					evpc_palette.color[evpc_palette.write_index].blue = data;
					break;
				}
				evpc_palette.state++;
				if (evpc_palette.state == 3)
				{
					evpc_palette.state = 0;
					evpc_palette.write_index++;
				}
				//evpc_palette.dirty = 1;
			}
			break;

		case 12:
			/* Palette Write Pixel Mask */
			logerror("EVPC palette mask write\n");
			evpc_palette.mask = data;
			break;

		case 14:
			/* Palette Write Address Register for Color Value */
			logerror("EVPC palette address write (for read access)\n");
			evpc_palette.read_index=data;
			evpc_palette.state=0;
			evpc_palette.read=1;
			break;
		}
	}
}
