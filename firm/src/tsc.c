#include "tsc.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(tsc, LOG_LEVEL_INF);

/*
 * The st,stm32-tsc driver exports these but ships no public header, so we
 * declare the prototypes ourselves. (drivers/input/input_tsc_keys.c)
 */
typedef void (*stm32_tsc_group_ready_cb)(uint32_t count_value, void *user_data);
extern int stm32_tsc_group_register_callback(const struct device *dev, uint8_t group_idx,
					     stm32_tsc_group_ready_cb cb, void *user_data);
extern void stm32_tsc_start(const struct device *dev);

/* Our single configured group is index 0 in the driver's group array. */
#define TSC_GROUP_IDX 0
#define TSC_SAMPLE_PERIOD_MS 200

static const struct device *const tsc_dev = DEVICE_DT_GET(DT_NODELABEL(tsc));

static volatile uint32_t last_count;

static void group_ready(uint32_t count_value, void *user_data)
{
	ARG_UNUSED(user_data);
	last_count = count_value;
}

static void sample_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	/* Kick off the next acquisition; the result arrives via group_ready(). */
	stm32_tsc_start(tsc_dev);
}

static K_TIMER_DEFINE(sample_timer, sample_timer_expiry, NULL);

uint32_t tsc_last_count(void)
{
	return last_count;
}

int tsc_init(void)
{
	if (!device_is_ready(tsc_dev)) {
		LOG_ERR("TSC device not ready");
		return -ENODEV;
	}

	int ret = stm32_tsc_group_register_callback(tsc_dev, TSC_GROUP_IDX, group_ready, NULL);
	if (ret < 0) {
		LOG_ERR("TSC callback register failed (%d)", ret);
		return ret;
	}

	k_timer_start(&sample_timer, K_NO_WAIT, K_MSEC(TSC_SAMPLE_PERIOD_MS));
	LOG_INF("TSC sampling every %d ms (group 1)", TSC_SAMPLE_PERIOD_MS);
	return 0;
}

/* `tsc` shell command: print the latest raw count (for the bench sweep). */
static int cmd_tsc(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "g1 count = %u", tsc_last_count());
	return 0;
}

SHELL_CMD_REGISTER(tsc, NULL, "Print the latest raw TSC acquisition count", cmd_tsc);
