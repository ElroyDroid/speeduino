# v17 - 202501.7-style AVR Flex sensor input

This revision keeps all custom features from v16, including:
- automatic Flex startup/transient table blending
- Flex WUE, cranking, priming, ASE, ASE duration, TPS AE, MAP AE
- Flex cold-AE correction vs CLT
- normal rotational idle
- independent overheat rotational-idle protection
- isolated rotational-idle switch input

Only the AVR Flex sensor pin-reading path has been changed.

## Why

The EverythingFuelInjection Basic ECU works correctly with the 202501.7-era
Speeduino Flex input implementation on pin 2.

Current master moved Flex input reading to `boardInputPin_t` / `fastInputPin_t`.
On AVR, that ultimately uses `port_pin_t`, whose current implementation stores
`portOutputRegister(...)` in the underlying port object.

The 202501.7 code instead explicitly reads the AVR input register using:
- `portInputRegister(digitalPinToPort(pin))`
- `digitalPinToBitMask(pin)`

v17 restores that proven AVR input method for Flex only.

## What remains current-master style

- `pinNumbers.pinFlex` board mapping is retained
- current `initialiseFlexSensor()` structure is retained
- current interrupt attachment is retained
- current ethanol and fuel-temperature calculation is retained
- non-AVR targets retain the current `boardInputPin_t` implementation

## Expected behavior on Mega 2560

For the EverythingFuelInjection Basic ECU / v0.4-style mapping, Flex remains on
pin 2. The ISR now reads the actual AVR PIN/input register directly, as the
202501.7 firmware did.

Firmware/INI signature:
`speeduino 202504-flexrot-v17`
