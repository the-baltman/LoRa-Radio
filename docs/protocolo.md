# Protocolo LoRa-Radio v1

Fuente del código: `firmware/lora-radio/src/protocol/` (portable, probado en host con
`tests/host/run.sh`). Los parámetros físicos vienen del scientist
(`Ciencia\03-Presupuestos-de-error\LoRa-Radio.md`); aquí solo se decide lo que es de software.
Etiquetas: **[DER]** derivado/medido en el simulador de host, **[PV]** por verificar en placa.

## 1. Capa física

| Perfil | SF / BW / CR | Uso | ToA 12 B | ToA 255 B |
|---|---|---|---|---|
| `ALARM` (reposo) | SF12 / 500 kHz / 4/5 | mensajes, ANN, ACK de mensajes | 248 ms | 1927 ms |
| `VOICE_C2_1300` | SF9 / 500 kHz / 4/5 | fragmentos de voz, NACK, ACK de clip | 37 ms | 313 ms |

915 MHz, +22 dBm, preámbulo 8, CRC LoRa activo, cabecera explícita, **sync word 0x6B** (privada:
distinta de 0x12 RadioLib, 0x34 LoRaWAN y 0x2B Meshtastic [PV]). BW500 = "modulación digital"
(IFT-008-2015 4.3), sin límite de tiempo por canal [scientist 1]. Un SX1262 demodula un solo SF/BW a la
vez: de ahí la negociación de la sección 5.

## 2. Formato (todo little-endian)

```
off len campo
 0   2  net_id    0x4C52 ("LR")     rechaza tráfico ajeno con la misma PHY (LoRaWAN, Meshtastic)
 2   1  ver|type  nibble alto = versión (1), bajo = tipo
 3   1  src       ID de nodo 1..254
 4   1  dst       ID de nodo o 0xFF = broadcast (0x00 inválido)
 5   2  msg_id    contador por emisor; los reintentos del mismo mensaje conservan el valor
 7   1  frag      solo VFRAG: índice(4) | total(4); 0 en los demás
 8  ..  payload
```

Un paquete se descarta (y se cuenta en `stats`) si: longitud < 8, `net_id` distinto, versión distinta,
tipo 0 o > 10, `src` = 0/0xFF, `dst` = 0, `src` = yo, `dst` ≠ yo y ≠ broadcast, o el payload no cumple el
tipo. El nodo no cifra ni autentica (recomendación del scientist, sec. 5). Un HMAC de 4 B en SOS/PELIGRO
queda **opcional y no implementado**: lo decide Baltasar vía PJ Rick.

### Tipos

| Tipo | Nombre | Payload | Perfil | Notas |
|---|---|---|---|---|
| 1 | SOS | - | ALARM | el tipo **es** el código de 1 byte del mensaje |
| 2 | PELIGRO | - | ALARM | |
| 3 | PRUEBA | - | ALARM | sin sirena; el receptor registra RSSI/SNR y llegada |
| 4 | SI | - | ALARM | respuesta manual, no reemplaza el ACK |
| 5 | NO | - | ALARM | |
| 6 | ACK | `acked_type, status, rssi_dbm (i8), snr_q2 (i8, 0.25 dB)` | el del acusado | `status` 0 = ok, 1 = clip con huecos, 2 = ACK_SOS (el receptor del clip pulsó SOS, sec. 6); `msg_id` = el acusado; siempre unicast |
| 7 | NACK | `missing_mask` (u16, bit i = fragmento i falta) | VOICE | solo en sesión de clip |
| 8 | VANN | `profile (u8), frag_cnt (u8), pairs (u16)` | ALARM | anuncio de clip |
| 9 | VFRAG | `n x 13 B` (n <= 19) | VOICE | `frag` = idx/total |
| 10 | VABORT | - | VOICE | el emisor cancela el clip |

Mensajes (1-5) y ANN son 8 y 12 B: 248 ms en SF12. El ACK lleva el RSSI/SNR que **el receptor** midió,
así el emisor de un PRUEBA obtiene el enlace de ida y de vuelta con un solo paquete.

