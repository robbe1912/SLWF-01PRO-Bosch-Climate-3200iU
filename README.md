# 3200iESPHome

ESPHome external component for **Bosch Climate 3200iU** (Midea OEM) air conditioners with **SLWF-01PRO** v2.1 WiFi dongles.

Extends the upstream ESPHome Midea climate component with **ionizer control**, **display mute toggle**, and **on/off timers** — features the Bosch 3200iU supports via Midea UART protocol but which the upstream component doesn't expose.

## Features

| Feature | Upstream | This Component |
|---------|----------|---------------|
| Climate (modes, fan, swing, presets) | ✅ | ✅ |
| Outdoor temperature sensor | ✅ | ✅ |
| Power on/off/toggle | ✅ | ✅ |
| Beeper feedback | ✅ | ✅ |
| **Ionizer (Anion) switch** | ❌ | ✅ |
| **Display mute toggle** | ❌ (capability-gated) | ✅ (bypassed) |
| **Timer ON (0–24h, 15min steps)** | ❌ | ✅ |
| **Timer OFF (0–24h, 15min steps)** | ❌ | ✅ |

## Hardware

- **Dongle**: SLWF-01PRO v2.1 (ESP8266 / ESP12F)
- **AC**: Bosch Climate 3200iU (multi-split or single)
- **Protocol**: Midea UART, 9600 baud, TX=GPIO12 RX=GPIO14 (software UART)
- **Also works with**: Any Midea-OEM AC that supports anion/ionizer via byte 9 bit 5

## Installation

Add this component as an external component in your ESPHome YAML:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USERNAME/3200iESPHome
      ref: main
    components: [midea]
```

Or use a local path during development:

```yaml
external_components:
  - source:
      type: local
      path: ./3200iESPHome/components
```

## Configuration

### Minimal Example

```yaml
esp8266:
  board: esp12e

uart:
  tx_pin: 12
  rx_pin: 14
  baud_rate: 9600

climate:
  - platform: midea
    name: "Bosch AC"
    autoconf: true
    beeper: false
    outdoor_temperature:
      name: "Outdoor Temperature"
    ionizer:
      name: "Ionizer"
    mute:
      name: "Display Mute"
    timer_on:
      name: "Timer ON"
    timer_off:
      name: "Timer OFF"
```

### Full Example with WiFi, API, OTA

See [`yaml-examples/`](yaml-examples/) for complete device configurations.

### New Configuration Keys

All new keys go under the `climate:` platform: midea block.

| Key | Type | Description |
|-----|------|-------------|
| `ionizer` | switch | Ionizer (Anion) on/off switch entity |
| `mute` | switch | Display mute toggle (read: on=muted, toggle-only write) |
| `timer_on` | number | ON timer in minutes (0–1440, step 15). 0 = disabled |
| `timer_off` | number | OFF timer in minutes (0–1440, step 15). 0 = disabled |

## How It Works

### Ionizer

Reads/writes Midea 0xC0 status frame byte 9 bit 5 (`0x20`). The capability report (0xB5 ID `0x021E` = anion) must be present in the AC's capabilities for this to work.

### Display Mute

The Bosch 3200iU lacks capability `0x0224` (light control), causing the upstream ESPHome component to block `display_toggle`. This component bypasses the capability check and sends the `DisplayToggleData` command (`0x41 0x61`) directly.

State is read from 0xC0 byte 14 mask `0x70` — `0x70` = muted, `0x00` = normal.

**Note**: The protocol only supports toggle, not direct on/off. If the current state already matches the desired state, no command is sent.

### Timers

Timer encoding: `byte = 0x7F + floor(minutes / 15)`. `0x7F` = no timer.

Byte 4 = ON timer, byte 5 = OFF timer, byte 6 bit 4 (`0x10`) = timer active flag.

## Multi-Split Limitations

When used in a Bosch multi-split setup (multiple indoor units on one outdoor unit):

- ❌ Silent mode (reduces indoor + outdoor noise)
- ❌ iClean (self-clean)
- ❌ Wind Avoid Me
- ❌ Save/Energy mode
- ❌ Power control (100%/75%/50%)

These are hardware limitations, not software. The remote and AC firmware do not support these in multi-split mode.

## Troubleshooting

### Commands don't work (AC doesn't respond)

ESPHome 2026.5.1 changed Midea protocol byte 8 from `0x03` to `0x00`. Some AC models only respond to `0x03`. If sensors work but commands don't:

1. Try downgrading ESPHome to 2024.4.2 (factory firmware version)
2. Monitor UART traffic with the sniffer firmware to confirm

### WDT crashes

Do NOT use `remote_transmitter` with software UART (GPIO12/14). ESPHome issue #9709 — `InterruptLock` blocks software UART interrupts. The v2.1 dongle uses software UART.

### `enable_serial: true`

ESPHome 2026.x PR #12736 excluded the `Serial` object when `logger: baud_rate: 0`. Add to your `esp8266:` block:

```yaml
esp8266:
  board: esp12e
  enable_serial: true
```

## Protocol Reference

See [`PROTOCOL.md`](PROTOCOL.md) for the complete Midea UART protocol documentation, including frame format, status byte mapping, capability IDs, and timer encoding.

## Credits

- [dudanov/MideaUART](https://github.com/dudanov/MideaUART) — Midea UART protocol library (used by ESPHome)
- [ESPHome](https://esphome.io/) — ESPHome framework and upstream Midea component
- [SMLIGHT](https://smlight.tech/) — SLWF-01PRO hardware manufacturer
- [parkghost/esphome](https://github.com/parkghost/esphome) — Reference for display light switch via UART

## License

MIT
