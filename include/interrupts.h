#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/* Configure scroll registers, calibrate the vertical-compression timer,
 * register Timer + VBlank ISRs, and enable interrupts.
 * Must be called after the display is off and tile data is loaded. */
void interrupts_init(uint8_t board_w, uint8_t board_h);

#endif /* INTERRUPTS_H */
