/*
 * bucket — dehumidifier bucket auto-evacuator
 *
 * Milestone 1 bring-up: boot the WB55, initialize the TSC capacitive sensor
 * and the Bluetooth controller, and run a shell. The pump control loop is
 * wired in but harmless until a real electrode + pump are connected and the
 * TSC thresholds are characterized.
 */
#include "gatt.h"
#include "level.h"
#include "pump.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define CONTROL_PERIOD_MS 200

/* Service UUID in the primary advertisement so centrals can filter/scan for us;
 * must match gatt.c / client ble.ts. Name goes in the scan response (the two
 * together would overflow a single 31-byte advertisement). */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      BT_UUID_128_ENCODE(0xd2f2a658, 0x2605, 0x4993, 0x9794, 0x01961b93a091)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void bt_bringup(void)
{
	int err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (%d) — is the WB55 wireless coprocessor "
			"stack flashed on the M0+?",
			err);
		return;
	}
	LOG_INF("Bluetooth controller ready");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("advertising start failed (%d)", err);
		return;
	}
	LOG_INF("advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
}

int main(void)
{
	LOG_INF("bucket starting on %s", CONFIG_BOARD);

	if (pump_init() != 0) {
		LOG_ERR("pump init failed");
	}
	if (level_init() != 0) {
		LOG_ERR("level init failed");
	}

	bt_bringup();

	LOG_INF("bring-up complete: TSC + BT + shell running");

	/* Control loop: in AUTO follow the level sensor; FORCE_ON/OFF come from
	 * the BLE pump characteristic. The min-run / max-run guards are enforced
	 * inside the pump module. Status is pushed to subscribed centrals. */
	while (1) {
		switch (pump_get_mode()) {
		case PUMP_MODE_FORCE_ON:
			pump_request(true);
			break;
		case PUMP_MODE_FORCE_OFF:
			pump_request(false);
			break;
		case PUMP_MODE_AUTO:
		default:
			pump_request(level_water_present());
			break;
		}
		pump_tick();
		bucket_gatt_notify();
		k_sleep(K_MSEC(CONTROL_PERIOD_MS));
	}

	return 0;
}
