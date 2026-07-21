.nds
.thumb

// Update this macro with the specific starters that you want.
// Keep in mind that these choices will impact Rival team
// determination in the appropriate trigger-scripts.
//
// This will NOT update the text during the starter-selection
// sequence. To update that text, modify text archive 190 in
// DSPRE.
// The starter species come from the generated randomizer table so the
// selection screen shows (and gives) the mapped starters. Regenerate with
// `make randomizer`; `make randomizer_identity` restores the original three.
.include "armips/data/randomizer_starters.s"

.macro STARTER_CHOICES
    RANDOMIZER_STARTER_CHOICES
.endmacro

.open "base/arm9.bin", 0x02000000

.org 0x02108514

sStarterChoices_Species:
    STARTER_CHOICES

.close

.open "base/overlay/overlay_0061.bin", 0x021E5900

.org 0x021E7398 // 0x021E5900 + 0x1A98

sStarterChoices_Cries:
    STARTER_CHOICES

.close
