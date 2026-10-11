#include "level.h"
#include "gatt.h"
#include "tsc.h"

#include <errno.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(level, LOG_LEVEL_INF);

/*
 * Water raises the electrode capacitance, which LOWERS the TSC count. So water
 * is "present" (pump ON) when the count falls below `on_count`, and clears
 * (pump OFF) when it rises back above `off_count` (hysteresis: off > on).
 *
 * Defaults are calibrated for Cs = 10 nF and ~21 pF of pin/lead parasitic
 * (bench, see hard/tsc-validation.md). Electrode dry (2 pF) ~330, half band
 * (18 pF) ~195, full (34 pF) ~138. Tune at runtime with `level set <off> <on>`
 * or the app's Settings form; values persist in settings as "level/thr".
 */
#define LEVEL_DEFAULT_OFF_COUNT 260
#define LEVEL_DEFAULT_ON_COUNT  220

/* Below this, no sampling cap / electrode is connected (the acquisition
 * completes in ~1 cycle) — not a real reading, so don't report "full". */
#define LEVEL_COUNT_MIN_VALID 20

static bool water_present;
static uint16_t off_count = LEVEL_DEFAULT_OFF_COUNT;
static uint16_t on_count = LEVEL_DEFAULT_ON_COUNT;

static bool thresholds_valid(uint16_t off, uint16_t on)
{
	return off > on && on > LEVEL_COUNT_MIN_VALID;
}

int level_init(void)
{
	water_present = false;
	return tsc_init();
}

void level_get_thresholds(uint16_t *off, uint16_t *on)
{
	*off = off_count;
	*on = on_count;
}

int level_set_thresholds(uint16_t off, uint16_t on)
{
	if (!thresholds_valid(off, on)) {
		return -EINVAL;
	}

	off_count = off;
	on_count = on;

	uint8_t buf[4];

	sys_put_le16(off, &buf[0]);
	sys_put_le16(on, &buf[2]);
	int err = settings_save_one("level/thr", buf, sizeof(buf));
	if (err) {
		LOG_WRN("thresholds not persisted (%d)", err);
	}

	LOG_INF("level thresholds -> off %u on %u", off, on);
	bucket_gatt_thresholds_changed();
	return 0;
}

static int level_settings_set(const char *name, size_t len, settings_read_cb read_cb,
			      void *cb_arg)
{
	if (!settings_name_steq(name, "thr", NULL)) {
		return -ENOENT;
	}

	uint8_t buf[4];

	if (len != sizeof(buf) || read_cb(cb_arg, buf, sizeof(buf)) != sizeof(buf)) {
		return -EINVAL;
	}

	uint16_t off = sys_get_le16(&buf[0]);
	uint16_t on = sys_get_le16(&buf[2]);

	if (!thresholds_valid(off, on)) {
		LOG_WRN("ignoring stored thresholds off %u on %u", off, on);
		return -EINVAL;
	}
	off_count = off;
	on_count = on;
	LOG_INF("level thresholds loaded: off %u on %u", off, on);
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(level, "level", NULL, level_settings_set, NULL, NULL);

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
		if (count > off_count) {
			water_present = false;
			LOG_INF("water absent (count %u)", count);
		}
	} else {
		if (count < on_count) {
			water_present = true;
			LOG_INF("water PRESENT (count %u)", count);
		}
	}

	return water_present;
}

static void print_thresholds(const struct shell *sh)
{
	shell_print(sh, "level thresholds: off %u on %u", off_count, on_count);
}

static int cmd_level_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	print_thresholds(sh);
	return 0;
}

static int cmd_level_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	char *end_off;
	char *end_on;
	unsigned long off = strtoul(argv[1], &end_off, 10);
	unsigned long on = strtoul(argv[2], &end_on, 10);

	if (*end_off || *end_on || off > UINT16_MAX || on > UINT16_MAX ||
	    level_set_thresholds((uint16_t)off, (uint16_t)on) != 0) {
		shell_error(sh, "error: off must be greater than on (and on > %d)",
			    LEVEL_COUNT_MIN_VALID);
		return -EINVAL;
	}
	print_thresholds(sh);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(level_cmds,
	SHELL_CMD_ARG(get, NULL, "Print the pump thresholds (TSC counts)", cmd_level_get, 1, 0),
	SHELL_CMD_ARG(set, NULL, "<off> <on>: pump OFF above <off>, ON below <on>",
		      cmd_level_set, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(level, &level_cmds, "Water-level pump thresholds", NULL);