### Contadores, IDs y duplicados

* `msg_id` (16 bit) arranca en un valor aleatorio en cada arranque (`esp_random`) y da la vuelta
  65535 -> 0 (`seq_gap()`, probado en la vuelta). Un clip consume un `msg_id`.
* El receptor guarda (src, msg_id, tipo) 30 s (`RecentTable`). Un reintento duplicado **se vuelve a
  ACKear pero no vuelve a disparar la alarma** (el emisor reintenta porque perdió el ACK).
* Por emisor se cuenta `rx` y `lost` (saltos de `msg_id`) para el registro de alcance.
* Direccionamiento: en v1 mensajes y clips salen **siempre** a broadcast: `VOICE_DST = NODE_BROADCAST` es una
  constante de `app.cpp` y no hay UI para elegir destino. El formato y el FSM admiten unicast sin cambios
  (los tests lo ejercen), pero el clip unicast es **función futura** (límite en la sec. 6). En broadcast los ACK se escalonan en 4 ranuras
  (`(id-1) % 4 x (ToA_ACK + 20 ms)`) para que los receptores no choquen; el emisor da por entregado
  con el primer ACK y cuenta los demás (`EV_ACK_EXTRA`). Con más de 4 nodos las ranuras se comparten
  (colisión posible): **límite conocido de v1**, validado solo con 2 y 4 nodos simulados.

## 3. Mensajes y ACK automático

Emisor: manda, espera el ACK `20 ms + ToA_ACK + 200 ms` (+ 3 ranuras si es broadcast) **más un backoff
pseudoaleatorio de ranuras de `ToA_SOS + 20 ms` = 268 ms: 0 a 3 ranuras (0-804 ms) en los reintentos 1 y 2 (`RETRY_BACKOFF_SLOTS_EARLY 4`) y 0 a 7 (0-1876 ms) desde el 3o (`RETRY_BACKOFF_SLOTS 8`); xorshift32
con semilla mezclada de `id` y `msg_id`, distinta por nodo) y reintenta con el mismo `msg_id`: SOS hasta 10
intentos, PELIGRO/SI/NO 3, **PRUEBA 1** (un reintento escondería la pérdida que se quiere medir; el timeout
cuenta como "sin ACK"). Periodo de reintento del SOS broadcast: **1.52-2.32 s** en los reintentos 1 y 2 y **1.52-3.40 s** (3396 ms) desde el 3o; los 10 intentos sin
respuesta tardan 15-32 s en terminar en "SIN ACK". Un SOS reemplaza a cualquier otro mensaje pendiente; los
demás devuelven OCUPADO mientras haya un mensaje o clip en curso.

Por qué el backoff: sin LBT y con periodo de reintento idéntico, dos SOS pulsados con < 0.2 s de diferencia
chocaban en **cada** intento (half-duplex: ninguno oía al otro) y con 0.25-0.5 s la alarma llegaba pero ningún
ACK. Barrido del re-gate (`dualsweep.cpp`, 3 semillas): sin backoff 162/453 casos sin entrega completa con 2
nodos y 75/453 con 3; con backoff 0/453 en los dos.

**Semillas.** En el simulador el backoff es determinista: cada semilla (el `msg_id` inicial, que en placa sale
de `esp_random`) es **una muestra** del retardo aleatorio. Los barridos corren 16 semillas por defecto
(`LR_SEEDS=N` corre N; `run.sh` corre `dualsweep` con 256) y las cifras de abajo son el máximo sobre esas 16, salvo donde dice 256 o 1024.

