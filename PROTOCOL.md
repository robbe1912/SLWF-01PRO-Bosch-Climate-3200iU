# Bosch Climate 3200iU — Midea UART Protocol Reference

Reverse-engineered protocol for Bosch Climate 3200iU indoor units connected via
SLWF-01PRO r2.1 dongles (ESP8266, Midea UART 9600 baud, TX=GPIO12, RX=GPIO14).

---

## Hardware

| Component | Model |
|-----------|-------|
| Outdoor Unit | Bosch CL 5000M 79/3 (7.9kW) |
| Indoor Units | 3x Bosch Climate 3200iU W 26 E (2.6kW) |
| Dongles | 3x SLWF-01PRO r2.1 (ESP12F / ESP8266) |
| Setup | Multi-split: 1 outdoor, 3 indoor, 1 dongle per indoor |
| Protocol | Midea UART 9600 baud (software UART on GPIO12/14) |

---

## Midea UART Frame Structure

```
[HEADER][LEN][APP][SYNC][0x00 x4][PROTO][TYPE][DATA...][CRC8][CHECKSUM]
  0xAA         0xAC                                         poly 0x854
```

| Offset | Name | Value |
|--------|------|-------|
| 0 | HEADER | `0xAA` |
| 1 | LENGTH | Total frame length |
| 2 | APPTYPE | `0xAC` (Air Conditioner) |
| 3 | SYNC | `0x00` |
| 4-7 | RESERVED | `0x00` x4 |
| 8 | PROTOCOL | `0x00` (queries), `0x02` (AC responses) |
| 9 | FRAMETYPE | See below |
| 10+ | DATA | Variable payload |
| -2 | CRC8 | Over bytes 10 to N-3, polynomial 0x854 |
| -1 | CHECKSUM | `256 - sum(bytes 1 to N-2)` |

### Frame Types

| Type | Name | Description |
|------|------|-------------|
| `0x02` | DEVICE_RESPONSE | Command acknowledgment |
| `0x03` | DEVICE_QUERY | Query (data ID at offset 10) |
| `0x04` | NET_STATUS | Network status (periodic, ~2min) |
| `0x05` | PARAMS_NOTIFY | Change notification (requires echo) |
| `0x07` | ELECTRONIC_ID | Device identification |
| `0x63` | HEARTBEAT | Keep-alive, no useful data |

### Data IDs (for type 0x03 queries)

| ID | Description |
|----|-------------|
| `0xC0` | Main status (all operational data) |
| `0xB5` | Capabilities report (paged) |
| `0xC1` | Power usage |
| `0xA1` | Temperature data |

---

## 0xC0 Status Response — Complete Byte Map

Query frame (34 bytes):
```
AA 21 AC 00 00 00 00 00 00 03 41 81 00 FF 03 FF 00 02 00x12 03 <ID> <CRC> <CHK>
```

Response data payload (offset from `0xC0` byte):

```
Byte  Bit/Mask    Meaning                       Encoding
───────────────────────────────────────────────────────────────────────
 0    -           Data ID                       Always 0xC0
 1    0x01        Power ON                      0=OFF, 1=ON
 1    0x40        Beep on command               (write-only, no getBeeper)
 1    0x10        Timer mode                    Flag
 2    0xE0        AC Mode                       001=COOL, 010=DRY, 011=HEAT,
                                               100=FAN_ONLY, 101=AUTO/HEAT_COOL
 2    0x10        Temperature decimal            Bit 4
 2    0x0F        Temperature setpoint           Value + 16°C (1=17°C, 5=21°C)
 3    full byte   Fan speed                     Direct percentage: 5, 20, 60, 85, 100
 4    full byte   ON timer                      0x7F = none, else: 0x7F + minutes/15
 5    full byte   OFF timer                     0x7F = none, else: 0x7F + minutes/15
 6    0x10        Timer active flag              1=timer running (inconsistent)
 6    0x20        Unknown                       Observed in some captures
 7    full byte   Swing mode                    0x00=OFF, 0x3C=VERTICAL,
                                               0x33=HORIZONTAL, 0x3F=BOTH
 8    0x80        Follow Me (UART)              Requires UART temp sending
 8    0x40        Power saver
 8    0x20        Turbo
 8    0x10        Low frequency fan
 8    0x08        Save mode
 9    0x80        ECO mode (write)              Different bit from read
 9    0x20        Ionizer / Anion               0=OFF, 1=ON
 9    0x10        ECO mode (read)               Different bit from write
 9    0x02        Natural fan
10    0x04        Temperature unit              0=°C, 1=°F
10    0x02        Turbo mode flag
10    0x01        Sleep mode flag
11    full byte   Indoor temperature            (value - 50) / 2 + decimal from byte 15
12    full byte   Outdoor temperature           (value - 50) / 2 + decimal from byte 15
13    -           Unknown                       Always 0x09 in recent captures
14    0x70        Mute / Display state          0x00=normal, 0x70=mute (display+beeper off)
15    low nibble  Indoor temp decimal           (value & 0x0F) / 10 °C
15    high nibble Outdoor temp decimal          (value >> 4) / 10 °C
19    0x7F        Humidity setpoint             NOT available on Bosch 3200iU
21    0x80        Frost protection              Flag
```

