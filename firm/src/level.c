#include "level.h"
#include "tsc.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(level, LOG_LEVEL_INF);

/*
 * Water raises the electrode capacitance, which LOWERS the TSC count. So water
 * is "present" when the count falls below LEVEL_TRIGGER_COUNT, and clears when
 * it rises back above LEVEL_RELEASE_COUNT (hysteresis to stop chatter).
 *
 * TODO: these are placeholders — calibrate from the bench sweep described in
 * hard/tsc-validation.md. With Cs=47nF the empty count is expected ~650 and a
 * full bucket ~130, so trigger/release sit between those.
 */
#define LEVEL_TRIGGER_COUNT 300
#define LEVEL_RELEASE_COUNT 400

/* Below this, no sampling cap / electrode is connected (the acquisition
 * completes in ~1 cycle) — not a real reading, so don't report "full". */
#define LEVEL_COUNT_MIN_VALID 20

static bool water_present;

int level_init(void)
{
	water_present = false;
	return tsc_init();
}

bool level_water_present(void)
{
	uint32_t count = tsc_last_count();

	if (count < LEVEL_COUNT_MIN_VALID) {
		/* No acquisition yet, or no electrode/sampling cap wired. */
		if (water_present) {
			water_present = false;
			LOG_INF("sensor not ready (count %u) -> dry", count);
		}
		return false;
	}

	if (water_present) {
		if (count > LEVEL_RELEASE_COUNT) {
			water_present = false;
			LOG_INF("water absent (count %u)", count);
		}
	} else {
		if (count < LEVEL_TRIGGER_COUNT) {
			water_present = true;
			LOG_INF("water PRESENT (count %u)", count);
		}
	}

	return water_present;
}
