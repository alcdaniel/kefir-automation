# Kéfir automático con Arduino Nano

Firmware para un **Arduino Nano clásico** (ATmega328P, 5 V y 16 MHz) que controla la fermentación de kéfir: muestra el tiempo en una pantalla OLED, acciona un servomotor al finalizar y se puede manejar tanto con botones como por el puerto serie USB.

No necesita Wi-Fi ni una aplicación. Guarda la configuración y el estado del temporizador en la EEPROM del Nano.

> **Seguridad alimentaria y mecánica.** La electrónica debe permanecer fuera de la parte húmeda. Las piezas que toquen leche o gránulos deben ser aptas para alimentos y fáciles de limpiar. Antes de usar leche, prueba el mecanismo sin carga hasta comprobar que no fuerza ni atasca el servo.

## Material necesario

- Arduino Nano clásico, basado en ATmega328P.
- Pantalla OLED I2C SSD1306 de 128×64 píxeles, normalmente en la dirección `0x3C`.
- Servomotor de 5 V, por ejemplo SG90 o MG90S.
- Opcional: tres pulsadores momentáneos.
- Fuente USB de 5 V para el Nano.
- Fuente de 5 V para el servo, capaz de entregar al menos 1 A; se recomienda 2 A para tener margen.
- Cables Dupont y, opcionalmente, un condensador de 470–1000 µF para el servo.

## Cableado

| Componente | Pin del componente | Arduino Nano | Nota |
| --- | --- | --- | --- |
| OLED I2C | VCC | 5V o 3V3* | El módulo JMD0.96D-1 admite ambos. |
| OLED I2C | GND | GND | Tierra común. |
| OLED I2C | SDA | A4 | I2C. |
| OLED I2C | SCL | A5 | I2C. |
| Servo | Señal, naranja/amarillo | D9 | Control PWM. |
| Servo | GND, negro/marrón | GND común | Unir al GND de su fuente y del Nano. |
| Servo | +5 V, rojo | Fuente externa de 5 V | No usar el pin 5V del Nano para el servo. |
| Botón arriba, opcional | Una pata | D2 | La otra pata va a GND. |
| Botón abajo, opcional | Una pata | D3 | La otra pata va a GND. |
| Botón seleccionar, opcional | Una pata | D4 | La otra pata va a GND. |

\* El módulo mostrado, JMD0.96D-1, está preparado para alimentación y señales de 3,3 V o 5 V. Con Arduino Nano puedes conectar `VCC` a `5V` y las líneas I2C directamente a A4/A5. En un módulo distinto que especifique sólo 3,3 V, no conectes sus señales I2C directamente al Nano de 5 V.

El I2C es el mismo protocolo que usaba la ESP32, pero los pines físicos cambian: en ESP32 eran `GPIO 21` (SDA) y `GPIO 22` (SCL); en Nano clásico son obligatoriamente `A4` (SDA) y `A5` (SCL). El programa busca automáticamente la pantalla en las direcciones I2C estándar `0x3C` y `0x3D` e informa el resultado por el Monitor Serie. No uses `0x7B`: es una dirección de 8 bits que corresponde a la dirección I2C de 7 bits `0x3D`.

Los botones son opcionales. Cuando se usen, sólo van entre el pin indicado y GND: el programa activa las resistencias internas del Nano, por lo que no se requieren resistencias externas.

En un pulsador táctil de cuatro patillas, las dos patillas de cada lado ya están unidas entre sí. Coloca los cables en **lados opuestos** del pulsador (no en las dos patillas del mismo lado); de lo contrario, al pulsarlo no cambiará nada.

## Alimentación del servo: importante

Un Nano conectado al ordenador o a una batería externa no debe alimentar directamente el servo desde su pin `5V`. Los picos de corriente al moverlo pueden reiniciar el Nano, dañar el regulador o hacer que el mecanismo falle.

Conéctalo así:

1. Alimenta el Nano por USB o por una fuente regulada de 5 V.
2. Alimenta el cable rojo del servo con una fuente externa de 5 V capaz de aportar al menos 1 A.
3. Une el GND de la fuente del servo con un GND del Nano.
4. Conecta la señal del servo a D9.

Un condensador de 470–1000 µF entre los 5 V y GND, cerca del servo, ayuda a absorber sus picos de corriente. Una batería externa USB de buena calidad puede alimentar ambos elementos si entrega suficiente corriente, pero mantener la alimentación del servo separada es más fiable.

## Cargar el programa con Arduino IDE

