import { useCallback, useEffect, useState } from 'react';
import { AppState } from 'react-native';
import { bleService, BucketState } from '../services/BleService';
import { PumpMode } from '../utils/ble';

export interface UseBucketReturn extends BucketState {
  toggleScan: () => void;
  forget: () => Promise<void>;
  setPumpMode: (mode: PumpMode) => Promise<void>;
}

export const useBucket = (): UseBucketReturn => {
  const [state, setState] = useState<BucketState>(bleService.getState());

  useEffect(() => {
    const unsub = bleService.subscribe(setState);
    bleService.start();

    // Scan/connect while in the foreground. On background, keep the link for a
    // grace period (see BleService) — the Android pairing prompt itself puts
    // the app in the background.
    const sub = AppState.addEventListener('change', s => {
      if (s === 'active') {
        bleService.start();
      } else {
        bleService.background();
      }
    });

    return () => {
      sub.remove();
      unsub();
      bleService.stop();
    };
  }, []);

  const toggleScan = useCallback(() => bleService.toggleScan(), []);
  const forget = useCallback(() => bleService.forget(), []);
  const setPumpMode = useCallback(
    (mode: PumpMode) => bleService.setPumpMode(mode),
    [],
  );

  return { ...state, toggleScan, forget, setPumpMode };
};
