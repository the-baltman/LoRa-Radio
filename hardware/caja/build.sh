#!/bin/bash
# Compila STL de cada pieza y checks de interferencia. Requiere OpenSCAD 2021.01+ (ruta en $OSC).
OSC="${OSC:-openscad}"
cd "$(dirname "$0")"; mkdir -p out
for p in front back plate_ui plate_sos bayplate pl_ptt pl_sos pl_nav_sq pl_nav_rd ring_sos; do
  "$OSC" -o out/$p.stl -D "part=\"$p\"" caja_lora.scad > out/$p.log 2>&1 &
done
wait
for p in front back; do :; done
for c in check_back check_front check_pl_ui check_pl_sos check_plates check_fb check_bay; do
  "$OSC" -o out/$c.stl -D "part=\"$c\"" caja_lora.scad > out/$c.log 2>&1 &
done
wait
echo done
