/**
 * BLE contract + helpers shared across the app.
 *
 * These UUIDs and payload layouts must match the firmware's GATT service
 * (see ../../../firm). Until the firmware exposes this service, the app will
 * connect but find no bucket characteristics.
 */
import { PermissionsAndroid, Platform } from 'react-native';

/** Custom GATT service and characteristics (128-bit). */
export const BUCKET_SERVICE_UUID = 'd2f2a658-2605-4993-9794-01961b93a091';
/** Level/status: Read + Notify. Payload: [water:u8, pump:u8, rawTSC:u16 LE]. */
export const LEVEL_CHAR_UUID = '3ec2dc2f-cfdf-49e1-b232-da0dc4a88003';
/** Pump control: Read + Write. Payload: u8 PumpMode. */
export const PUMP_CHAR_UUID = '4feebce2-e1af-4634-915d-9e62b0756af1';
/** Pump thresholds (encrypted Read + Write + Notify). Payload: [off:u16 LE, on:u16 LE]. */
export const THRESHOLDS_CHAR_UUID = 'abbb42ed-de11-4636-bbfa-09158f1b0177';

/** Name the firmware advertises (see firm/prj.conf CONFIG_BT_DEVICE_NAME). */
export const DEVICE_NAME = 'BucketEvac';

export enum PumpMode {
  Auto = 0,
  ForceOff = 1,
  ForceOn = 2,
}

export interface LevelStatus {
  waterPresent: boolean;
  pumpOn: boolean;
  rawTsc: number;
}

/** Decode the 4-byte Level characteristic payload. */
export function decodeLevel(bytes: Uint8Array): LevelStatus {
  return {
    waterPresent: (bytes[0] ?? 0) !== 0,
    pumpOn: (bytes[1] ?? 0) !== 0,
    rawTsc: (bytes[2] ?? 0) | ((bytes[3] ?? 0) << 8),
  };
}

/** Pump hysteresis in raw TSC counts: pump OFF above `off`, ON below `on`. */
export interface Thresholds {
  off: number;
  on: number;
}

export function decodeThresholds(bytes: Uint8Array): Thresholds {
  return {
    off: (bytes[0] ?? 0) | ((bytes[1] ?? 0) << 8),
    on: (bytes[2] ?? 0) | ((bytes[3] ?? 0) << 8),
  };
}

export function encodeThresholds(t: Thresholds): Uint8Array {
  return Uint8Array.of(t.off & 0xff, t.off >> 8, t.on & 0xff, t.on >> 8);
}

/** Minimal form validation; the board re-checks and rejects invalid writes. */
export function thresholdsError(off: string, on: string): string | null {
  const isCount = (s: string) => /^\d+$/.test(s) && Number(s) <= 0xffff;
  if (!isCount(off) || !isCount(on)) {
    return 'Enter whole-number counts';
  }
  if (Number(off) <= Number(on)) {
    return 'OFF count must be greater than ON count';
  }
  return null;
}

// --- base64 <-> bytes (ble-plx characteristic values are base64 strings) ---

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

export function bytesToBase64(bytes: Uint8Array): string {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const b0 = bytes[i];
    const b1 = i + 1 < bytes.length ? bytes[i + 1] : 0;
    const b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
    out += B64[b0 >> 2];
    out += B64[((b0 & 3) << 4) | (b1 >> 4)];
    out += i + 1 < bytes.length ? B64[((b1 & 15) << 2) | (b2 >> 6)] : '=';
    out += i + 2 < bytes.length ? B64[b2 & 63] : '=';
  }
  return out;
}

export function base64ToBytes(b64: string): Uint8Array {
  const clean = b64.replace(/[^A-Za-z0-9+/]/g, '');
  const out: number[] = [];
  for (let i = 0; i < clean.length; i += 4) {
    const n0 = B64.indexOf(clean[i]);
    const n1 = B64.indexOf(clean[i + 1]);
    const n2 = B64.indexOf(clean[i + 2]);
    const n3 = B64.indexOf(clean[i + 3]);
    out.push((n0 << 2) | (n1 >> 4));
    if (n2 >= 0) out.push(((n1 & 15) << 4) | (n2 >> 2));
    if (n3 >= 0) out.push(((n2 & 3) << 6) | n3);
  }
  return Uint8Array.from(out);
}

/**
 * The board sets manufacturer data [0xFF, 0xFF, 1] while its bonding window is
 * open (SW1 short-short-long / `bond`). The app only auto-pairs when it sees
 * this, so the user initiates bonding from the board. `mfg` is the base64 from
 * ble-plx.
 */
export function isBondingAdv(mfg: string | null | undefined): boolean {
  if (!mfg) {
    return false;
  }
  const b = base64ToBytes(mfg);
  return b.length >= 3 && b[0] === 0xff && b[1] === 0xff && b[2] === 1;
}

/**
 * Request the runtime permissions BLE needs on Android. iOS relies on the
 * NSBluetoothAlwaysUsageDescription Info.plist string and returns true here.
 */
export async function requestBlePermissions(): Promise<boolean> {
  if (Platform.OS !== 'android') {
    return true;
  }
  const api = Platform.Version as number;
  try {
    if (api >= 31) {
      const res = await PermissionsAndroid.requestMultiple([
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      ]);
      return Object.values(res).every(v => v === PermissionsAndroid.RESULTS.GRANTED);
    }
    const granted = await PermissionsAndroid.request(
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    );
    return granted === PermissionsAndroid.RESULTS.GRANTED;
  } catch (e) {
    console.error('BLE permission request failed:', e);
    return false;
  }
}
