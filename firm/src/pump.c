#include "pump.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pump, LOG_LEVEL_INF);

/* Minimum time the pump stays on once started, and minimum off time. */
#define PUMP_MIN_RUN_MS 3000
#define PUMP_MIN_OFF_MS 1000

/* Hard safety cap: never run the pump longer than this in one shot. */
#define PUMP_MAX_RUN_MS 120000

/* `pump` is a GPIO alias in boards/<board>.overlay. */
#define PUMP_NODE DT_ALIAS(pump)

#if DT_NODE_HAS_STATUS(PUMP_NODE, okay)
static const struct gpio_dt_spec pump_gpio = GPIO_DT_SPEC_GET(PUMP_NODE, gpios);
#define PUMP_HAVE_GPIO 1
#else
#define PUMP_HAVE_GPIO 0
#warning "no `pump` GPIO alias in devicetree; pump control is a no-op (bring-up stub)"
#endif

static bool state_on;           /* actual output state */
static bool want_on;            /* last requested state */
static int64_t last_change_ms;  /* uptime at last actual transition */

static void apply(bool on)
{
	if (on == state_on) {
		return;
	}
#if PUMP_HAVE_GPIO
	gpio_pin_set_dt(&pump_gpio, on ? 1 : 0);
#endif
	state_on = on;
	last_change_ms = k_uptime_get();
	LOG_INF("pump %s", on ? "ON" : "OFF");
}

int pump_init(void)
{
	state_on = false;
	want_on = false;
	last_change_ms = k_uptime_get();

#if PUMP_HAVE_GPIO
	if (!gpio_is_ready_dt(&pump_gpio)) {
		LOG_ERR("pump GPIO not ready");
		return -ENODEV;
	}
	int ret = gpio_pin_configure_dt(&pump_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("pump GPIO config failed (%d)", ret);
		return ret;
	}
#else
	LOG_WRN("pump GPIO not defined; running as a no-op stub");
#endif
	return 0;
}

void pump_request(bool on)
{
	want_on = on;
}

void pump_tick(void)
{
	int64_t now = k_uptime_get();
	int64_t held = now - last_change_ms;

	if (want_on && !state_on) {
		if (held >= PUMP_MIN_OFF_MS) {
			apply(true);
		}
	} else if (!want_on && state_on) {
		if (held >= PUMP_MIN_RUN_MS) {
			apply(false);
		}
	}

	/* Safety: force off if we've been running too long. */
	if (state_on && held >= PUMP_MAX_RUN_MS) {
		LOG_WRN("pump max run-time reached, forcing OFF");
		apply(false);
		want_on = false;
	}
}

bool pump_is_on(void)
{
	return state_on;
}
