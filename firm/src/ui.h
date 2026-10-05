/*
 * Board UI: SW1 morse codes and the status LED.
 *
 *   SW1 "."    show the current status on the LED for a minute
 *   SW1 "..-"  clear bonds and open the bonding window
 *
 * LED: solid = bonded, 2 Hz blink = no bond (or pairing failed),
 * slow sine glow = waiting for a pairing, off = idle (power saving).
 */
#ifndef BUCKET_UI_H
#define BUCKET_UI_H

#include "bond.h"

/* Bind the SW1 codes. Call after bond_init(). Returns 0 on success. */
int ui_init(void);

/* Show the current bond status for a minute (also used at boot). */
void ui_show_status(void);

/* Bond-event sink for bond_init(). */
void ui_on_bond_event(enum bond_event ev);

#endif /* BUCKET_UI_H */
