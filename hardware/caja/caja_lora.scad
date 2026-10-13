// =====================================================================
// caja_lora.scad  -  Caja parametrica del radio LoRa-Radio (rev A)
// Uso:  openscad -o frente.stl -D 'part="front"' caja_lora.scad
// part = front | back | plate_ui | plate_sos | bayplate | pl_ptt | pl_sos |
//        pl_nav_sq | pl_nav_rd | ring_sos | assembly | exploded | section | check_<x>
// Todas las medidas salen de params.scad. Coordenadas de modelado: ver params.scad.
// =====================================================================
include <params.scad>

part = "assembly";
eps  = 0.01;
alpha_back = 1.0;   // 0.5 para ver a traves de la trasera
$fn  = 36;

// ---------------- derivadas ----------------
bay_w_i   = holder_W + bay_w_extra;
bay_len_i = holder_L + 2*bay_end_clr;
Wi        = left_col_w + pt + bay_w_i;
pcb_x0 = 0.5; pcb_y0 = 1.0; pcb_cx = pcb_x0+pcb_w/2; pcb_cy = pcb_y0+pcb_l/2;
tp_zone_top = bay_len_i + pt + 2.0 + tp_l + 2.0 + sma_zone;           // pila derecha
left_top = pcb_y0 + pcb_l + 8 + pt + cham_h;                            // pila izquierda
H_i       = max(tp_zone_top,left_top);
D_i       = zs + bay_plate_t + holder_H + foam_t + bay_slack;
cham_y0   = H_i - cham_h;
bx0       = left_col_w + pt;          // x donde empieza la bahia
W = Wi + 2*wall;  H = H_i + 2*wall;  D = D_i + 2*wall;

// tamanos/posiciones de botones: [id, forma, tamano, cx, cy, sobresale, largo de brida]
bay_cx = bx0 + bay_w_i/2;
btns = [
  ["ptt","r",14,16,24,1.5,19],
  ["n1","s",9,10,48,1.0,13],
  ["n2","r",9,26,48,1.0,13],
  ["n3","s",9,42,48,1.0,13],
  ["sos","r",14,bay_cx,20,0.5,19]
];
plate_ui  = [2,11,49,58];                                  // x0,y0,x1,y1
plate_sos = [bay_cx-9.5, 9, bay_cx+9.5, 31];
sw_plate_z0 = fl_t + 0.1 + sw_plunger - (0.0);             // cara trasera de la placa = frente del cuerpo del switch
plate_t = 1.2;
plate_zf = sw_plate_z0 - plate_t;                          // cara delantera de la placa
sleeve_h = sw_body + 0.6;
ring_h = 5.5;  ring_od = 26; ring_id = 18.4;

// OLED
ocx = bx0 + 0.3 + oled_w/2;  ocy = 45;
// microfono
mic_cx = 64.5;  mic_cy = H_i - 12;
// bocina
spk_cx = left_col_w/2;  spk_cy = cham_y0 + cham_h/2;
// bosses: [x,y]
bosses = [[55,5],[55,H_i-cham_h-5],[4.4,H_i-cham_h-5],[4.4,H_i-4.4],[47,H_i-4.4]];
// pressers sobre la tapa de bateria (costillas del frente)
pressers = [[bx0+9,3],[Wi-5,3],[bx0+9,76],[Wi-5,76]];
// placas: nido de tarjetas  [cx,cy,w,l,t,lift]
pcb_stand = esp_lift - esp_sock_h - pcb_t;          // separacion baquelita-pared trasera
pcb_zb = D_i - pcb_stand; pcb_zf = pcb_zb - pcb_t;     // caras trasera/delantera de la baquelita
esp_cx = pcb_x0 + esp_x_off + esp_W/2;  esp_cy = pcb_y0 + esp_L/2;
esp_zf = D_i - esp_lift - esp_t;                       // cara de componentes de la DevKit
tp_cx = Wi - 0.5 - tp_w/2;  tp_cy = bay_len_i + pt + 2.0 + tp_l/2;
tp_zf = D_i - tp_lift - tp_t;
// acomodo de electronica (arbitraje E4): franja derecha x 28-50: C_VS y 0-21, MAX98357 y 22-41, Core1262 y 44-66 (IPEX arriba)
lora_x = pcb_x0 + 28;  lora_y = pcb_y0 + 44;   amp_x = lora_x;  amp_y = pcb_y0 + 22;
holder_x = Wi - holder_W - 0.5;  holder_y = bay_end_clr;
holder_z = zs + bay_plate_t;     // cara frontal del porta

