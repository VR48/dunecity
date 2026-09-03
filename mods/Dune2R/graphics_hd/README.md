# Dune2R Enhanced Graphics

Place high-resolution anchored graphics here.

## Runtime packs

Enhanced assets are stored as independently downloadable directories under
`units/`. Despite the historical directory name, a pack may describe a moving
unit (`unit.ini`), a footprint-anchored structure (`building.ini`), or terrain
topologies (`tile.ini`).

### Animated units

Unit animations are complete-unit atlases selected by stable game item ID and,
optionally, house ID. This avoids changing shared classic chassis/turret
sprites used by other vehicles.

The packager prefers Oathkeeper's optional `full_unit_idle`,
`full_unit_movement`, `full_unit_combat`, and directional full-unit damage
products. If an optional product
has no processed frame sequence but does have its baked aligned Sprite Image,
that sprite is packaged as a one-frame atlas. Older complete-unit categories
remain a compatibility fallback.

```text
units/
  ordostank/
    unit.ini
    atlases/
      idle/east.png
      movement/east.png
      combat/east.png
```

Each atlas is a row-major grid of transparent PNG frames. Example manifest:

```ini
[Unit]
ItemID=41
HouseID=2

[Render]
BaseWidth=48
BaseHeight=48
Scale=1.21

[Idle.east]
Atlas=atlases/idle/east.png
Columns=8
Rows=6
Frames=41
FrameMs=90
AnchorX=96
AnchorY=96
Loop=true
```

Supported visual states are `Idle`, `Movement`, `Combat`, `DamageSmoking`,
`DamageDamaged`, `DamageExploded`, `DamageAftermath`, and
`DamageDissipation`; supported
directions are `east`, `north_east`, `north`, `north_west`, `west`,
`south_west`, `south`, and `south_east`. Missing states or directions fall
through to the classic renderer. Combat is triggered only by an actual weapon
fire event and remains cosmetic; it does not change simulation or save data.
Complete-tank sprites use the chassis direction while moving and the weapon
direction while stationary or firing, presenting the vehicle as a rigid
Dune II-style unit without changing projectile or targeting mechanics.

`Dune2R EditoR` is available from the main menu only while Dune2R is the exact
active mod. For each packaged unit/state/direction slot, `Layered` uses the
existing independent chassis/turret renderer, `Full Animation` uses the atlas
described here, and `Random` chooses between those renderers per gameplay
transition. Missing complete-unit slots always fall through to the layered
renderer. Preferences are local and cosmetic.

When Dune2R is active, its in-game Zoom button cycles three local presentation
views: Action (3x), Tactical (2x), and Strategic (1x). The selected view changes
only rendering and camera coverage; simulation, multiplayer synchronization,
and save data are unaffected. The control is hidden for every other mod. The
mouse wheel changes view while it is over the map; scrollable interface widgets
retain priority and continue consuming wheel input normally.

The adjacent `Classic` / `Dune2R` button crossfades all available enhanced
terrain, building, HD objpic, and complete-unit art over the classic renderer.
The target mode is saved locally in `Dune City.ini`; it is not simulation or
network state.

### Buildings and terrain

Building source frames are normalized to `192` authored pixels per footprint
tile and split into atlases no larger than `2048x2048`. Rectangular cells keep
their own width and height; atlas rows never assume square building frames. Placement,
construction, damage, repair, and destruction play once; idle and working
states loop. If processed frames are absent, the category's Sprite Image is
packaged as a one-frame fallback. Each state also carries a separate `Still`
PNG (the authored Sprite Image, or first processed frame when no sprite exists).
Native builds decode one animation page at a time on a worker, prefetch the next
page, and retain at most 192 MiB of building atlas textures in an LRU cache.
PNG dimensions are checked before decoding. While a page loads or after a
failed page, the state still takes priority over classic graphics. Single-threaded
WebAssembly builds use deferred page decoding instead of native worker threads.
The engine anchors every state at the bottom
center of the fixed collision footprint, allowing smoke and effects to extend
above it without changing gameplay bounds.

The `refinery` pack is Atreides-only (`ItemID=10`, `HouseID=1`). Other houses
retain their classic Refinery visuals.

Terrain packs contain all 16 north/east/south/west topology variants. Each
enhanced tile is `192x192`, remains opaque and padding-free, and is rendered
into the same logical tile rectangle as classic terrain. Compact `16x16`
products travel in the same pack for later compact-mode support.

Package numbered Oathkeeper PNG frames with:

```text
python scripts/package-dune2r-unit.py <dune2/units/unit> <mods/Dune2R/graphics_hd/units/unit> --item-id 41 --house-id 2 --scale 1.21
```

For normal iteration, Oathkeeper packages and mounts a configured unit into
this source mod and, when present, the current Windows AppData installation:

```text
~dune2mount HarkonnenDevastator
```

The command infers the stable item and house IDs from the unit name. Explicit
full-unit products are preferred. When those are absent, completed independent
chassis and turret layers are aligned, scaled, animated, and composed into the
whole-unit atlases consumed by the current runtime. The command swaps the unit
directory as one update and writes a mount revision only after all files are
ready.

The repository copy under `mods/Dune2R/graphics_hd/units` is authoritative and
is included in later source commits and release packages. If the live AppData
mod exists, `~dune2mount` updates it too for immediate testing. If AppData was
deleted, mounting still succeeds against the repository and DuneCity seeds the
managed mod on its next launch.

A running Dune2R game checks `.mount-revision` about once per second and
releases its old atlas textures before loading the new package. `RELOAD MOUNTS`
performs the same rescan immediately. It reads packaged `unit.ini` manifests
and atlases; it does not read Oathkeeper's raw `dune2/units` authoring cache.

Only the active mod's `graphics_hd/units` directory is scanned. Switching away
from Dune2R clears every enhanced texture and manifest cache.

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
