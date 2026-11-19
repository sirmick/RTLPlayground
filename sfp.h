#ifndef SFP_H
#define SFP_H

#define SFP_WAKE_TICKS	100

/* sfp_read_next(), sfp_ddm_next(): one I2C transaction each */
#define SFP_DONE	0
#define SFP_MORE	1
#define SFP_FAIL	2

#define SFP_ID_LEN	40	/* EEPROM 20..59 */
#define SFP_DDM_FIRST	184	/* diagnostics (0x51) 184..239: calibration, values, state */
#define SFP_DDM_LEN	56
#define SFP_DDM_POLLS	40	/* a module's diagnostics read again every 40 polls: 2 s */

extern __xdata uint8_t sfp_pins_last;
extern __xdata char sfp_module_vendor[2][17];
extern __xdata char sfp_module_model[2][17];
extern __xdata char sfp_module_serial[2][17];
extern __xdata uint8_t sfp_options[2];
extern __xdata uint8_t sfp_speed[2];
extern __xdata uint8_t sfp_quirks[2];
extern __xdata uint8_t sfp_wake_pending[2];
extern __xdata uint8_t sfp_id[2][SFP_ID_LEN];
extern __xdata uint8_t sfp_diag[2][SFP_DDM_LEN];
extern __xdata uint8_t sfp_id_ok[2], sfp_diag_ok[2];
extern __xdata uint8_t sfp_ready[2];

void sfp_print(uint8_t sfp) __banked;	/* `sfp`'s lines for a present module, from the caches */
void sfp_apply_quirks(uint8_t sfp) __banked __reentrant;
void setup_sfp_gpio(void) __banked;
/* the sfp tasks' steps (tasks.c), one slot each */
uint8_t sfp_pins(uint8_t slot) __banked;	/* 1: the module is to be read */
uint8_t sfp_read_next(uint8_t slot) __banked;
void sfp_configure(uint8_t slot) __banked;
uint8_t sfp_ddm_wanted(uint8_t slot) __banked;
uint8_t sfp_ddm_next(uint8_t slot) __banked;

#endif
