/*
 * Advertising control shared between main.c (owns the advertiser) and bond.c
 * (opens/closes the bonding window).
 */
#ifndef BUCKET_ADV_H
#define BUCKET_ADV_H

#include <stdbool.h>

/*
 * Toggle the "bonding mode" marker in the advertising data. The mobile client
 * only auto-pairs when it sees this marker, so pairing is effectively initiated
 * by the user opening the window (SW1 / `bond`), not by mere discovery.
 */
void bt_set_bonding_adv(bool on);

#endif /* BUCKET_ADV_H */
