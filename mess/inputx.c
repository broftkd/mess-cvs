#include <ctype.h>
#include <assert.h>
#include <wctype.h>

#include "inputx.h"
#include "inptport.h"
#include "mame.h"

#define NUM_CODES		128
#define NUM_SIMUL_KEYS	2

#ifdef MAME_DEBUG
#define LOG_INPUTX	0
#else
#define LOG_INPUTX	0
#endif

struct InputCode
{
	UINT16 port[NUM_SIMUL_KEYS];
	const struct InputPortTiny *ipt[NUM_SIMUL_KEYS];
};

enum
{
	STATUS_KEYDOWN = 1	
};

enum
{
	MODIFIER_SHIFT = 1
};

struct KeyBuffer
{
	int begin_pos;
	int end_pos;
	int status;
	unicode_char_t buffer[4096];
};

struct InputMapEntry
{
	const char *name;
	int code;
};

/***************************************************************************

	Code assembling

***************************************************************************/

static struct InputMapEntry input_map[] =
{
	{ "UP",			'^' },
	{ "DOWN",		0 },
	{ "LEFT",		8 },
	{ "RIGHT",		9 },
	{ "ENTER",		13 },
	{ "CLEAR",		12 },
	{ "SPACE",		' ' },
	{ "BREAK",		3 },
	{ DEF_STR( Unused ), 	0 },
	{ "L-SHIFT",		0 },
	{ "R-SHIFT",		0 }
};

#if LOG_INPUTX
static const char *charstr(unicode_char_t ch)
{
	static char buf[3];

	switch(ch) {
	case '\0':	strcpy(buf, "\\0");		break;
	case '\r':	strcpy(buf, "\\r");		break;
	case '\n':	strcpy(buf, "\\n");		break;
	case '\t':	strcpy(buf, "\\t");		break;
	default:
		buf[0] = (char) ch;
		buf[1] = '\0';
		break;
	}
	return buf;
}
#endif

static unicode_char_t find_code(const char *name, int modifiers)
{
	int i;
	unicode_char_t code;
	int len = strlen(name);

	if (len == 1)
		code = (modifiers == 0) ? name[0] : 0;
	else if ((len == 4) && isspace(name[1]) && isspace(name[2]))
	{
		if (modifiers & MODIFIER_SHIFT)
			code = isspace(name[3]) ? 0 : name[3];
		else
			code = name[0];
	}
	else if ((len >= 3) && isspace(name[1]) && ((len == 3) || isspace(name[3])))
	{
		if (modifiers & MODIFIER_SHIFT)
			code = isspace(name[2]) ? 0 : name[2];
		else
			code = name[0];
	}
	else
	{
		code = 0;
		for (i = 0; i < sizeof(input_map) / sizeof(input_map[0]); i++)
		{
			if (!strcmp(name, input_map[i].name))
			{
				code = modifiers ? 0 : input_map[i].code;
				break;
			}
		}
	}

	if (code >= NUM_CODES)
		code = 0;
	return code;
}

static void scan_keys(const struct GameDriver *gamedrv, struct InputCode *codes, UINT16 *ports, const struct InputPortTiny **ipts, int keys, int modifiers, int *used_modifiers)
{
	const struct InputPortTiny *ipt;
	UINT16 port = (UINT16) -1;
	unicode_char_t code;

	assert(keys < NUM_SIMUL_KEYS);

	ipt = gamedrv->input_ports;
	while(ipt->type != IPT_END)
	{
		switch(ipt->type) {
		case IPT_PORT:
			port++;
			break;

		case IPT_KEYBOARD:
			if (!strcmp(ipt->name, "SHIFT") || !strcmp(ipt->name, "R-SHIFT") || !strcmp(ipt->name, "L-SHIFT"))
			{
				/* we've found a shift key */
				if ((*used_modifiers & MODIFIER_SHIFT) == 0)
				{
					ports[keys] = port;
					ipts[keys] = ipt;				
					*used_modifiers |= MODIFIER_SHIFT;
					scan_keys(gamedrv, codes, ports, ipts, keys+1, modifiers | MODIFIER_SHIFT, used_modifiers);
				}
			}
			else
			{
				code = find_code(ipt->name, modifiers);
				if (code > 0)
				{
					memcpy(codes[code].port, ports, sizeof(ports[0]) * keys);
					memcpy(codes[code].ipt, ipts, sizeof(ipts[0]) * keys);
					codes[code].port[keys] = port;
					codes[code].ipt[keys] = ipt;
				}
#if LOG_INPUTX
				if (gamedrv == Machine->gamedrv)
					logerror("inputx: code=%i (%s) port=%i ipt->name='%s'\n", (int) code, charstr(code), port, ipt->name);
#endif
			}
			break;
		}
		ipt++;
	}
}

