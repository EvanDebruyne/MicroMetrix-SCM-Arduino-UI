# Streaming Current Monitor - Arduino GIGA Display

A professional water treatment monitoring interface for the Arduino GIGA R1 with GIGA Display Shield, modeled after the MicroMetrix SCD-W610 streaming current detector.

## Features

- **Multi-Channel Monitoring**: Real-time display of streaming current, flow rate, pH, and temperature
- **Interactive Graphs**: Live trending with customizable parameter selection
- **Alarm Management**: Visual alarm indicators with activity logging
- **Output Control**: HOA (Hand/Off/Auto) controls for pumps and relays
- **Zero Calibration**: Calibration feature for streaming current sensor
- **Customizable Home Screen**: Drag-and-drop layout editor for home screen tiles

## Hardware Requirements

- Arduino GIGA R1
- Arduino GIGA Display Shield (7" 800x480 TFT touchscreen)
- LVGL library (included with Arduino GIGA Display Shield)

## Installation

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (version 2.0 or later recommended)
2. Install the Arduino GIGA board support:
   - Go to **Tools > Board > Boards Manager**
   - Search for "Arduino GIGA" and install
3. Install required libraries:
   - `Arduino_GigaDisplay_GFX` (should be included with board support)
   - `Arduino_GigaDisplayTouch` (should be included with board support)
   - `lvgl` (should be included with board support)
4. Open `SCM_Cursor_v2_patched.ino` in Arduino IDE
5. Select **Tools > Board > Arduino GIGA R1 WiFi**
6. Select the correct port under **Tools > Port**
7. Upload to the board

## Usage

### Navigation
- Use the tab bar at the top to switch between pages:
  - **Home**: Overview of all monitored parameters
  - **Inputs**: Detailed input channel configuration and status
  - **Outputs**: Control outputs (pumps, relays) with HOA modes
  - **Graphs**: Real-time trending graphs
  - **Config**: System configuration and display settings

### Zero Calibration
1. Navigate to **Inputs** tab
2. Click **View Details** on the Streaming Current channel
3. Click **Zero Calibration** to zero the current reading
4. The displayed value will now show relative to the zero point



## Project Structure

```
SCM_Cursor_v2_patched/
├── SCM_Cursor_v2_patched.ino  # Main sketch file
├── README.md                   # This file
└── .gitignore                  # Git ignore rules
```

## Technical Details

- **Display**: 800x480 pixel TFT with capacitive touch
- **UI Framework**: LVGL (Light and Versatile Graphics Library)
- **Update Rate**: 500ms for data updates
- **Trend History**: 180 samples per channel
- **Alarm Events**: 16 event history buffer

## Modeled After

This interface is designed to replicate the functionality and user experience of the **MicroMetrix Model SCD-W610** Streaming Current Detector and Controller, adapted for the Arduino GIGA platform.


## Author

Developed for water treatment monitoring applications.

