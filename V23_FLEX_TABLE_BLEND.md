# v23 - Flex VE / Spark table blending

Reference concept: Speeduino PR #1110 Flex Fuel Table Blending.

This revision ports only the secondary VE/spark blending concept into the current v22 codebase.
VVT code, VVT EEPROM fields, PID code and VVT tables are unchanged.

## New mode
Fuel Table 2 and Spark Table 2 now have a `Flex Blend` mode.

The existing secondary-table switch threshold storage is reused as `100% Table 2 at ethanol` so no EEPROM bytes or page offsets move.

Example with Full Table 2 = E70:
- E0 = 0% Table 2
- E35 = 50% Table 2
- E60 = ~86% Table 2
- E70+ = 100% Table 2

Fuel Flex correction remains active.
Legacy Flex Timing Advance is automatically disabled whenever Spark Table 2 is in Flex Blend mode.
Spark tables are blended first and ignition corrections are then applied once to the blended result.

Recommended initial settings for this tune:
- Fuel Table 2 mode: Flex Blend
- Fuel full Table 2: 70% ethanol
- Spark Table 2 mode: Flex Blend
- Spark full Table 2: 70% ethanol

Signature: speeduino 202504-flexrot-v23