#define CODE_BUFFER_SIZE	(sizeof(struct InputCode) * NUM_CODES + sizeof(struct KeyBuffer))

static void build_codes(const struct GameDriver *gamedrv, struct InputCode *codes)
{
	UINT16 ports[NUM_SIMUL_KEYS];
	const struct InputPortTiny *ipts[NUM_SIMUL_KEYS];
	int used_modifiers;
	int switch_upper;
	unicode_char_t c;

	memset(codes, 0, CODE_BUFFER_SIZE);

	scan_keys(gamedrv, codes, ports, ipts, 0, 0, &used_modifiers);

	/* special case; scan to see if upper case characters are specified, but not lower case */
	switch_upper = 1;
	for (c = 'A'; c <= 'Z'; c++)
	{
		if (!inputx_can_post_key(c) || inputx_can_post_key(towlower(c)))
		{
			switch_upper = 0;
			break;
		}
	}
	if (switch_upper)
		memcpy(&codes['a'], &codes['A'], sizeof(codes[0]) * 26);
}

/***************************************************************************

	Validity checks

***************************************************************************/

#ifdef MAME_DEBUG
int inputx_validitycheck(const struct GameDriver *gamedrv)
{
	char buf[CODE_BUFFER_SIZE];
	struct InputCode *codes;
	const struct InputPortTiny *ipt;
	int port_count, i, j;
	int error = 0;

	if (gamedrv->flags & GAME_COMPUTER)
	{
		codes = (struct InputCode *) buf;
		build_codes(gamedrv, codes);

		port_count = 0;
		for (ipt = gamedrv->input_ports; ipt->type != IPT_END; ipt++)
		{
			if (ipt->type == IPT_PORT)
				port_count++;
		}

		for (i = 0; i < NUM_CODES; i++)
		{
			for (j = 0; j < NUM_SIMUL_KEYS; j++)
			{
				if (codes[i].port[j] >= port_count)
				{
					printf("%s: invalid inputx translation for code %i port %i\n", gamedrv->name, i, j);
					error = 1;
				}
			}
		}
	}
	return error;
}
#endif

/***************************************************************************

	Alternative key translations

***************************************************************************/

