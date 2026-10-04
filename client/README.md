# Bucket — mobile client

React Native app to monitor and control the dehumidifier bucket evacuator board
over Bluetooth LE. **Android-first** (tested on a Samsung tablet); iOS builds
too but is secondary (no paid Apple Developer license maintained here).

Structure mirrors the in-house AI-chat RN client: a thin root `App.tsx` wrapper,
a singleton **service** + a **hook**, and `src/{components,hooks,services,utils}`.

```
client/
├── App.tsx                 SafeAreaProvider wrapper -> src/App
├── index.js                registers "BucketClient"
├── src/
│   ├── App.tsx             main screen (status + pump control + connect)
│   ├── styles.ts           plain-object styles
│   ├── components/         StatusCard, PumpControl
│   ├── hooks/useBucket.ts  React hook over the BLE service
│   ├── services/BleService.ts   react-native-ble-plx singleton
│   └── utils/ble.ts        GATT UUIDs, payload codecs, permissions
├── android/                native Android project (Gradle wrapper)
└── ios/                    native iOS project (secondary)
```

## BLE / GATT contract

Must match the firmware (`../firm`). Defined in `src/utils/ble.ts`:

| UUID | Name | Props | Payload |
| ---- | ---- | ----- | ------- |
| `d2f2a658-2605-4993-9794-01961b93a091` | Service | — | — |
| `3ec2dc2f-cfdf-49e1-b232-da0dc4a88003` | Level | Read + Notify | `[water:u8, pump:u8, rawTSC:u16 LE]` |
| `4feebce2-e1af-4634-915d-9e62b0756af1` | Pump | Read + Write | `u8` mode: `0=auto, 1=off, 2=on` |

Advertised device name: **`BucketEvac`**.

> ⚠️ The firmware does not expose this custom service **yet** (it currently only
> advertises + DIS). The app will connect but see no bucket characteristics
> until the GATT service is added on the device side.

## Prerequisites

- Node ≥ 18, JDK 17/21, Android SDK + platform-tools (`adb`).
- A **physical device** — BLE does not work in the Android emulator or iOS
  simulator.

## Run on Android (primary)

```sh
cd client
npm install
# plug in the tablet, authorize USB debugging (tap "Allow" on the device)
adb devices                 # should show the device as "device", not "unauthorized"
npm run android             # builds via ./android/gradlew and installs
# in another terminal, if Metro didn't auto-start:
npm start
```

Gradle is driven by the committed **wrapper** (`android/gradlew`); it downloads
the pinned Gradle distribution on demand — nothing large is vendored in git.

## Run on iOS (secondary)

```sh
cd client/ios && pod install && cd ..
npm run ios                 # requires Xcode; free provisioning works for debug
```

## Notes

- Permissions: Android 12+ needs `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT` (requested
  at runtime in `utils/ble.ts`); older Android uses location. iOS uses the
  `NSBluetoothAlwaysUsageDescription` Info.plist string.
- `src/utils/ble.ts` contains a small base64 codec so no extra dependency is
  needed to (de)serialize characteristic values.
