# Kefir automático con ESP32

Firmware para una **ESP32-WROOM-32D** que cuenta el tiempo de fermentación, muestra el estado en una OLED y mueve un servomotor al terminar para liberar los gránulos de kéfir. Está preparado para abrirse y cargarse directamente desde el **Arduino IDE**; no requiere Wi-Fi, una app ni una cuenta externa.

> **Seguridad alimentaria y mecánica.** La electrónica debe ir fuera de la zona húmeda y los elementos dentro del tarro han de ser aptos para alimentos. Prueba siempre el mecanismo sin leche ni gránulos antes de usarlo. Este controlador no sustituye unas prácticas seguras de elaboración ni de limpieza.

## Material necesario

- Placa de desarrollo ESP32-WROOM-32D.
- Pantalla OLED I2C SSD1306 de 128×64 píxeles (normalmente dirección `0x3C`).
- Servomotor de 5 V (por ejemplo SG90/MG90S u otro compatible).
- Tres pulsadores momentáneos.
- Fuente USB de 5 V para la ESP32 y, preferiblemente, fuente de 5 V independiente de al menos 1 A para el servo.
- Cables Dupont. Para el servo, un cable/adaptador de tres pines resulta cómodo: señal, 5 V y GND.

## Conexiones

| Componente | Pin del componente | ESP32 | Nota |
| --- | --- | --- | --- |
| OLED I2C | VCC | 3V3 | Usa 3,3 V salvo que el módulo indique explícitamente que admite 5 V con I2C a 3,3 V. |
| OLED I2C | GND | GND | Tierra común. |
| OLED I2C | SDA | GPIO 21 | Bus I2C. |
| OLED I2C | SCL | GPIO 22 | Bus I2C. |
| Servo | Señal (naranja/amarillo) | GPIO 18 | PWM de control. |
| Servo | GND (negro/marrón) | GND común | También debe unirse al GND de la fuente externa si se usa. |
| Servo | +5 V (rojo) | Fuente externa 5 V | Recomendado: 5 V, ≥1 A. |
| Botón arriba | Una pata | GPIO 32 | La otra pata va a GND. |
| Botón abajo | Una pata | GPIO 33 | La otra pata va a GND. |
| Botón seleccionar | Una pata | GPIO 25 | La otra pata va a GND. |

Los botones usan las resistencias internas de la ESP32: **cada pulsador se conecta sólo entre su GPIO y GND**; no hace falta resistencia externa.

### Alimentación del servo: importante

Aunque una placa de desarrollo suele exponer un pin `5V`/`VIN`, no conviene alimentar el servo desde él salvo que la placa, el cable USB y el servo estén comprobados para el pico de corriente. Un servo pequeño puede provocar reinicios de la ESP32 al arrancar o al atascarse. La solución fiable y prácticamente igual de sencilla es:

1. Alimentar la ESP32 por USB.
2. Alimentar el cable rojo del servo con una fuente USB de 5 V capaz de dar 1 A o más.
3. Unir el GND de esa fuente al GND de la ESP32.
4. Conectar la señal del servo a GPIO 18.

El cable de tres hilos del servo sigue siendo plug-and-play; sólo se separa su alimentación para que el temporizador no se reinicie. Un condensador de 470–1000 µF entre 5 V y GND junto al servo ayuda con picos de corriente.

## Cargar el firmware con Arduino IDE

