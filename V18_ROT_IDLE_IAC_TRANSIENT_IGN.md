# v18 custom changes

- Added `Rotational Idle IAC Duty` (0-100%) to normal rotational idle. When normal rotational idle is active and the configured idle type is PWM, the PWM idle valve is forced to this duty. Overheat rotational-idle protection still has priority and retains its separate IAC duty.
- Added a single fuel-independent `Transient Ignition Retard` feature under the main Acceleration Enrichment window. It follows the selected primary AE mode (TPS or MAP), shares that mode's existing four rate bins, and provides four retard values, enable, minimum RPM, and decay time.
- Default transient retard is disabled. Migration defaults: 1500 RPM minimum, 300 ms decay, 0/1/3/5 degrees.
- Retained the v17 AVR Flex input compatibility change and the conditional Flex TPS/MAP AE menu entries.
