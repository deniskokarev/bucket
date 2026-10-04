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
  const scanning = bucket.status === 'scanning';

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
        {bucket.bonded ? (
          <>
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
            ) : (
              <View style={styles.card}>
                <Text style={styles.cardTitle}>Board</Text>
                <Text style={styles.bigValue}>Connecting…</Text>
                <Text style={styles.subValue}>
                  Will connect automatically when the board is in range.
                </Text>
              </View>
            )}

            {/* Shown whenever bonded, connected or not. */}
            <TouchableOpacity style={styles.forgetButton} onPress={bucket.forget}>
              <Text style={styles.buttonText}>Forget Board</Text>
            </TouchableOpacity>
          </>
        ) : (
          <>
            <TouchableOpacity style={styles.primaryButton} onPress={bucket.toggleScan}>
              <Text style={styles.buttonText}>
                {scanning ? 'Stop Scanning' : 'Scan'}
              </Text>
            </TouchableOpacity>

            {scanning ? (
              <Text style={styles.hint}>
                Scanning for the board…{'\n\n'}
                To pair, press the button (SW1) on the Nucleo board. Pairing
                starts automatically, then confirm the Android prompt.
              </Text>
            ) : (
              <Text style={styles.hint}>
                Tap Scan, then press SW1 on the board to pair.
              </Text>
            )}
          </>
        )}

        {bucket.error ? <Text style={styles.errorText}>{bucket.error}</Text> : null}
      </ScrollView>
    </SafeAreaView>
  );
}

export default App;
