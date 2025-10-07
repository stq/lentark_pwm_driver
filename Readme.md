# PWM Controller with Data Filtering

Program for controlling Lentark PP504F0A-02W30 PWM driver via Digispark ATtiny85 with advanced sensor data filtering.

**WIP** Pending implementation: duty-cycle limit for low-voltage battery to avoid strain on DC-DC converter

## 🔌 Hardware Configuration

### Main Components
- **MCU**: Digispark ATtiny85 (with removed USB resistors)
- **Programmer**: Arduino Mega2560 in Arduino ISP mode (or other with correct pins)
- **PWM Driver**: Lentark PP504F0A-02W30 (channel 2) ( just because on my chip only channel 2 works :) )
- **Sensors**:
    - Potentiometer for duty cycle control (10k)
    - Current sensor (standart arduino 5A sensor module)
    - Voltage divider (100k + 39k)
- **LED**: 20v 300ma LED
- **LED Power**: LDD-300L
- **MCU PSU**: Digispark ATtiny85 built-in dc-dc to 5v (supports up to 30v), also used for voltage divider
- **Power**: 15v 3000f supercapacitor battery
- **Power converter**: XL6018 module or similar (4-15v to 20v, 3 A peak, 1.3 A stable without heat dissipation)

### ATtiny85 Pin Connections
- **P0** → Mega2560 RX (debug UART)
- **P1** → Lentark RX (PWM control)
- **P2** → Voltage sensor (A1)
- **P3** → Current sensor (A3)
- **P4** → Potentiometer (A2)

## ⚙️ Features

### Filtering Algorithms
- **Median filtering** of data buffer
- **Statistical analysis** (mode, median, trimmed mean)
- **Adaptive hysteresis** for each sensor
- **Noise and outlier suppression**

### PWM Control
- **Logarithmic conversion** of potentiometer input
- **Smooth duty cycle changes** with adaptive step size
- **Control commands**: `2DXXXX.XX>` (duty cycle), `2U2500.00>` (frequency)

## 🛠️ Programming and Debugging

### Programmer Setup
```cpp
// Adjust pins in Arduino IDE settings for other programmers
// Tools → Programmer → Arduino as ISP
```

### Debug Output
- **Baud rate**: 19200 (software UART)
- **Format**: Min-max values, median/mode, filter statistics
- **Labels**: 'C' - current, 'V' - voltage, 'R' - potentiometer

## 📊 Implementation Details

- Optimized for ATtiny85 (limited resources)
- Safe interrupt handling (transmission lock during debugging)
- Automatic clock frequency adaptation
- Power-efficient PWM management

## 🔧 Compilation

1. Select board: **Digispark (Default - 16.5mhz)** in Arduino IDE
2. Set programmer: **Arduino as ISP**
3. Upload via ICSP (ignoring USB bootloader)

> **Note**: USB resistors on Digispark board must be removed for stable ISP operation.