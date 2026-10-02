# v19 transient ignition retard

- Transient ignition retard moved to the Spark menu.
- It now has a dedicated 8-point TPSdot (%/s) vs ignition-retard (deg) curve, independent of fuel AE mode and ethanol content.
- Default curve: 0, 50, 100, 200, 300, 500, 800, 1200 %/s => 0, 0, 1, 2, 3, 4, 5, 5 deg.
- Minimum RPM and decay time remain configurable.
- Existing rotational-idle forced PWM duty and all v17/v18 Flex features remain.
