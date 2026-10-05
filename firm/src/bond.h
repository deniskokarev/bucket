/*
 * BLE bonding control.
 *
 * New pairings are only accepted during a short "bonding window". It is opened
 * by bond_rebond() (SW1 code short-short-long, or the `bond` shell command),
 * which first clears all existing bonds, so the board is always re-paired from
 * scratch. A client pairs during that window by reading the secure Pump
 * characteristic. Already-bonded centrals reconnect with their stored keys at
 * any time, without the window.
 */
#ifndef BUCKET_BOND_H
#define BUCKET_BOND_H

#include <stdbool.h>

enum bond_event {
	BOND_EV_WINDOW_OPEN,    /* bonds cleared, accepting a new pairing */
	BOND_EV_PAIRED,         /* a new bond was stored; window closed */
	BOND_EV_WINDOW_TIMEOUT, /* window expired without a pairing */
	BOND_EV_FAILED,         /* pairing failed during the window; window closed */
};

typedef void (*bond_event_cb_t)(enum bond_event ev);

/* Register pairing callbacks; @p cb (may be NULL) gets bond events. Call after
 * bt_enable() and settings_load(). Returns 0 on success. */
int bond_init(bond_event_cb_t cb);

/* Clear all bonds, then open the bonding window for BOND_WINDOW_SECONDS. */
void bond_rebond(void);

bool bond_exists(void);
bool bond_window_is_open(void);

#endif /* BUCKET_BOND_H */
