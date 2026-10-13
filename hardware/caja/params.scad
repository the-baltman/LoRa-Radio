// =====================================================================
// params.scad  -  TABLA DE PARAMETROS de la caja LoRa-Radio (rev A, 2026-10-02)
// Cuando llegue la BOM final de electronics-engineer SOLO se editan estos valores.
// Unidades: mm. Ejes: x = ancho (izq->der), y = alto (abajo->arriba), z = profundidad
// (z=0 es la cara INTERIOR de la pared frontal; z crece hacia atras).
// Etiquetas de fuente:
//   [DS]  valor tomado de hoja de datos / norma (citada, aun asi verificar con la pieza)
//   [TIP] valor tipico de catalogos de modulos; cada fabricante varia
//   [PV]  POR VERIFICAR: medir con calibrador la pieza que llegue
//   [DEC] decision de diseno (no es una medida)
// =====================================================================

// ---------- Impresion FDM (PETG, boquilla 0.4, capa 0.2) ----------
wall      = 2.4;   // [DEC] pared exterior = 6 perimetros de 0.4
pt        = 2.0;   // [DEC] pared interior (mamparas) = 5 perimetros
r_out     = 5;     // [DEC] radio de esquina exterior
tg_w      = 1.0;   // [DEC] ancho de la lengueta del cierre
tg_h      = 2.0;   // [DEC] alto de la lengueta
tg_cl     = 0.2;   // [DEC] holgura por lado ranura/lengueta (FDM)
pcb_slack = 0.4;   // [DEC] holgura por lado en nidos de PCB (FDM, ajustar tras impresion de prueba)

// ---------- ESP32 DevKit (Steren, USB-C) ----------
// Modulo ESP32-WROOM-32: 18.0 x 25.5 x 3.1 [DS Espressif ESP32-WROOM-32 datasheet, "Module dimensions"]
// Placa DevKit completa (30 o 38 pines): ~51-55 x 25-29 [TIP, PV]. Se usa el ENVOLVENTE peor caso.
esp_L      = 50.0;  // [LST Steren, render] 25.6 x 50, 38 pines (nota electronics 2026-10-02) [PV medir]
esp_W      = 25.6;  // [LST Steren]
esp_t      = 1.6;   // [TIP] espesor PCB
esp_hdr    = 8.5;   // [TIP] pines de cabezal por debajo del PCB (3 plastico + 5.5 pin)
esp_lift   = 17.0;  // [ELEC] cara trasera del PCB de la DevKit a la pared trasera, DevKit en zocalo hembra (>= 17) [PV]
usbc_open_w = 13.0; // [DS USB Type-C Cable&Connector Spec: sobremoldeo del plug <= 12.35 x 6.5] + 0.65 holgura
usbc_open_h = 7.2;  // [DS idem]
usbc_cz_off = 1.6;  // [TIP] centro del conector USB-C bajo la cara de componentes de la DevKit [PV]
esp_sock_h  = 8.5;  // [TIP] altura del zocalo hembra de 2.54 mm (MGM pinhembra) [PV]
pcb_w = 50.0; pcb_l = 68.0; pcb_t = 1.6;   // [DEC] baquelita 80x60 CORTADA a 50 x 68: PROPUESTA, requiere OK de electronics
esp_x_off = 1.0;    // [DEC] DevKit a 1 mm del borde izquierdo de la baquelita, extremo USB al borde inferior
use_usb2    = true; // TP4056 micro-USB separado (rev B)
micro_open_w = 11.0;// [DS USB micro-B: sobremoldeo plug <= 10.6 x 8.5] + holgura
micro_open_h = 8.6;

// ---------- Modulo de carga TP4056 con micro-USB (DW01/8205): la CARGA es por aqui; el USB-C de la DevKit solo programa ----------
tp_w   = 26.0;      // [PV] largo (eje x, USB-micro al extremo derecho) ~26 x 17, foto MGM/Resistore
tp_l   = 17.0;      // [PV] ancho (eje y)
tp_t   = 1.2;       // [TIP]
tp_lift= 5.0;       // [DEC]
tp_vent_n = 3;     // ranuras de ventilacion junto al TP4056 (nota electronics: ventilacion junto al TP4056)
// NOTA: el TP4056 "pelon" NO protege la celda; electronics debe elegir version con DW01+FS8205 (con proteccion).

