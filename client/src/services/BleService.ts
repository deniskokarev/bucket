/**
 * BLE service for the Bucket evacuator board (singleton, mirrors the
 * ttsService pattern from the AI-chat client).
 *
 * Wraps react-native-ble-plx: scans for the firmware's custom GATT service,
 * connects, subscribes to Level notifications, and writes the Pump mode.
 */
import { BleManager, Device, Subscription } from 'react-native-ble-plx';
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
  deviceName: string | null;
  level: LevelStatus | null;
  pumpMode: PumpMode;
  error: string | null;
}

type Listener = (state: BucketState) => void;

const SCAN_TIMEOUT_MS = 15000;

class BleService {
  private manager = new BleManager();
  private device: Device | null = null;
  private levelSub: Subscription | null = null;
  private scanTimer: ReturnType<typeof setTimeout> | null = null;
  private listeners = new Set<Listener>();

  private state: BucketState = {
    status: 'idle',
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

  /** Scan for the bucket board and connect to the first match. */
  async connect(): Promise<void> {
    const ok = await requestBlePermissions();
    if (!ok) {
      this.update({ status: 'error', error: 'Bluetooth permissions denied' });
      return;
    }

    this.update({ status: 'scanning', error: null });
    // Scan unfiltered and match in JS: the OS 128-bit service-UUID filter is
    // unreliable across devices, and the name may live in the scan response.
    this.manager.startDeviceScan(null, { allowDuplicates: false }, (error, device) => {
      if (error) {
        this.stopScan();
        this.update({ status: 'error', error: error.message });
        return;
      }
      if (!device) {
        return;
      }
      const matches =
        device.name === DEVICE_NAME ||
        device.localName === DEVICE_NAME ||
        device.serviceUUIDs?.includes(BUCKET_SERVICE_UUID);
      if (matches) {
        this.stopScan();
        this.attach(device);
      }
    });

    this.scanTimer = setTimeout(() => {
      if (this.state.status === 'scanning') {
        this.stopScan();
        this.update({ status: 'error', error: 'Device not found' });
      }
    }, SCAN_TIMEOUT_MS);
  }

  private stopScan() {
    this.manager.stopDeviceScan();
    if (this.scanTimer) {
      clearTimeout(this.scanTimer);
      this.scanTimer = null;
    }
  }

  private async attach(device: Device) {
    try {
      this.update({ status: 'connecting', deviceName: device.name ?? device.id });
      this.device = await device.connect();
      await this.device.discoverAllServicesAndCharacteristics();

      // Subscribe to Level notifications.
      this.levelSub = this.device.monitorCharacteristicForService(
        BUCKET_SERVICE_UUID,
        LEVEL_CHAR_UUID,
        (err, char) => {
          if (err) {
            this.update({ status: 'error', error: err.message });
            return;
          }
          if (char?.value) {
            this.update({ level: decodeLevel(base64ToBytes(char.value)) });
          }
        },
      );

      // Read current pump mode once.
      const pump = await this.device.readCharacteristicForService(
        BUCKET_SERVICE_UUID,
        PUMP_CHAR_UUID,
      );
      if (pump?.value) {
        this.update({ pumpMode: base64ToBytes(pump.value)[0] as PumpMode });
      }

      // Notice unexpected disconnects.
      this.device.onDisconnected(() => this.handleDisconnect());

      this.update({ status: 'connected', error: null });
    } catch (e) {
      this.update({
        status: 'error',
        error: e instanceof Error ? e.message : String(e),
      });
    }
  }

  /** Set the pump mode (auto / force-off / force-on). */
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
      this.update({ error: e instanceof Error ? e.message : String(e) });
    }
  }

  async disconnect(): Promise<void> {
    this.stopScan();
    this.levelSub?.remove();
    this.levelSub = null;
    if (this.device) {
      try {
        await this.device.cancelConnection();
      } catch {
        /* already gone */
      }
    }
    this.handleDisconnect();
  }

  private handleDisconnect() {
    this.levelSub?.remove();
    this.levelSub = null;
    this.device = null;
    this.update({ status: 'idle', deviceName: null, level: null });
  }

  destroy() {
    this.disconnect();
    this.manager.destroy();
  }
}

export const bleService = new BleService();
