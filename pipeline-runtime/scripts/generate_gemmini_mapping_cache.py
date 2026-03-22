#!/usr/bin/env python3

import argparse
import struct
import sys
from pathlib import Path


PRT_MAX_LAYER_TENSORS = 8

CACHE_MAGIC0 = 0x5052544D  # "PRTM"
CACHE_MAGIC1 = 0x43414348  # "CACH"
CACHE_VERSION = 1
CACHE_ENTRY_WORDS = 58

HEADER_STRUCT = struct.Struct("<8I")
ENTRY_STRUCT = struct.Struct("<58I")

SPLIT_KIND_MAP = {
    "single": 1,
    "oc": 2,
    "spatial": 3,
    "resadd_spatial": 4,
}


def parse_scalar(value: str) -> int:
    num = int(value.strip(), 10)
    if num < 0 or num > 0xFFFFFFFF:
        raise ValueError(f"u32 value out of range: {value!r}")
    return num


def parse_list(value: str) -> list[int]:
    left = value.find("[")
    if left < 0:
        return []
    right = value.find("]", left + 1)
    if right < 0:
        raise ValueError(f"unterminated list: {value!r}")
    body = value[left + 1 : right].strip()
    if not body:
        return []

    items: list[int] = []
    for token in body.split(","):
        token = token.strip()
        if not token:
            continue
        items.append(parse_scalar(token))
    return items


def parse_split_kind(value: str) -> int:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in ("'", '"'):
        text = text[1:-1]
    return SPLIT_KIND_MAP.get(text, 0)


def new_entry() -> dict[str, object]:
    return {
        "layer_id": 0,
        "target_accel": 0,
        "split_kind": 0,
        "dram": [],
        "spm": [],
        "spm_addr": [],
        "first_vpage": [],
        "page_count": [],
        "spm_bytes": [],
        "active": False,
    }


def append_entry(entries: list[dict[str, object]], entry: dict[str, object]) -> None:
    if not entry["active"]:
        return

    for key in ("dram", "spm", "spm_addr", "first_vpage", "page_count", "spm_bytes"):
        values = entry[key]
        if not isinstance(values, list):
            raise ValueError(f"entry field {key} is not a list")
        if len(values) > PRT_MAX_LAYER_TENSORS:
            raise ValueError(f"entry field {key} exceeds {PRT_MAX_LAYER_TENSORS} values")

    entries.append(entry.copy())


def parse_mapping_yaml(path: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    current = new_entry()

    with path.open("r", encoding="utf-8") as handle:
        for lineno, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            if line.startswith("- layer_id:"):
                append_entry(entries, current)
                current = new_entry()
                current["active"] = True
                current["layer_id"] = parse_scalar(line.split(":", 1)[1])
                continue

            if not current["active"]:
                continue

            if ":" not in line:
                continue

            key, value = line.split(":", 1)
            key = key.strip()
            value = value.strip()

            try:
                if key == "target_accel":
                    current["target_accel"] = parse_scalar(value)
                elif key == "mapping_dram_bypass":
                    current["dram"] = parse_list(value)
                elif key == "mapping_spm_bypass":
                    current["spm"] = parse_list(value)
                elif key == "split_kind":
                    current["split_kind"] = parse_split_kind(value)
                elif key == "others_spm_tensor_addr":
                    current["spm_addr"] = parse_list(value)
                elif key == "others_first_tensor_page_num":
                    current["first_vpage"] = parse_list(value)
                elif key == "others_spm_tensor_page_count":
                    current["page_count"] = parse_list(value)
                elif key == "others_spm_tensor_util":
                    current["spm_bytes"] = parse_list(value)
            except ValueError as exc:
                raise ValueError(f"{path}:{lineno}: {exc}") from exc

    append_entry(entries, current)
    return entries


def entry_words(entry: dict[str, object]) -> tuple[int, ...]:
    def fixed_list(key: str) -> list[int]:
        values = list(entry[key])
        return values + [0] * (PRT_MAX_LAYER_TENSORS - len(values))

    words: list[int] = [
        int(entry["layer_id"]),
        int(entry["target_accel"]),
        int(entry["split_kind"]),
        len(entry["dram"]),
        *fixed_list("dram"),
        len(entry["spm"]),
        *fixed_list("spm"),
        len(entry["spm_addr"]),
        *fixed_list("spm_addr"),
        len(entry["first_vpage"]),
        *fixed_list("first_vpage"),
        len(entry["page_count"]),
        *fixed_list("page_count"),
        len(entry["spm_bytes"]),
        *fixed_list("spm_bytes"),
        1 if entry["active"] else 0,
    ]
    if len(words) != CACHE_ENTRY_WORDS:
        raise ValueError(f"entry word count mismatch: expected {CACHE_ENTRY_WORDS}, got {len(words)}")
    return tuple(words)


def write_cache(entries: list[dict[str, object]], source_path: Path, output_path: Path) -> None:
    source_bytes = source_path.stat().st_size
    output_path.parent.mkdir(parents=True, exist_ok=True)

    header = HEADER_STRUCT.pack(
        CACHE_MAGIC0,
        CACHE_MAGIC1,
        CACHE_VERSION,
        PRT_MAX_LAYER_TENSORS,
        len(entries),
        CACHE_ENTRY_WORDS,
        source_bytes & 0xFFFFFFFF,
        0,
    )

    with output_path.open("wb") as handle:
        handle.write(header)
        for entry in entries:
            handle.write(ENTRY_STRUCT.pack(*entry_words(entry)))


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a compact Gemmini layer-mapping cache")
    parser.add_argument("input_yaml", help="Path to gemmini_layer_mapping.*.yaml")
    parser.add_argument("output_bin", nargs="?", help="Destination .cache.bin path")
    args = parser.parse_args()

    input_path = Path(args.input_yaml)
    output_path = Path(args.output_bin) if args.output_bin else Path(f"{args.input_yaml}.cache.bin")

    entries = parse_mapping_yaml(input_path)
    write_cache(entries, input_path, output_path)
    print(f"mapping-cache path={output_path} entries={len(entries)} source_bytes={input_path.stat().st_size}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover
        print(f"mapping-cache error: {exc}", file=sys.stderr)
        raise SystemExit(1)
