# v22 - 4-point transient retard compile fix

The v21 compile error occurred because `table2D_u8_u8_4` requires pointers to
actual 4-element arrays, while the EEPROM-compatible calibration storage remains
8 elements long.

v22 keeps the 8-byte arrays in `configPage15` so no EEPROM/page offsets move,
but removes the invalid `table2D_u8_u8_4` constructor and performs linear
interpolation directly on elements 0..3.

The TunerStudio UI remains a 4-point TPSdot vs retard curve.

Signature: speeduino 202504-flexrot-v22