echo(str("Exterior W x H x D (mm): ", W, " x ", H, " x ", D));
echo(str("Camara de bocina (cm3, sin bocina): ", left_col_w*cham_h*D_i/1000 - PI*pow(spk_d/2,2)*spk_h/1000));
echo(str("head_free sobre baquelita hasta z=zs (mm): ", D_i-pcb_stand-pcb_t-zs));
echo(str("D_i=",D_i," bahia profundidad=",D_i-zs," (porta+placa+espuma=",holder_H+bay_plate_t+foam_t,")"));

// ---------------- 2D ----------------
module rr2(w,h,r){ translate([r,r]) offset(r=r) square([w-2*r,h-2*r]); }
module outer2(){ translate([-wall,-wall]) rr2(Wi+2*wall,H_i+2*wall,r_out); }
module inner2(){ rr2(Wi,H_i,max(r_out-wall,0.8)); }
module ring2(){ difference(){ outer2(); inner2(); } }
// mamparas extendidas dentro del anillo para continuidad de lengueta
module part2(){
  translate([left_col_w,-wall]) square([pt,H_i+2*wall]);                       // V
  translate([bx0,bay_len_i]) square([bay_w_i+wall,pt]);                        // H1 (tope bahia)
  translate([-wall,cham_y0-pt]) square([left_col_w+wall,pt]);                  // H2 (fondo camara)
}
module part2_in(){ intersection(){ part2(); inner2(); } }
module tongue2(){
  offset(delta=-(wall-tg_w)/2) ring2();
  offset(delta=-(pt-tg_w)/2) part2();
}
module groove2(){
  g = tg_w + 2*tg_cl;
  offset(delta=-(wall-g)/2) ring2();
  offset(delta=-(pt-g)/2) part2();
}
module profile2(shape,size){ if(shape=="r") circle(d=size,$fn=48); else square(size,center=true); }

