# v16 Flex input isolation revision

Reason for this revision
------------------------
On the EverythingFuelInjection Basic ECU, the stock current-master firmware reads
the flex sensor correctly on the documented Flex input (pin 2), while the custom
branch showed 0% ethanol and -40 C fuel temperature.

The stock Flex sensor implementation itself (`sensors.cpp`, timer conversion and
the normal board pin mapping) has therefore been left untouched.

Change made
-----------
The custom rotational-idle switch input is no longer stored inside Speeduino's
core `pinNumbers_t` structure.

v15 inserted `pinRotationalIdle` into that core structure immediately before the
stock `pinFlex` member. Even though the current board mappings assign members by
name, changing a central board/pin structure creates unnecessary coupling with
the stock pin system.

v16 restores `pinNumbers_t` to the exact current-master layout and keeps the
rotational-idle switch input in a separate standalone variable:
`rotationalIdleInputPin`.

This means:
- stock `pinFlex` remains in its original structure and original position
- stock board mapping continues to assign the Flex input normally
- stock `initialiseFlexSensor()` is unchanged
- stock Flex interrupt code is unchanged
- rotational idle still supports its configurable switch input
- rotational idle no longer modifies the core board pin structure

Diagnostic interpretation
-------------------------
After flashing v16 and power-cycling:
- If Ethanol % and Fuel Temp return to normal, the custom pin-structure change
  was the regression and this revision isolates it.
- If they remain at 0% / -40 C, the fault lies elsewhere in the custom branch.
  The next step should then be a dedicated raw Flex pulse/frequency diagnostic
  channel rather than modifying the sensor hardware or stock Flex ISR.

Firmware / TunerStudio signature
--------------------------------
speeduino 202504-flexrot-v16
