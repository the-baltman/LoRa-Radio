# Caja LoRa-Radio (rev B.2)

Caja paramétrica en OpenSCAD, diseño de `mechanical-engineer`, aprobada por c-morty (2026-10-02). Exterior
**87.8 x 125.8 x 41.4 mm**, dos mitades (frente + trasera) con cierre de lengüeta, **5 tornillos M3x30 + 5
insertos M3** en el frente. Material PETG, boquilla 0.4, capa 0.2. Decisiones, tolerancias, orientación de
impresión y lo que falta medir: `Mecanica\03-Disenos\LoRa-Radio-caja.md` (bóveda de mecánica).

| Archivo | Qué es |
|---|---|
| `params.scad` | Todas las medidas (mm), con su origen |
| `caja_lora.scad` | Modelo; `part = front \| back \| plate_ui \| plate_sos \| bayplate \| pl_ptt \| pl_sos \| pl_nav_sq \| pl_nav_rd \| ring_sos \| assembly \| exploded \| section \| check_<x>` |
| `stl/` | Las 10 piezas listas para el slicer: `front`, `back`, `plate_ui`, `plate_sos`, `bayplate`, vástagos `pl_ptt`, `pl_sos`, `pl_nav_sq`, `pl_nav_rd`, aro `ring_sos` |
| `renders/` | 01 frente iso, 02 trasera iso, 04 y 05 piezas en posición de impresión, 06 corte interior, 07 explosionado, 08 trasera transparente, 09 corte lateral con componentes fantasma |
| `build.sh` | Regenera las STL (salvo `front`) y los 7 checks de interferencia en `out/` |
| `stlinfo.py` | Volumen y cajas de cada bloque de un STL: `python stlinfo.py out/check_*.stl` (interferencia = V > 0) |

## Regenerar

```bash
OSC=/ruta/a/openscad hardware/caja/build.sh        # OpenSCAD 2021.01+; escribe en hardware/caja/out/
openscad -o out/front.stl -D 'part="front"' caja_lora.scad   # el frente no está en el lazo de build.sh
python stlinfo.py out/check_*.stl                  # los 7 checks deben dar V = 0.00
```

Las STL de `stl/` son las que generó mecánica para B.2 (idénticas byte a byte a su `out/`); copiar de `out/` a
`stl/` solo después de revisar los checks y los renders.
