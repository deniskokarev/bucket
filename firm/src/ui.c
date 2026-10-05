#include "ui.h"

#include <drivers/morse_button.h>
#include <drivers/signal_led.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#define SHOW_FOR       K_SECONDS(60)
#define BLINK_2HZ_MS   500
#define GLOW_PERIOD_MS 2000

#define STATUS_CODE DT_NODELABEL(morse_status)
#define REBOND_CODE DT_NODELABEL(morse_rebond)

static const struct device *const led = DEVICE_DT_GET(DT_NODELABEL(status_led));

/* Log lines "led: <state>" are part of the e2e contract (test_e2e/board.py). */
static void led_show(const char *state, enum signal_led_mode mode, uint32_t period_ms,
		     k_timeout_t duration)
{
	int err = signal_led_set(led, mode, period_ms, duration);

	if (err) {
		LOG_ERR("led: %s failed (%d)", state, err);
		return;
	}
	LOG_INF("led: %s", state);
}

void ui_show_status(void)
{
	if (bond_window_is_open()) {
		LOG_INF("led: pairing (unchanged)"); /* the glow already says it */
	} else if (bond_exists()) {
		led_show("bonded", SIGNAL_LED_SOLID, 0, SHOW_FOR);
	} else {
		led_show("no-bond", SIGNAL_LED_BLINK, BLINK_2HZ_MS, SHOW_FOR);
	}
}

void ui_on_bond_event(enum bond_event ev)
{
	switch (ev) {
	case BOND_EV_WINDOW_OPEN:
		/* Until the window ends; the closing event changes it. */
		led_show("pairing", SIGNAL_LED_GLOW, GLOW_PERIOD_MS, K_FOREVER);
		break;
	case BOND_EV_PAIRED:
		led_show("bonded", SIGNAL_LED_SOLID, 0, SHOW_FOR);
		break;
	case BOND_EV_WINDOW_TIMEOUT:
		led_show("off", SIGNAL_LED_OFF, 0, K_FOREVER);
		break;
	case BOND_EV_FAILED:
		led_show("error", SIGNAL_LED_BLINK, BLINK_2HZ_MS, SHOW_FOR);
		break;
	}
}

static void on_status_code(const struct device *dev, size_t entry, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(entry);
	ARG_UNUSED(user_data);
	ui_show_status();
}

static void on_rebond_code(const struct device *dev, size_t entry, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(entry);
	ARG_UNUSED(user_data);
	bond_rebond();
}

int ui_init(void)
{
	const struct device *sw1 = MORSE_BUTTON_DEV(STATUS_CODE);
	int err;

	if (!device_is_ready(led) || !device_is_ready(sw1)) {
		LOG_ERR("status LED or SW1 morse button not ready");
		return -ENODEV;
	}
	err = morse_button_set_handler(sw1, MORSE_BUTTON_ENTRY(STATUS_CODE), on_status_code, NULL);
	if (err) {
		return err;
	}
	return morse_button_set_handler(MORSE_BUTTON_DEV(REBOND_CODE),
					MORSE_BUTTON_ENTRY(REBOND_CODE), on_rebond_code, NULL);
}
