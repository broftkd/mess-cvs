#ifndef COCOCART_H
#define COCOCART_H

#include "driver.h"

enum {
	CARTLINE_CLEAR,
	CARTLINE_ASSERTED,
	CARTLINE_Q
};

struct cartridge_callback {
	void (*setcartline)(int data);
	void (*setbank)(int bank);
};

struct cartridge_slot {
	void (*init)(const struct cartridge_callback *callbacks);
	void (*term)(void);
	mem_read_handler io_r;
	mem_write_handler io_w;
	void (*enablesound)(int enable);
};

extern const struct cartridge_slot cartridge_fdc_coco;
extern const struct cartridge_slot cartridge_fdc_dragon;
extern const struct cartridge_slot cartridge_standard;
extern const struct cartridge_slot cartridge_banks;

#endif /* COCOCART_H */
