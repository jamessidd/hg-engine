#include "../include/types.h"
#include "../include/config.h"
#include "../include/randomizer.h"
#include "../include/constants/species.h"

// Set by the encounter asm hooks to carry the pending form into mon creation.
extern u32 space_for_setmondata;

#ifdef IMPLEMENT_BUILD_RANDOMIZER
#include "../include/generated/randomizer_species_map.h"
#endif

u16 LONG_CALL Randomizer_MapSpecies(u16 species)
{
#ifdef IMPLEMENT_BUILD_RANDOMIZER
    if (species <= RANDOMIZER_SPECIES_MAP_MAX)
    {
        return sRandomizerSpeciesMap[species];
    }
#endif
    return species;
}

u16 LONG_CALL Randomizer_MapSpeciesAndForm(u16 species, u8 *form)
{
    u16 mapped = Randomizer_MapSpecies(species);

    if (mapped != species && form != NULL)
    {
        *form = 0;
    }

    return mapped;
}

u16 LONG_CALL Randomizer_MapEncounterSpecies(u16 species)
{
    u16 mapped = Randomizer_MapSpecies(species);

    if (mapped != species)
    {
        // The mapped species uses its base form; drop the source's pending form.
        space_for_setmondata = 0;
    }

    return mapped;
}
