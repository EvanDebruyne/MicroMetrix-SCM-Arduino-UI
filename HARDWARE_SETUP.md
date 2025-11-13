# Hardware Setup Guide - 4-20mA Input Connection

This guide explains how to connect 4-20mA sensors (like particle counters) to your Arduino GIGA R1 for real data input.

## Hardware Requirements

1. **Arduino GIGA R1** with GIGA Display Shield
2. **4-20mA sensor** (particle counter, flow meter, etc.)
3. **Current-to-voltage converter** (resistor or module)

## Wiring Options

### Option 1: Simple Resistor Method (Recommended for testing)

For Arduino GIGA R1 (3.3V max input):
- Use a **165Ω resistor** between sensor output and ground
- Connect sensor positive to Arduino analog input (A0-A3)
- Connect sensor negative through resistor to ground

**Wiring:**
```
Sensor + → Arduino A0 (or A1, A2, A3)
Sensor - → 165Ω Resistor → GND
Arduino GND → Sensor GND (common ground)
```

**Voltage Range:**
- 4mA × 165Ω = 0.66V
- 20mA × 165Ω = 3.3V (max safe for GIGA)

### Option 2: 250Ω Resistor with Voltage Divider

If you have a 250Ω resistor (standard for 4-20mA):
- Use a voltage divider to scale 5V max down to 3.3V
- Resistor values: R1 = 1.5kΩ, R2 = 2.2kΩ

**Wiring:**
```
Sensor + → 250Ω Resistor → Arduino A0
Sensor - → GND
Voltage divider: A0 → 1.5kΩ → 2.2kΩ → GND
```

### Option 3: Dedicated 4-20mA Receiver Module

Use a commercial 4-20mA receiver module (e.g., INA219, ADS1115 with 4-20mA adapter):
- More accurate and isolated
- Follow module manufacturer's wiring instructions
- Connect module output to Arduino analog input

## Software Configuration

### Step 1: Enable Hardware Mode

In `SCM_Cursor_v2_patched.ino`, change:

```cpp
#define USE_REAL_HARDWARE false
```

to:

```cpp
#define USE_REAL_HARDWARE true
```

### Step 2: Configure Pin Assignments

Default pin assignments:
- **A0** - Streaming Current / Particle Counter (Channel 1)
- **A1** - Flow sensor (Channel 2)
- **A2** - pH sensor (Channel 3)
- **A3** - Temperature sensor (Channel 4)

To change pins, modify these lines:
```cpp
#define PIN_4_20MA_CH1 A0   // Change A0 to your desired pin
#define PIN_4_20MA_CH2 A1
#define PIN_4_20MA_CH3 A2
#define PIN_4_20MA_CH4 A3
```

### Step 3: Adjust Resistor Value

If using a different resistor value, update:
```cpp
#define CURRENT_TO_VOLTAGE_RESISTOR 165.0f  // Change to your resistor value in Ohms
```

### Step 4: Configure Sensor Ranges

The code automatically scales 4-20mA to your sensor's range. To adjust the range for each channel, modify the `input_channels` array:

```cpp
static InputChannel input_channels[INPUT_CHANNEL_COUNT] = {
  {
    "sc",
    "Streaming Current",
    "mV",
    1,
    5.0f,
    -500.0f,  // minValue - minimum sensor reading
    500.0f,   // maxValue - maximum sensor reading
    // ... rest of config
  },
  // ...
};
```

**Example for Particle Counter:**
```cpp
{
  "sc",
  "Particle Count",
  "particles/mL",
  0,
  500.0f,
  0.0f,      // 4mA = 0 particles/mL
  1000.0f,   // 20mA = 1000 particles/mL
  // ...
}
```

## Testing

1. **Upload the code** with `USE_REAL_HARDWARE = true`
2. **Open Serial Monitor** (115200 baud) to see "Hardware mode" message
3. **Connect your sensor** to the configured analog pin
4. **Check the display** - values should update based on actual sensor readings
5. **Verify scaling** - 4mA should show minimum value, 20mA should show maximum value

## Calibration

If readings are off:

1. **Check wiring** - ensure proper connections and common ground
2. **Verify resistor value** - measure actual resistance
3. **Test with known current** - use a current source to verify 4mA and 20mA readings
4. **Adjust resistor constant** - fine-tune `CURRENT_TO_VOLTAGE_RESISTOR` if needed
5. **Use zero calibration** - Use the UI's zero calibration feature to offset any bias

## Troubleshooting

**No readings or stuck values:**
- Check wiring connections
- Verify sensor is powered
- Check Serial Monitor for errors
- Ensure `USE_REAL_HARDWARE` is set to `true`

**Readings are wrong:**
- Verify resistor value matches code
- Check sensor output range matches configured min/max
- Test with multimeter to verify voltage at analog pin

**Readings are noisy:**
- Add a small capacitor (0.1µF) across the resistor
- Use shielded cable for sensor connections
- Keep sensor wires away from power lines

## Example: Particle Counter Setup

For a particle counter with 0-1000 particles/mL range:

1. Connect particle counter 4-20mA output to A0
2. Use 165Ω resistor (or 250Ω with voltage divider)
3. Update channel config:
   ```cpp
   {
     "sc",
     "Particle Count",
     "particles/mL",
     0,
     500.0f,
     0.0f,      // 4mA = 0
     1000.0f,   // 20mA = 1000
     // ...
   }
   ```
4. Enable hardware mode and upload

The display will now show actual particle count readings!

## Safety Notes

- Always ensure proper grounding between sensor and Arduino
- Don't exceed 3.3V on analog inputs (use voltage divider if needed)
- Check sensor specifications for maximum voltage/current
- Use appropriate fuses/protection if working with high-power sensors

