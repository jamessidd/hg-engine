#!/usr/bin/env python3
"""Generate a build-time, one-to-one species remap for the HGG randomizer.

The mapping is a fixed permutation (derangement) over the fully-implemented base
species of this build. Every source species is remapped to exactly one target
species; the mapped species then supplies ALL of its own data at creation time
(name, base stats, types, ability, learnset/moves, gender, nature, IVs, EVs,
friendship, and evolutions), because the runtime hooks remap the species BEFORE
the vanilla creation pipeline runs.

Outputs:
  - include/generated/randomizer_species_map.h  (compiled lookup table)
  - armips/data/randomizer_starters.s           (starter selection table)
  - build/randomizer/species_map.tsv            (spoiler log for this seed)
"""

from __future__ import annotations

import argparse
import random
import re
import secrets
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SPECIES = REPO_ROOT / "include" / "constants" / "species.h"
DEFAULT_MONDATA = REPO_ROOT / "armips" / "data" / "mondata.s"
DEFAULT_HEADER = REPO_ROOT / "include" / "generated" / "randomizer_species_map.h"
DEFAULT_ARMIPS_STARTERS = REPO_ROOT / "armips" / "data" / "randomizer_starters.s"
DEFAULT_SPOILER = REPO_ROOT / "build" / "randomizer" / "species_map.tsv"

DEFINE_RE = re.compile(r"^#define\s+(SPECIES_[A-Z0-9_]+)\s+(\d+)\b")
MAX_CANONICAL_RE = re.compile(r"^#define\s+MAX_CANONICAL_MON_NUM\s+\((SPECIES_[A-Z0-9_]+)\)")
MONDATA_RE = re.compile(r'^\s*mondata\s+(SPECIES_[A-Z0-9_]+)\s*,\s*"([^"]*)"', re.MULTILINE)
PLACEHOLDER_NAME = "-----"

# Non-species entries that carry real display names and therefore are not caught
# by the placeholder-name filter below.
HARD_EXCLUDE_NAMES = ("SPECIES_EGG", "SPECIES_BAD_EGG")

STARTER_NAMES = ("SPECIES_CHIKORITA", "SPECIES_CYNDAQUIL", "SPECIES_TOTODILE")


def read_species(path: Path) -> tuple[dict[str, int], dict[int, str], int]:
    name_to_value: dict[str, int] = {}
    value_to_name: dict[int, str] = {}
    max_canonical_name: str | None = None

    for line in path.read_text().splitlines():
        match = DEFINE_RE.match(line)
        if match:
            name, value_text = match.groups()
            value = int(value_text)
            name_to_value[name] = value
            value_to_name.setdefault(value, name)
            continue

        match = MAX_CANONICAL_RE.match(line)
        if match:
            max_canonical_name = match.group(1)

    if max_canonical_name is None:
        raise SystemExit(f"Could not find MAX_CANONICAL_MON_NUM in {path}")
    if max_canonical_name not in name_to_value:
        raise SystemExit(f"Could not resolve {max_canonical_name} in {path}")

    return name_to_value, value_to_name, name_to_value[max_canonical_name]


def read_excluded_ids(mondata_path: Path, name_to_value: dict[str, int], max_species: int) -> set[int]:
    """Return canonical ids that are NOT real base species.

    A species is excluded when its mondata display name is the placeholder
    "-----" (this covers SPECIES_NONE, in-range alternate forms such as the
    Deoxys/Rotom/Giratina/Shaymin forms, and the reserved placeholder block),
    or when it is one of the non-battle egg entries.
    """
    excluded: set[int] = set()

    for name in HARD_EXCLUDE_NAMES:
        value = name_to_value.get(name)
        if value is not None and value <= max_species:
            excluded.add(value)

    text = mondata_path.read_text(errors="ignore")
    for symbol, display in MONDATA_RE.findall(text):
        if display != PLACEHOLDER_NAME:
            continue
        value = name_to_value.get(symbol)
        if value is not None and value <= max_species:
            excluded.add(value)

    return excluded


def build_pool(name_to_value: dict[str, int], max_species: int, excluded: set[int]) -> list[int]:
    candidates = [value for value in range(1, max_species + 1) if value not in excluded]
    if not candidates:
        raise SystemExit("Randomizer pool is empty; check species.h / mondata.s parsing")

    # Every starter must be a valid remap source so starters are always randomized.
    for name in STARTER_NAMES:
        value = name_to_value.get(name)
        if value is None:
            raise SystemExit(f"Could not resolve starter {name}")
        if value not in candidates:
            raise SystemExit(f"Starter {name} ({value}) is not in the randomizer pool")

    return candidates