### Byte Examples (from sniffer captures)

**Baseline (FAN mode, no timer, ionizer OFF):**
```
C0 01 A5 14 7F 7F 00 00 00 00 00 60 58 09 00 02
```
- Power=ON, Mode=FAN_ONLY, Temp=21°C, Fan=20%, Timers=none, Ionizer=OFF
- Indoor=(96-50)/2=23°C, Outdoor=(88-50)/2=19°C

**Ionizer ON (DRY mode):**
```
C0 01 A4 14 7F 7F 00 00 00 20 00 62 5B 08 00 83
```
- Byte 9 = 0x20 = ionizer ON

**Timer 1h (DRY mode):**
```
C0 01 41 14 83 7F 10 00 00 00 00 63 5B 04 00 85
```
- Byte 4 = 0x83 = 0x7F + 4 = 60min = 1h
- Byte 6 = 0x10 = timer active flag

**Vertical swing (FAN mode):**
```
C0 01 A5 14 85 7F 20 3C 00 00 00 5F 58 09 00 49
```
- Byte 7 = 0x3C = vertical swing

---

## Timer Encoding

**Formula: `timer_byte = 0x7F + floor(total_minutes / 15)`**

| Timer | byte | Calculation |
|-------|------|-------------|
| OFF | 0x7F | — |
| 0.5h | 0x81 | 0x7F + 2 |
| 1h | 0x83 | 0x7F + 4 |
| 1.5h | 0x85 | 0x7F + 6 |
| 4h | 0x8F | 0x7F + 16 |
| 24h | 0xDF | 0x7F + 96 |

- Resolution: 15 minutes (remote restricts to 30 min steps)
- Range: 15 min (0x80) to 24h (0xDF)
- Byte 4 = ON timer, Byte 5 = OFF timer
- Byte 6 bit 4 (0x10) = timer active flag (not always consistent)
- **Note:** dudanov/MideaUART formula (`byte >> 4` = hours) is WRONG for Bosch

### Decode/Encode

```cpp
uint8_t encodeTimer(uint16_t minutes) {
    if (minutes == 0) return 0x7F;
    return 0x7F + (minutes / 15);
}

uint16_t decodeTimer(uint8_t byte) {
    if (byte == 0x7F) return 0;
    return (byte - 0x7F) * 15;
}
```

---

## Control Commands

### Ionizer — 0x40 Control Frame

**Write:** Set byte 9 bit 5 (0x20) in a standard 0x40 control frame.

Base frame (24 bytes):
```cpp
{0x40, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00}
```

Set ionizer: `m_setMask(9, state, 0x20)` — sets or clears bit 5 of byte 9.

### Mute / Display Toggle — 0x41 0x61 Special Command

**Toggle-only** — no direct ON/OFF. Frame:
```cpp
{0x41, 0x61, 0x00, 0xFF, 0x02, 0x00, 0x02, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, <random_byte>}
```
+ CRC8 + checksum appended.

**Read state** from 0xC0 byte 14 mask 0x70:
- `0x00` = display ON (normal mode)
- `0x70` = display OFF (mute mode)

**Implementation pattern:** Read current state → if different from desired → send toggle → update local state.

### Fan Speed — 0x40 Control Frame

Byte 3 = direct percentage (5-100). No lookup needed.

### Swing Mode — 0x40 Control Frame

Byte 7 values:
- `0x00` = OFF (static)
- `0x3C` = VERTICAL
- `0x33` = HORIZONTAL
- `0x3F` = BOTH

---

## Capabilities (0xB5 Response)

Format: 16-bit little-endian IDs, 4 bytes per entry: `[id_lo] [id_hi] [type] [data...]`

### Bosch 3200iU Capabilities (Confirmed)

