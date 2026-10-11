# v13 Mega 2560 compile fix

This revision fixes the next PlatformIO Mega 2560 compiler error from v12.

The custom rotational-idle code referenced a non-existent
`CALIBRATION_TEMPERATURE_OFFSET` symbol.

Current Speeduino stores configuration temperatures with its standard offset and
uses the existing helpers in `units.h`:

- `temperatureAddOffset()`
- `temperatureRemoveOffset()`

The rotational-idle minimum CLT and overheat-protection CLT calculations now use
`temperatureRemoveOffset()`, matching the rest of current master.

The same correction was also applied to the overheat IAC logic in `idle.cpp`.

Firmware/INI signature:
`speeduino 202504-flexrot-v13`
