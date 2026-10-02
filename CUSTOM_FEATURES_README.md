# Custom Speeduino features in this branch

This branch is based on the user's supplied current Speeduino master and adds two groups of features:

1. Flex-fuel startup/transient blending
2. Rotational idle, including independent overheat rotational-idle protection

The normal Speeduino pin mappings and existing feature pin selections are unchanged.

## Flex-fuel startup and transient blending

When the normal Speeduino Flex Fuel feature is enabled, measured ethanol percentage automatically blends each normal calibration toward its matching **Flex endpoint** calibration.

- 0% ethanol: normal table/value only
- 50% ethanol: halfway between normal and Flex endpoint
- 85% ethanol: 85% of the way toward the Flex endpoint
- 100% ethanol: Flex endpoint

There is no separate startup/transient blending enable switch.

### Flex endpoint calibrations

TunerStudio exposes separate Flex calibrations for:
- Warmup enrichment (WUE)
- Cranking enrichment
- Priming pulsewidth
- Afterstart enrichment (ASE)
- ASE duration
- TPS acceleration enrichment
- MAP acceleration enrichment
- Cold acceleration-enrichment correction vs CLT

The Flex tables are prefilled with conservative starting values derived from the normal/petrol calibrations during EEPROM migration. They are starting points only and still require tuning for the engine and fuel.

## Normal rotational idle

Normal rotational idle remains under the Idle/Startup area and can be configured independently.

It supports:
- activation by coolant-temperature condition and/or switched input
- maximum TPS
- RPM limits
- rotating cut percentage/pattern
- configured cut behavior
- immediate exit when the activation conditions are no longer met

## Overheat rotational-idle protection

Engine Protection has a separate **Rotational Idle Protection** submenu.

This protection mode is independent of normal rotational idle. It can activate even when normal rotational idle is disabled.

It has its own:
- enable
- activation coolant temperature
- coolant hysteresis
- TPS limit
- RPM window
- IAC duty
- rotating cut percentage

For overheat protection, skipped events cut **fuel and ignition together** so the selected cylinders pump air rather than raw fuel into the exhaust.

Existing higher-priority Speeduino engine-protection/limiter cuts remain dominant.

## TunerStudio

Use the `speeduino.ini` included with this exact firmware revision. The custom firmware and INI signatures must match.

## Safety status

This branch should be treated as a bench-test candidate until:
- the `megaatmega2560` PlatformIO build succeeds
- relevant unit tests pass
- the firmware is exercised on a test Mega
- injector/ignition output behavior is verified before engine use
