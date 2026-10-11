/*
 * BLE service for the Bucket board (singleton).
 *
 * Lifecycle, driven by the app's foreground state (see useBucket):
 *  - start(): if we have a stored bond, auto-connect (and keep retrying);
 *             otherwise scan. Scanning auto-pairs when it sees the board
 *             advertising its bonding marker (user entered SW1 short-short-long).
 *  - background(): stop scanning and Level notifications, keep the link.
 *  - stop():  stop scanning and disconnect immediately.
 *  - forget(): drop the stored bond and return to the scanning state.
 *
 * Why background keeps the link: Android shows the pairing prompt as a
 * separate system activity, which RN reports as AppState 'background', so
 * disconnecting on background aborted first-time pairing. A timed disconnect
 * doesn't work either: RN pauses JS timers while backgrounded. With
 * notifications off the idle link costs little; power is a later concern.
 */
import { BleManager, Device, Subscription } from 'react-native-ble-plx';
import AsyncStorage from '@react-native-async-storage/async-storage';
import {
  BUCKET_SERVICE_UUID,
  DEVICE_NAME,
  LEVEL_CHAR_UUID,
  PUMP_CHAR_UUID,
  PumpMode,
  LevelStatus,
  THRESHOLDS_CHAR_UUID,
  Thresholds,
  base64ToBytes,
  bytesToBase64,
  decodeLevel,
  decodeThresholds,
  encodeThresholds,
  isBondingAdv,
  requestBlePermissions,
} from '../utils/ble';

export type ConnStatus =
  | 'idle'
  | 'scanning'
  | 'connecting'
  | 'connected'
  | 'error';

export interface BucketState {
  status: ConnStatus;
  /** We hold a stored bond for a board (connected or not). */
  bonded: boolean;
  deviceName: string | null;
  level: LevelStatus | null;
  pumpMode: PumpMode;
  /** Pump hysteresis stored on the board (null until read). */
  thresholds: Thresholds | null;
  error: string | null;
}

type Listener = (state: BucketState) => void;

const STORAGE_KEY = 'bucket.bondedDeviceId';
const RECONNECT_DELAY_MS = 3000;
const PAIR_RETRY_MS = 2000;
const PAIR_RETRIES = 20; // ~40 s: time for the user to confirm the OS prompt
// Rediscovering on the same link doesn't help once Android returned an empty
// database (see discover()); a fresh connection does, so give up fast.
const DISCOVER_RETRIES = 2;
const DISCOVER_RETRY_MS = 1000;

const msg = (e: unknown) => (e instanceof Error ? e.message : String(e));
const delay = (ms: number) => new Promise<void>(r => setTimeout(() => r(), ms));
// Shows in logcat as ReactNativeJS (debug builds); e2e failure dumps include it.
const log = (...args: unknown[]) => console.log('[ble]', ...args);

class BleService {
  private manager = new BleManager();
  private device: Device | null = null;
  private levelSub: Subscription | null = null;
  private thrSub: Subscription | null = null;
  private disconnectSub: Subscription | null = null;
  private listeners = new Set<Listener>();

  private bondedId: string | null = null;
  private loaded = false;
  private active = false; // app is in the foreground
  private scanning = false; // a scan is currently running (avoid restarts)
  private linking = false; // a connect/bond attempt is in flight
  private secure = false; // link encrypted (secure chars readable)
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  private state: BucketState = {
    status: 'idle',
    bonded: false,
    deviceName: null,
    level: null,
    pumpMode: PumpMode.Auto,
    thresholds: null,
    error: null,
  };

  getState(): BucketState {
    return this.state;
  }

  subscribe(fn: Listener): () => void {
    this.listeners.add(fn);
    fn(this.state);
    return () => this.listeners.delete(fn);
  }

  private update(patch: Partial<BucketState>) {
    this.state = { ...this.state, ...patch };
    this.listeners.forEach(l => l(this.state));
  }

  // ---- lifecycle ----

