import React from 'react';
import { Text, TouchableOpacity, View } from 'react-native';
import { PumpMode } from '../utils/ble';
import { styles } from '../styles';

interface Props {
  pumpMode: PumpMode;
  pumpOn: boolean;
  disabled: boolean;
  onChange: (mode: PumpMode) => void;
}

const OPTIONS: { mode: PumpMode; label: string }[] = [
  { mode: PumpMode.Auto, label: 'Auto' },
  { mode: PumpMode.ForceOff, label: 'Off' },
  { mode: PumpMode.ForceOn, label: 'On' },
];

export function PumpControl({ pumpMode, pumpOn, disabled, onChange }: Props) {
  return (
    <View style={styles.card}>
      <View style={styles.row}>
        <Text style={styles.cardTitle}>Pump</Text>
        <View
          style={[
            styles.pill,
            { backgroundColor: pumpOn ? '#2563eb' : '#334155' },
          ]}>
          <Text style={styles.pillText}>{pumpOn ? 'running' : 'stopped'}</Text>
        </View>
      </View>
      <View style={styles.segment}>
        {OPTIONS.map(opt => {
          const active = pumpMode === opt.mode;
          return (
            <TouchableOpacity
              key={opt.mode}
              disabled={disabled}
              style={[
                styles.segmentButton,
                active && styles.segmentButtonActive,
                disabled && { opacity: 0.5 },
              ]}
              onPress={() => onChange(opt.mode)}>
              <Text
                style={[styles.segmentText, active && styles.segmentTextActive]}>
                {opt.label}
              </Text>
            </TouchableOpacity>
          );
        })}
      </View>
    </View>
  );
}
