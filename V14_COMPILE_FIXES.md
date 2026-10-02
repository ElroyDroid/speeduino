# v14 Mega 2560 compile fix

This revision fixes the next AVR/PlatformIO compile error found in v13.

`scheduler_fuel_controller.cpp` passed the volatile `current.ethanolPct` field
directly into `std::min()`. AVR's C++ template deduction rejects the mix of a
volatile byte and a normal `uint8_t`.

The code now:
1. copies `ethanolPct` to a normal local `uint8_t`
2. clamps that local value to 100 with a simple conditional

The source tree was also scanned for any other direct `std::min(...ethanolPct...)`
calls and none remain.

Firmware/INI signature:
`speeduino 202504-flexrot-v14`
