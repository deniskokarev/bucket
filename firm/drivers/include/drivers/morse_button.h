/*
 * Morse-code button: dot/dash sequences on one key, dispatched to the codes
 * listed as child nodes of a "dkv,morse-button" devicetree node.
 */
#ifndef DRIVERS_MORSE_BUTTON_H_
#define DRIVERS_MORSE_BUTTON_H_

#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Called from the system workqueue when entry @p entry's code is entered. */
typedef void (*morse_button_handler_t)(const struct device *dev, size_t entry, void *user_data);

/** Morse-button device owning the code node @p node_id. */
#define MORSE_BUTTON_DEV(node_id) DEVICE_DT_GET(DT_PARENT(node_id))

/** Entry index of the code node @p node_id (its child index). */
#define MORSE_BUTTON_ENTRY(node_id) DT_NODE_CHILD_IDX(node_id)

/**
 * Register the handler for one code entry (replaces any previous one).
 *
 * @retval 0 on success
 * @retval -EINVAL if @p entry is out of range
 */
int morse_button_set_handler(const struct device *dev, size_t entry, morse_button_handler_t handler,
			     void *user_data);

/** The code string ("." / "-" sequence) of entry @p entry, or NULL if out of range. */
const char *morse_button_code(const struct device *dev, size_t entry);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_MORSE_BUTTON_H_ */