  async start(): Promise<void> {
    log('start', { device: !!this.device, linking: this.linking, bonded: !!this.bondedId });
    this.active = true;
    if (!this.loaded) {
      this.bondedId = await AsyncStorage.getItem(STORAGE_KEY);
      this.loaded = true;
      this.update({ bonded: this.bondedId !== null });
    }
    const ok = await requestBlePermissions();
    if (!ok) {
      this.update({ status: 'error', error: 'Bluetooth permissions denied' });
      return;
    }
    if (this.device || this.linking) {
      // Link kept while backgrounded (or still coming up).
      this.attachLevel();
      this.attachThresholds();
      return;
    }
    if (this.bondedId) {
      this.tryConnectBonded();
    } else {
      this.startScan();
    }
  }

  background(): void {
    log('background', { device: !!this.device, linking: this.linking });
    this.active = false;
    this.scanning = false;
    this.clearReconnect();
    this.manager.stopDeviceScan();
    if (!this.device && !this.linking) {
      this.update({ status: 'idle' });
      return;
    }
    this.levelSub?.remove(); // stop the 5 Hz notifications; keep the link
    this.levelSub = null;
    this.thrSub?.remove();
    this.thrSub = null;
  }

  async stop(): Promise<void> {
    this.active = false;
    this.scanning = false;
    this.clearReconnect();
    this.manager.stopDeviceScan();
    await this.teardown();
    this.update({ status: 'idle', level: null });
  }

  // ---- scanning (unbonded) ----

  startScan(): void {
    // Keep one scan running; Android throttles scan *restarts* (~5 / 30 s).
    if (this.bondedId || !this.active || this.scanning) {
      log('startScan skipped', { bonded: !!this.bondedId, active: this.active, scanning: this.scanning, linking: this.linking });
      return;
    }
    log('startScan', { linking: this.linking });
    this.scanning = true;
    this.clearReconnect();
    this.update({ status: 'scanning', error: null });
    this.manager.startDeviceScan(null, { allowDuplicates: false }, (err, device) => {
      if (err) {
        this.scanning = false;
        this.manager.stopDeviceScan();
        // Most often Android's scan throttle — pause and retry rather than
        // getting stuck in an error state.
        this.update({ status: 'error', error: `Scanning paused (${err.message}); retrying…` });
        this.clearReconnect();
        this.reconnectTimer = setTimeout(() => {
          this.update({ error: null });
          this.startScan();
        }, 15000);
        return;
      }
      if (!device) {
        return;
      }
      const isBucket =
        device.name === DEVICE_NAME ||
        device.localName === DEVICE_NAME ||
        device.serviceUUIDs?.includes(BUCKET_SERVICE_UUID);
      // Only auto-pair when the board signals bonding mode (SW1 code entered).
      if (isBucket && isBondingAdv(device.manufacturerData)) {
        log('bonding marker seen', device.id);
        this.scanning = false;
        this.manager.stopDeviceScan();
        this.autoBond(device);
      }
    });
  }

  stopScan(): void {
    this.scanning = false;
    this.manager.stopDeviceScan();
    this.update({ status: 'idle' });
  }

  toggleScan(): void {
    if (this.state.status === 'scanning') {
      this.stopScan();
    } else {
      this.startScan();
    }
  }

