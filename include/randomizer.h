#ifndef RANDOMIZER_H
#define RANDOMIZER_H

#include "types.h"

// Build-time one-to-one species remap.
//
// These are real (non-inline) functions so that both C creation paths and the
// encounter asm hooks (asm/other_hook.s) can call them. When
// IMPLEMENT_BUILD_RANDOMIZER is disabled in config.h they behave as identity.

// Map a source species to its fixed target species. Out-of-pool species and
// species above the generated table max map to themselves.
u16 LONG_CALL Randomizer_MapSpecies(u16 species);

// Map a species and, when it actually changes, force *form to 0 so the mapped
// species is created in its base form.
u16 LONG_CALL Randomizer_MapSpeciesAndForm(u16 species, u8 *form);

// Encounter-path helper called from the EncountParamSet asm hooks. Maps the
// species and, when it changes, clears the pending encounter form global
// (space_for_setmondata) so the mapped species is built in its base form.
u16 LONG_CALL Randomizer_MapEncounterSpecies(u16 species);

#endif // RANDOMIZER_H
