# Wiring and commissioning

## Low-voltage side

For the classic ESP32 DevKit/WROOM-32 defaults:

```text
ESP32 3.3 V ───────────── DS18B20 VDD
ESP32 GND ────────────── DS18B20 GND
ESP32 GPIO 4 ─────────── DS18B20 DQ
ESP32 3.3 V ── 4.7 kΩ ── DS18B20 DQ

ESP32 GPIO 25 ────────── compatible SSR logic input / suitable driver
ESP32 GND ────────────── logic-side return (where the input circuit requires it)
```

Check the probe manufacturer's wire identification; colours are not universal. Use external 3-wire power, not parasite power. A 12-bit conversion takes up to 750 ms; firmware requests one per second and discards the first conversion. Missing/invalid readings stop an active session; freshness expires after 3 seconds. A repaired probe becomes readable again on the same bus, but faults stay latched. If it was absent or there were multiple probes at boot, restart/reconfigure with the heater disconnected and exactly one probe attached.

## SSR polarity and internal bias

| Selected polarity | GPIO heating on | GPIO heating off | Internal bias | External reset-time bias |
|---|---|---|---|---|
| Active high (default) | HIGH | LOW | Pull-down | 10 kΩ to GND |
| Active low | LOW | HIGH | Pull-up | 10 kΩ to 3.3 V |

Firmware preloads the off level before enabling the GPIO output, sets the corresponding ESP32 internal pull, and keeps it enabled during operation. These weak pulls are supplementary; they are not a reset-time or hardware isolation guarantee. The external resistor and SSR/driver must provide a fail-off condition when the controller is unpowered. Confirm this with the actual module, particularly for active-low optocoupler boards, which may have their own pull-ups or power-domain interactions. Do not back-feed an unpowered ESP32.

GPIO 25 is a normal output-capable, non-strapping pin. GPIO 4 supports the digital one-wire bus. The pin selector intentionally excludes flash, serial, strapping, PSRAM-associated and input-only pins. Other hardware connected to a selected pin can still create a conflict; the selector cannot detect it.

## Independent protection

The firmware uses a 2-second SSR time window and a 5-second task watchdog. Reboot always returns to stopped operation, but software cannot force a GPIO while the CPU is held in reset. Nor can a GPIO command open an SSR that has failed short. Use a properly designed independent disconnect/contactor, high-temperature limiter, dry-fire protection, protective earth, fuse/breaker and residual-current protection as required for your installation. A physical power isolator must remain reachable.

The temperature probe must be rated for immersion in water/wort and located where it measures bulk liquid, not an isolated pocket or dry kettle wall. The controller has no liquid-level input in this version. Volume is a user-entered estimate, not a measured interlock.

## Commissioning checklist

1. **Disconnect heating power.** Check polarity against the real input/driver with a meter or isolated indicator. Verify the output is off at power-up, reset, USB flashing, dashboard stop, and fault reset.
2. Verify SSR input-current and voltage compatibility with 3.3 V GPIO. Fit the appropriate external off-state bias. Check both ESP32-powered and ESP32-unpowered states.
3. Check probe calibration at known temperatures. Verify the correct probe is attached and exactly one probe is on the bus.
4. With only the logic side connected, Start, Manual, Pause, Resume, Stop and disconnect the probe. Verify actual output shutdown and the latched fault. Test reboot without resuming heat.
5. Check the independent thermal/dry-fire protection and power isolator separately. Software simulation is not a substitute.
6. Fill with water above the element's minimum safe immersion level. Enter the actual volume/power, local boiling point, conservative boil power and session cutoff. Start a supervised low-temperature trial.
7. Observe settling and overshoot during supervised water-only trials. Check probe placement, circulation, SSR temperature/heatsink and session limits. Test controlled interruption and recovery.
8. Only after commissioning should you use automatic stage sequencing. This repository has not been validated on your kettle and is not a certified appliance controller.

