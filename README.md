# ESP32 32E Relay Board - Firebasket Ignition System Firmware

Automatic firebasket ignition firmware designed for ESP32 32E Wroom Relay Boards. Operates on system power-up, executes a safety-checked ignition sequence with propane gas pre-flow, high-voltage spark control, synchronized burner LED pulses, AD8495 K-type thermocouple analog flame sensing, Relay 4 flame detect indicator light energization, 30-second purge delays, and an automatic 5-attempt (1 initial + 4 retries) safety lockout.

---

## ⚡ Hardware Wiring & Pinout Guide

| ESP32 Pin | Connected Component | Relay # | Logic Level | Description |
|---|---|---|---|---|
| **GPIO 32** | Gas Solenoid Valve Relay | **Relay 1** | Active HIGH (`+3.3V`) | Closes/opens spring-loaded ball valve for propane gas flow |
| **GPIO 33** | Piezo Beeper Speaker Relay | **Relay 2** | Active HIGH (`+3.3V`) | Warning beeper speaker audio output relay |
| **GPIO 25** | HV Spark Ignitor Relay | **Relay 3** | Active HIGH (`+3.3V`) | High-voltage spark ignition driver relay |
| **GPIO 26** | Flame Detect Indicator Light Relay | **Relay 4** | Active HIGH (`+3.3V`) | Energized whenever flame is detected by thermocouple |
| **GPIO 27** | Burner LED Relay | **Relay 5** | Active HIGH (`+3.3V`) | Burner LED relay (pulsed in sync with Relay 2) |
| **GPIO 12** | *Unused (for now)* | Relay 6 | - | Unused for now |
| **GPIO 13** | *Unused* | Relay 7 | - | Unused in this project |
| **GPIO 23** | *Unused* | Relay 8 | - | Unused in this project |
| **GPIO 34** | AD8495 Analog OUT Pin | - | `0 - 3.3V` Analog | Analog output from AD8495 K-Type Thermocouple Amplifier (ADC1_CH6) |
| **GPIO 2** | Onboard Status LED | - | Active HIGH | Visual system running state indicator |
| **GND** | System Ground | - | Ground (`0V`) | Common ground across relay module, AD8495 & sensors |
| **5V / VIN** | System Power Input | - | `5V DC` | Power supply input (System boots & runs on power-up) |

> 📌 **Note on eMariete Board & AD8495 Wiring**: This firmware is configured for the **eMariete ESP32-WROOM-32E** 8-relay board. Relays 1 through 5 use GPIOs 32, 33, 25, 26, and 27. Relays 6, 7 and 8 are currently unused. The AD8495 Thermocouple output is connected to input-only pin **GPIO 34** (ADC1_CH6) for analog temperature sensing.

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
         │  • Relay 5 (Burner LED) pulses in       │
         │    sync with Relay 2                         │
         └──────────────────────┬───────────────────────┘
                                │
                                ▼
         ┌──────────────────────────────────────────────┐
         │ 2. Spark Ignition & Rapid Pulse (10 Sec Max) │
         │  • Relay 3 (Spark Ignitor) pulses 50% duty  │
         │  • Relay 1 (Gas Valve) stays CLOSED          │
         │  • Relay 2 (Beeper) pulses 2x/sec (2 Hz)     │
         │  • Relay 5 (Burner LED) pulses 2x/sec   │
         │  • AD8495 Thermocouple monitored continuously│
         └──────────────┬───────────────┬───────────────┘
                        │               │
      (Flame Detected)  │               │  (No Flame after 10s)
                        ▼               ▼
     ┌────────────────────────┐   ┌───────────────────────────────────┐
     │ 3. RUNNING STATE       │   │ 4. SAFETY PURGE DELAY (30 Sec)    │
     │  • Relay 3 (Spark) OFF │   │  • Relays 1, 2, 3, 4, 5, 6 ALL OFF│
     │  • Relay 2 (Beeper) OFF│   │  • Wait 30s safety purge delay    │
     │  • Relay 1 (Gas) ON    │   │  • Retry sequence (Up to 5 total) │
     │  • Relay 4 (Flame) ON  │   └─────────────────┬─────────────────┘
     │  • Relay 5 (Light) ON  │                     │
     └────────────────────────┘          (5th Attempt Fails)
                                                    │
                                                    ▼
                                          ┌──────────────────┐
                                          │ 5. LOCKOUT STATE │
                                          │  • Relay 5 ON    │
                                          │  • All others OFF│
                                          │  • Reboot needed │
                                          └──────────────────┘
```

### 1. Gas Pre-Flow & Initial Beeping (10 Seconds)
- Power applied -> ESP32 boots up.
- **Relay 1 (Gas Valve)** closes (turns ON), opening the spring-loaded ball valve to allow propane gas flow.
- Simultaneously, **Relay 2 (Piezo Beeper Speaker)** pulses ON/OFF once per second (1 Hz) 10 times (500ms ON / 500ms OFF).
- For every ON pulse of Relay 2, **Relay 5 (Burner LED)** also pulses ON/OFF simultaneously.

### 2. Spark Ignition & Rapid Pulse (10 Seconds Max)
- After 10 beeps are complete, **Relay 3 (Spark Ignitor)** pulses at **50% duty cycle** (500ms ON / 500ms OFF) for up to 10 seconds instead of staying continuously energized.
- Relay 1 remains CLOSED (gas ON).
- Simultaneously, **Relay 2 (Beeper Speaker)** pulses ON/OFF twice per second (2 Hz: 250ms ON / 250ms OFF).
- **Relay 5 (Burner LED)** pulses ON/OFF at 2 Hz in sync with Relay 2.
- Continuous analog monitoring of AD8495 K-Type Thermocouple during this 10-second window.

### 3. Flame Sensor Outcomes & Relay 4/5 Control
- **Flame Detected (Temp ≥ 150°C)**:
  - Relay 3 (Ignitor) and Relay 2 (Beeper Speaker) turn OFF.
  - Relay 1 (Gas Valve) remains ON in **RUNNING** state.
  - **Relay 4 (Flame Detect Indicator Light)** closes (turns ON) to signal active flame detection.
  - **Relay 5 (Burner LED)** **STAYS ON** continuously during running operation.
  - (If temperature drops below 130°C hysteresis during operation, flame loss is triggered, Relay 4 de-energizes, and gas valve closes instantly for safety).
- **No Flame Detected**: If temperature stays below 150°C after 10 seconds, gas valve, ignitor, and beeper turn OFF to enter 30-second purge wait.

### 4. Maximum Run Time Shutoff (4 Hours)
- If the flame stays detected for **4 hours** (`MAX_RUN_TIME_MS` in `src/main.cpp`), the firmware shuts off the gas valve and all other relays, then enters **LOCKOUT**.
- Relay 5 (Burner LED) stays ON in lockout, as it does for other lockouts.
- Power must be cycled to restart the system.
- This timer runs in the ESP32 firmware. It is not an independent hardware shutoff. Use a separate hardware timer or a flame-sensing gas valve for real safety.

### 5. Retries (30-Second Purge) & 5-Attempt Lockout
- System waits **30 seconds** with all relays OFF (Safety Purge Delay).
- The sequence repeats after 30 seconds and repeats 4 more times (**5 total attempts**: 1 initial + 4 retries).
- If all 5 attempts fail to detect a flame, the system enters **LOCKOUT** state where **Relay 5 (Burner LED) STAYS ON** to indicate finished ignition attempts, while all other relays (gas valve, ignitor, beeper) remain OFF until power reboot.

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