1. Instala [Arduino IDE 2](https://www.arduino.cc/en/software).
2. En `Herramientas → Placa`, selecciona `Arduino Nano`.
3. En `Herramientas → Procesador`, selecciona `ATmega328P`. Si la carga falla y tu placa es un clon, prueba `ATmega328P (Old Bootloader)`.
4. En `Herramientas → Gestionar bibliotecas`, instala:
   - `SSD1306Ascii`
   - `Servo`
5. Abre [KefirAutomaticoNano.ino](KefirAutomaticoNano.ino) desde Arduino IDE.
6. En `Herramientas → Puerto`, selecciona el puerto serie del Nano.
7. Pulsa **Subir**.

## Uso con botones

La pantalla principal está pensada para iniciar una fermentación sin entrar en menús:

- `Arriba` / `abajo`: cambia entre la **prueba de 5 segundos**, **rápida, 12 H**; **normal, 24 H**; y **larga, 36 H**. La prueba está a la izquierda del todo y ejecuta el mismo ciclo automático: espera 5 segundos y libera los gránulos.
- `Seleccionar`: inicia inmediatamente el preajuste que se ve en pantalla.
- Mantener `Seleccionar` unos 1,5 segundos desde la pantalla principal: edita el tiempo del preajuste visible. `Arriba` / `abajo` lo cambian en pasos de una hora y `Seleccionar` lo guarda.
- Mantener `Arriba` o `abajo` unos 1,5 segundos desde la pantalla principal: abre **Avanzado**.

En **Avanzado** hay cuatro opciones: `TIEMPO` (una duración manual de 0 minutos a 72 horas, en pasos de 30 minutos; `Seleccionar` la inicia), `PRUEBA` (abre y cierra el servo), `SERVO` (ángulos y tiempo de apertura) y `SALIR`.

Durante una fermentación, `Seleccionar` pausa o continúa; mantener `Seleccionar` unos 1,5 segundos la cancela sin liberar los gránulos.

La cuenta atrás normal se muestra en horas y minutos y la pantalla sólo actualiza su cifra cuando cambia el minuto. La prueba de 5 segundos es la excepción: muestra segundos para poder comprobar el ciclo.

Al terminar la cuenta atrás, el servo se mueve a la posición de liberación durante 3 segundos por defecto y vuelve a la posición cerrada. Los valores iniciales son **0° cerrado**, **60° abierto** y **3 s** de apertura. Calibra siempre el mecanismo sin leche antes de usarlo.

> **Importante al actualizar.** Esta versión reinicia una vez los valores guardados en EEPROM para aplicar la nueva interfaz y esos ángulos iniciales. No la subas mientras haya una fermentación en curso.

Los preajustes de 12, 24 y 36 horas son puntos de partida para leche de vaca y granos activos a temperatura ambiente: 12 h suele producir un kéfir más suave, 24 h es el ritmo habitual y 36 h es una opción más intensa. La temperatura, cantidad de leche y cantidad/estado de los gránulos cambian el resultado; el temporizador no sustituye observar el producto ni controlar su pH.

## Uso sin botones: puerto serie

Conecta el Nano por USB y abre el **Monitor Serie** del Arduino IDE a **115200 baudios**. Elige `Nueva línea` o `Ambos NL y CR` como terminación y escribe los comandos sin acentos. Al arrancar aparece la ayuda; puedes volver a verla con `AYUDA`.

| Comando | Acción |
| --- | --- |
| `AYUDA` | Muestra todos los comandos disponibles. |
| `ESTADO` | Muestra la configuración y el tiempo restante. |
| `DURACION 14` | Guarda una duración de 14 horas. |
| `DURACION 12 30` | Guarda una duración de 12 horas y 30 minutos. |
| `INICIAR` | Inicia con la duración que estaba guardada. |
| `INICIAR 14` | Guarda 14 horas e inicia el temporizador. |
| `INICIAR 12 30` | Guarda 12 h 30 min e inicia el temporizador. |
| `PAUSA` | Pausa una fermentación activa. |
| `CONTINUAR` | Reanuda una fermentación pausada. |
| `CANCELAR` | Detiene el temporizador sin liberar los gránulos. |
| `PRUEBA` | Ejecuta un ciclo de apertura y cierre del servo. |
| `CERRAR` | Lleva el servo a la posición cerrada. |
| `CERRADO 0` | Guarda 0° como posición cerrada y mueve el servo allí. |
| `LIBERACION 60` | Guarda 60° como posición de liberación. |
| `RETENCION 3` | Guarda 3 segundos de apertura. |

Puedes usar botones y comandos serie en paralelo si más adelante conectas los pulsadores.

## Si los botones no responden

El programa principal sí tiene los botones activos: `D2` es arriba, `D3` abajo y `D4` seleccionar. Su lógica es `INPUT_PULLUP`, el esquema habitual para Nano: el pin queda en `LIBRE` sin pulsar y pasa a `PULSADO` sólo cuando el pulsador lo conecta a `GND`.

Para comprobar el montaje sin depender de la OLED, el servo o el menú, carga [DiagnosticoBotones.ino](DiagnosticoBotones/DiagnosticoBotones.ino) y abre el Monitor Serie a 115200 baudios. Debe informar de cada pulsación y liberación. Si no cambia, el problema está en el pulsador, en qué filas de la protoboard están unidas, o en que el cable no llega a los pines marcados `D2`, `D3` y `D4` del Nano.

## Si la pantalla no aparece

El sketch principal ya busca las direcciones I2C habituales `0x3C` y `0x3D`. Para descartar por completo el resto del proyecto, está disponible [DiagnosticoI2C.ino](DiagnosticoI2C/DiagnosticoI2C.ino). Cárgalo en el Nano con sólo la OLED conectada y abre el Monitor Serie a 115200 baudios. Una OLED I2C operativa mostrará al menos una dirección, normalmente `0x3C` o `0x3D`.

## Persistencia y cortes de alimentación

El Nano guarda el estado al iniciar, pausar, continuar, cancelar y aproximadamente cada minuto durante la fermentación. Tras un corte de corriente, continúa desde el último minuto guardado: **el tiempo que estuvo apagado no se descuenta**. Así se evita liberar los gránulos sin control cuando vuelve la alimentación.

Si necesitas que los cortes de alimentación cuenten con precisión, habría que añadir un reloj en tiempo real con batería, como un DS3231.

## Primera calibración

1. Deja el mecanismo sin leche ni gránulos.
2. Ajusta `CERRADO` hasta que el soporte quede sujeto sin que el servo fuerce su recorrido.
3. Ajusta `LIBERACION` hasta que suelte de forma fiable.
4. Usa `PRUEBA` para comprobar el ciclo completo.
5. Sólo cuando funcione de forma repetible, coloca la leche y los gránulos.