def deranged(values: list[int], rng: random.Random, allow_unchanged: bool) -> list[int]:
    if allow_unchanged or len(values) <= 1:
        shuffled = values[:]
        rng.shuffle(shuffled)
        return shuffled

    shuffled = values[:]
    for _ in range(1000):
        rng.shuffle(shuffled)
        if all(src != dst for src, dst in zip(values, shuffled)):
            return shuffled

    # Deterministic fallback for pathological RNG luck: a single rotation is a
    # derangement of a list with more than one element.
    return values[1:] + values[:1]


def build_mapping(candidates: list[int], max_species: int, seed: str, identity: bool, allow_unchanged: bool) -> dict[int, int]:
    mapping = {value: value for value in range(0, max_species + 1)}

    if identity:
        return mapping

    rng = random.Random(seed)
    shuffled = deranged(candidates, rng, allow_unchanged)
    mapping.update(zip(candidates, shuffled))
    return mapping


def write_header(path: Path, mapping: dict[int, int], max_species: int, seed: str, identity: bool, pool_size: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef GENERATED_RANDOMIZER_SPECIES_MAP_H",
        "#define GENERATED_RANDOMIZER_SPECIES_MAP_H",
        "",
        "/* Auto-generated by tools/randomizer/generate_species_map.py. Do not edit. */",
        f'#define RANDOMIZER_SPECIES_MAP_SEED "{seed}"',
        f"#define RANDOMIZER_SPECIES_MAP_IS_IDENTITY {1 if identity else 0}",
        f"#define RANDOMIZER_SPECIES_MAP_POOL_SIZE {pool_size}",
        "#define RANDOMIZER_SPECIES_MAP_MAX MAX_CANONICAL_MON_NUM",
        "",
        "static const u16 sRandomizerSpeciesMap[MAX_CANONICAL_MON_NUM + 1] = {",
    ]

    for src in range(0, max_species + 1):
        lines.append(f"    [{src}] = {mapping[src]},")

    lines.extend([
        "};",
        "",
        "#endif",
        "",
    ])
    path.write_text("\n".join(lines))


def write_armips_starters(path: Path, mapping: dict[int, int], name_to_value: dict[str, int], value_to_name: dict[int, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    starter_lines = []
    for starter_name in STARTER_NAMES:
        source_value = name_to_value[starter_name]
        mapped_value = mapping[source_value]
        mapped_name = value_to_name.get(mapped_value, f"id {mapped_value}")
        # Emit the numeric id (with a name comment) so alternate armips species
        # spellings never break assembly.
        starter_lines.append(f"    .word {mapped_value} // {mapped_name}")

    lines = [
        ".nds",
        ".thumb",
        "",
        "// Auto-generated by tools/randomizer/generate_species_map.py. Do not edit.",
        ".macro RANDOMIZER_STARTER_CHOICES",
        *starter_lines,
        ".endmacro",
        "",
    ]
    path.write_text("\n".join(lines))


def write_spoiler(path: Path, mapping: dict[int, int], candidates: list[int], value_to_name: dict[int, str], seed: str, identity: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"# seed\t{seed}",
        f"# identity\t{1 if identity else 0}",
        f"# pool_size\t{len(candidates)}",
        "source_id\tsource_species\tmapped_id\tmapped_species",
    ]

    for src in sorted(candidates):
        dst = mapping[src]
        lines.append(f"{src}\t{value_to_name.get(src, '?')}\t{dst}\t{value_to_name.get(dst, '?')}")

    path.write_text("\n".join(lines) + "\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", help="Seed for a reproducible random map. Defaults to a generated token.")
    parser.add_argument("--identity", action="store_true", help="Write an identity (no-op) map.")
    parser.add_argument("--allow-unchanged", action="store_true", help="Allow a species to map to itself.")
    parser.add_argument("--species", type=Path, default=DEFAULT_SPECIES)
    parser.add_argument("--mondata", type=Path, default=DEFAULT_MONDATA)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--armips-starters", type=Path, default=DEFAULT_ARMIPS_STARTERS)
    parser.add_argument("--spoiler", type=Path, default=DEFAULT_SPOILER)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    name_to_value, value_to_name, max_species = read_species(args.species)
    excluded = read_excluded_ids(args.mondata, name_to_value, max_species)
    candidates = build_pool(name_to_value, max_species, excluded)

    seed = "identity" if args.identity else (args.seed or secrets.token_hex(8))
    mapping = build_mapping(candidates, max_species, seed, args.identity, args.allow_unchanged)

    write_header(args.header, mapping, max_species, seed, args.identity, len(candidates))
    write_armips_starters(args.armips_starters, mapping, name_to_value, value_to_name)
    write_spoiler(args.spoiler, mapping, candidates, value_to_name, seed, args.identity)

    print(f"randomizer seed: {seed}")
    print(f"species randomized: {len(candidates)} (canonical max {max_species}, excluded {len(excluded)})")
    print(f"header: {args.header}")
    print(f"armips starters: {args.armips_starters}")
    print(f"spoiler: {args.spoiler}")


if __name__ == "__main__":
    main()