Medido en el simulador [DER] (2 nodos, distancia corta, ToA reales): ciclo SOS broadcast + ACK
**784 ms** (1->2) y **516 ms** (2->1); criterio del scientist <= 1.0 s (el backoff solo afecta reintentos).
Con un ACK perdido: 2 intentos, una sola alarma en el receptor. Dos SOS casi simultáneos (0-1500 ms de
diferencia): **siempre se entregan los dos y se acusan** (0 casos sin ACK con 1024 semillas, 2 y 3 nodos); la alarma
se oye en el otro nodo hasta en **17.3 s** (3 nodos; 15.5 s con 2) y el ACK llega hasta en **18.3 s** (2 nodos; 17.5 s con 3). El test falla
con un solo caso sin entrega (límite de tiempo 20000 ms).

**Por qué 8 ranuras desde el 3er reintento (re-gate 3, R3-1).** Con 4 ranuras fijas, 75 de 38656 casos (0.19 %,
256 semillas, 2 nodos) terminaban en "SIN ACK" aunque las dos alarmas llegaban: los dos ACK chocan, los relojes
de reintento quedan ~20 ms desfasados y, con ranura de 268 ms y SOS de 248 ms, chocan en la misma ranura y en la
vecina. Escalonar (4 ranuras en los reintentos 1-2, 8 desde el 3o) lo elimina y no mueve ninguna otra cifra;
8 ranuras desde el 2o reintento subía el choque con el ANN a 6.2 s.

## 4. Clip de voz (Codec2 1300, SF9)

* Codec2 1300: trama de 52 bit / 40 ms. Dos tramas se empaquetan en 13 B (`pair_pack.h`: 7 % menos que
  7 B por trama). Un **fragmento** = 19 pares = 247 B de audio = 1.52 s = paquete de 255 B.
* Clip máximo **8 s** = 100 pares = 6 fragmentos (5 x 255 B + 1 x 73 B: 5 pares + 8 B de cabecera), **1300 B** de audio (`MAX_CLIP_PAIRS 100`).
  Era 10 s; el arbitraje de c-morty (2026-10-02) lo bajó para que un SOS durante un clip quepa en < 3 s.
  Se codifica al vuelo mientras se graba; nunca se guarda PCM (128 kB no caben contiguos en el ESP32 sin PSRAM).
* Reensamblado (`ClipRx`): mapa de bits de recibidos; fragmentos en cualquier orden; duplicados y
  fragmentos con tamaño inconsistente se rechazan; los huecos quedan en cero y se reproducen como
  silencio.

### Línea de tiempo (sin pérdidas)

```
emisor   : [ANN SF12 248 ms][guarda 150][VFRAG 0..5 SF9 con 10 ms entre ellos ~1.7 s][escucha ACK/NACK]
receptor : ...ANN -> cambia a SF9 ........ recibe fragmentos ......... ACK (SF9, ~40 ms) -> vuelve a SF12
```

Medido [DER]: clip de 8 s, el receptor lo tiene a los **2121 ms** de `send_clip()` (**~2.2 s**) y el emisor se
entera a los 2178 ms. El scientist estimó 2.24 s para 10 s sin negociación; con 8 s y la negociación (ANN 248 ms
+ guarda 150 ms) queda en el mismo orden. El criterio "mediana <= 3 s" se cumple con 0.8 s de margen.

### Pérdida: reintento selectivo

El receptor sabe cuántos fragmentos esperar (ANN) y cuándo debería terminar la ráfaga
(`burst_end_est`). Cuando pasó ese instante **y** hubo 150 ms de silencio, manda `NACK(mask)`. El emisor
reenvía solo esos fragmentos 30 ms después. Máximo **2 rondas**; si aún faltan, el receptor manda
`ACK(status=1)` y reproduce con silencio en el hueco ("CLIP (HUECO)" en la OLED). Medido [DER]: 1 fragmento
perdido -> completo a los 2671 ms (+0.55 s por la ronda de NACK en unicast; **+0.61 s en broadcast**, dato del gate de c-morty) con exactamente 1 VFRAG extra y 1 NACK; fragmento 3 perdido siempre ->
2 NACK y reproducción parcial con máscara `1<<3`.