static const char *find_alternate(unicode_char_t ch)
{
	static const struct
	{
		unicode_char_t ch;
		const char *str;
	} map[] =
	{
		{ 0x00a0,	" " },		/* non breaking space */
		{ 0x00a1,	"!" },		/* inverted exclaimation mark */
		{ 0x00a6,	"|" },		/* broken bar */
		{ 0x00a9,	"(c)" },	/* copyright sign */
		{ 0x00ab,	"<<" },		/* left pointing double angle */
		{ 0x00ae,	"(r)" },	/* registered sign */
		{ 0x00bb,	">>" },		/* right pointing double angle */
		{ 0x00bc,	"1/4" },	/* vulgar fraction one quarter */
		{ 0x00bd,	"1/2" },	/* vulgar fraction one half */
		{ 0x00be,	"3/4" },	/* vulgar fraction three quarters */
		{ 0x00bf,	"?" },		/* inverted question mark */
		{ 0x00c0,	"A" },		/* 'A' grave */
		{ 0x00c1,	"A" },		/* 'A' acute */
		{ 0x00c2,	"A" },		/* 'A' circumflex */
		{ 0x00c3,	"A" },		/* 'A' tilde */
		{ 0x00c4,	"A" },		/* 'A' diaeresis */
		{ 0x00c5,	"A" },		/* 'A' ring above */
		{ 0x00c6,	"AE" },		/* 'AE' ligature */
		{ 0x00c7,	"C" },		/* 'C' cedilla */
		{ 0x00c8,	"E" },		/* 'E' grave */
		{ 0x00c9,	"E" },		/* 'E' acute */
		{ 0x00ca,	"E" },		/* 'E' circumflex */
		{ 0x00cb,	"E" },		/* 'E' diaeresis */
		{ 0x00cc,	"I" },		/* 'I' grave */
		{ 0x00cd,	"I" },		/* 'I' acute */
		{ 0x00ce,	"I" },		/* 'I' circumflex */
		{ 0x00cf,	"I" },		/* 'I' diaeresis */
		{ 0x00d0,	"D" },		/* 'ETH' */
		{ 0x00d1,	"N" },		/* 'N' tilde */
		{ 0x00d2,	"O" },		/* 'O' grave */
		{ 0x00d3,	"O" },		/* 'O' acute */
		{ 0x00d4,	"O" },		/* 'O' circumflex */
		{ 0x00d5,	"O" },		/* 'O' tilde */
		{ 0x00d6,	"O" },		/* 'O' diaeresis */
		{ 0x00d7,	"X" },		/* multiplication sign */
		{ 0x00d8,	"O" },		/* 'O' stroke */
		{ 0x00d9,	"U" },		/* 'U' grave */
		{ 0x00da,	"U" },		/* 'U' acute */
		{ 0x00db,	"U" },		/* 'U' circumflex */
		{ 0x00dc,	"U" },		/* 'U' diaeresis */
		{ 0x00dd,	"Y" },		/* 'Y' acute */
		{ 0x00df,	"SS" },		/* sharp S */
		{ 0x00e0,	"a" },		/* 'a' grave */
		{ 0x00e1,	"a" },		/* 'a' acute */
		{ 0x00e2,	"a" },		/* 'a' circumflex */
		{ 0x00e3,	"a" },		/* 'a' tilde */
		{ 0x00e4,	"a" },		/* 'a' diaeresis */
		{ 0x00e5,	"a" },		/* 'a' ring above */
		{ 0x00e6,	"ae" },		/* 'ae' ligature */
		{ 0x00e7,	"c" },		/* 'c' cedilla */
		{ 0x00e8,	"e" },		/* 'e' grave */
		{ 0x00e9,	"e" },		/* 'e' acute */
		{ 0x00ea,	"e" },		/* 'e' circumflex */
		{ 0x00eb,	"e" },		/* 'e' diaeresis */
		{ 0x00ec,	"i" },		/* 'i' grave */
		{ 0x00ed,	"i" },		/* 'i' acute */
		{ 0x00ee,	"i" },		/* 'i' circumflex */
		{ 0x00ef,	"i" },		/* 'i' diaeresis */
		{ 0x00f0,	"d" },		/* 'eth' */
		{ 0x00f1,	"n" },		/* 'n' tilde */
		{ 0x00f2,	"o" },		/* 'o' grave */
		{ 0x00f3,	"o" },		/* 'o' acute */
		{ 0x00f4,	"o" },		/* 'o' circumflex */
		{ 0x00f5,	"o" },		/* 'o' tilde */
		{ 0x00f6,	"o" },		/* 'o' diaeresis */
		{ 0x00f8,	"o" },		/* 'o' stroke */
		{ 0x00f9,	"u" },		/* 'u' grave */
		{ 0x00fa,	"u" },		/* 'u' acute */
		{ 0x00fb,	"u" },		/* 'u' circumflex */
		{ 0x00fc,	"u" },		/* 'u' diaeresis */
		{ 0x00fd,	"y" },		/* 'y' acute */
		{ 0x00ff,	"y" }		/* 'y' diaeresis */
	};

	int i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
	{
		if (ch == map[i].ch)
			return map[i].str;
	}
	return NULL;
}

/***************************************************************************

	Core

***************************************************************************/

static struct InputCode *codes;
static void *inputx_timer;

static void inputx_timerproc(int dummy);

void inputx_init(void)
{
	codes = NULL;
	inputx_timer = NULL;

	if (Machine->gamedrv->flags & GAME_COMPUTER)
	{
		codes = (struct InputCode *) auto_malloc(CODE_BUFFER_SIZE);
		if (!codes)
			return;
		build_codes(Machine->gamedrv, codes);

		inputx_timer = timer_alloc(inputx_timerproc);
	}
}

int inputx_can_post(void)
{
	return codes != NULL;
}

static struct KeyBuffer *get_buffer(void)
{
	assert(codes);
	return (struct KeyBuffer *) (codes + NUM_CODES);
}

static int can_post_key_directly(unicode_char_t ch)
{
	assert(codes);
	return ((ch < NUM_CODES) && codes[ch].ipt[0] != NULL);
}

static int can_post_key_alternate(unicode_char_t ch)
{
	const char *s;

	s = find_alternate(ch);
	if (!s)
		return 0;

	while(*s)
	{
		if (!can_post_key_directly(*s))
			return 0;
		s++;
	}
	return 1;
}

int inputx_can_post_key(unicode_char_t ch)
{
	return inputx_can_post() && (can_post_key_directly(ch) || can_post_key_alternate(ch));
}

static void internal_post_key(unicode_char_t ch)
{
	struct KeyBuffer *keybuf;
	keybuf = get_buffer();
	keybuf->buffer[keybuf->end_pos++] = ch;
	keybuf->end_pos %= sizeof(keybuf->buffer) / sizeof(keybuf->buffer[0]);
}