**Page 07:**
| ID | Name | Supported |
|----|------|-----------|
| 0x0212 | Eco Preset | YES |
| 0x0214 | All Modes | YES |
| 0x0215 | V+H Swing | YES |
| 0x0216 | Power Calibration | NO (state=00) |
| 0x021A | Turbo Preset | YES |
| 0x0210 | Fan Speed | YES |
| 0x0225 | Temperature Ranges | YES (multi-byte) |

**Page 08:**
| ID | Name | Supported |
|----|------|-----------|
| 0x021E | Anion / Ionizer | YES |
| 0x0213 | Frost Protection | YES |
| 0x0222 | °C/°F Switchable | NO (state=00) |
| 0x0219 | Aux Electric Heating | NO (state=00) |
| 0x0039 | iClean | NO (multi-split) |
| 0x0042 | One Key No Wind | NO |
| 0x0009 | Unknown | YES |
| 0x000A | Unknown | YES |

**NOT present (explains broken features):**
| ID | Name | Impact |
|----|------|--------|
| 0x0224 | Light Control | ESPHome blocks display_toggle |
| 0x022C | Buzzer/Beeper | ESPHome blocks beeper_on/off |

---

## Features NOT in 0xC0 Status (IR-Only)

These features change nothing in the UART status response. They are controlled
exclusively via IR from the remote:

- **Fixed vane position** (top/middle/bottom) — swing ON/OFF is in 0xC0, but discrete positions are not
- **"Avoid middle"** — unknown feature, not reflected in UART
- **Follow Me (IR version)** — remote sends temperature via IR; UART Follow Me is separate (byte 8 bit 7)

---

## ESPHome Midea Component Limitations (for Bosch 3200iU)

### Why Beeper and Display Toggle Don't Work

ESPHome's `do_display_toggle()` checks `supportLightControl()` before sending:
```cpp
void AirConditioner::do_display_toggle() {
  if (this->base_.getCapabilities().supportLightControl()) {
    this->base_.displayToggle();  // UART — works but never reached
  } else {
    // IR fallback — requires remote_transmitter (removed, causes crash)
  }
}
```

Bosch 3200iU lacks capability 0x0224 → `supportLightControl()` returns false → UART toggle never sent → falls through to IR → no remote_transmitter → nothing happens.

Same for beeper: capability 0x022C absent → beeper commands blocked.

**Fix:** Bypass capability check, send DisplayToggleData (0x41 0x61) directly.

### Other ESPHome Issues

- **Protocol byte 8:** ESPHome 2026.5.1 sends `0x00`, AC responds with `0x02`. Factory firmware (2024.4.2) sends `0x03`. May cause command failures.
- **Timer:** Not exposed in ESPHome Midea component at all.
- **Ionizer:** Not exposed in ESPHome Midea component.
- **Humidity/power_usage:** Hardware has sensors but firmware doesn't expose via Midea UART (issue #6308).

---

## Multi-Split Limitations (Bosch Manual Confirmed)

NOT available on multi-split (indoor unit firmware rejects these):
- Silent mode (reduces indoor + outdoor noise)
- iClean (self-clean cycle)
- Wind Avoid Me (indirect airflow)
- Save/Energy mode (power reduction)
- Power control (100%/75%/50%)

---

## File Structure

```
ESPHome/
├── secrets.yaml              # WiFi, OTA, API keys
├── common.yaml               # v5 shared config (273 lines)
├── bosch-ac-unit{1,2,3}.yaml # Device configs (21 lines each)
├── bosch-ac-sniffer.yaml     # Protocol sniffer (v2, active queries)
├── midea_helpers.h           # CRC8-854 + checksum + hex helpers
├── SETUP_GUIDE.md            # OTA flashing instructions
├── PROTOCOL.md               # This file
├── bin/
│   ├── bosch-ac-unit{1,2,3}.bin  # v5 production firmware (530KB)
│   └── bosch-ac-sniffer.bin      # v2 sniffer firmware (492KB)
└── .esphome/                 # ESPHome build cache
```

---

## Sources

- dudanov/MideaUART (v1.1.9) — C++ Midea UART library used by ESPHome
- reneklootwijk/midea-uart — Python Midea UART library
- mill1000/midea-msmart — Python Midea protocol implementation
- ESPHome midea component (2026.5.1, commit bf62124)
- parkghost/esphome — Display light control via UART (reference implementation)
- Bosch Climate 3200iU Operations Manual (6721894177, 2025/03)
- ESPHome issues: #9709 (WDT crash), #16601 (byte 8 protocol), #12736 (Serial exclusion)
