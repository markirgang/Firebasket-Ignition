# ESP32 32E Relay Board - Firebasket Ignition System Firmware

Automatic firebasket ignition firmware designed for ESP32 32E Wroom Relay Boards. Operates on system power-up, executes a safety-checked ignition sequence with propane gas pre-flow, high-voltage spark control, synchronized spa/pool light interruption pulses, AD8495 K-type thermocouple analog flame sensing, dedicated Relay 6 flame indicator energization, 30-second purge delays, and an automatic 5-attempt (1 initial + 4 retries) safety lockout.

---

## ⚡ Hardware Wiring & Pinout Guide

| ESP32 Pin | Connected Component | Relay # | Logic Level | Description |
|---|---|---|---|---|
| **GPIO 32** | Gas Solenoid Valve Relay | **Relay 1** | Active HIGH (`+3.3V`) | Closes/opens spring-loaded ball valve for propane gas flow |
| **GPIO 33** | Piezo Beeper Relay | **Relay 2** | Active HIGH (`+3.3V`) | Warning beeper audio output relay |
| **GPIO 25** | HV Spark Ignitor Relay | **Relay 3** | Active HIGH (`+3.3V`) | High-voltage spark ignition driver relay |
| **GPIO 26** | Spa Light Relay | **Relay 4** | Active HIGH (`+3.3V`) | Spa light control relay (pulsed in sync with Relay 2) |
| **GPIO 27** | Pool Light Relay | **Relay 5** | Active HIGH (`+3.3V`) | Pool light control relay (pulsed in sync with Relay 2) |
| **GPIO 12** | Flame Detected Relay | **Relay 6** | Active HIGH (`+3.3V`) | Energized whenever flame is detected by thermocouple |
| **GPIO 13** | *Unused* | Relay 7 | - | Unused in this project |
| **GPIO 23** | *Unused* | Relay 8 | - | Unused in this project |
| **GPIO 34** | AD8495 Analog OUT Pin | - | `0 - 3.3V` Analog | Analog output from AD8495 K-Type Thermocouple Amplifier (ADC1_CH6) |
| **GPIO 2** | Onboard Status LED | - | Active HIGH | Visual system running state indicator |
| **GND** | System Ground | - | Ground (`0V`) | Common ground across relay module, AD8495 & sensors |
| **5V / VIN** | System Power Input | - | `5V DC` | Power supply input (System boots & runs on power-up) |

> 📌 **Note on eMariete Board & AD8495 Wiring**: This firmware is configured for the **eMariete ESP32-WROOM-32E** 8-relay board. Relays 1 through 6 use GPIOs 32, 33, 25, 26, 27, and 12. Relays 7 and 8 are unused. The AD8495 Thermocouple output is connected to input-only pin **GPIO 34** (ADC1_CH6) for analog temperature sensing.

---

## ⏱️ Ignition Sequence Breakdown & Timing

```
                       Power Applied (Boot)
                                │
                                ▼
         ┌──────────────────────────────────────────────┐
         │ 1. Gas Pre-Flow & Initial Beeping (10 Sec)   │
         │  • Relay 1 (Gas Valve) CLOSED -> Propane ON  │
         │  • Relay 2 (Beeper) pulses 1x/sec (10 times) │
         │  • Relays 4 & 5 (Spa/Pool Lights) pulse in   │
         │    sync with Relay 2 to interrupt lights    │
         └──────────────────────┬───────────────────────┘
                                │
                                ▼
         ┌──────────────────────────────────────────────┐
         │ 2. Spark Ignition & Rapid Pulse (10 Sec Max) │
         │  • Relay 3 (Spark Ignitor) CLOSED -> Spark ON│
         │  • Relay 1 (Gas Valve) stays CLOSED          │
         │  • Relay 2 (Beeper) pulses 2x/sec (2 Hz)     │
         │  • Relays 4 & 5 pulse 2x/sec (2 Hz) in sync  │
         │  • AD8495 Thermocouple monitored continuously│
         └──────────────┬───────────────┬───────────────┘
                        │               │
      (Flame Detected)  │               │  (No Flame after 10s)
                        ▼               ▼
     ┌───────────────────────┐    ┌───────────────────────────────────┐
     │ 3. RUNNING STATE      │    │ 4. SAFETY PURGE DELAY (30 Sec)    │
     │  • Relay 3 (Spark) OFF│    │  • Relays 1, 2, 3, 4, 5, 6 ALL OFF│
     │  • Relays 2,4,5 OFF   │    │  • Wait 30s safety purge delay    │
     │  • Relay 1 (Gas) ON   │    │  • Retry sequence (Up to 5 total) │
     │  • Relay 6 (Flame) ON │    └─────────────────┬─────────────────┘
     └───────────────────────┘                      │
                                         (5th Attempt Fails)
                                                    │
                                                    ▼
                                          ┌──────────────────┐
                                          │ 5. LOCKOUT STATE │
                                          │  • All relays OFF│
                                          │  • Halt sequence │
                                          │  • Reboot needed │
                                          └──────────────────┘
```

