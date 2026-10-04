#include "gatt.h"
#include "level.h"
#include "pump.h"

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gatt, LOG_LEVEL_INF);

/* Service + characteristic UUIDs (must match client/src/utils/ble.ts). */
#define BUCKET_SVC_UUID    BT_UUID_128_ENCODE(0xd2f2a658, 0x2605, 0x4993, 0x9794, 0x01961b93a091)
#define BUCKET_LEVEL_UUID  BT_UUID_128_ENCODE(0x3ec2dc2f, 0xcfdf, 0x49e1, 0xb232, 0xda0dc4a88003)
#define BUCKET_PUMP_UUID   BT_UUID_128_ENCODE(0x4feebce2, 0xe1af, 0x4634, 0x915d, 0x9e62b0756af1)

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(BUCKET_SVC_UUID);
static const struct bt_uuid_128 level_uuid = BT_UUID_INIT_128(BUCKET_LEVEL_UUID);
static const struct bt_uuid_128 pump_uuid = BT_UUID_INIT_128(BUCKET_PUMP_UUID);

static bool level_notify_enabled;

/* Level payload: [water:u8, pump:u8, rawTSC:u16 LE]. */
static void fill_level(uint8_t buf[4])
{
	/* rawTSC is reserved (0) for now: the tsc-keys input driver only exposes
	 * press/release, not the raw acquisition count. TODO: read the raw TSC
	 * count directly once we bypass the input layer. */
	uint16_t raw = 0;

	buf[0] = level_water_present() ? 1 : 0;
	buf[1] = pump_is_on() ? 1 : 0;
	buf[2] = (uint8_t)(raw & 0xff);
	buf[3] = (uint8_t)(raw >> 8);
}

static ssize_t read_level(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	uint8_t value[4];

	fill_level(value);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static void level_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	level_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("level notifications %s", level_notify_enabled ? "enabled" : "disabled");
}

static ssize_t read_pump(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	uint8_t mode = (uint8_t)pump_get_mode();

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &mode, sizeof(mode));
}

static ssize_t write_pump(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0 || len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	uint8_t v = ((const uint8_t *)buf)[0];
	if (v > PUMP_MODE_FORCE_ON) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	pump_set_mode((enum pump_mode)v);
	return len;
}

/* clang-format off */
BT_GATT_SERVICE_DEFINE(bucket_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&level_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_level, NULL, NULL),
	BT_GATT_CCC(level_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&pump_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_pump, write_pump, NULL),
);
/* clang-format on */

void bucket_gatt_notify(void)
{
	if (!level_notify_enabled) {
		return;
	}

	uint8_t value[4];

	fill_level(value);
	/* attrs[1] is the Level characteristic value attribute. */
	bt_gatt_notify(NULL, &bucket_svc.attrs[1], value, sizeof(value));
}