  private async autoBond(device: Device): Promise<void> {
    log('autoBond start', device.id, { linking: this.linking });
    this.linking = true;
    try {
      this.update({ status: 'connecting', deviceName: device.name ?? device.id, error: null });
      this.device = await device.connect();
      log('autoBond connected');
      await this.discover();
      this.attachLevel();
      this.disconnectSub = this.device.onDisconnected(() => this.handleDisconnect());
      this.update({ status: 'connected' });

      // Reading the secure Pump char triggers OS pairing; it errors while the
      // Android prompt is up, so retry to give the user time to confirm.
      let ok = false;
      for (let i = 0; i < PAIR_RETRIES; i++) {
        if (await this.readPump()) {
          ok = true;
          break;
        }
        if (this.state.status !== 'connected') {
          log('autoBond: link dropped during pairing', { attempt: i });
          break; // link dropped
        }
        await delay(PAIR_RETRY_MS);
      }
      log('autoBond result', { ok });

      if (ok) {
        this.bondedId = device.id;
        await AsyncStorage.setItem(STORAGE_KEY, device.id);
        this.secure = true;
        this.update({ bonded: true, error: null });
        this.attachThresholds();
      } else {
        this.update({
          error: 'Pairing not completed — keep the board in bonding mode and confirm the prompt.',
        });
        await this.teardown();
        this.resumeScanSoon();
      }
    } catch (e) {
      log('autoBond error', msg(e));
      this.update({ status: 'error', error: msg(e) });
      // Drop the link if it's still up: the board stops advertising (and its
      // bonding marker) while connected, so a rescan could never find it.
      await this.teardown();
      this.resumeScanSoon();
    } finally {
      this.linking = false;
    }
  }

  private resumeScanSoon(): void {
    this.clearReconnect();
    this.reconnectTimer = setTimeout(() => this.startScan(), RECONNECT_DELAY_MS);
  }

  // ---- bonded auto-connect ----

  private async tryConnectBonded(): Promise<void> {
    if (!this.active || !this.bondedId) {
      return;
    }
    this.update({ status: 'connecting', error: null });
    log('tryConnectBonded', this.bondedId);
    this.linking = true;
    try {
      // autoConnect: Android (re)connects whenever the device is in range.
      this.device = await this.manager.connectToDevice(this.bondedId, {
        autoConnect: true,
      });
      await this.discover();
      this.attachLevel();
      this.disconnectSub = this.device.onDisconnected(() => this.handleDisconnect());
      this.secure = await this.readPump(); // bonded link encrypts automatically
      this.update({
        status: 'connected',
        deviceName: this.device.name ?? this.device.id,
        error: null,
      });
      this.attachThresholds();
    } catch (e) {
      log('tryConnectBonded error', msg(e));
      await this.teardown(); // a half-open link would block the next attempt
      this.scheduleReconnect();
    } finally {
      this.linking = false;
    }
  }

  private scheduleReconnect(): void {
    this.clearReconnect();
    if (!this.active || !this.bondedId) {
      return;
    }
    this.update({ status: 'connecting' });
    this.reconnectTimer = setTimeout(() => this.tryConnectBonded(), RECONNECT_DELAY_MS);
  }

