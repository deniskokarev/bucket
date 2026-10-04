/*
 * bucket — dehumidifier bucket auto-evacuator
 *
 * Milestone 1 bring-up: boot the WB55, initialize the TSC capacitive sensor
 * and the Bluetooth controller, and run a shell. The pump control loop is
 * wired in but harmless until a real electrode + pump are connected and the
 * TSC thresholds are characterized.
 */
#include "adv.h"
#include "bond.h"
#include "gatt.h"
#include "level.h"
#include "pump.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define CONTROL_PERIOD_MS 200

/* Service UUID in the primary advertisement so centrals can filter/scan for us;
 * must match gatt.c / client ble.ts. Name + bonding marker go in the scan
 * response (name + UUID together would overflow a single 31-byte packet). */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      BT_UUID_128_ENCODE(0xd2f2a658, 0x2605, 0x4993, 0x9794, 0x01961b93a091)),
};

/* Manufacturer data: company 0xFFFF (test/internal) + a bonding-mode flag byte.
 * mfg_data[2] == 1 while the bonding window is open (see bt_set_bonding_adv). */
static uint8_t mfg_data[] = {0xFF, 0xFF, 0x00};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};

void bt_set_bonding_adv(bool on)
{
	mfg_data[2] = on ? 1 : 0;
	/* Live-update if currently advertising; the byte is also authoritative for
	 * the next start_adv(), so ignore errors (e.g. while connected). */
	(void)bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

static int start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err == 0) {
		LOG_INF("advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
	}
	return err;
}

/*
 * Re-advertising is deferred to the system workqueue: restarting from inside
 * the disconnected callback fails with -ENOMEM on the WB controller because the
 * connection object isn't freed yet. Retry until a slot is available.
 */
static void adv_work_handler(struct k_work *work)
{
	int err = start_adv();
	if (err == -ENOMEM || err == -EAGAIN) {
		k_work_reschedule(k_work_delayable_from_work(work), K_MSEC(100));
	} else if (err) {
		LOG_ERR("advertising start failed (%d)", err);
	}
}

static K_WORK_DELAYABLE_DEFINE(adv_work, adv_work_handler);

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("connection failed (0x%02x)", err);
		return;
	}
	LOG_INF("central connected");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	/* Connectable advertising stops on connect; restart it (deferred) so the
	 * board is discoverable again after the central goes away. */
	LOG_INF("central disconnected (reason 0x%02x), re-advertising", reason);
	k_work_reschedule(&adv_work, K_MSEC(50));
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
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

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load(); /* restore stored bonds */
	}
	if (bond_init() != 0) {
		LOG_ERR("bond init failed");
	}

	if (start_adv() != 0) {
		k_work_reschedule(&adv_work, K_MSEC(100));
	}
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
