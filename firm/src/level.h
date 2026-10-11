/*
 * Capacitive water-level sensing via the STM32 TSC.
 *
 * Reads the raw TSC acquisition count (see tsc.c) and applies a threshold with
 * hysteresis to expose a debounced "water present" state. Water raises the
 * electrode capacitance, which lowers the count.
 */
#ifndef BUCKET_LEVEL_H
#define BUCKET_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

/* Set up the input-event subscription. Returns 0 on success. */
int level_init(void);

/* True once water has been detected at/above the electrode (trigger level). */
bool level_water_present(void);

/* Pump hysteresis in raw TSC counts: OFF above `off`, ON below `on`. */
void level_get_thresholds(uint16_t *off, uint16_t *on);

/* Validate (off > on), apply, persist and notify. -EINVAL if invalid. */
int level_set_thresholds(uint16_t off, uint16_t on);

#endif /* BUCKET_LEVEL_H */
