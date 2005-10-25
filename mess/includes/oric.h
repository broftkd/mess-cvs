/* from machine/oric.c */
extern MACHINE_INIT( oric );
extern MACHINE_STOP( oric );
extern INTERRUPT_GEN( oric_interrupt );
extern READ8_HANDLER( oric_IO_r );
extern WRITE8_HANDLER( oric_IO_w );
extern UINT8 *oric_ram;

/* from vidhrdw/oric.c */
extern VIDEO_START( oric );
extern VIDEO_UPDATE( oric );

extern WRITE8_HANDLER(oric_psg_porta_write);

DEVICE_INIT( oric_floppy );
DEVICE_LOAD( oric_floppy );

DEVICE_LOAD( oric_cassette );

/* Telestrat specific */
extern MACHINE_INIT( telestrat );
extern MACHINE_STOP( telestrat );
extern READ8_HANDLER( telestrat_IO_r );
extern WRITE8_HANDLER( telestrat_IO_w );