// ---------------- FRENTE ----------------
module boss_front_solid(){ for(p=bosses) translate([p[0],p[1],0]) cylinder(d=boss_od,h=zs,$fn=36); }
module boss_front_holes(){
  for(p=bosses) translate([p[0],p[1],0]){
    translate([0,0,zs-insert_depth]) cylinder(d=insert_hole,h=insert_depth+1,$fn=30);
    translate([0,0,2.4]) cylinder(d=2.9,h=zs,$fn=24);
  }
}
module grille_holes(){
  for(i=[-8:8], j=[-8:8]){
    x = i*grille_pitch + (abs(j)%2)*grille_pitch/2;
    y = j*grille_pitch*0.866;
    if(sqrt(x*x+y*y) <= grille_R - grille_hole/2)
      translate([spk_cx+x,spk_cy+y,-wall-eps]) cylinder(d=grille_hole,h=wall+2*eps,$fn=20);
  }
}
module front_shell(){
  difference(){
    union(){
      translate([0,0,-wall]) linear_extrude(wall) outer2();
      linear_extrude(zs) ring2();
      linear_extrude(zs) part2_in();
      translate([0,0,zs-eps]) linear_extrude(tg_h+eps) tongue2();
      boss_front_solid();
      // bocina: aro de alojamiento
      translate([spk_cx,spk_cy,0]) difference(){ cylinder(d=spk_d+1.6+2.4,h=spk_h+gasket_t,$fn=64); translate([0,0,-eps]) cylinder(d=spk_d+1.6,h=spk_h+gasket_t+1,$fn=64); }
      // OLED posts
      for(sx=[-1,1],sy=[-1,1]) translate([ocx+sx*oled_hole_dx/2,ocy+sy*oled_hole_dy/2,0]) cylinder(d=5.5,h=oled_glass_h+0.3,$fn=24);
      // postes de placas
      for(pl=[[plate_ui,plate_zf],[plate_sos,plate_zf]]) for(p=plate_posts(pl[0])) translate([p[0],p[1],0]) cylinder(d=6,h=pl[1],$fn=24);
      // nido de microfono
      translate([mic_cx,mic_cy,0]) difference(){
        translate([-(mic_w/2+pcb_slack+1.2),-(mic_h/2+pcb_slack+1.2),0]) cube([mic_w+2*pcb_slack+2.4,mic_h+2*pcb_slack+2.4,gasket_t+3.6]);
        translate([-(mic_w/2+pcb_slack),-(mic_h/2+pcb_slack),-eps]) cube([mic_w+2*pcb_slack,mic_h+2*pcb_slack,5]);
      }
      // costillas que presionan la tapa de bateria
      for(p=pressers) translate([p[0]-1.5,p[1]-1.5,0]) cube([3,3,zs]);
    }
    // --- huecos
    boss_front_holes();
    // ventana OLED
    translate([ocx-oled_win_w/2,ocy+oled_win_dy-oled_win_h/2,-wall-eps]) cube([oled_win_w,oled_win_h,wall+2*eps]);
    // tornillos OLED (autorroscante M2, ciego)
    for(sx=[-1,1],sy=[-1,1]) translate([ocx+sx*oled_hole_dx/2,ocy+sy*oled_hole_dy/2,-1.4]) cylinder(d=m2_pilot,h=1.4+oled_glass_h+0.4,$fn=16);
    // orificios de botones
    for(b=btns) translate([b[3],b[4],-wall-eps]) linear_extrude(wall+0.2+2*eps) offset(delta=ui_clear/2) profile2(b[1],b[2]);
    // clavijas del aro SOS (ciegas desde afuera)
    for(a=[90,210,330]) translate([bay_cx+11.2*cos(a),20+11.2*sin(a),-wall-eps]) cylinder(d=2.4,h=1.6,$fn=16);
    // pilotos de placas
    for(pl=[plate_ui,plate_sos]) for(p=plate_posts(pl)) translate([p[0],p[1],-1]) cylinder(d=m2_pilot,h=1+plate_zf+0.1,$fn=16);
    // puerto de microfono: contrabarreno exterior + puerto corto
    translate([mic_cx+mic_port_dx,mic_cy+mic_port_dy,0]){
      translate([0,0,-wall-eps]) cylinder(d=6,h=wall-mic_web+eps,$fn=32);
      translate([0,0,-wall-eps]) cylinder(d=mic_port_d,h=wall+1,$fn=20);
    }
    // rejilla de bocina
    grille_holes();
    // canal de cables LoRa<->ESP32 sobre la bahia (muescas en mamparas, capa frontal)
    translate([left_col_w-eps,59.5,3]) cube([pt+2*eps,7,6.5]);
    translate([sma_x-2,bay_len_i-eps,3]) cube([10,pt+2*eps,6.5]);
    // grabado tactil alrededor del SOS (3 ranuras radiales por fuera)
    // (se omite: el aro pegado lo identifica; ver notas)
  }
}
function in_list(x,l) = len([for(i=l) if(i==x) 1])>0;
function plate_posts(pl) = [[pl[0]+3,pl[1]+3],[pl[2]-3,pl[1]+3],[pl[0]+3,pl[3]-3],[pl[2]-3,pl[3]-3]];

// ---------------- TRASERA ----------------
module boss_back(){
  for(p=bosses) translate([p[0],p[1],zs]) cylinder(d=boss_od,h=D_i-zs+eps,$fn=36);
}
module corner_nest(w,l,zb,zf){
  // esquina (+,+) respecto al centro; bloque de apoyo + paredes en L
  e = w/2; le = l/2; s = pcb_slack;
  translate([e-4,le-1.8,zb]) cube([4+s+1.2,1.8+s+1.2,D_i-zb+eps]);
  // paredes guia: parten del muro trasero (sin voladizos al imprimir)
  translate([e+s,le-6,zf-1.5]) cube([1.2,6+s+1.2,D_i-zf+1.5+eps]);
  translate([e-6,le+s,zf-1.5]) cube([6+s+1.2,1.2,D_i-zf+1.5+eps]);
}
module cradle(cx,cy,w,l,t,lift){
  zb = D_i-lift; zf = zb-t;
  for(sx=[-1,1],sy=[-1,1]) translate([cx,cy,0]) scale([sx,sy,1]) corner_nest(w,l,zb,zf);
}
module usb_cut(cx,zf){ translate([cx-usbc_open_w/2,-wall-eps,zf-usbc_cz_off-usbc_open_h/2]) cube([usbc_open_w,wall+1.5,usbc_open_h]); }
function zf_of(lift,t) = D_i-lift-t;

