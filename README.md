# Kéfir automático · Tapa de tarro con Arduino Nano

Firmware y guía de montaje de una **tapa de tarro impresa en 3D que automatiza la fermentación del kéfir de leche**. Eliges 12, 24 o 36 horas en una pequeña pantalla OLED y, cuando termina la cuenta atrás, un microservo libera un émbolo con muelle que saca los gránulos de la leche. Sin Wi-Fi, sin aplicaciones y sin cuentas: un Arduino Nano, tres botones y una pantalla.

> *English summary below.*

> **Seguridad alimentaria y mecánica.** Toda la electrónica va en la parte superior de la tapa, fuera de la zona húmeda. Las piezas que toquen la leche o los gránulos deben ser aptas para alimentos y fáciles de limpiar. Prueba siempre el mecanismo sin leche antes de usarlo. El temporizador no sustituye observar el kéfir: la temperatura, la cantidad de leche y el estado de los gránulos cambian el resultado.

## Contenido del repositorio

| Ruta | Descripción |
| --- | --- |
| [`KefirAutomaticoNano/KefirAutomaticoNano.ino`](KefirAutomaticoNano/KefirAutomaticoNano.ino) | Firmware para Arduino Nano. |
| [`KefirAutomaticoNano/README.md`](KefirAutomaticoNano/README.md) | Resumen rápido para cargar el sketch. |
| `README.md` | Esta guía completa. |

Los modelos STL de la tapa, el émbolo y el soporte (clamp) se distribuyen por separado en el paquete descargable del proyecto.

## Material necesario

**Electrónica**

- Arduino Nano clásico (ATmega328P, 5 V, 16 MHz).
- Pantalla OLED SSD1306 de 0,96", 128×64 píxeles, I2C (dirección `0x3C` o `0x3D`).
- Microservo de 9 g (SG90, MG90S o similar).
- 3 pulsadores táctiles (arriba, abajo y seleccionar).
- Cables jumper (Dupont) y algo de soldadura sencilla.
- Fuente USB de 5 V para el Nano y, recomendado, una fuente de 5 V de al menos 1 A para el servo.
- Opcional: condensador de 470–1000 µF para absorber los picos de corriente del servo.

**Mecánica**

- Piezas impresas en 3D: tapa, émbolo y soporte.
- Muelle de compresión de **8 mm de diámetro y unos 10 cm** de longitud, para subir el émbolo.
- Muelle de unos **3 mm de diámetro y 5 cm**, para cortar a medida, para el sistema de liberación.
- Unos tornillos **M3** para fijar las piezas.
- Un tarro de vidrio con boca de rosca compatible con la tapa.

## Conexiones

| Componente | Pin del componente | Arduino Nano | Nota |
| --- | --- | --- | --- |
| OLED | VDD / VCC | 5V | La mayoría de módulos de 0,96" admiten 3,3 V y 5 V; compruébalo en el tuyo. |
| OLED | GND | GND | Tierra común. |
| OLED | SDA | A4 | Datos I2C (pin fijo en el Nano). |
| OLED | SCK / SCL | A5 | Reloj I2C (pin fijo en el Nano). |
| Servo | Señal (naranja/amarillo) | D9 | Control PWM. |
| Servo | +5 V (rojo) | Fuente externa de 5 V | Recomendado ≥ 1 A. |
| Servo | GND (marrón/negro) | GND | Unido también al GND de la fuente externa. |
| Botón arriba | Una pata | D2 | La otra pata a GND. |
| Botón abajo | Una pata | D3 | La otra pata a GND. |
| Botón seleccionar | Una pata | D4 | La otra pata a GND. |

- Los botones usan las resistencias **pull-up internas** del Nano: cada uno va solo entre su pin y GND, sin resistencias externas.
- En un pulsador de cuatro patas, las dos patas de cada lado están unidas entre sí. Conecta los cables en **lados opuestos**; si no, al pulsar no cambia nada.
- Si tu módulo OLED solo admite 3,3 V en sus señales, no conectes SDA/SCL directamente al Nano de 5 V.

