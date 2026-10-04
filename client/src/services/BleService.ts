/*
 * BLE service for the Bucket board (singleton).
 *
 * Lifecycle, driven by the app's foreground state (see useBucket):
 *  - start(): if we have a stored bond, auto-connect (and keep retrying);
 *             otherwise scan. Scanning auto-pairs when it sees the board
 *             advertising its bonding marker (user pressed SW1 on the board).
 *  - stop():  stop scanning and disconnect (no background interaction).
 *  - forget(): drop the stored bond and return to the scanning state.
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
const PAIR_RETRIES = 8;

const msg = (e: unknown) => (e instanceof Error ? e.message : String(e));
const delay = (ms: number) => new Promise<void>(r => setTimeout(() => r(), ms));

class BleService {
  private manager = new BleManager();
  private device: Device | null = null;
  private levelSub: Subscription | null = null;
  private listeners = new Set<Listener>();

  private bondedId: string | null = null;
  private loaded = false;
  private active = false; // app is in the foreground
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

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
    if (this.state.status === 'connected') {
      return; // already up
    }
    if (this.bondedId) {
      this.tryConnectBonded();
    } else {
      this.startScan();
    }
  }

  async stop(): Promise<void> {
    this.active = false;
    this.clearReconnect();
    this.manager.stopDeviceScan();
    await this.teardown();
    this.update({ status: 'idle', level: null });
  }

  // ---- scanning (unbonded) ----

  startScan(): void {
    if (this.bondedId || !this.active) {
      return;
    }
    this.manager.stopDeviceScan(); // guard against a scan already in progress
    this.update({ status: 'scanning', error: null });
    this.manager.startDeviceScan(null, { allowDuplicates: false }, (err, device) => {
      if (err) {
        this.manager.stopDeviceScan();
        this.update({ status: 'error', error: err.message });
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
        this.manager.stopDeviceScan();
        this.autoBond(device);
      }
    });
  }

  stopScan(): void {
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
    try {
      this.update({ status: 'connecting', deviceName: device.name ?? device.id, error: null });
      this.device = await device.connect();
      await this.device.discoverAllServicesAndCharacteristics();
      this.attachLevel();
      this.device.onDisconnected(() => this.handleDisconnect());
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
    try {
      // autoConnect: Android (re)connects whenever the device is in range.
      this.device = await this.manager.connectToDevice(this.bondedId, {
        autoConnect: true,
      });
      await this.device.discoverAllServicesAndCharacteristics();
      this.attachLevel();
      this.device.onDisconnected(() => this.handleDisconnect());
      await this.readPump(); // bonded link encrypts automatically
      this.update({
        status: 'connected',
        deviceName: this.device.name ?? this.device.id,
        error: null,
      });
    } catch {
      this.scheduleReconnect();
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

  private attachLevel(): void {
    if (!this.device) {
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
