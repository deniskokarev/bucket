import React from 'react';
import {
  SafeAreaView,
  ScrollView,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { useBucket } from './hooks/useBucket';
import { StatusCard } from './components/StatusCard';
import { PumpControl } from './components/PumpControl';
import { styles } from './styles';

const STATUS_COLOR: Record<string, string> = {
  idle: '#64748b',
  scanning: '#f59e0b',
  connecting: '#f59e0b',
  connected: '#16a34a',
  error: '#dc2626',
};

function App() {
  const bucket = useBucket();
  const connected = bucket.status === 'connected';
  const busy = bucket.status === 'scanning' || bucket.status === 'connecting';

  return (
    <SafeAreaView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Bucket</Text>
        <View
          style={[
            styles.connDot,
            { backgroundColor: STATUS_COLOR[bucket.status] },
          ]}
        />
      </View>

      <ScrollView style={styles.content}>
        {/* Live sections are hidden until connected, so placeholder values
            can't be mistaken for real readings. */}
        {connected ? (
          <>
            <StatusCard level={bucket.level} />

            <PumpControl
              pumpMode={bucket.pumpMode}
              pumpOn={bucket.level?.pumpOn ?? false}
              disabled={false}
              onChange={bucket.setPumpMode}
            />
          </>
        ) : null}

        {connected ? (
          <TouchableOpacity style={styles.dangerButton} onPress={bucket.disconnect}>
            <Text style={styles.buttonText}>Disconnect</Text>
          </TouchableOpacity>
        ) : (
          <TouchableOpacity
            style={styles.primaryButton}
            disabled={busy}
            onPress={bucket.connect}>
            <Text style={styles.buttonText}>
              {busy ? 'Searching…' : 'Connect to board'}
            </Text>
          </TouchableOpacity>
        )}

        {bucket.error ? (
          <Text style={styles.errorText}>{bucket.error}</Text>
        ) : null}

        {!connected && !busy ? (
          <Text style={styles.hint}>
            Power the board and tap Connect. The app scans for the “BucketEvac”
            BLE service, then shows live water level and lets you override the
            pump.
          </Text>
        ) : null}
      </ScrollView>
    </SafeAreaView>
  );
}

export default App;
