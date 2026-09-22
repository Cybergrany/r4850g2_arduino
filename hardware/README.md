# Hardware

This directory contains the schematics PDF and the Gerber zip.

The PCB footprint is for the original Nano controller. The modular firmware now
targets **Mega 2560** and needs external wiring: MCP2515 MISO/MOSI/SCK on
50/51/52, CS 10, INT 2; ILI9341 SPI MISO/MOSI/SCK shared on 50/51/52,
CS 22, DC 23, RST 24; encoder CLK/DT/SW on 3/4/5.
See [the current display wiring guide](../docs/LOCAL_UI.md).
The Mega does not fit the existing Nano footprint. Firmware pin and crystal
settings are in `src/config/BoardConfig.h`.

![PCB 2d view front](https://github.com/user-attachments/assets/c18db02c-cf32-4fbb-a179-55b502b8e7af)
![PCB 3d view front](https://github.com/user-attachments/assets/5cee2819-e5f1-4449-be82-1d81b582f415)

**Historical Nano PCB only:** the footprint for the SSD1306 has reversed SDA/SCL. The latest gerber fixes this (I2C pins cannot be changed on the pro mini).

## Historical Nano BOM (not the current Mega/ILI9341 wiring)

- Arduino Nano
- EC11 rotary encoder (e.g. PEC11R-4215F-S0024)
- SSD1306 display
- MCP2515 CAN bus module (will be mounted flipped so boards with already soldered pins can be used)
- 2x 100nF 0805 for rotary debounce
- 2x 3,5mm screw terminal (optional, one can be salvaged from the CAN bus module)
- Female headers 2.54mm (optional when the break out boards are soldered directly to the PCB)
