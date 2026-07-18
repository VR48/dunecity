# Tornie changelog

## 1.0.523 - 2026-07-18

- Added generic ninth-house content registration for Tharpique: slot 8, letter T, region prefix THA, cyan palette ramp, and Mercenary fallback.
- Added the Tharpique campaign, region map, herald, voice assets, Chani Mentat configuration, and tested technology overrides.
- Reworked all nine Tornie campaigns and opening scenarios with faction-specific opponents, varied starting units, WOR placement, preserved seeds, corrected start screens, and 1000-credit intro objectives.
- Corrected Harkonnen campaign region progression using the final tested REGIONH data.
- Updated Scoutpost damage, Tharpique IX units, Sonic Trike and Trike availability, Trooper technology, and special-vehicle rules.
- Updated Sonic Trike, Rocket Trike mask, Tornie building coloration, and custom cyan palette graphics.
- Updated Worfinery graphics and occupied-Harvester overlay content.
- Updated Neutral, Rebels, and Tharpique voice assets.
- Added mod-scoped Mentat declarations for Atreides, Neutral, Rebels, and Tharpique.
- Added machine-readable manifest, SHA-256 checksums, provenance notes, and exact-case filenames.
- No DuneCity application version, save format, or multiplayer protocol change is included in this content milestone.
## 1.0.523 presentation integration follow-up

- Register the Tharpique herald and processed house-name voice through the generic mod-scoped custom-house hooks.
- Require the generic custom-house palette routing and editor Team9 correction.

## 1.0.523 intro and sprite-colour follow-up

- Removed all CPU-owned structures from the nine Tornie opening scenarios while preserving enemy units, player Construction Yard and WOR, credits, objectives, seeds, start screens, and unit placement.
- Require the generic HOUSE_CUSTOM sprite-palette correction so only the Harkonnen colour ramp is remapped to the active mod palette on units and buildings.
