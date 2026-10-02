# Rotational idle

This branch contains two separate rotational-idle uses.

## Normal rotational idle

Normal rotational idle is configured under the Idle/Startup tuning area. It is optional and intended as a user-selected idle behavior.

Typical controls include:
- activation mode (coolant temperature and/or switch)
- minimum coolant temperature
- maximum TPS
- RPM limits
- cut percentage
- switched-input selection/polarity where used

Normal rotational idle can be completely disabled.

## Rotational Idle Protection

A separate submenu exists under **Engine Protection**.

This is independent of normal rotational idle and may activate even when normal rotational idle is disabled.

It is intended as an experimental overheat-protection strategy. It can:
- activate above a configured coolant temperature
- use coolant hysteresis for release
- operate only within configured TPS and RPM limits
- command a configured IAC duty
- apply a rotating cut

In protection mode, skipped events cut **fuel and spark together**.

Existing Speeduino engine-protection and limiter cuts retain priority.

This feature is experimental and is not a substitute for a correctly functioning cooling system or the existing engine-protection features.
