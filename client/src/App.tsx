import React from 'react';
import {
  Alert,
  SafeAreaView,
  ScrollView,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { useBucket } from './hooks/useBucket';
import { StatusCard } from './components/StatusCard';
import { PumpControl } from './components/PumpControl';
import { ThresholdsForm } from './components/ThresholdsForm';
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

  const confirmForget = () => {
    Alert.alert(
      'Forget this board?',
      'The app will unpair and return to scanning for a board.',
      [
        { text: 'Cancel', style: 'cancel' },
        { text: 'Forget', style: 'destructive', onPress: () => bucket.forget() },
      ],
    );
  };

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
                <ThresholdsForm
                  thresholds={bucket.thresholds}
                  onSave={bucket.setThresholds}
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

            {/* Shown whenever bonded, connected or not. Confirms first. */}
            <TouchableOpacity style={styles.forgetButton} onPress={confirmForget}>
              <Text style={styles.buttonText}>
                {connected
                  ? 'Disconnect & Forget Board'
                  : 'Stop Reconnecting & Forget Board'}
              </Text>
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
                To pair, on the Nucleo board press SW1 short, short, long
                (hold the last press ~2 s). Pairing starts automatically, then
                confirm the Android prompt.
              </Text>
            ) : (
              <Text style={styles.hint}>
                Tap Scan, then press SW1 on the board short, short, long to pair.
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