module back_shell(){
  difference(){
    union(){
      translate([0,0,D_i]) linear_extrude(wall) outer2();
      translate([0,0,zs]) linear_extrude(D_i-zs+eps) ring2();
      translate([0,0,zs]) linear_extrude(D_i-zs+eps) part2_in();
      boss_back();
      cradle(pcb_cx,pcb_cy,pcb_w,pcb_l,pcb_t,pcb_stand);
      if(use_usb2) cradle(tp_cx,tp_cy,tp_w,tp_l,tp_t,tp_lift);
    }
    // ranura de la lengueta (cierre)
    translate([0,0,zs-eps]) linear_extrude(tg_h+0.4+eps) groove2();
    // agujeros de tornillos de union (pasante M3 + cabeza en la tapa)
    for(p=bosses) translate([p[0],p[1],0]){
      translate([0,0,zs-1]) cylinder(d=m3_clear,h=D_i-zs+wall+2,$fn=24);
      translate([0,0,D_i]) cylinder(d=m3_head_cb,h=wall+1,$fn=30);
    }
    // USB-C de la DevKit (solo programacion) en pared inferior
    usb_cut(esp_cx,esp_zf);
    // micro-USB del TP4056 en pared derecha
    if(use_usb2) translate([Wi-eps,tp_cy-micro_open_w/2,tp_zf-1.3-micro_open_h/2]) cube([wall+1.5,micro_open_w,micro_open_h]);
    // ventilacion junto al TP4056: ranuras en pared superior sobre su zona
    for(i=[0:tp_vent_n-1]) translate([tp_cx-8+i*5,H_i-eps,tp_zf-8]) cube([vent_w,wall+2*eps,10]);
    // SW1 en pared derecha (ventana + rebaje interior para clip)
    translate([Wi-eps,sw1_y-sw1_cut_w/2,D_i/2-sw1_cut_h/2]) cube([wall+2*eps,sw1_cut_w,sw1_cut_h]);
    translate([Wi-(wall-sw1_panel)-eps,sw1_y-(sw1_cut_w+4)/2,D_i/2-(sw1_cut_h+4)/2]) cube([wall-sw1_panel+eps,sw1_cut_w+4,sw1_cut_h+4]);
    // (M1) sin ventana de fusible: pared ciega
    // SMA en pared superior (agujero con plano D: se resta circulo y se deja plano)
    translate([sma_x,H_i-eps,D_i/2]) rotate([-90,0,0]) difference(){
      cylinder(d=sma_hole,h=wall+1,$fn=36);
      translate([sma_flat/2,-5,-1]) cube([5,10,wall+3]);
    }
    // ventilacion de bahia: ranuras en pared derecha cerca de ambos extremos, z central
    for(i=[0:vent_n-1]){
      for(y0=[6,bay_len_i-6-(vent_n-1)*4])
        translate([Wi-eps,y0+i*4,zs+(D_i-zs-vent_len)/2]) cube([wall+2*eps,vent_w,vent_len]);
    }
    // salida de cables de bateria hacia el cargador (mampara V)
    // (rev B) cables de bateria: muesca en mampara H1 hacia el TP4056
    translate([bx0+4,bay_len_i-eps,D_i-12]) cube([8,pt+2*eps,6]);
    // paso de cables de bocina a la camara (mampara H2) -> sellar con pegamento caliente
    translate([42,cham_y0-pt-eps,D_i/2]) rotate([-90,0,0]) cylinder(d=4.5,h=pt+2*eps,$fn=20);
  }
}

