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


def is_campaign_file(name: str) -> bool:
    upper = name.upper()
    return upper.endswith(".INI") and (upper.startswith("REGION") or upper.startswith("SCEN"))


def add_entry(file_order, file_data, file_path: Path):
    name = file_path.name
    if name not in file_data:
        file_order.append(name)
    file_data[name] = file_path.read_bytes()


def main():
    if len(sys.argv) != 3:
        print("Usage: rebuild_tornie_pak.py <mods/Tornie/data> <output.pak>", file=sys.stderr)
        return 2

    data_dir = Path(sys.argv[1])
    output = Path(sys.argv[2])
    campaign_dir = data_dir.parent / "campaign"

    # Rebuild from the current loose mod files.  Carrying the previous PAK
    # forward preserves stale entries and duplicates, and those can shadow fixed
    # Tornie assets at runtime.
    file_data = {}
    file_order = []

    for file_path in sorted(data_dir.iterdir(), key=lambda p: p.name.lower()):
        if not file_path.is_file() or file_path.name.upper() == "TORNIE.PAK" or is_campaign_file(file_path.name):
            continue

        add_entry(file_order, file_data, file_path)

    campaign_count = 0
    if campaign_dir.is_dir():
        for file_path in sorted(campaign_dir.iterdir(), key=lambda p: p.name.lower()):
            if not file_path.is_file():
                continue

            add_entry(file_order, file_data, file_path)
            campaign_count += 1

    write_pak(output, file_order, file_data)
    print(f"Wrote {output} with {len(file_order)} entries ({campaign_count} campaign entries)")


if __name__ == "__main__":
    raise SystemExit(main())
