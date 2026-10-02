# v21 - transient ignition retard UI fix

- Fixes the missing TunerStudio curve by placing it in the [CurveEditor] section.
- Uses a 4-point TPSdot vs retard curve.
- Keeps the existing 8-byte EEPROM storage layout to avoid shifting page-15 offsets.
- Firmware uses only the first four points.
- Default points: 0, 200, 500, 1000 %/s -> 0, 2, 4, 5 degrees.
- Signature: speeduino 202504-flexrot-v22
