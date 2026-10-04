/*
 * Bucket custom GATT service.
 *
 * Exposes the water-level status (read + notify) and the pump mode
 * (read + write) to a BLE central (the mobile client). UUIDs and payload
 * layouts are the contract shared with client/src/utils/ble.ts.
 */
#ifndef BUCKET_GATT_H
#define BUCKET_GATT_H

/*
 * Push the current level/pump status to any subscribed central.
 * Safe to call periodically; a no-op when nobody is subscribed.
 */
void bucket_gatt_notify(void);

#endif /* BUCKET_GATT_H */