### Alimentación del servo

Un servo puede pedir picos de corriente que reinician el Nano si se alimenta desde su pin 5V mientras está conectado por USB. La opción fiable es:

1. Alimentar el Nano por USB.
2. Alimentar el cable rojo del servo con una fuente de 5 V de al menos 1 A.
3. Unir el GND de esa fuente con el GND del Nano.
4. Conectar la señal del servo a D9.

Un condensador de 470–1000 µF entre 5 V y GND, cerca del servo, ayuda a absorber esos picos.

## Cargar el firmware

1. Instala [Arduino IDE 2](https://www.arduino.cc/en/software).
2. En **Herramientas → Gestionar bibliotecas**, instala:
   - `SSD1306Ascii` (de Bill Greiman).
   - `Servo`.

   `Wire` y `EEPROM` ya vienen incluidas con el núcleo Arduino AVR.
3. Abre [`KefirAutomaticoNano/KefirAutomaticoNano.ino`](KefirAutomaticoNano/KefirAutomaticoNano.ino). La carpeta debe llamarse igual que el archivo `.ino`.
4. En **Herramientas → Placa**, selecciona **Arduino Nano**.
5. En **Herramientas → Procesador**, selecciona **ATmega328P**. Si la carga falla con un Nano clónico, prueba **ATmega328P (Old Bootloader)**.
6. Selecciona el puerto del Nano en **Herramientas → Puerto** y pulsa **Subir**.

Al arrancar, el Monitor Serie (115200 baudios) indica en qué dirección ha encontrado la OLED y muestra la lista de comandos.

## Uso con los botones

- **Arriba / abajo**: cambian entre **PRUEBA** (5 s), **RAPIDA** (12 h), **NORMAL** (24 h) y **LARGA** (36 h).
- **Seleccionar**: inicia la opción visible. La prueba de 5 s ejecuta el ciclo completo de liberación para comprobar el mecanismo.
- **Mantener seleccionar 1,5 s** en un preajuste: edita su duración en pasos de una hora; seleccionar la guarda.
- **Mantener arriba o abajo 1,5 s**: abre **OPCIONES**:
  - `TIEMPO`: duración manual de 0 minutos a 72 horas, en pasos de 30 minutos; seleccionar la inicia.
  - `PRUEBA`: abre y cierra el servo una vez.
  - `SERVO`: ajusta el ángulo **cerrado**, el ángulo **abierto** y los segundos que se mantiene abierto.
  - `SALIR`.
- **Durante la fermentación**: seleccionar pausa o continúa; mantenerlo 1,5 s **cancela sin liberar** los gránulos.

La cuenta atrás se muestra en horas y minutos y solo se redibuja cuando cambia el minuto (en la prueba de 5 s se muestran segundos).

Los preajustes de 12, 24 y 36 horas son puntos de partida para leche de vaca y gránulos activos a temperatura ambiente: 12 h suele dar un kéfir más suave, 24 h es el ritmo habitual y 36 h una opción más intensa.

## Uso sin botones: consola serie

Conecta el Nano por USB, abre el **Monitor Serie** a **115200 baudios** y elige `Nueva línea` (o `Ambos NL y CR`). Escribe los comandos sin acentos.

| Comando | Acción |
| --- | --- |
| `AYUDA` | Muestra todos los comandos. |
| `ESTADO` | Muestra la configuración y el tiempo restante. |
| `DURACION 14` | Guarda una duración de 14 horas. |
| `DURACION 12 30` | Guarda 12 horas y 30 minutos. |
| `INICIAR` | Inicia con la duración guardada. |
| `INICIAR 14` / `INICIAR 12 30` | Guarda la duración indicada e inicia. |
| `PAUSA` / `CONTINUAR` | Pausa o reanuda la fermentación. |
| `CANCELAR` | Detiene el temporizador sin liberar los gránulos. |
| `PRUEBA` | Ejecuta un ciclo de apertura y cierre del servo. |
| `CERRAR` | Lleva el servo a la posición cerrada. |
| `CERRADO 0` | Guarda 0° como posición cerrada y mueve el servo allí. |
| `LIBERACION 60` | Guarda 60° como posición de liberación. |
| `RETENCION 3` | Guarda 3 segundos de apertura. |

Los botones y la consola pueden usarse a la vez. Cada pulsación de botón aparece también en el Monitor Serie, lo que sirve para comprobar el cableado.

## Primera calibración

Los valores iniciales son **0° cerrado**, **60° abierto** y **3 s** de apertura. Cada montaje necesita los suyos:

1. Monta la tapa sin leche ni gránulos.
2. En `OPCIONES → SERVO → CERRAR`, ajusta el ángulo en que el émbolo queda retenido sin que el servo fuerce.
3. En `ABRIR`, ajusta el ángulo en que el émbolo se libera de forma fiable.
4. Usa `PRUEBA` varias veces y comprueba que no hay roces ni atascos.
5. Solo cuando funcione de forma repetible, añade la leche y los gránulos.

## Cortes de corriente

La configuración y la cuenta atrás se guardan en la EEPROM del Nano al iniciar, pausar, continuar o cancelar, y cada minuto durante la fermentación. Tras un corte de corriente se continúa desde el último minuto guardado: **el tiempo sin alimentación no se descuenta**. Así nunca se liberan los gránulos por sorpresa al volver la luz. Para descontar los cortes con precisión haría falta añadir un reloj en tiempo real con batería, como un DS3231.

## Solución de problemas

**La pantalla no se enciende.** Revisa que SDA vaya a A4 y SCL a A5, y que VDD y GND estén bien. El Monitor Serie indica si se ha encontrado la OLED en `0x3C` o `0x3D`; la consola sigue funcionando aunque no haya pantalla.

**Un botón no responde.** Abre el Monitor Serie: cada pulsación debe imprimir `Boton detectado`. Si no aparece, revisa que el cable llegue a D2, D3 o D4 y que uses patas en lados opuestos del pulsador.

**El Nano se reinicia al mover el servo.** El servo está pidiendo más corriente de la que da el USB. Aliméntalo con una fuente de 5 V aparte, con GND común, y añade el condensador.

## Licencia

Uso personal y no comercial. Consulta [LICENSE.md](LICENSE.md).

---

## English summary

Firmware and build guide for a **3D-printed jar lid that automates milk kefir fermentation**. Pick 12, 24 or 36 hours on a small SSD1306 OLED; when the countdown ends, a 9 g micro servo releases a spring-loaded plunger that lifts the grains out of the milk.

- **Hardware:** classic Arduino Nano (ATmega328P), 0.96" SSD1306 128×64 I2C OLED, 9 g micro servo (SG90/MG90S), 3 push buttons, an 8 mm × ~10 cm compression spring (plunger), a ~3 mm × 5 cm spring cut to size (release latch), a few M3 screws, jumper wires and simple soldering.
- **Wiring:** OLED VDD→5V, GND→GND, SDA→A4, SCK→A5 · servo signal→D9, +5 V from an external 5 V ≥ 1 A supply, common GND · buttons→D2 (up), D3 (down), D4 (select), other leg to GND (internal pull-ups).
- **Libraries:** `SSD1306Ascii` and `Servo` (Arduino IDE Library Manager). Board: *Arduino Nano*, processor *ATmega328P* (or *Old Bootloader* for some clones).
- **Usage:** Up/Down choose TEST (5 s), QUICK (12 h), NORMAL (24 h) or LONG (36 h); Select starts it. Hold Up/Down for the options menu (manual time up to 72 h, servo test and calibration). The serial console at 115200 baud accepts the Spanish commands listed above (`AYUDA` prints them).
- **Power loss:** state is saved to EEPROM every minute; after a power cut it resumes from the last saved minute and never releases unexpectedly.

Personal, non-commercial use only. See [LICENSE.md](LICENSE.md).
