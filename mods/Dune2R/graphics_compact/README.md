# Dune2R Compact Graphics

Place Dune II-compatible sprite atlas replacements here.

The loader currently supports PNG objpic atlas overrides. Preferred names use
the `ObjPic_` prefix; the shorter legacy form is also accepted.

```text
objpics/
  ObjPic_Tank_Base.png
  ObjPic_Tank_Gun.png
  ObjPic_Trike.png
  Tank_Base.png
```

These assets should match the classic `GFXManager` atlas shapes. Ground units
use 8 columns by 1 row in DuneCity draw-angle order.

Tiny Dune II compact exports should go here. Larger padded Dune2R/enhanced
renders belong under `graphics_hd/` and will use separate anchor metadata.
