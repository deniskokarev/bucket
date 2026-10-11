#include "gatt.h"
#include "level.h"
#include "pump.h"
#include "tsc.h"

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(gatt, LOG_LEVEL_INF);

/* Service + characteristic UUIDs (must match client/src/utils/ble.ts). */
#define BUCKET_SVC_UUID    BT_UUID_128_ENCODE(0xd2f2a658, 0x2605, 0x4993, 0x9794, 0x01961b93a091)
#define BUCKET_LEVEL_UUID  BT_UUID_128_ENCODE(0x3ec2dc2f, 0xcfdf, 0x49e1, 0xb232, 0xda0dc4a88003)
#define BUCKET_PUMP_UUID   BT_UUID_128_ENCODE(0x4feebce2, 0xe1af, 0x4634, 0x915d, 0x9e62b0756af1)
#define BUCKET_THR_UUID    BT_UUID_128_ENCODE(0xabbb42ed, 0xde11, 0x4636, 0xbbfa, 0x09158f1b0177)

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(BUCKET_SVC_UUID);
static const struct bt_uuid_128 level_uuid = BT_UUID_INIT_128(BUCKET_LEVEL_UUID);
static const struct bt_uuid_128 pump_uuid = BT_UUID_INIT_128(BUCKET_PUMP_UUID);
static const struct bt_uuid_128 thr_uuid = BT_UUID_INIT_128(BUCKET_THR_UUID);

static bool level_notify_enabled;
static bool thr_notify_enabled;

/* Level payload: [water:u8, pump:u8, rawTSC:u16 LE]. */
static void fill_level(uint8_t buf[4])
{
	uint32_t count = tsc_last_count();
	uint16_t raw = count > UINT16_MAX ? UINT16_MAX : (uint16_t)count;

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

/* Thresholds payload: [off:u16 LE, on:u16 LE] (pump OFF above, ON below). */
static void fill_thresholds(uint8_t buf[4])
{
	uint16_t off, on;

	level_get_thresholds(&off, &on);
	sys_put_le16(off, &buf[0]);
	sys_put_le16(on, &buf[2]);
}

static ssize_t read_thr(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			uint16_t len, uint16_t offset)
{
	uint8_t value[4];

	fill_thresholds(value);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static ssize_t write_thr(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0 || len != 4) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	const uint8_t *v = buf;

	if (level_set_thresholds(sys_get_le16(&v[0]), sys_get_le16(&v[2])) != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

static void thr_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	thr_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("threshold notifications %s", thr_notify_enabled ? "enabled" : "disabled");
}

/* clang-format off */
BT_GATT_SERVICE_DEFINE(bucket_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&level_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_level, NULL, NULL),
	BT_GATT_CCC(level_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	/* Pump is secure: read + write require an encrypted (bonded) link. A
	 * client reads this attribute to trigger pairing during the bonding
	 * window (see src/bond.c). */
	BT_GATT_CHARACTERISTIC(&pump_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       read_pump, write_pump, NULL),
	/* Pump thresholds (persistent settings): encrypted read/write/subscribe. */
	BT_GATT_CHARACTERISTIC(&thr_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       read_thr, write_thr, NULL),
	BT_GATT_CCC(thr_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
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

void bucket_gatt_thresholds_changed(void)
{
	if (!thr_notify_enabled) {
		return;
	}

	uint8_t value[4];

	fill_thresholds(value);
	/* attrs[6] is the Thresholds characteristic declaration. */
	bt_gatt_notify(NULL, &bucket_svc.attrs[6], value, sizeof(value));
}
