/*
 * Signal LED: coded feedback patterns on one LED ("dkv,signal-led").
 */
#ifndef DRIVERS_SIGNAL_LED_H_
#define DRIVERS_SIGNAL_LED_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

enum signal_led_mode {
	SIGNAL_LED_OFF,
	/** Full brightness. */
	SIGNAL_LED_SOLID,
	/** On/off square wave, 50 % duty, period_ms per cycle. */
	SIGNAL_LED_BLINK,
	/** Brightness follows sin^2: dark -> full -> dark every period_ms. */
	SIGNAL_LED_GLOW,
};

/**
 * Show a pattern, replacing the current one.
 *
 * @param period_ms cycle length for BLINK and GLOW (ignored otherwise)
 * @param duration  how long to show it before turning off; K_FOREVER keeps it
 *                  until the next call
 */
int signal_led_set(const struct device *dev, enum signal_led_mode mode, uint32_t period_ms,
		   k_timeout_t duration);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_SIGNAL_LED_H_ */
