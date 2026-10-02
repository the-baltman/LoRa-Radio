# LoRa-Radio

Radios de voz por clips (push-to-talk) y alarmas por LoRa 915 MHz sobre ESP32 + SX1262, con OLED, micrófono
y bocina I2S. Proyecto de Baltasar ("Baltman"). Firmware, protocolo y herramientas de prueba.

> Estado: **compila** y la lógica pasa los tests en host. **Nada se ha probado en placa**: todo lo marcado
> "por verificar en placa" (sección 7) está sin confirmar.

## Hardware objetivo

* **Steren ESP32 DevKit = ESP32-WROOM (clásico), USB-C, USB-serie CP2102** (confirmado por Baltasar, 2026-10-02).
  En el IDE: placa **ESP32 Dev Module**.
* Radio Waveshare Core1262-HF (SX1262), OLED SSD1306 128x64 I2C (`-DOLED_SH1106` para SH1106), INMP441,
  MAX98357 + bocina 8 ohm, 18650 + TP4056, 4 botones. Pines: `firmware/lora-radio/src/config/pins.h`, copiados
  de `Electronica\02-Proyectos\LoRa-Radio\LoRa-Radio.md` (no se cambian aquí).
* Wi-Fi y Bluetooth **nunca** se activan (el presupuesto de corriente de los LDO depende de eso).

## Estructura (un módulo = un commit = un QR del manual)

| Carpeta | Módulo | Se verifica solo con |
|---|---|---|
| `docs/protocolo.md` | Protocolo documentado | lectura; los tests de abajo lo ejercen |
| `firmware/lora-radio/src/protocol/` (+ `audio/pair_pack.h`) | Formato de paquetes, reensamblado, FSM (ACK, reintento selectivo, prioridad SOS) | `tests/host/run.sh` (sin placa) |
| `firmware/lora-radio/src/config/` | Pines y constantes con unidades | revisión contra la ficha de electrónica |
| `firmware/lora-radio/src/drivers/radio_sx1262.*` | Radio SX1262 (RadioLib) | paso "SPI de la radio" y P-RF-1 (sección 6) |
| `firmware/lora-radio/src/audio/` | I2S, Codec2 1300, sirenas, DSP | `status` por serie + prueba de lazo (sección 6) |
| `firmware/lora-radio/src/drivers/` (display, buttons, battery_adc) y `src/app/` lógica UI/batería | OLED, botones, ADC, UI de 4 botones | `tests/host/run.sh` (lógica) + placa |
| `firmware/lora-radio/src/app/app.*` y `lora-radio.ino` | Aplicación (todo junto, consola serie, light-sleep) | placa |
| `tools/registro_serie.py` | Registro serie y resumen de alcance | `python -m unittest tools/test_registro_serie.py` |
| `scripts/build.sh` | Compilación con versiones fijas | - |

## Compilar y grabar

```bash
scripts/build.sh                 # descarga core y librerías fijadas y compila
scripts/build.sh --flash COM5    # compila y graba (CP2102: puerto COM del administrador de dispositivos)
```

Necesita `arduino-cli` (probado con 1.5.2-rc.1). Resultado de referencia (core 3.3.0, ESP32 Dev Module,
partición por defecto): **756 515 B de flash (57 % de 1 310 720 B)**, **34 160 B de RAM estática (10 % de
327 680 B)**, sin errores ni avisos en el código propio. Además la tarea de audio reserva 32 768 B de pila
y 2 x 1625 B de buffers de clip.

### Versiones fijadas (exactas)

| Pieza | Versión | Nota |
|---|---|---|
| arduino-esp32 (core) | **3.3.0** (IDF 5.5) | único core con el que compila el firmware completo |
| RadioLib | **7.5.0** | la que usa la referencia que corre Codec2 en ESP-WROOM-32 (esp32_loradv) |
| esp32_codec2 | **1.0.7** | GPL-3.0, ver licencia |
| U8g2 | **2.36.19** | |
| arduino-cli | 1.5.2-rc.1 | |

**arduino-esp32 2.x vs 3.x** (verificado compilando): las tres librerías compilan en **2.0.17** y en **3.3.0**
(sketch de compatibilidad con `codec2_create(CODEC2_MODE_1300)`, `SX1262.begin()` y U8g2). El firmware
completo **solo** compila en 3.x porque el audio usa `ESP_I2S.h` (I2S full-duplex en un solo puerto con
BCLK/WS compartidos, necesario para el hardware); en 2.0.17 falla con `ESP_I2S.h: No such file`. No se
mantiene una variante 2.x.