### 1. Gas Pre-Flow & Initial Beeping (10 Seconds)
- Power applied -> ESP32 boots up.
- **Relay 1 (Gas Valve)** closes (turns ON), opening the spring-loaded ball valve to allow propane gas flow.
- Simultaneously, **Relay 2 (Beeper)** pulses ON/OFF once per second (1 Hz) 10 times (500ms ON / 500ms OFF).
- For every ON pulse of Relay 2, **Relay 4 (Spa Light)** and **Relay 5 (Pool Light)** also pulse ON/OFF simultaneously, interrupting the spa and pool lights.

### 2. Spark Ignition & Rapid Pulse (10 Seconds Max)
- After 10 beeps are complete, **Relay 3 (Spark Ignitor)** closes (turns ON) to energize spark ignition.
- Relay 1 remains CLOSED (gas ON).
- Simultaneously, **Relay 2 (Beeper)** pulses ON/OFF twice per second (2 Hz: 250ms ON / 250ms OFF).
- **Relays 4 & 5 (Spa & Pool Lights)** pulse ON/OFF at 2 Hz in sync with Relay 2.
- Continuous analog monitoring of AD8495 K-Type Thermocouple during this 10-second window.

### 3. Flame Sensor Outcomes & Relay 6 Control
- **Flame Detected (Temp ≥ 150°C)**:
  - Relay 3 (Ignitor) turns OFF immediately. Relays 2, 4, 5 turn OFF.
  - Relay 1 (Gas Valve) remains ON in **RUNNING** state.
  - **Relay 6 (Flame Relay)** closes (turns ON) to signal active flame detection.
  - (If temperature drops below 130°C hysteresis during operation, flame loss is triggered, Relay 6 de-energizes, and gas valve closes instantly for safety).
- **No Flame Detected**: If temperature stays below 150°C after 10 seconds, Relay 1 (Gas Valve), Relay 3 (Ignitor), Relay 2 (Beeper), Relays 4 & 5 (Lights), and Relay 6 (Flame Relay) turn OFF.


### 4. Retries (30-Second Purge) & 5-Attempt Lockout
- System waits **30 seconds** with all relays OFF (Safety Purge Delay).
- The sequence repeats after 30 seconds and repeats 4 more times (**5 total attempts**: 1 initial + 4 retries).
- If all 5 attempts fail to detect a flame, the system enters **LOCKOUT** state (all relays OFF) and will not attempt ignition again until power is removed and restored to reboot the ESP32.

---

## 🛠️ How to Build and Upload

### External CH340 USB-TTL Wiring Setup
When programming an ESP32-WROOM-32E module using an external CH340 USB-TTL adapter:

| CH340 Pin | ESP32 Pin |
|---|---|
| **TXD** | **RXD0 (GPIO 3)** |
| **RXD** | **TXD0 (GPIO 1)** |
| **GND** | **GND** |
| **5V / 3.3V** | **VIN / 3V3** |

### Bootloader Mode Entry (Manual Flash Procedure)
If auto-reset is not present on your CH340 adapter breakout:
1. Connect CH340 to your PC via USB.
2. Press and hold the **BOOT (IO0)** button on the ESP32 board.
3. Press and release the **RESET (EN)** button while keeping BOOT held down.
4. Release the **BOOT** button. The ESP32 is now in flashing mode.
5. Click **Upload** in PlatformIO or Arduino IDE.

### PlatformIO (Recommended)
1. Open project folder in Visual Studio Code with PlatformIO extension.
2. Click **Build** (Checkmark) and **Upload** (Arrow icon).
3. Open Serial Monitor at **115200 baud** to view real-time diagnostics logs.

### Arduino IDE
1. Open `src/main.cpp` or copy its content into Arduino IDE.
2. Select Board: **ESP32 Dev Module**.
3. Select Port: (Your CH340 COM port).
4. Click **Upload**.
