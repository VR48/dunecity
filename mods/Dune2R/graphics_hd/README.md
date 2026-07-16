# Dune2R Enhanced Graphics

Place high-resolution anchored graphics here.

Current supported layout:

```text
objpics/
  ObjPic_Tank_Base.png
  ObjPic_Tank_Base.ini
  ObjPic_Tank_Gun.png
  ObjPic_Tank_Gun.ini
```

Each PNG is an atlas. For Dune II-style facing sprites, use 8 columns by 1 row
in the same draw-angle order as classic objpics.

Example metadata:

```ini
[Sprite]
Columns=8
Rows=1
AnchorX=256
AnchorY=256

[Render]
BaseWidth=48
BaseHeight=48
Scale=1.18
```

`AnchorX` and `AnchorY` are measured in the source frame. The anchor lands on
the same world point used by classic unit sprites. `BaseWidth` and `BaseHeight`
are zoom-0 screen pixels before `Scale`; zoom levels multiply them by 1, 2, and
3.

Large padded Dune2R renders belong here. Tiny Dune II compact replacements
belong under `graphics_compact/`.
