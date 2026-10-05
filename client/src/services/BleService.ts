/*
 * BLE service for the Bucket board (singleton).
 *
 * Lifecycle, driven by the app's foreground state (see useBucket):
 *  - start(): if we have a stored bond, auto-connect (and keep retrying);
 *             otherwise scan. Scanning auto-pairs when it sees the board
 *             advertising its bonding marker (user pressed SW1 on the board).
 *  - background(): stop scanning and Level notifications at once, then keep
 *             the link for BACKGROUND_GRACE_MS before disconnecting.
 *  - stop():  stop scanning and disconnect immediately.
 *  - forget(): drop the stored bond and return to the scanning state.
 *
 * Why a grace period instead of disconnecting on background: Android shows the
 * pairing prompt as a separate system activity, which RN reports as AppState
 * 'background'. Disconnecting right away aborted every first-time pairing
 * (the OS finished bonding, but the app had already dropped the link). With
 * notifications off the idle link costs little, and dropping it after the grace
 * period still bounds the BLE power drain of a backgrounded app. The grace also
 * covers other short trips away (notification shade, app switcher).
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
  base64ToBytes,
  bytesToBase64,
  decodeLevel,
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
  error: string | null;
}

type Listener = (state: BucketState) => void;

const STORAGE_KEY = 'bucket.bondedDeviceId';
const RECONNECT_DELAY_MS = 3000;
const PAIR_RETRY_MS = 2000;
const PAIR_RETRIES = 20; // ~40 s: time for the user to confirm the OS prompt
// Must exceed the pairing window above (see the header comment).
const BACKGROUND_GRACE_MS = 2 * 60 * 1000;

const msg = (e: unknown) => (e instanceof Error ? e.message : String(e));
const delay = (ms: number) => new Promise<void>(r => setTimeout(() => r(), ms));

class BleService {
  private manager = new BleManager();
  private device: Device | null = null;
  private levelSub: Subscription | null = null;
  private disconnectSub: Subscription | null = null;
  private listeners = new Set<Listener>();

  private bondedId: string | null = null;
  private loaded = false;
  private active = false; // app is in the foreground
  private scanning = false; // a scan is currently running (avoid restarts)
  private linking = false; // a connect/bond attempt is in flight
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private graceTimer: ReturnType<typeof setTimeout> | null = null;

  private state: BucketState = {
    status: 'idle',
    bonded: false,
    deviceName: null,
    level: null,
    pumpMode: PumpMode.Auto,
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
    this.active = true;
    this.clearGrace();
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
      // Link survived the background grace period (or is still coming up).
      this.attachLevel();
      return;
    }
    if (this.bondedId) {
      this.tryConnectBonded();
    } else {
      this.startScan();
    }
  }

  background(): void {
    this.active = false;
    this.scanning = false;
    this.clearReconnect();
    this.manager.stopDeviceScan();
    if (!this.device && !this.linking) {
      this.update({ status: 'idle' });
      return;
    }
    this.levelSub?.remove(); // stop the 5 Hz notifications right away
    this.levelSub = null;
    this.clearGrace();
    this.graceTimer = setTimeout(() => {
      this.graceTimer = null;
      this.stop();
    }, BACKGROUND_GRACE_MS);
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
      return;
    }
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
      // Only auto-pair when the board signals bonding mode (SW1 pressed).
      if (isBucket && isBondingAdv(device.manufacturerData)) {
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
    this.linking = true;
    try {
      this.update({ status: 'connecting', deviceName: device.name ?? device.id, error: null });
      this.device = await device.connect();
      await this.device.discoverAllServicesAndCharacteristics();
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
          break; // link dropped
        }
        await delay(PAIR_RETRY_MS);
      }

      if (ok) {
        this.bondedId = device.id;
        await AsyncStorage.setItem(STORAGE_KEY, device.id);
        this.update({ bonded: true, error: null });
      } else {
        this.update({
          error: 'Pairing not completed — keep the board in bonding mode and confirm the prompt.',
        });
        await this.teardown();
        this.resumeScanSoon();
      }
    } catch (e) {
      this.update({ status: 'error', error: msg(e) });
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
    this.linking = true;
    try {
      // autoConnect: Android (re)connects whenever the device is in range.
      this.device = await this.manager.connectToDevice(this.bondedId, {
        autoConnect: true,
      });
      if (!this.active && !this.graceTimer) {
        await this.stop(); // connected after the grace period ran out
        return;
      }
      await this.device.discoverAllServicesAndCharacteristics();
      this.attachLevel();
      this.disconnectSub = this.device.onDisconnected(() => this.handleDisconnect());
      await this.readPump(); // bonded link encrypts automatically
      this.update({
        status: 'connected',
        deviceName: this.device.name ?? this.device.id,
        error: null,
      });
    } catch {
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

  private clearGrace(): void {
    if (this.graceTimer) {
      clearTimeout(this.graceTimer);
      this.graceTimer = null;
    }
  }

  // ---- shared ----

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
    } catch {
      return false;
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
      error: null,
    });
    this.startScan();
  }

  private async teardown(): Promise<void> {
    this.clearGrace();
    // Remove the disconnect watcher first
    // (stop/forget) doesn't trigger handleDisconnect's reconnect/rescan.
    this.disconnectSub?.remove();
    this.disconnectSub = null;
    this.levelSub?.remove();
    this.levelSub = null;
    const d = this.device;
    this.device = null;
    if (d) {
      try {
        await d.cancelConnection();
      } catch {
        /* already gone */
      }
    }
  }

  private handleDisconnect(): void {
    this.clearGrace();
    this.disconnectSub?.remove();
    this.disconnectSub = null;
    this.levelSub?.remove();
    this.levelSub = null;
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