// ---------------- PLACAS DE INTERRUPTORES ----------------
module ui_plate(box, ids){
  difference(){
    union(){
      translate([box[0],box[1],plate_zf]) cube([box[2]-box[0],box[3]-box[1],plate_t]);
      for(b=btns) if(in_list(b[0],ids))
        translate([b[3]-(sw_w+2*1.6)/2,b[4]-(sw_w+2*1.6)/2,sw_plate_z0-eps]) cube([sw_w+3.2,sw_w+3.2,sleeve_h]);
    }
    for(b=btns) if(in_list(b[0],ids)){
      translate([b[3],b[4],plate_zf-eps]) cylinder(d=4.6,h=plate_t+2*eps,$fn=24);
      translate([b[3]-(sw_w+0.3)/2,b[4]-(sw_w+0.3)/2,sw_plate_z0]) cube([sw_w+0.3,sw_w+0.3,sleeve_h+1]);
      // paso de patas hacia atras (abierto): ya queda abierto por detras
    }
    for(p=plate_posts(box)) translate([p[0],p[1],plate_zf-eps]) cylinder(d=m2_clear,h=plate_t+2*eps,$fn=16);
  }
}

// ---------------- BOTONES (vastagos impresos) ----------------
module plunger(shape,size,h_out,bar){
  Ls = wall + h_out;
  translate([0,0,-Ls]) linear_extrude(Ls+eps) profile2(shape,size);
  linear_extrude(fl_t) square([bar,6],center=true);
}
module ring_sos(){
  // en coordenadas de montaje: sobre la cara exterior (z<-wall); la cara de montaje es la de z mayor
  translate([0,0,-wall-ring_h]) difference(){
    cylinder(d=ring_od,h=ring_h,$fn=64);
    translate([0,0,-eps]) cylinder(d=ring_id,h=ring_h+1,$fn=64);
  }
  for(a=[90,210,330]) translate([11.2*cos(a),11.2*sin(a),-wall-1.4]) cylinder(d=2.2,h=1.4+eps,$fn=16);
}
module bay_plate(){
  x0=bx0+0.3; x1=bx0+bay_w_i-0.3; y0=0.6; y1=bay_len_i-0.6; ch=3.5;   // ch: chaflan de esquina (radio interior de la caja)
  difference(){
    translate([0,0,zs]) linear_extrude(bay_plate_t) polygon([[x0+ch,y0],[x1-ch,y0],[x1,y0+ch],[x1,y1-ch],[x1-ch,y1],[x0+ch,y1],[x0,y1-ch],[x0,y0+ch]]);
    // muescas para los bultos de tornillo de la mampara V
    for(p=bosses) if(p[0]>left_col_w && p[0]<bx0+3) translate([p[0],p[1],zs-1]) cylinder(d=boss_od+0.8,h=bay_plate_t+2,$fn=36);
  }
}