Si nadie responde tras la ráfaga (ANN perdido, o receptor fuera del alcance de SF9 pero dentro del de
SF12), el emisor vuelve a perfil ALARM **3.65 s** (`RETRY_LISTEN_MS`, >= periodo máximo de reintento del SOS 3396 ms (248 + 1272 + 7 x 268) + ToA del SOS 248 ms = 3644 ms; un test falla si se rompe la desigualdad) y reintenta todo el clip **una**
vez; luego informa "CLIP SIN RESPUESTA". Un receptor que oyó el ANN pero ningún fragmento regresa solo a
ALARM a los ~1 s (`NO_SIGNAL`). Un ANN repetido de un clip ya recibido (se perdió mi ACK) se
re-recibe y re-ACKea **sin reproducirlo otra vez**.

## 5. Negociación de perfil (reposo en alarma, voz solo si el emisor lo anuncia)

Restricción del scientist: un SX126x no demodula SF12 y SF9 a la vez. Diseño:

1. **Reposo = ALARM** en todos los nodos. Un SOS siempre se oye.
2. Al soltar PTT el emisor manda **VANN** en ALARM (SF12: llega más lejos que la voz, así que si la
   ráfaga no llega, el ANN sí) con `profile`, `frag_cnt`, `pairs`. `profile` permite un perfil futuro
   (700C + SF10, plan B del scientist) sin cambiar el formato; v1 solo acepta `1`.
3. Quien lo oye y es destinatario cambia a VOICE. El emisor espera **150 ms** (tiempo de sobra para
   reprogramar SF/BW, unos pocos ms) y empieza la ráfaga.
4. Todo lo de la sesión (fragmentos, NACK, ACK final, VABORT) va en VOICE. Termina con ACK, VABORT,
   agotar reintentos o `NO_SIGNAL`; ambos lados regresan a ALARM.
5. Elegí **sin "listo" previo** (handshake) para ahorrar 0.3 s y porque el ANN en SF12 es más robusto que la
   ráfaga: un ANN perdido casi solo ocurre donde la voz tampoco llegaría.
6. Un nodo no destinatario nunca sale de ALARM: puede mandar su SOS durante un clip ajeno.

La grabación ocurre con el radio en ALARM, así que el emisor oye SOS mientras habla (y descarta la
grabación si llega uno).

## 6. Prioridad del SOS sobre un clip en curso

Primera versión: el receptor que pulsaba SOS se callaba y el emisor, sordo en SF9 y sin respuesta, reintentaba el
clip entero; el barrido del gate (pulsación cada 10 ms de 0 a 4 s) dio **máximo 8.1 s** (146/401 instantes >= 3 s). Se
aplicó el arbitraje de c-morty (`QA\02-Arbitrajes\2026-10-02-lora-radio-sos-durante-clip.md`):

* **SOS del receptor en su ranura de voz.** El receptor del clip que pulsa SOS **se queda en VOICE**, y en su
  ranura de ACK, después del fin estimado de la ráfaga, manda `ACK(status = ACK_SOS = 2)` del clip. El emisor,
  esperando respuesta, lo recibe, termina el clip (`CLIPTX_REMOTE_SOS`) y vuelve a ALARM; el receptor pasa a
  ALARM y manda el SOS normal. El emisor ya no reintenta el clip.
* **El SOS pendiente nunca se pierde** (re-gate S9). Mientras espera la ranura, el SOS del receptor está
  "pendiente" (`sos_pending`). Todo cierre de la recepción lo arma con `vr_release_sos()`: ACK_SOS transmitido,
  VABORT del emisor (si el emisor también pulsó SOS), cierre normal del clip o cualquier aborto. Si la ranura no
  llega a transmitirse (el radio rechaza el TX o la cola de ACK está llena), a los **300 ms** de la ranura
  (`ACK_SOS_TIMEOUT_MS`) el receptor sale de VOICE y manda el SOS igual. Antes, en esos casos, el nodo quedaba
  en VOICE para siempre y otra pulsación daba `SEND_ALREADY` (botón SOS muerto).
