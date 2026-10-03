/*
 * Capacitive water-level sensing via the STM32 TSC.
 *
 * The TSC + tsc-keys input driver turns a capacitance change on the electrode
 * (water rising past it) into INPUT_EV_KEY press/release events. This module
 * subscribes to those events and exposes a debounced "water present" state.
 *
 * During bring-up it also logs every input event so the raw behavior of the
 * electrode can be characterized against a real bucket.
 */
#ifndef BUCKET_LEVEL_H
#define BUCKET_LEVEL_H

#include <stdbool.h>

/* Set up the input-event subscription. Returns 0 on success. */
int level_init(void);

/* True once water has been detected at/above the electrode (trigger level). */
bool level_water_present(void);

#endif /* BUCKET_LEVEL_H */
