# MotoKey CORE 3.3.0 — Timer START (source preview)

This directory contains the Arduino source for a development version based on CORE 3.2.0 Shared Token.

## Command changes

- START remains unchanged: the authorized BLE client must activate ARM first, and the 15-second ARM window is still enforced by ESP32.
- TIMER_START is a separate, authorized BLE command for the watch countdown path. It does not require manual ARM.
- TIMER_START is accepted only while the ESP32 POWER relay state is already ON and the START relay is not active.
- The command uses the existing persisted pulse duration (500–3000 ms), then the existing firmware loop turns the START relay off automatically.
- The watch sends the timer command once at countdown expiry and must not retry if the BLE acknowledgement is uncertain.

## Build target

- Board: classic ESP32 / ESP32 Dev Module, 4 MB flash
- Arduino-ESP32 core: 3.3.12
- NimBLE-Arduino: 2.5.2
- Flash mode: DIO
- Flash frequency: 80 MHz
- MTU: 64

## Important release status

This is SOURCE ONLY, not a compiled .bin release. It has not yet been compiled or bench-tested on physical hardware. It is intentionally not added to the stable manifest.json and is not installable by the public Flasher feed yet. Compile and validate it on a disconnected/bench setup first. Do not overwrite CORE 3.2.0 or flash it onto a motorcycle before the timer path and relay pulse are verified safely.

The existing NVS namespaces and pairing/token data are preserved by the source. No full flash erase is required by this code change.
