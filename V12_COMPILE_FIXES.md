# v12 Mega 2560 compile fixes

This revision addresses the first real PlatformIO Mega 2560 compile errors found in v11:

1. `FlexTAETable` incorrectly referenced `configPage2.taeBins`.
   The normal TPS acceleration-enrichment bins live in `configPage4`, so the Flex TPS AE endpoint now correctly shares `configPage4.taeBins`.

2. `currentStatus.ethanolPct` is volatile.
   Passing it directly to `std::min()` caused AVR template type deduction to fail. The code now copies the volatile value to a normal `uint8_t` first and clamps it with a simple conditional.

The custom firmware and TunerStudio INI signature is now:
`speeduino 202504-flexrot-v12`
