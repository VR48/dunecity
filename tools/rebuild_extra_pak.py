from pathlib import Path
import sys


def read_pak(path: Path):
    data = path.read_bytes()
    offset = 0
    entries = []

    while True:
        start = int.from_bytes(data[offset:offset + 4], "little")
        offset += 4
        if start == 0:
            break

        name_end = data.index(0, offset)
        name = data[offset:name_end].decode("ascii")
        offset = name_end + 1
        entries.append([name, start, 0])

    for index, entry in enumerate(entries):
        end = entries[index + 1][1] if index + 1 < len(entries) else len(data)
        entry[2] = end

    return {name: data[start:end] for name, start, end in entries}, [name for name, _, _ in entries]


def write_pak(path: Path, file_order, file_data):
    header_size = sum(4 + len(name.encode("ascii")) + 1 for name in file_order) + 4
    data_offset = 0

    with path.open("wb") as out:
        for name in file_order:
            out.write((header_size + data_offset).to_bytes(4, "little"))
            out.write(name.encode("ascii") + b"\0")
            data_offset += len(file_data[name])

        out.write((0).to_bytes(4, "little"))

        for name in file_order:
            out.write(file_data[name])


def add_or_replace(file_order, file_data, source: Path, pak_name: str):
    if pak_name not in file_data:
        file_order.append(pak_name)
    file_data[pak_name] = source.read_bytes()


def add_matching_ini_files(file_order, file_data, source_dir: Path, prefixes):
    added = 0

    for source in sorted(source_dir.iterdir(), key=lambda path: path.name.lower()):
        if not source.is_file():
            continue

        pak_name = source.name.upper()
        if not pak_name.endswith(".INI"):
            continue

        if any(pak_name.startswith(prefix) for prefix in prefixes):
            add_or_replace(file_order, file_data, source, pak_name)
            added += 1

    return added


def add_region_files(file_order, file_data, source_dir: Path, houses):
    added = 0

    for house in houses:
        source = source_dir / f"REGION{house}.INI"
        if not source.is_file():
            continue

        add_or_replace(file_order, file_data, source, source.name.upper())
        added += 1

    return added


def main():
    if len(sys.argv) != 4:
        print("Usage: rebuild_extra_pak.py <input Extra.PAK> <campaign_vanilla dir> <output Extra.PAK>", file=sys.stderr)
        return 2

    input_pak = Path(sys.argv[1])
    campaign_dir = Path(sys.argv[2])
    data_dir = campaign_dir.parent
    output_pak = Path(sys.argv[3])

    file_data, file_order = read_pak(input_pak)
    add_or_replace(file_order, file_data, campaign_dir / "REGIONN.INI", "REGIONN.INI")
    add_or_replace(file_order, file_data, campaign_dir / "REGIONR.INI", "REGIONR.INI")
    addon_scenario_count = add_matching_ini_files(file_order, file_data, data_dir, ("SCENN", "SCENR"))
    core_region_count = add_region_files(file_order, file_data, data_dir, "AHOFMS")
    core_scenario_count = add_matching_ini_files(file_order, file_data, data_dir, ("SCENA", "SCENH", "SCENO", "SCENF", "SCENM", "SCENS"))

    if addon_scenario_count != 44:
        print(f"Expected 44 vanilla Neutral/Rebels scenarios, found {addon_scenario_count}", file=sys.stderr)
        return 1

    if core_region_count != 6:
        print(f"Expected 6 fallback vanilla core regions, found {core_region_count}", file=sys.stderr)
        return 1

    if core_scenario_count != 132:
        print(f"Expected 132 fallback vanilla core scenarios, found {core_scenario_count}", file=sys.stderr)
        return 1

    write_pak(output_pak, file_order, file_data)
    print(
        f"Wrote {output_pak} with {len(file_order)} entries "
        f"({addon_scenario_count} add-on scenarios, {core_scenario_count} core scenarios)"
    )


if __name__ == "__main__":
    raise SystemExit(main())
