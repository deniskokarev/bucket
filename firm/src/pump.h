/*
 * Evacuation pump actuation.
 *
 * Drives the `pump` GPIO (see devicetree overlay) through an external
 * MOSFET/relay, with a minimum run-time guard so the pump can't chatter when
 * the level sensor hovers around the trigger point.
 */
#ifndef BUCKET_PUMP_H
#define BUCKET_PUMP_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Pump control mode (values match the BLE Pump characteristic and the mobile
 * client's PumpMode enum).
 */
enum pump_mode {
	PUMP_MODE_AUTO = 0,     /* follow the level sensor */
	PUMP_MODE_FORCE_OFF = 1, /* manual: keep the pump off */
	PUMP_MODE_FORCE_ON = 2,  /* manual: keep the pump on */
};

/* Initialize the pump GPIO (off). Returns 0 on success, <0 on error. */
int pump_init(void);

/* Set/get the control mode. Default is PUMP_MODE_AUTO. */
void pump_set_mode(enum pump_mode mode);
enum pump_mode pump_get_mode(void);

/*
 * Request the pump on/off. Honors a minimum run-time once started: a request
 * to stop before PUMP_MIN_RUN_MS has elapsed is deferred. Call pump_tick()
 * periodically so deferred transitions actually happen.
 */
void pump_request(bool on);

/* Service deferred transitions / safety timers. Call from the control loop. */
void pump_tick(void);

/* Current actual pump state. */
bool pump_is_on(void);

#endif /* BUCKET_PUMP_H */