* **Clip máximo 8 s** (ráfaga más corta, 4 ranuras de ACK caben).
* **`RETRY_LISTEN_MS = 3650`**: entre intentos de clip el emisor escucha en ALARM más que el periodo máximo de
  reintento del SOS (con backoff escalonado, 3396 ms) + su ToA.

| Situación | Qué pasa | Medido [DER] (barrido, tras el ANN, 16 semillas) |
|---|---|---|
| El emisor del clip presiona SOS | termina el fragmento en el aire, manda **VABORT** en VOICE, pasa a ALARM y manda SOS | peor caso **3139 ms** = 2335 + 3 ranuras de 268 (cota; igual con 256 semillas); p50 815 ms. >= 3000 ms en 20 de 6000 casos (0.33 %), solo si pulsa a 2170-2200 ms del inicio, en los ~30 ms que siguen al fin de su propia ráfaga: el receptor aún cierra en VOICE, pierde el primer SOS y el reintento lleva backoff. Aceptado y declarado por c-morty (2026-10-12); puerta del test 3200 ms |
| Llega un SOS ajeno antes de que empiece la ráfaga (ANN o guarda) | el clip se cancela (`REMOTE_SOS`), VABORT, el SOS se ACKea | probado |
| El **receptor** presiona SOS (broadcast, 2 nodos) | ACK_SOS en su ranura de voz (arriba) | peor caso **2561 ms** (no depende de la semilla: 16/16 y 256/256) |
| Ídem con 3 nodos | ídem | peor caso **2618 ms** (ídem) |
| El receptor presiona SOS y el emisor también, antes de la ranura | VABORT del emisor; el receptor arma su SOS al cerrar; llegan los dos | probado (`edge.cpp` E2) |
| La ranura ACK_SOS no sale (radio rechaza, cola llena) | a los 300 ms el receptor deja VOICE y manda el SOS | probado (`edge.cpp` E1, E1b) |
| Dos nodos en reposo pulsan SOS con 0-1500 ms de diferencia | backoff aleatorio en los reintentos (sec. 3) | siempre se entregan: alarma hasta **17.3 s** (3 nodos; 15.5 s con 2), ACK hasta **18.3 s** (2 nodos; 1024 semillas, 0 sin ACK) |
| *(función futura)* Clip unicast y un tercero que no es destinatario manda SOS | los participantes están en SF9 y no lo oyen hasta que terminan; el SOS se reintenta | peor caso **4628 ms** (igual con 256 semillas); no ocurre en v1 (`VOICE_DST` broadcast constante) |

`tests/host/sweep.cpp` repite el barrido (16 semillas) en cada corrida y **falla** si, tras el ANN, algún nodo
nunca recibe la alarma o el máximo llega a 3000 ms (receptor, 2 y 3 nodos) o a 3200 ms (emisor). El unicast con
tercero se imprime sin puerta (función futura). `tests/host/edge.cpp` (inyección de fallas) y
`tests/host/dualsweep.cpp` (dos SOS, 256 semillas, límite 20000 ms y 0 sin entrega) también fallan si se rompe su límite; las
puertas se comprobaron rompiéndolas a propósito (quitar `vr_release_sos`, quitar el plazo de 300 ms, backoff de
1 ranura, `RETRY_LISTEN_MS 2400`, 5 ranuras de backoff (con las 8 de ahora, `RETRY_LISTEN_MS` menor que el periodo máximo): cada mutación da código 1).

Límites que quedan para el manual (sin LBT; CAD del SX1262 queda como mejora):

* SOS durante un clip: <= 3.0 s, salvo si lo pulsa el emisor en los ~30 ms que siguen al fin de su ráfaga:
  <= 3.14 s (simulador).
* Un SOS que arranca a menos de ~0.26 s del ANN de otro nodo choca con él. Con 2 nodos se entrega igual, en
  hasta **5.2 s** (5164 ms; igual con 256 semillas). Unicast futuro con un tercero: hasta 4.6 s (4628 ms), solo como función futura.