## Pruebas en host (sin hardware)

```bash
tests/host/run.sh          # g++ C++17; SANITIZE=1 activa ASan/UBSan donde existan
python -m unittest tools/test_registro_serie.py
```

381 comprobaciones (325 de protocolo y simulador de radio con ToA reales, 56 de UI/batería/DSP): códec de
paquetes y rechazos, ToA contra las cifras del scientist, empaquetado de pares, reensamblado, contadores con
vuelta 65535->0, ciclo SOS+ACK, ACK perdido sin doble alarma, clip sin pérdidas, con 1 fragmento perdido,
con pérdida persistente (hueco), ANN perdido, **prioridad del SOS** (emisor, receptor, tercero), falla del
radio, ranuras de ACK en broadcast.

## Verificar un módulo solo, en placa (para el manual de armado)

1. **Radio**: arrancar; si `radio.begin()` falla, la OLED y el serie muestran el código de RadioLib
   (-2 = chip no encontrado: SPI/BUSY/RESET o voltaje del TCXO). Probar polaridad RXEN/TXEN (P-RF-1):
   si el RSSI de otro nodo a 2 m es mejor con `RF_SWITCH_INVERTED = true`, cambiarlo.
2. **OLED**: la pantalla de arranque muestra "LoRa-Radio / ID n". Sin pantalla el nodo sigue funcionando.
3. **Botones**: PTT graba, SOS mantenido 1.5 s manda alarma, MENU navega y manda, VOL cicla 0..5.
4. **Batería**: `status` por serie imprime `bat=...mV`; comparar con el multímetro y ajustar
   `VBAT_CAL_GAIN` / `VBAT_CAL_OFFSET_MV` (calibración de 2 puntos).
5. **Audio y Codec2**: `status` da `c2enc_max` y `c2dec_max` (ms por trama de 40 ms; objetivo < 20, límite 40).
6. **Protocolo**: dos nodos con `id 1` e `id 2` (comando serie, se guarda en NVS). `prueba 20 3000`
   en uno; en el otro debe verse `RX` sin sirena; en el emisor `ACK` con RSSI/SNR en ambos sentidos.

Consola serie (115200): `id N`, `sleep 0|1`, `log 0|1`, `prueba N [periodo_ms]`, `status`, `help`.
El light-sleep no se usa durante 30 s tras el arranque ni tras cualquier byte recibido (la UART no
funciona dormida): configurar `id`/`sleep 0` en esa ventana.

## Licencia

**GPL-3.0-or-later**. `LICENSE` es el texto de la GPL-3.0. Motivo: `esp32_codec2` v1.0.7 es GPL-3.0 y se
enlaza al firmware, así que el firmware distribuido tiene que serlo. Baltasar aceptó GPL-3.0 (2026-10-02).

| Componente | Licencia | Relación |
|---|---|---|
| Código de este repo | GPL-3.0-or-later | |
| esp32_codec2 1.0.7 (empaqueta codec2) | **GPL-3.0** | enlazada |
| RadioLib 7.5.0 | MIT | enlazada |
| U8g2 2.36.19 | BSD-2-Clause (fuentes: ver su licencia) | enlazada |
| arduino-esp32 3.3.0 | LGPL-2.1 | núcleo |

## Por verificar en placa (software)

Cada uno bloquea su afirmación en el manual hasta medirse:

1. Voltaje del TCXO (1.8 V supuesto) y polaridad RXEN/TXEN (`RF_SWITCH_INVERTED`).
2. Corriente y wake-up de light-sleep (GPIO wake en pines 34/35 solo-entrada; DIO1 como fuente) y que la
   radio siga recibiendo con el ESP32 dormido. Sin esto la autonomía no está demostrada.
3. Tiempo de arranque del INMP441 (`MIC_WARMUP_MS 260`, recorta el inicio de la frase) y ganancia
   (`MIC_SHIFT 14`).
4. ms por trama de Codec2 1300 en el ESP32 real, y heap contiguo libre (`status`).
5. Techo de volumen `AUDIO_CEILING_Q15` con osciloscopio (P-AUD-2, <= 1.84 Vrms diferenciales) y el
   golpe ("pop") al encender el amplificador.
6. Calibración del ADC de batería y de los umbrales de corte.
7. Latencia fin-de-PTT a audio (esperado ~2.6 s) y ciclo SOS+ACK (esperado 0.5-0.8 s) contra los del simulador.
8. Dirección I2C de la OLED (0x3C supuesto).
