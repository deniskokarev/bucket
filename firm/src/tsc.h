/*
 * Raw STM32 TSC acquisition for the water-level electrode.
 *
 * The Zephyr st,stm32-tsc driver exposes the raw per-group acquisition count
 * through a group-ready callback (there is no `tsc-keys` child here). This
 * module owns TSC Group 1, triggers periodic acquisitions, keeps the latest
 * count, and reports it (periodic log + `tsc` shell command) for the
 * characterization work in hard/tsc-validation.md.
 *
 * Recall: water raises the electrode capacitance Cx, which LOWERS the count
 * (count ≈ k·Cs/Cx).
 */
#ifndef BUCKET_TSC_H
#define BUCKET_TSC_H

#include <stdint.h>

/* Start periodic TSC acquisition. Returns 0 on success, <0 on error. */
int tsc_init(void);

/* Latest raw acquisition count for the electrode group (0 before first read). */
uint32_t tsc_last_count(void);

#endif /* BUCKET_TSC_H */
