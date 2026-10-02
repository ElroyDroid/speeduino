# v15 Mega 2560 compile fixes

This revision fixes the next PlatformIO errors found in v14.

## TPS acceleration-enrichment migration/prefill

The custom update/migration code still referenced an old/non-existent
`configPage2.taeRates` member.

Current Speeduino stores the TPS acceleration-enrichment calibration on page 4:

- bins: `configPage4.taeBins`
- values: `configPage4.taeValues`

All custom migration and Flex-table prefill references now use
`configPage4.taeValues`, matching `corrections.cpp` and current master.

## Unsigned constrain warnings

The two custom Flex scaling helpers used Arduino's `constrain()` macro with an
unsigned value and a lower bound of zero. That produced harmless but noisy
"comparison of unsigned expression < 0 is always false" warnings.

Those helpers now explicitly clamp only the upper limit to 255.

Firmware/INI signature:
`speeduino 202504-flexrot-v15`
