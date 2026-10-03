#include "level.h"

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(level, LOG_LEVEL_INF);

/* Key code emitted by the tsc-keys node for the water electrode (see overlay). */
#define WATER_KEY_CODE INPUT_KEY_0

static volatile bool water_present;

static void input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	/* Log every event during bring-up / electrode characterization. */
	LOG_INF("input: type=%u code=%u value=%d sync=%u", evt->type, evt->code, evt->value,
		evt->sync);

	if (evt->type == INPUT_EV_KEY && evt->code == WATER_KEY_CODE) {
		water_present = (evt->value != 0);
		LOG_INF("water %s", water_present ? "PRESENT" : "absent");
	}
}

/* NULL device => receive events from every input device. */
INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

int level_init(void)
{
	water_present = false;
	LOG_INF("level sensing ready (water key code = %d)", WATER_KEY_CODE);
	return 0;
}

bool level_water_present(void)
{
	return water_present;
}
