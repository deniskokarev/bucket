import React from 'react';
import { Text, View } from 'react-native';
import { LevelStatus } from '../utils/ble';
import { styles } from '../styles';

interface Props {
  level: LevelStatus | null;
}

export function StatusCard({ level }: Props) {
  const water = level?.waterPresent ?? false;
  return (
    <View style={styles.card}>
      <Text style={styles.cardTitle}>Water level</Text>
      <View style={styles.row}>
        <View>
          <Text style={styles.bigValue}>{water ? 'FULL' : 'OK'}</Text>
          <Text style={styles.subValue}>
            {level ? `raw TSC: ${level.rawTsc}` : 'no data yet'}
          </Text>
        </View>
        <View
          style={[
            styles.pill,
            { backgroundColor: water ? '#dc2626' : '#16a34a' },
          ]}>
          <Text style={styles.pillText}>
            {water ? 'water detected' : 'below trigger'}
          </Text>
        </View>
      </View>
    </View>
  );
}
