import React, { useEffect, useState } from 'react';
import { Text, TextInput, TouchableOpacity, View } from 'react-native';
import { Thresholds, thresholdsError } from '../utils/ble';
import { styles } from '../styles';

interface Props {
  thresholds: Thresholds | null;
  onSave: (t: Thresholds) => void;
}

/** Pump hysteresis in raw TSC counts, same order as `level set <off> <on>`. */
export function ThresholdsForm({ thresholds, onSave }: Props) {
  const [off, setOff] = useState('');
  const [on, setOn] = useState('');
  // Collapsed by default so the main screen keeps its layout.
  const [open, setOpen] = useState(false);

  // Reset only when the board's values change, not on every Level re-render.
  const boardOff = thresholds?.off;
  const boardOn = thresholds?.on;
  useEffect(() => {
    if (boardOff !== undefined && boardOn !== undefined) {
      setOff(String(boardOff));
      setOn(String(boardOn));
    }
  }, [boardOff, boardOn]);

  const error = thresholds ? thresholdsError(off, on) : null;
  const changed = off !== String(boardOff) || on !== String(boardOn);
  const canSave = thresholds !== null && error === null && changed;

  return (
    <View style={styles.card}>
      <TouchableOpacity
        accessibilityLabel="settings-toggle"
        style={styles.row}
        onPress={() => setOpen(o => !o)}>
        <Text style={[styles.cardTitle, !open && styles.cardTitleCollapsed]}>
          Settings
        </Text>
        <Text style={styles.subValue}>{open ? '▾' : '▸'}</Text>
      </TouchableOpacity>
      {open ? (
        <>
          <View style={[styles.row, styles.fieldRow]}>
            <Text style={styles.subValue}>Pump OFF above</Text>
            <TextInput
              accessibilityLabel="threshold-off"
              style={styles.input}
              keyboardType="number-pad"
              value={off}
              onChangeText={setOff}
              editable={thresholds !== null}
            />
          </View>
          <View style={styles.row}>
            <Text style={styles.subValue}>Pump ON below</Text>
            <TextInput
              accessibilityLabel="threshold-on"
              style={styles.input}
              keyboardType="number-pad"
              value={on}
              onChangeText={setOn}
              editable={thresholds !== null}
            />
          </View>
          {error ? <Text style={styles.errorText}>{error}</Text> : null}
          <TouchableOpacity
            style={[styles.primaryButton, styles.saveButton, !canSave && styles.disabled]}
            disabled={!canSave}
            onPress={() => onSave({ off: Number(off), on: Number(on) })}>
            <Text style={styles.buttonText}>Save thresholds</Text>
          </TouchableOpacity>
        </>
      ) : null}
    </View>
  );
}