  private clearReconnect(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  // ---- shared ----

  /*
   * Discover services and make sure the Bucket service is there. Android can
   * report discovery as successful with an empty database: right after a link
   * drop (e.g. the stack deleting a stale bond on "key missing") the previous
   * discovery is still pending, the new one is refused (GATT_BUSY) and an
   * empty cache is returned. Every GATT op then fails with "service not found"
   * and pairing never starts. Retrying on the same link keeps returning the
   * empty database, so the caller drops the link and reconnects.
   */
  private async discover(): Promise<void> {
    for (let i = 0; ; i++) {
      const d = this.device;
      if (!d) {
        throw new Error('disconnected during service discovery');
      }
      await d.discoverAllServicesAndCharacteristics();
      const services = await d.services();
      if (services.some(s => s.uuid.toLowerCase() === BUCKET_SERVICE_UUID)) {
        return;
      }
      log('discover: Bucket service missing, retrying', { attempt: i, found: services.length });
      if (i + 1 >= DISCOVER_RETRIES) {
        throw new Error('Bucket service not found on the board');
      }
      await delay(DISCOVER_RETRY_MS);
    }
  }

  private attachLevel(): void {
    if (!this.device || !this.active || this.levelSub) {
      return;
    }
    this.levelSub = this.device.monitorCharacteristicForService(
      BUCKET_SERVICE_UUID,
      LEVEL_CHAR_UUID,
      (err, char) => {
        if (err) {
          return; // disconnects are handled by onDisconnected
        }
        if (char?.value) {
          this.update({ level: decodeLevel(base64ToBytes(char.value)) });
        }
      },
    );
  }

  private async readPump(): Promise<boolean> {
    if (!this.device) {
      return false;
    }
    try {
      const pump = await this.device.readCharacteristicForService(
        BUCKET_SERVICE_UUID,
        PUMP_CHAR_UUID,
      );
      if (pump?.value) {
        this.update({ pumpMode: base64ToBytes(pump.value)[0] as PumpMode });
      }
      return true;
    } catch (e) {
      log('readPump failed', msg(e));
      return false;
    }
  }

  /* Thresholds are encrypted: read + subscribe only once the link is secure. */
  private async attachThresholds(): Promise<void> {
    const d = this.device;
    if (!d || !this.active || !this.secure || this.thrSub) {
      return;
    }
    this.thrSub = d.monitorCharacteristicForService(
      BUCKET_SERVICE_UUID,
      THRESHOLDS_CHAR_UUID,
      (err, char) => {
        if (!err && char?.value) {
          this.update({ thresholds: decodeThresholds(base64ToBytes(char.value)) });
        }
      },
    );
    try {
      const c = await d.readCharacteristicForService(BUCKET_SERVICE_UUID, THRESHOLDS_CHAR_UUID);
      if (c?.value) {
        this.update({ thresholds: decodeThresholds(base64ToBytes(c.value)) });
      }
    } catch (e) {
      log('read thresholds failed', msg(e));
    }
  }

  async setThresholds(t: Thresholds): Promise<void> {
    if (!this.device) {
      return;
    }
    try {
      await this.device.writeCharacteristicWithResponseForService(
        BUCKET_SERVICE_UUID,
        THRESHOLDS_CHAR_UUID,
        bytesToBase64(encodeThresholds(t)),
      );
      this.update({ thresholds: t, error: null });
    } catch (e) {
      this.update({ error: msg(e) });
    }
  }

  async setPumpMode(mode: PumpMode): Promise<void> {
    if (!this.device) {
      return;
    }
    try {
      await this.device.writeCharacteristicWithResponseForService(
        BUCKET_SERVICE_UUID,
        PUMP_CHAR_UUID,
        bytesToBase64(Uint8Array.of(mode)),
      );
      this.update({ pumpMode: mode });
    } catch (e) {
      this.update({ error: msg(e) });
    }
  }

  async forget(): Promise<void> {
    this.bondedId = null;
    await AsyncStorage.removeItem(STORAGE_KEY);
    this.clearReconnect();
    await this.teardown();
    this.update({
      bonded: false,
      status: 'idle',
      level: null,
      pumpMode: PumpMode.Auto,
      thresholds: null,
      error: null,
    });
    this.startScan();
  }

  private async teardown(): Promise<void> {
    // Remove the disconnect watcher first so an *intentional* teardown
    // (stop/forget) doesn't trigger handleDisconnect's reconnect/rescan.
    this.disconnectSub?.remove();
    this.disconnectSub = null;
    this.levelSub?.remove();
    this.levelSub = null;
    this.thrSub?.remove();
    this.thrSub = null;
    this.secure = false;
    const d = this.device;
    this.device = null;
    if (d) {
      log('teardown: cancelConnection', d.id, { linking: this.linking });
      try {
        await d.cancelConnection();
      } catch {
        /* already gone */
      }
    }
  }

  private handleDisconnect(): void {
    log('disconnected', { active: this.active, bonded: !!this.bondedId, linking: this.linking });
    this.disconnectSub?.remove();
    this.disconnectSub = null;
    this.levelSub?.remove();
    this.levelSub = null;
    this.thrSub?.remove();
    this.thrSub = null;
    this.secure = false;
    this.device = null;
    this.update({ level: null });
    if (!this.active) {
      this.update({ status: 'idle' });
    } else if (this.bondedId) {
      this.scheduleReconnect(); // keep trying while the app is open
    } else {
      this.startScan();
    }
  }
}

export const bleService = new BleService();
