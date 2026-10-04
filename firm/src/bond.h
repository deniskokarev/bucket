/*
 * BLE bonding control.
 *
 * New pairings are normally rejected; they are only accepted during a short
 * "bonding window" opened by the `bond` shell command or the SW1 button (EXTI).
 * A client bonds during that window by reading the secure Pump characteristic,
 * which triggers pairing. Already-bonded centrals reconnect with their stored
 * keys and are unaffected.
 */
#ifndef BUCKET_BOND_H
#define BUCKET_BOND_H

/* Register pairing callbacks and the bond button (EXTI). Call after bt_enable()
 * and settings_load(). Returns 0 on success. */
int bond_init(void);

/* Open the bonding window (accept new pairings) for BOND_WINDOW_SECONDS. */
void bond_start_window(void);

#endif /* BUCKET_BOND_H */