1. Instala [Arduino IDE 2](https://www.arduino.cc/en/software), si aún no lo tienes.
2. En `Archivo → Preferencias → URLs adicionales del gestor de tarjetas`, añade:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. En `Herramientas → Placa → Gestor de tarjetas`, instala **esp32 by Espressif Systems**.
4. En `Herramientas → Gestionar bibliotecas`, instala estas dos bibliotecas de Adafruit:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`
5. Abre el sketch [KefirAutomatico.ino](KefirAutomatico/KefirAutomatico.ino) desde Arduino IDE. Es importante abrir el archivo `.ino`, no copiarlo a otra carpeta.
6. En `Herramientas → Placa`, elige `ESP32 Dev Module` (o el modelo equivalente de tu placa); después selecciona su puerto serie.
7. Pulsa el botón **Subir**. Al encenderse, la pantalla mostrará `KEFIR AUTOMATICO`.

El servo usa el PWM nativo de la ESP32, por lo que no necesita ninguna biblioteca adicional. El sketch es compatible con las ramas 2.x y 3.x del paquete de placas de Espressif.

## Uso

- `Arriba` / `abajo`: navegar o cambiar valores.
- `Seleccionar`: abrir una opción, confirmar o pausar/continuar.
- Mantener `Seleccionar` unos 1,5 s durante una fermentación: cancelarla sin liberar el servo.

Desde la pantalla principal se puede:

- Elegir una duración entre 1 y 72 horas, en pasos de 30 minutos.
- Iniciar la fermentación con confirmación.
- Probar el ciclo de liberación antes de montar el tarro.
- Ajustar la posición cerrada, la posición de liberación (0–180°) y el tiempo de retención (1–10 s).

Cuando llega a cero, el servo pasa a la posición de liberación durante el tiempo configurado (3 s por defecto), vuelve a la posición cerrada y la pantalla indica que la fermentación ha terminado.

La configuración y el temporizador se guardan en la memoria interna cada minuto y al pausar, iniciar o cancelar. Si se corta la corriente, al volver se reanuda desde el último minuto guardado; el tiempo que estuvo apagado **no se descuenta**. Es la opción prudente porque evita una liberación imprevista. Para descontar cortes de alimentación con precisión haría falta añadir un RTC con batería (por ejemplo DS3231) o conectividad Wi-Fi para obtener la hora.

## Primera calibración

1. Deja el mecanismo sin carga y entra en `Ajustes servo`.
2. Ajusta `Cerrado` hasta la posición en que los gránulos quedan sujetos, sin forzar el servo.
3. Ajusta `Liberar` hasta que el mecanismo los suelte de forma fiable.
4. Usa `Probar liberacion` y verifica que no hay roces, torsión ni esfuerzo excesivo.
5. Sólo entonces coloca leche y gránulos.

Los valores iniciales (`15°` cerrado, `100°` liberar y `3 s`) son sólo puntos de partida: cada soporte mecánico necesita sus propios valores.

## Versión para Arduino Nano

También hay una versión independiente para **Arduino Nano clásico** (ATmega328P): [KefirAutomaticoNano.ino](KefirAutomaticoNano/KefirAutomaticoNano.ino), con su [guía completa de montaje y uso](KefirAutomaticoNano/README.md). Ofrece la misma interfaz OLED, los mismos botones opcionales y los mismos comandos por puerto serie, pero guarda el estado en EEPROM en vez de usar la memoria interna del ESP32.

| Componente | Arduino Nano |
| --- | --- |
| OLED SDA | A4 |
| OLED SCL | A5 |
| Servo, señal | D9 |
| Botón arriba | D2 |
| Botón abajo | D3 |
| Botón seleccionar | D4 |

En Arduino IDE selecciona `Arduino Nano` y el procesador correcto; muchas placas Nano clon necesitan `ATmega328P (Old Bootloader)`. Instala desde el Gestor de bibliotecas `SSD1306Ascii` y `Servo`. `Wire` y `EEPROM` ya vienen con Arduino AVR. El Monitor Serie funciona a **115200 baudios** y acepta los mismos comandos descritos abajo.

El Nano y el servo pueden recibir 5 V, pero el servo debe alimentarse preferiblemente desde una fuente de 5 V separada (al menos 1 A), con GND común al Nano. No lo alimentes desde el pin 5 V del Nano si está conectado al USB: los picos del servo pueden reiniciar la placa.

## Uso sin botones: consola serie

La ESP32 puede usarse íntegramente por el cable USB, sin conectar botones. Tras cargar el programa, abre el **Monitor Serie** de Arduino IDE, selecciona **115200 baudios** y termina cada comando con `Nueva línea` (o `Ambos NL y CR`). La ayuda aparece al arrancar y se puede volver a mostrar escribiendo `AYUDA`.

| Comando | Acción |
| --- | --- |
| `AYUDA` | Muestra todos los comandos. |
| `ESTADO` | Muestra la configuración y el tiempo restante. |
| `DURACION 14` | Guarda una duración de 14 horas. |
| `DURACION 12 30` | Guarda una duración de 12 h 30 min. |
| `INICIAR` | Inicia con la duración guardada. |
| `INICIAR 14` | Guarda 14 horas e inicia inmediatamente. |
| `PAUSA` / `CONTINUAR` | Pausa o reanuda el temporizador. |
| `CANCELAR` | Detiene el temporizador sin liberar los gránulos. |
| `PRUEBA` | Ejecuta una liberación de prueba y vuelve a cerrar. |
| `CERRAR` | Lleva el servo a la posición cerrada. |
| `CERRADO 15` | Guarda 15° como posición cerrada y mueve el servo a ella. |
| `LIBERACION 100` | Guarda 100° como posición de liberación. |
| `RETENCION 3` | Guarda 3 segundos de retención en la posición de liberación. |

Escribe los comandos sin acentos. Los botones, cuando se conecten, siguen disponibles y pueden usarse en paralelo con la consola serie.