void inputx_postn(const unicode_char_t *text, size_t text_len)
{
	struct KeyBuffer *keybuf;
	int last_cr = 0;
	unicode_char_t ch;
	const char *s;

	if ((text_len == 0) || !inputx_can_post())
		return;

	keybuf = get_buffer();

	/* need to start up the timer? */
	if (keybuf->begin_pos == keybuf->end_pos)
	{
		timer_adjust(inputx_timer, 0, 0, TIME_IN_SEC(0.1));
		keybuf->status |= STATUS_KEYDOWN;
	}

	while((text_len > 0) && (keybuf->end_pos+1 != keybuf->begin_pos))
	{
		ch = *(text++);
		text_len--;

		/* change all eolns to '\r' */
		if ((ch != '\n') || !last_cr)
		{
			if (ch == '\n')
				ch = '\r';
			else
				last_cr = (ch == '\r');

#if LOG_INPUTX
			logerror("inputx_postn(): code=%i (%s) port=%i ipt->name='%s'\n", (int) ch, charstr(ch), codes[ch].port[0], codes[ch].ipt[0] ? codes[ch].ipt[0]->name : "<null>");
#endif

			if (can_post_key_directly(ch))
			{
				internal_post_key(ch);
			}
			else if (can_post_key_alternate(ch))
			{
				s = find_alternate(ch);
				assert(s);
				while(*s)
					internal_post_key(*(s++));
			}
		}
		else
		{
			last_cr = 0;
		}
	}
}

static void inputx_timerproc(int dummy)
{
	struct KeyBuffer *keybuf;

	keybuf = get_buffer();
	if (keybuf->status & STATUS_KEYDOWN)
	{
		keybuf->status &= ~STATUS_KEYDOWN;
	}
	else
	{
		keybuf->status |= STATUS_KEYDOWN;
		keybuf->begin_pos++;
		keybuf->begin_pos %= sizeof(keybuf->buffer) / sizeof(keybuf->buffer[0]);
		if (keybuf->begin_pos == keybuf->end_pos)
			timer_reset(inputx_timer, TIME_NEVER);
	}
}

void inputx_update(unsigned short *ports)
{
	const struct KeyBuffer *keybuf;
	const struct InputCode *code;
	const struct InputPortTiny *ipt;
	unicode_char_t ch;
	int i;
	int value;

	if (inputx_can_post())
	{
		keybuf = get_buffer();
		if ((keybuf->status & STATUS_KEYDOWN) && (keybuf->begin_pos != keybuf->end_pos))
		{
			ch = keybuf->buffer[keybuf->begin_pos];
			code = &codes[ch];

			for (i = 0; code->ipt[i] && (i < sizeof(code->ipt) / sizeof(code->ipt[0])); i++)
			{
				ipt = code->ipt[i];
				value = ports[code->port[i]];

				switch(ipt->default_value) {
				case IP_ACTIVE_LOW:
					value &= ~ipt->mask;
					break;
				case IP_ACTIVE_HIGH:
					value |= ipt->mask;
					break;
				}
				ports[code->port[i]] = value;
			}
		}
	}
}

/***************************************************************************

	Alternative calls

***************************************************************************/

void inputx_post(const unicode_char_t *text)
{
	size_t len = 0;
	while(text[len])
		len++;
	inputx_postn(text, len);
}

void inputx_postc(unicode_char_t ch)
{
	inputx_postn(&ch, 1);
}

void inputx_postn_utf16(const utf16_char_t *text, size_t text_len)
{
	size_t len = 0;
	unicode_char_t c;
	utf16_char_t w1, w2;
	unicode_char_t buf[256];

	while(text_len > 0)
	{
		if (len == (sizeof(buf) / sizeof(buf[0])))
		{
			inputx_postn(buf, len);
			len = 0;
		}

		w1 = *(text++);
		text_len--;

		if ((w1 >= 0xd800) && (w1 <= 0xdfff))
		{
			if (w1 <= 0xDBFF)
			{
				w2 = 0;
				if (text_len > 0)
				{
					w2 = *(text++);
					text_len--;
				}
				if ((w2 >= 0xdc00) && (w2 <= 0xdfff))
				{
					c = w1 & 0x03ff;
					c <<= 10;
					c |= w2 & 0x03ff;
				}
				else
				{
					c = '?';
				}
			}
			else
			{
				c = '?';
			}
		}
		else
		{
			c = w1;
		}
		buf[len++] = c;
	}
	inputx_postn(buf, len);
}

void inputx_post_utf16(const utf16_char_t *text)
{
	size_t len = 0;
	while(text[len])
		len++;
	inputx_postn_utf16(text, len);
}



