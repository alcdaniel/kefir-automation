# KefirJarLidNano

Firmware for the automatic kefir jar lid, for the **classic Arduino Nano** (ATmega328P, 5 V, 16 MHz).

The full guide (bill of materials, wiring, usage, serial console, calibration and troubleshooting) is in the [main README](../README.md).

## Quick flashing

1. In Arduino IDE 2, install the `SSD1306Ascii` and `Servo` libraries from **Tools → Manage Libraries**.
2. Open `KefirJarLidNano.ino` (the folder must keep this exact name).
3. Board: **Arduino Nano**. Processor: **ATmega328P** (or **ATmega328P (Old Bootloader)** on some clones).
4. Select the port and click **Upload**.

## Pins

| Function | Nano pin |
| --- | --- |
| OLED SDA | A4 |
| OLED SCL / SCK | A5 |
| Servo signal | D9 |
| Up button | D2 (other leg to GND) |
| Down button | D3 (other leg to GND) |
| Select button | D4 (other leg to GND) |

Power the servo from a 5 V supply of at least 1 A and join its GND to the Nano's GND. Serial Monitor: 115200 baud; type `HELP` to list the commands.
