# KefirAutomaticoNano

Firmware de la tapa automática para fermentar kéfir, para **Arduino Nano clásico** (ATmega328P, 5 V, 16 MHz).

La guía completa (material, conexiones, uso, consola serie, calibración y solución de problemas) está en el [README principal](../README.md).

## Carga rápida

1. En Arduino IDE 2, instala las bibliotecas `SSD1306Ascii` y `Servo` desde **Herramientas → Gestionar bibliotecas**.
2. Abre `KefirAutomaticoNano.ino` (la carpeta debe conservar este mismo nombre).
3. Placa: **Arduino Nano**. Procesador: **ATmega328P** (o **ATmega328P (Old Bootloader)** en algunos clones).
4. Selecciona el puerto y pulsa **Subir**.

## Pines

| Función | Pin del Nano |
| --- | --- |
| OLED SDA | A4 |
| OLED SCL / SCK | A5 |
| Señal del servo | D9 |
| Botón arriba | D2 (otra pata a GND) |
| Botón abajo | D3 (otra pata a GND) |
| Botón seleccionar | D4 (otra pata a GND) |

Alimenta el servo con una fuente de 5 V de al menos 1 A y une su GND con el del Nano. Monitor Serie: 115200 baudios; escribe `AYUDA` para ver los comandos.