* **Con 3 o más nodos, un SOS pulsado durante el ANN de otro (0-240 ms del inicio del clip) puede dejar al
  emisor del clip sin la alarma** (`sweep.cpp`, resultado 88888 en 375 de 400 casos antes del ANN con 16
  semillas, 6085/6400 con 256): el SOS choca con el ANN, el emisor pasa a la ráfaga en SF9 sin haberlo oído, un
  tercer nodo lo acusa y el SOS broadcast se da por entregado con ese primer ACK. El que pulsó recibe ACK (hasta
  2840 ms) y el tercero la alarma; el emisor del clip no. La mejora sería reintentar hasta que acusen todos los
  pares conocidos. El barrido no lo cuenta como falla porque está antes del fin del ANN; el manual científico
  lo declara.
* Dos SOS casi simultáneos: se entregan y se acusan siempre; alarma hasta 17.3 s (3 nodos; 15.5 s con 2) y ACK hasta 18.3 s (2 nodos) (simulador,
  1024 semillas). Simulador sin efecto captura ni ruido; la prueba de campo de dos SOS va al manual de armado.

## 7. Constantes de tiempo

Todas en `node_fsm.h` (con su unidad): `ACK_TURNAROUND_MS 20`, `ACK_MARGIN_MS 200`, `VOICE_GUARD_MS 150`,
`FRAG_GAP_MS 10`, `RESP_WAIT_MS 400`, `RETX_START_MS 30`, `FRAG_IDLE_MS 150`, `NACK_MARGIN_MS 60`,
`RETX_WAIT_MS 700`, `FIRST_FRAG_SLACK_MS 500`, `RETRY_LISTEN_MS 3650`, `RETRY_BACKOFF_SLOTS_EARLY 4`, `RETRY_BACKOFF_SLOTS 8`,
`ACK_SOS_TIMEOUT_MS 300`, `MAX_NACK_ROUNDS 2`,
`MAX_CLIP_ATTEMPTS 2`, `MAX_CLIP_PAIRS 100` (8 s). Los tiempos de aire salen de `airtime.h` (fórmula Semtech; coincide con el
scientist: SF9/BW500/255 B = 312.6 ms, SF12/BW500/255 B = 1927.2 ms, verificado en los tests).

## 8. Contrato con el radio

El FSM no toca hardware: pide `radio_set_profile`, `radio_tx` y recibe `on_rx`, `on_tx_done`, `tick`. El
driver reinicia RX **antes** de avisar `on_tx_done`. Un watchdog de TX (1.5 s) aborta una transmisión
que no reporta fin. Reloj: `millis()` envuelve cada 49 días; todas las comparaciones son con resta con signo.

## 9. Registro serie (para las pruebas de alcance)

Una línea por evento, 12 campos tras `LOG`:

```
LOG,<ms>,<ev>,<self>,<peer>,<type>,<id>,<rssi>,<snr>,<a>,<b>,<c>,<d>
```

| ev | peer | a | b | c | d | rssi/snr |
|---|---|---|---|---|---|---|
| `TX` | destino | bytes | | | | |
| `RX` | origen | | | | | del paquete |
| `ACK` | quien acusó | RSSI remoto | SNR remoto x100 | intentos | ms desde la pulsación (mensaje aceptado por el firmware), no desde el primer TX | del ACK |
| `FAIL` | | 1 = reemplazado | | intentos | | |
| `CLIPTX` | quien acusó | código | fragmentos sin entregar | intentos | ms PTT->ACK | del ACK |
| `CLIPRX` | origen | 0 completo / 1 parcial / 2 duplicado | máscara faltante | rondas | ms ANN->completo | |
| `CLIPSTART`, `CLIPABORT`, `CLIPSEND`, `RADIOERR`, `TXWDT`, `MICERR`, `BOOT` | diagnóstico | | | | | |

`tools/registro_serie.py` guarda el CSV y resume PDR (ida y vuelta), RSSI/SNR (media, desviación) y
latencias de clips. El comando serie `prueba 100 3000` dispara 100 PRUEBA, una cada 3 s.
