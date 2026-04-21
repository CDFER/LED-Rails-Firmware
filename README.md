# LED-Rails-Firmware

[![PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-orange?logo=platformio)](https://platformio.org/)

Firmware for my LED train maps (Currently includes Auckland, Wellington and Melbourne)

You can edit and run your firmware using platformio (An extention for VSCode)

The firmware is responsible for:

1. Connecting to Wi-Fi
2. Fetching live train data from the [GTFS Realtime Cache API](https://github.com/CDFER/GTFS-Realtime-Cache-Server)
3. Processing data to determine train locations
4. Controlling WS2812B LEDs to display train positions

## Web Installer

Easily flash the latest firmware to your map using your browser:

[Open the LED Train Map Web Installer](https://cdfer.github.io/Auckland-LED-Train-Map/led-rails.html)

- Works with Chrome, Edge, or any Web Serial-compatible browser
- Follow on-screen instructions to connect and flash your device
