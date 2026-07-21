# HeartGold Generations Build Randomizer

This branch adds a build-time, one-time species remap for HeartGold Generations v2.0.

The randomizer generates one fixed mapping per seed before a ROM build. Each source species is remapped to exactly one target species (a permutation), for example `SPECIES_TOTODILE -> SPECIES_DIALGA`. The mapping is applied at the earliest point of every Pokemon-creation path, so the vanilla creation pipeline builds the entire creature from the mapped species. The mapped species is the single source of truth: its name, base stats, types, ability, learnset/moves, gender, nature, IVs, EVs, friendship, and evolution line all come from the new species automatically.

## Design

The old approach patched a few fields on top of an already-created Pokemon (so IVs, nature, and gender still came from the original species). This version instead remaps the species **before** creation:

- Wild / static / headbutt / roaming battles: remapped at the start of `EncountParamSet` / `EncountParamSetRare` (`asm/other_hook.s`) via `Randomizer_MapEncounterSpecies`, before stats/ability/nature/IVs/moves are rolled.
- Trainers (`MakeTrainerPokemonParty`): remapped when the party is read. A remapped trainer mon drops its authored moves, ability, IVs, EVs, nature, and custom stats and uses the mapped species' natural values. Its held item is kept.
- Gifts (`GiveMon`): remapped before creation; a script-forced ability is dropped on remap.
- Eggs (`ScrCmd_GiveEgg`) and the Togepi egg: remapped before `SetEggStats`; the Togepi egg's forced Extrasensory is skipped when remapped.
- NPC trades (`_CreateTradeMon`): the given species is remapped (the trade keeps its preset PID/OT identity).
- Starters: replaced at build time in `armips/data/starters.s` via the generated `armips/data/randomizer_starters.s`.

Evolutions require no special handling: since the stored species is the mapped species, evolution follows the mapped species' evolution line.

## What Is Randomized

The mapping is a one-to-one permutation over every fully-implemented base species in this build (National Dex 1-1025 / internal ids `1..1075` excluding eggs, alternate forms, and reserved placeholders). Legendaries are valid sources and targets. Alternate forms, megas, and primal/form-only species are never used as sources or targets, and any species outside the pool maps to itself.

## What Is Not Changed

- Species constants, base stats, abilities, evolutions, learnsets, TM/tutor data, and Pokedex metadata files are untouched; the mapped species simply uses its own existing data.
- Dialogue and message text are not rewritten, so NPC text and the starter-selection text may still mention the original Pokemon (edit text archive 190 in DSPRE to change starter text).
- The roamer's on-map/Pokedex "seen roaming" metadata still reflects the original species; the roamer you actually battle and catch is randomized.

## Generate A Randomized Build Map

Run this before building the ROM:

```bash
make randomizer RANDOMIZER_SEED=my-seed
```

If `RANDOMIZER_SEED` is omitted, a random seed is generated and printed:

```bash
make randomizer
```

Generated files:

- `include/generated/randomizer_species_map.h`
- `armips/data/randomizer_starters.s`
- `build/randomizer/species_map.tsv`

Keep `build/randomizer/species_map.tsv` as the spoiler/log for that build. It records the seed and every species mapping.

## Build The NDS Elsewhere

This machine is not intended to build the `.nds`. On the build machine:

```bash
git clone git@github.com:jamessidd/hg-engine.git
cd hg-engine
git checkout hgg-v2-build-randomizer
make randomizer RANDOMIZER_SEED=my-seed
cp /path/to/clean-us-heartgold.nds rom.nds
make -j$(nproc)
```

The build output is `test.nds`. Run `make randomizer RANDOMIZER_SEED=another-seed` before building again to produce a different randomized ROM.

## Reset To Identity / Disable

Restore the generated files to a no-op (identity) mapping:

```bash
make randomizer_identity
```

To disable the randomizer entirely, comment out `IMPLEMENT_BUILD_RANDOMIZER` in `include/config.h`.
