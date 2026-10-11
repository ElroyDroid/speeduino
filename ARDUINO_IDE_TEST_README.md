# Building and bench-testing this custom Speeduino firmware

This custom branch is intended for a Mega 2560 ECU.

## Recommended build method

Use **VS Code + PlatformIO**, not Arduino IDE.

The untouched current Speeduino master and this custom branch both use C++ standard-library headers (for example `<algorithm>`) that may fail in some Arduino IDE AVR installations. If Arduino IDE reports `algorithm: No such file or directory`, that is a toolchain/environment issue rather than a custom-firmware change.

### PlatformIO build

1. Open the repository root (the folder containing `platformio.ini`) in VS Code.
2. Open the PlatformIO sidebar.
3. Under **Project Tasks**, expand **megaatmega2560**.
4. Run **Build**.
5. A successful build creates:
   `.pio/build/megaatmega2560/firmware.hex`
6. For SpeedyLoader x64, choose **Local Firmware** and select that `firmware.hex`.

## Before connecting to the engine

Bench-test on a spare/test Mega first.

Verify:
- firmware boots and communicates with TunerStudio using the included `speeduino.ini`
- normal Speeduino pin assignments remain unchanged
- all custom Flex and rotational-idle features are disabled after first boot/migration unless explicitly enabled
- settings survive a power cycle
- flex-fuel percentage blends the normal tables toward the Flex endpoint tables
- normal rotational idle exits immediately on TPS/RPM limits
- overheat rotational-idle protection works independently of normal rotational idle
- overheat protection cuts fuel and spark together on skipped events
- existing engine-protection cuts retain priority

Do not connect injectors/coils to a running engine until the Mega build succeeds and the bench checks above pass.