// ---------- Amplificador I2S tipo MAX98357A (breakout) ----------
amp_w  = 18.0;      // [LST MakerHawk 18 x 19 x 10]; HiLetgo [PV]
amp_l  = 19.0;
amp_t  = 1.6;
amp_lift = 8.0;     // [DEC] pines 2.5 + terminal de tornillo/soldadura

// ---------- Modulo LoRa (SX1262/SX1276) ----------
// Envolvente que cubre E22-900M22S (~24 x 16) y breakouts tipo Ra-01SH/-02 sobre placa (~26 x 20) [TIP, PV]
lora_w = 19.0;      // [DS Waveshare Core1262-HF 19.0 x 22.0, paso 2.54, IPEX]
lora_l = 22.0;
lora_t = 1.6;
lora_lift = 9.0;    // [DEC]

// ---------- OLED 0.96" I2C (SSD1306 128x64) ----------
oled_w  = 27.3;     // [TIP] PCB ancho (x)  [PV]
oled_h  = 27.8;     // [TIP] PCB alto  (y)  [PV]
oled_hole_dx = 23.5;// [TIP] separacion de agujeros M2 en x [PV]
oled_hole_dy = 23.5;// [TIP] en y [PV]
oled_glass_h = 1.5; // [TIP] vidrio+cinta sobre la cara del PCB [PV]
oled_win_w = 23.5;  // [DS panel UG-2864 area activa 21.74 x 10.86] + margen de 0.9 por lado
oled_win_h = 12.5;  // [DS idem]
oled_win_dy = 3.0;  // [PV] la pantalla NO esta centrada en el PCB: desplazamiento del centro de la ventana en +y (cabezal abajo)

// ---------- Microfono I2S tipo INMP441 (breakout) ----------
// Encapsulado INMP441: 4.72 x 3.76 x 1.0 mm, puerto ACUSTICO INFERIOR [DS INMP441 datasheet, InvenSense/TDK]
mic_w  = 14.5;      // [TIP] PCB (x) [PV]
mic_h  = 15.0;      // [TIP] PCB (y) [PV]
mic_port_dx = 0.0;  // [PV] posicion del orificio del PCB respecto al centro del PCB
mic_port_dy = 0.0;  // [PV]
mic_port_d  = 1.6;  // [DEC] puerto en la pared (>= orificio del PCB)
mic_web     = 1.2;  // [DEC] espesor de pared que queda en el puerto (L corto -> Helmholtz alto)

// ---------- Bocina 40 mm ----------
spk_d = 36.0;       // [LST Resistore 8 ohm 0.5 W 36 mm]
spk_h = 5.5;        // [TIP] altura bastidor+iman; las de 40 mm varian 4-6 mm [PV]
cham_h = 42.0;      // [DEC] alto de la camara sellada (y); ancho = left_col_w
grille_hole = 3.0;  // [DEC] orificio de rejilla
grille_pitch= 4.6;  // [DEC] paso hexagonal -> ~40 % area abierta
grille_R    = 14.0; // [DEC] radio efectivo del cono (~28 mm) [PV]

// ---------- Bateria 18650 + porta ----------
cell_d   = 18.0;    // [LST Steren, celda 18650 de 2200 mAh, 65 x 18, sin proteccion propia]
cell_l   = 65.0;
holder_L = 80.0;    // [TIP] porta 1x18650 plastico con cables, envolvente [PV]
holder_W = 22.0;    // [TIP] [PV]
holder_H = 21.0;    // [TIP] [PV]
bay_end_clr = 1.2;  // [DEC] holgura por extremo
bay_w_extra = 6.0;  // [DEC] holgura total en ancho
foam_t   = 4.0;     // [DEC] rev B.1: 2 capas de EVA de 2 mm (comprimen ~10-20 %) detras del porta
bay_plate_t = 1.6;  // [DEC] tapa interna de la bahia
bay_slack = 0.0;    // [DEC] rev B.1: la holgura vertical la absorbe el EVA

