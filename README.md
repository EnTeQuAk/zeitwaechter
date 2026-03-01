# Kids Time Tracker

A visual countdown timer for kids, built on [M5Stack Dial](https://docs.m5stack.com/en/core/M5Dial).

Inspired by the [AURIOL Time Tracker](https://www.discounto.de/Angebot/AURIOL-Time-Tracker-mit-digitaler-Anzeige-9969069/) — a physical timer that uses color phases to help kids understand how much time is left for an activity.

## How it works

The timer has 3 phases that run in sequence:

| Phase | Default | Meaning |
|-------|---------|---------|
| 🟢 Green | 15 min | Plenty of time — "Alles gut!" |
| 🟡 Yellow | 10 min | Get ready — "Mach dich fertig!" |
| 🟣 Magenta | 5 min | Go go go — "Los gehts!" |

A shrinking colored arc on the round display shows remaining time at a glance. The buzzer beeps on phase transitions and when time runs out.

### Child-proof

While the timer is running, the rotary encoder and touchscreen are locked. Kids can only:
- **Press the button** → shows "Noch X Min" + the current phase message
- That's it. No fiddling with the timer.

### Parent controls

- **Long press** (1.5s) the button → stops and resets the timer
- **Web UI** at `http://timetracker.local` → configure everything from your phone

## Web Configuration

The device connects to your home WiFi and exposes a config page via mDNS.

From any device on the same network, open `http://timetracker.local` to:
- Set duration for each phase (minutes)
- Set the message displayed per phase
- Configure WiFi credentials
- Save & start the timer remotely

### First-time WiFi setup

On first boot with no WiFi configured, the device shows the idle screen. Flash the firmware with your WiFi credentials in the code, or connect via serial to configure. (AP mode for initial setup is a future enhancement.)

## Hardware

- [M5Stack Dial v1.1](https://shop.m5stack.com/products/m5stack-dial-v1-1) (ESP32-S3, 1.28" round touchscreen, rotary encoder, buzzer)
- USB-C cable

## Project structure

```
src/
├── main.cpp        # Main loop, input handling, state coordination
├── config.h/cpp    # Settings struct, NVS persistence
├── timer.h/cpp     # 3-phase countdown state machine
├── display.h/cpp   # All drawing (arc, text, screens)
└── webserver.h/cpp # WiFi, mDNS, HTTP config page
```

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
# Install PlatformIO (if needed)
pipx install platformio

# Build
pio run

# Flash
pio run -t upload

# Serial monitor
pio device monitor
```

On Linux, add your user to the `uucp` group for serial port access:
```bash
sudo usermod -a -G uucp $USER
# then log out and back in
```
