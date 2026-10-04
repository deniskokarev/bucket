import { useCallback, useEffect, useState } from 'react';
import { bleService, BucketState } from '../services/BleService';
import { PumpMode } from '../utils/ble';

export interface UseBucketReturn extends BucketState {
  connect: () => Promise<void>;
  disconnect: () => Promise<void>;
  setPumpMode: (mode: PumpMode) => Promise<void>;
}

export const useBucket = (): UseBucketReturn => {
  const [state, setState] = useState<BucketState>(bleService.getState());

  useEffect(() => {
    const unsub = bleService.subscribe(setState);
    return unsub;
  }, []);

  const connect = useCallback(() => bleService.connect(), []);
  const disconnect = useCallback(() => bleService.disconnect(), []);
  const setPumpMode = useCallback(
    (mode: PumpMode) => bleService.setPumpMode(mode),
    [],
  );

  return { ...state, connect, disconnect, setPumpMode };
};