// ---------------- FANTASMAS (piezas comerciales, para verificar choques) ----------------
module ghost_board(cx,cy,w,l,t,lift,comp_h,col){
  zb = D_i-lift; zf = zb-t;
  color(col) translate([cx-w/2,cy-l/2,zf]) cube([w,l,t]);
  color("silver") translate([cx-w/2+2,cy-l/2+2,zf-comp_h]) cube([w-4,l-4,comp_h]);
}
module ghost_esp(){
  color("peru") translate([pcb_x0,pcb_y0,pcb_zf]) cube([pcb_w,pcb_l,pcb_t]);                    // baquelita
  color("darkgreen") translate([esp_cx-esp_W/2,esp_cy-esp_L/2,esp_zf]) cube([esp_W,esp_L,esp_t]); // DevKit
  color("silver") translate([esp_cx-9,esp_cy+esp_L/2-25.5-1,esp_zf-3.1]) cube([18,25.5,3.1]);     // modulo WROOM
  color("gray") translate([esp_cx-4.5,esp_cy-esp_L/2-0.5,esp_zf-3.3]) cube([9,7.4,3.3]);          // USB-C
  for(x=[1.0,esp_W-1.0-2.54]) color("black") translate([esp_cx-esp_W/2+x,esp_cy-esp_L/2+2.5,pcb_zf-esp_sock_h]) cube([2.54,esp_L-5,esp_sock_h]); // zocalos
  // Core1262-HF y MAX98357 sobre la baquelita (propuesta), alto ~12
  color("teal") translate([lora_x,lora_y,pcb_zf-12]) cube([lora_w,lora_l,12]);
  color("purple") translate([amp_x,amp_y,pcb_zf-10]) cube([amp_w,amp_l,10]);
  // fusible 5x20 con clip (eje en x) en y 50-68, x 1-27 (sobre la DevKit); alto 9
  color("orange") translate([pcb_x0+2,fuse_y-4,pcb_zf-9]) cube([25,8,9]);
  // C_VS 470 uF parado (8 x 11.5) y C7 220 uF parado (6.3 x 11) en la franja y 0-21
  color("lightblue") translate([lora_x+1,pcb_y0+2,pcb_zf-12]) cube([8,8,12]);
  color("lightblue") translate([lora_x+12,pcb_y0+3,pcb_zf-11]) cube([6.3,6.3,11]);
  // 2 MCP1700 TO-92 ACOSTADOS bajo la DevKit (entre filas del zocalo), alto 4.3; nada > 6 mm
  for(y=[10,20]) color("black") translate([pcb_x0+10,pcb_y0+y,pcb_zf-4.3]) cube([8,5,4.3]);
}
module ghost_sw1(){ color("black") translate([Wi-sw1_body_d,sw1_y-7.5,D_i/2-5]) cube([sw1_body_d-sw1_panel+0.0-0.0,15,10]); }
module ghost_holder(){
  color("dimgray") translate([holder_x,holder_y,holder_z]) cube([holder_W,holder_L,holder_H]);
  color("gold") translate([holder_x+holder_W/2,holder_y+(holder_L-cell_l)/2,holder_z+holder_H/2])
    rotate([-90,0,0]) cylinder(d=cell_d,h=cell_l,$fn=40);
  color("orange") translate([holder_x,holder_y,D_i-foam_t]) cube([holder_W,holder_L,foam_t]);
}
module ghost_oled(){
  color("black") translate([ocx-oled_w/2,ocy-oled_h/2,oled_glass_h+0.3]) cube([oled_w,oled_h,1.2]);
  color("navy") translate([ocx-13,ocy+oled_win_dy-6,0.3]) cube([26,12,oled_glass_h]);
  color("black") translate([ocx-5,ocy-oled_h/2+1,oled_glass_h+1.5]) cube([10,2.5,2.5]);   // cabezal
}
module ghost_spk(){ color("silver") translate([spk_cx,spk_cy,gasket_t]) cylinder(d=spk_d,h=spk_h-0.2,$fn=48); }
module ghost_mic(){ color("blue") translate([mic_cx-mic_w/2,mic_cy-mic_h/2,gasket_t]) cube([mic_w,mic_h,1.0]); }
module ghost_sw(){
  for(b=btns){
    pl = (b[0]=="sos") ? plate_sos : plate_ui;
    color("silver") translate([b[3]-sw_w/2,b[4]-sw_w/2,sw_plate_z0]) cube([sw_w,sw_w,sw_body]);
    color("silver") translate([b[3],b[4],plate_zf-0.0]) cylinder(d=3.4,h=sw_plunger,$fn=16);
  }
}
module ghost_sma(){
  color("goldenrod") translate([sma_x,H_i-8,D_i/2]) rotate([-90,0,0]) cylinder(d=6.3,h=8+wall+4,$fn=24);
  color("goldenrod") translate([sma_x,H_i-2.4,D_i/2]) rotate([-90,0,0]) cylinder(d=8.5,h=2,$fn=6);
  color("black") translate([sma_x,H_i+wall+4,D_i/2]) rotate([-90,0,0]) cylinder(d=8,h=82,$fn=24);   // antena lambda/4 ~ 82 mm
}
module ghosts(){
  ghost_esp();
  if(use_usb2) ghost_board(tp_cx,tp_cy,tp_w,tp_l,tp_t,tp_lift,3.3,"red");
  ghost_sw1(); ghost_holder(); ghost_oled(); ghost_spk(); ghost_mic(); ghost_sw();
}
module ghosts_nosma(){ ghost_esp(); if(use_usb2) ghost_board(tp_cx,tp_cy,tp_w,tp_l,tp_t,tp_lift,3.3,"red");
  ghost_sw1(); ghost_holder(); ghost_oled(); ghost_spk(); ghost_mic(); ghost_sw(); }

module all_printed(){
  front_shell(); back_shell(); bay_plate();
  ui_plate(plate_ui,["ptt","n1","n2","n3"]); ui_plate(plate_sos,["sos"]);
  for(b=btns) translate([b[3],b[4],0]) plunger(b[1],b[2],b[5],b[6]);
  translate([bay_cx,20,0]) ring_sos();
}