// ---------- Interruptores tactiles (PTT, SOS, navegacion) ----------
sw_w        = 6.0;  // [TIP] 6x6 mm
sw_plunger  = 1.5;  // [LST Resistore 6x6x5] vastago ~1.5 sobre cuerpo [PV]
sw_body     = 3.5;  // [PV] 6x6x5 total = cuerpo 3.5 + vastago 1.5
sw_leg_trim = 1.5;  // [DEC] recortar patas a 1.5 mm
fl_t        = 1.2;  // [DEC] espesor de la brida del vastago impreso
ui_clear    = 0.3;  // [DEC] holgura diametral del vastago en su orificio

// ---------- Conector SMA de panel (bulkhead, hembra) ----------
sma_hole   = 6.5;   // [DS rosca 1/4-36 UNS-2A = 6.35 mm] + 0.15
sma_flat   = 5.9;   // [PV] ancho del plano D (varia por fabricante)
sma_nut_d  = 10.0;  // [PV] circulo inscrito de tuerca+rondana de seguridad
sma_zone   = 12.0;  // [DEC] fondo libre bajo la pared superior para cuerpo del conector + pigtail
sma_x      = 60.0;  // [DEC] posicion en x (rev B: encima de la zona del TP4056, lejos de la celda)

// ---------- Tornilleria ----------
insert_hole  = 4.1; // [DEC] M3 x 5.7 de latón: 4.0-4.2 recomendado (CNC Kitchen) [PV con el insert comprado]
insert_depth = 6.5;
m3_clear     = 3.5; // [DEC] pasante M3 en FDM
m3_head_cb   = 6.6; // [DEC] avellanado/cabeza pan M3 (~5.7 x 2.4)
boss_od      = 8.5;
m2_pilot     = 1.7; // [DEC] autorroscante M2 en PETG [PV]
m2_clear     = 2.4;
rib_n        = 0;   // reservado

// ---------- Layout (decisiones) ----------
left_col_w = 53.0;  // [DEC] columna izquierda: ESP32 + (TP4056, amp) + camara de bocina
zs         = 10.0;  // [DEC] plano de union frontal/trasera (z). Capa frontal 0..10: OLED, botones, mic
vent_n     = 3;     // ranuras de ventilacion por extremo de la bahia
vent_w     = 1.2;   // [DEC] ancho de ranura (< 2 mm: no pasa un dedo ni un clip)
vent_len   = 16;

gasket_t   = 2.0;   // [DEC] rev B.1: EVA 2 mm bajo el aro de bocina y bajo el anillo del mic
holder_note = 1;    // PORTAPILA STEREN 258-813: SIN MEDIDAS PUBLICADAS. 80x22x21 es supuesto con holgura; MEDIR EL DIA 1
// ---------- Rev B: SW1, fusible, MCP1700 ----------
sw1_cut_w = 13.0;   // [PV] mini balancin Resistore 3 A: corte tipico 13 x 9 (cuerpo 15 x 10)
sw1_cut_h = 9.0;
sw1_body_d = 15.0;  // [PV] profundidad del cuerpo detras del panel
sw1_panel  = 1.6;   // [DEC] espesor local de panel para el clip (rebaje interior)
sw1_y      = 112;   // [DEC] posicion y en pared derecha (arriba, lejos del agarre)
// M1 (gate c-morty): SIN ventana de fusible (pared ciega frente a XF1a). El fusible se cambia abriendo la caja con SW1 en OFF.
fuse_y     = 60.0;  // [ELEC] fusible en y 50-68 de la baquelita, x 1-27 (sobre la DevKit)
// MCP1700 TO-92 (x2), caps, resistencias: van en la baquelita, sin hueco propio; alto libre sobre la baquelita: 18 mm hasta z=zs (rev B.1: electroliticos parados 11-12 mm, TO-92 parados 7.5, fusible+clip ~9 caben; ver echo head_free)