// ---------------- selector ----------------
module sect(){ difference(){ children(); translate([-50,-50,D_i*0.0+14]) cube([300,300,50]); } }
if(part=="front") translate([0,0,wall]) front_shell();
else if(part=="back") translate([0,H_i,D_i+wall]) rotate([180,0,0]) back_shell();
else if(part=="plate_ui") translate([0,0,-plate_zf]) ui_plate(plate_ui,["ptt","n1","n2","n3"]);
else if(part=="plate_sos") translate([0,0,-plate_zf]) ui_plate(plate_sos,["sos"]);
else if(part=="bayplate") translate([0,0,-zs]) bay_plate();
else if(part=="pl_ptt") translate([0,0,fl_t]) rotate([180,0,0]) plunger("r",14,1.5,19);
else if(part=="pl_sos") translate([0,0,fl_t]) rotate([180,0,0]) plunger("r",14,0.5,19);
else if(part=="pl_nav_sq") translate([0,0,fl_t]) rotate([180,0,0]) plunger("s",9,1.0,13);
else if(part=="pl_nav_rd") translate([0,0,fl_t]) rotate([180,0,0]) plunger("r",9,1.0,13);
else if(part=="ring_sos") translate([0,0,wall+ring_h]) ring_sos();
else if(part=="assembly"){
  color("lightsteelblue") front_shell(); color("lightgray",alpha_back) back_shell();
  color("tomato") bay_plate();
  color("white") ui_plate(plate_ui,["ptt","n1","n2","n3"]); color("white") ui_plate(plate_sos,["sos"]);
  for(b=btns) color(b[0]=="sos"?"red":(b[0]=="ptt"?"orange":"dimgray")) translate([b[3],b[4],0]) plunger(b[1],b[2],b[5],b[6]);
  color("red") translate([bay_cx,20,0]) ring_sos();
  ghosts(); ghost_sma();
}
else if(part=="exploded"){
  translate([0,0,-40]) { color("lightsteelblue") front_shell();
    for(b=btns) color(b[0]=="sos"?"red":(b[0]=="ptt"?"orange":"dimgray")) translate([b[3],b[4],0]) plunger(b[1],b[2],b[5],b[6]);
    color("red") translate([bay_cx,20,0]) ring_sos(); }
  translate([0,0,-18]) { color("white") ui_plate(plate_ui,["ptt","n1","n2","n3"]); color("white") ui_plate(plate_sos,["sos"]); ghost_oled(); ghost_spk(); ghost_mic(); }
  translate([0,0,0]) color("tomato") bay_plate();
  translate([0,0,22]) { color("lightgray") back_shell(); ghosts(); ghost_sma(); }
}
else if(part=="section"){ // corte en z=22 (vista desde el frente) de piezas impresas + fantasmas
  intersection(){ union(){ color("lightgray") back_shell(); ghosts(); ghost_sma(); color("tomato") bay_plate(); }
    translate([-50,-50,zs-0.5]) cube([300,300,D_i-zs+0.5]); }
}
else if(part=="check_back") intersection(){ back_shell(); ghosts_nosma(); }
else if(part=="check_front") intersection(){ front_shell(); ghosts_nosma(); }
else if(part=="check_plates") intersection(){ union(){ ui_plate(plate_ui,["ptt","n1","n2","n3"]); ui_plate(plate_sos,["sos"]); front_shell(); back_shell(); bay_plate(); }
   union(){ for(b=btns) translate([b[3],b[4],0]) plunger(b[1],b[2],b[5],b[6]); } }
else if(part=="check_pl_ui") intersection(){ ui_plate(plate_ui,["ptt","n1","n2","n3"]); union(){ front_shell(); back_shell(); bay_plate(); } }
else if(part=="check_pl_sos") intersection(){ ui_plate(plate_sos,["sos"]); union(){ front_shell(); back_shell(); bay_plate(); } }
else if(part=="check_fb") intersection(){ front_shell(); back_shell(); }
else if(part=="check_bay") intersection(){ bay_plate(); union(){front_shell(); back_shell(); ghost_holder();} }
