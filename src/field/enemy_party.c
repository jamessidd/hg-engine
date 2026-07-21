#include "../../include/types.h"
#include "../../include/bag.h"
#include "../../include/battle.h"
#include "../../include/config.h"
#include "../../include/debug.h"
#include "../../include/pokemon.h"
#include "../../include/randomizer.h"
#include "../../include/rtc.h"
#include "../../include/save.h"
#include "../../include/script.h"
#include "../../include/constants/ability.h"
#include "../../include/constants/file.h"
#include "../../include/constants/game.h"
#include "../../include/constants/hold_item_effects.h"
#include "../../include/constants/item.h"
#include "../../include/constants/moves.h"
#include "../../include/constants/species.h"
#include "../../include/constants/weather_numbers.h"

/**
 *  @brief swap two integer values with each other given pointers
 *
 *  @param a first to swap
 *  @param b second to swap
 */
void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

/**
 *  @brief randomize the order of an array size n
 *
 *  @param arr array to randomize
 *  @param n size of array
 */
void randomize(int arr[], int n) {
    int i;
    for (i = n-1; i > 0; i--) {
        int j = gf_rand() % (i+1);
        swap(&arr[i], &arr[j]);
    }
}

extern u32 gLastPokemonLevelForMoneyCalc;

#ifdef RANDOMIZER_SMART_TRAINER_MOVES
/**
 *  @brief score a single move for a remapped trainer Pokemon
 *
 *  Damaging moves are scored by power * accuracy, boosted for STAB and for
 *  matching the species' stronger attacking stat. Status/utility moves get a
 *  moderate flat score so at most one can round out a set without displacing a
 *  strong attack. Positive-priority moves get a bonus.
 *
 *  @param species mapped species
 *  @param form_no form number
 *  @param move move to score
 *  @param outIsStatus set to TRUE when the move is a status/no-power move
 *  @return move score (0 to skip)
 */
static u32 Randomizer_ScoreTrainerMove(u16 species, u8 form_no, u16 move, BOOL *outIsStatus)
{
    u32 adjustedSpecies = PokeOtherFormMonsNoGet(species, form_no);
    u32 type1 = PokePersonalParaGet(adjustedSpecies, PERSONAL_TYPE_1);
    u32 type2 = PokePersonalParaGet(adjustedSpecies, PERSONAL_TYPE_2);
    u32 attack = PokePersonalParaGet(adjustedSpecies, PERSONAL_BASE_ATTACK);
    u32 spatk = PokePersonalParaGet(adjustedSpecies, PERSONAL_BASE_SP_ATTACK);
    u32 power = GetMoveData(move, MOVE_DATA_BASE_POWER);
    u32 accuracy = GetMoveData(move, MOVE_DATA_ACCURACY);
    u32 moveType = GetMoveData(move, MOVE_DATA_TYPE);
    u32 split = GetMoveData(move, MOVE_DATA_PSS_SPLIT);
    u32 secondary = GetMoveData(move, MOVE_DATA_SECONDARY_EFFECT_CHANCE);
    s32 priority = (s8)GetMoveData(move, MOVE_DATA_PRIORITY);
    u32 pp = GetMoveData(move, MOVE_DATA_BASE_PP);
    u32 score;

    *outIsStatus = FALSE;

    if (move == MOVE_NONE)
    {
        return 0;
    }

    if (accuracy == 0) // never-miss moves store 0 accuracy
    {
        accuracy = 100;
    }

    if (power == 0 || split == SPLIT_STATUS)
    {
        *outIsStatus = TRUE;
        score = 2200 + accuracy * 8 + pp * 15 + secondary * 8;
    }
    else
    {
        score = power * accuracy;

        if (moveType == type1 || moveType == type2)
        {
            score = (score * 3) / 2; // STAB
        }

        if ((split == SPLIT_PHYSICAL && attack >= spatk) || (split == SPLIT_SPECIAL && spatk >= attack))
        {
            score = (score * 6) / 5; // uses the better attacking stat
        }
        else
        {
            score = (score * 4) / 5; // uses the weaker attacking stat
        }

        score += secondary * 12;
    }

    if (priority > 0)
    {
        score += (u32)priority * 400;
    }

    return score;
}

/**
 *  @brief give a remapped trainer Pokemon a curated moveset from its level-up pool
 *
 *  Considers every distinct level-up move the mapped species knows by its level,
 *  then greedily picks up to four, penalizing moves whose type is already covered
 *  (for coverage) and deprioritizing a second status move. Falls back to the
 *  natural moveset if nothing is available.
 *
 *  @param mon PartyPokemon to assign moves to
 *  @param species mapped species
 *  @param form_no form number
 *  @param level level of the Pokemon
 *  @param heapID heap to use for the learnset buffer
 */
static void Randomizer_SetSmartTrainerMoves(struct PartyPokemon *mon, u16 species, u8 form_no, u16 level, int heapID)
{
    u32 *learnset = sys_AllocMemory(heapID, LEARNSET_TOTAL_MOVES * sizeof(u32));
    u16 candMove[LEARNSET_TOTAL_MOVES];
    u32 candScore[LEARNSET_TOTAL_MOVES];
    u8 candType[LEARNSET_TOTAL_MOVES];
    u8 candStatus[LEARNSET_TOTAL_MOVES];
    int candCount = 0;
    int i, k;

    if (learnset == NULL)
    {
        InitBoxMonMoveset(&mon->box);
        return;
    }

    LoadLevelUpLearnset_HandleAlternateForm(species, form_no, learnset);

    for (i = 0; i < LEARNSET_TOTAL_MOVES && learnset[i] != LEVEL_UP_LEARNSET_END; i++)
    {
        u16 move = LEVEL_UP_LEARNSET_MOVE(learnset[i]);
        u32 moveLevel = LEVEL_UP_LEARNSET_LEVEL(learnset[i]);
        BOOL isStatus = FALSE;
        BOOL duplicate = FALSE;
        u32 score;

        if (move == MOVE_NONE || moveLevel > level)
        {
            continue;
        }

        for (k = 0; k < candCount; k++)
        {
            if (candMove[k] == move)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        score = Randomizer_ScoreTrainerMove(species, form_no, move, &isStatus);
        if (score == 0)
        {
            continue;
        }

        candMove[candCount] = move;
        candScore[candCount] = score;
        candType[candCount] = (u8)GetMoveData(move, MOVE_DATA_TYPE);
        candStatus[candCount] = isStatus ? 1 : 0;
        candCount++;
    }

    sys_FreeMemoryEz(learnset);

    if (candCount == 0)
    {
        InitBoxMonMoveset(&mon->box);
        return;
    }

    u16 picked[4];
    u8 coveredType[4];
    int pickedCount = 0;
    int coveredCount = 0;
    int statusPicked = 0;

    while (pickedCount < 4)
    {
        int best = -1;
        u32 bestScore = 0;

        for (int c = 0; c < candCount; c++)
        {
            u32 eff;

            if (candMove[c] == MOVE_NONE) // already consumed
            {
                continue;
            }

            eff = candScore[c];

            if (candStatus[c])
            {
                if (statusPicked >= 1)
                {
                    eff /= 4; // allow a second status only if nothing better remains
                }
            }
            else
            {
                for (int t = 0; t < coveredCount; t++)
                {
                    if (coveredType[t] == candType[c])
                    {
                        eff /= 3; // type already covered
                        break;
                    }
                }
            }

            if (best == -1 || eff > bestScore)
            {
                best = c;
                bestScore = eff;
            }
        }

        if (best == -1)
        {
            break;
        }

        picked[pickedCount++] = candMove[best];
        if (candStatus[best])
        {
            statusPicked++;
        }
        else if (coveredCount < 4)
        {
            coveredType[coveredCount++] = candType[best];
        }
        candMove[best] = MOVE_NONE; // consume
    }

    ClearMonMoves(mon);
    for (i = 0; i < pickedCount; i++)
    {
        SetPartyPokemonMoveAtPos(mon, picked[i], i);
    }
}
#endif // RANDOMIZER_SMART_TRAINER_MOVES

/**
 *  @brief create the trainer Party from the trainer data file and trainer party file
 *
 *  @param bp battle param
 *  @param num trainer index to read from both ARC_TRAINER_DATA and ARC_TRAINER_PARTY_DATA
 *  @param heapID heap to use for memory usage
 */
void MakeTrainerPokemonParty(struct BATTLE_PARAM *bp, int num, int heapID)
{
    u8 *buf;
    int i, j;
    u32 rnd_tmp, rnd, seed_tmp;
    u8 pow;

    seed_tmp = gf_get_seed();

    PokeParty_Init(bp->poke_party[num], 6);

    buf = (u8 *)sys_AllocMemory(heapID, sizeof(struct FULL_TRAINER_MON_DATA_STRUCTURE) * 6);

    TT_TrainerPokeDataGet(bp->trainer_id[num], buf);

    if (TT_TrainerTypeSexGet(bp->trainer_data[num].tr_type) == 1) // if trainer is female
    {
        rnd_tmp = 120;
    }
    else
    {
        rnd_tmp = 136;
    }

    u8 pokecount = bp->trainer_data[num].poke_count;
    u8 randomorder_flag = pokecount & 0x80;
    pokecount &= 0x7f;

    // goal:  get rid of massive switch statement with each individual byte.  make the trainer type a bitfield
    u32 id;
    u16 species = 0, adjustedSpecies = 0, item = 0, ability = 0, level = 0, ball = 0, hp = 0, atk = 0, def = 0, speed = 0, spatk = 0, spdef = 0;
    u16 offset = 0;
    u16 moves[4];
    u8 ivnums[6];
    u8 evnums[6];
    u8 ppcounts[4];
    u16 *nickname = sys_AllocMemory(heapID, 11*sizeof(u16));
    u8 form_no = 0, abilityslot = 0, nature = 0, ballseal = 0, shinylock = 0, status = 0, ab1 = 0, ab2 = 0;
    u32 additionalflags = 0;

    int partyOrder[pokecount];
    if (randomorder_flag)
    {
        if(gf_rand() % 2 == 0)
        {
            for(i = 0; i < pokecount; i++)
            {
                partyOrder[i] = pokecount - 1 - i;
            }
        }
        else
        {
            for(i = 0; i < pokecount; i++)
            {
                partyOrder[i] = i;
            }
        }
    }
    else
    {
        for(i = 0; i < pokecount; i++)
        {
            partyOrder[i] = i;
        }
    }

    if (randomorder_flag && pokecount > 1)
    {
        int numtimes = gf_rand() % 6 + 1;
        for(i = 0; i < numtimes; i++)
        {
            randomize(partyOrder, pokecount);
        }
    }

    struct PartyPokemon * mons[pokecount];

    for (i = 0; i < pokecount; i++)
    {
        mons[i] = AllocMonZeroed(heapID);
        // ivs field
        pow = buf[offset];
        offset++;

        // abilityslot field
        abilityslot = buf[offset];
        offset++;

        // level field
        level = buf[offset] | (buf[offset+1] << 8);
        gLastPokemonLevelForMoneyCalc = level; // ends up being the last level at the end of the loop that we use for the money calc loop default case
        offset += 2;

        // species field
        species = buf[offset] | (buf[offset+1] << 8);
        offset += 2;
        form_no = (species & 0xF800) >> 11;
        species &= 0x07FF;

        // Randomizer: remap the authored species to its mapped species. When a
        // mon is remapped, its authored moves/ability/IVs/EVs/nature/custom
        // stats/nickname are dropped so everything is derived from the new
        // species (the held item is still honored). All species-derived logic
        // below (gender, default ability, PokeParaSet) uses the mapped species.
        u16 originalSpecies = species;
        species = Randomizer_MapSpeciesAndForm(species, &form_no);
        BOOL trainerMonWasRemapped = (species != originalSpecies);

        // item field - conditional
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_ITEMS)
        {
            item = buf[offset] | (buf[offset+1] << 8);
            offset += 2;
        }

        // moves field - conditional
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_MOVES)
        {
            for (j = 0; j < 4; j++)
            {
                moves[j] = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }
        }

        // ability field
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_ABILITY)
        {
            ability = buf[offset] | (buf[offset+1] << 8);
            offset += 2;
        }

        // custom ball field
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_BALL)
        {
            ball = buf[offset] | (buf[offset+1] << 8);
            offset += 2;
        }

        // ivs and evs fields
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_IV_EV_SET)
        {
            for(j = 0; j < 6; j++)
            {
                ivnums[j] = buf[offset];
                if(ivnums[j] > 31)
                    ivnums[j] = 31;
                offset++;
            }

            for(j = 0; j < 6; j++)
            {
                evnums[j] = buf[offset];
                offset++;
            }
        }

        // nature field
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_NATURE_SET)
        {
            nature = buf[offset];
            offset++;
        }

        // shiny lock field
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_SHINY_LOCK)
        {
            shinylock = buf[offset];
            offset++;
        }

        // reads extra flags from the trainer pokemon file
        if(bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_ADDITIONAL_FLAGS)
        {
            additionalflags = buf[offset] | (buf[offset+1] << 8) | (buf[offset+2] << 16) | (buf[offset+3] << 24);
            offset += 4;

            // status pre-set field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_STATUS)
            {
                status = buf[offset] | (buf[offset+1] << 8) | (buf[offset+2] << 16) | (buf[offset+3] << 24);
                offset += 4;
            }

            // custom hp stat field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_HP)
            {
                hp = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }

            // custom atk stat field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_ATK)
            {
                atk = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }

            // custom def stat field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_DEF)
            {
                def = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }

            // custom speed stat field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_SPEED)
            {
                speed = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }

            // custom spatk stat field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_SP_ATK)
            {
                spatk = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }

            // custom spdef stat field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_SP_DEF)
            {
                spdef = buf[offset] | (buf[offset+1] << 8);
                offset += 2;
            }

            // move PP counts field
            if(additionalflags & TRAINER_DATA_EXTRA_TYPE_PP_COUNTS)
            {
                for(j = 0; j < 4; j++)
                {
                    ppcounts[j] = buf[offset];
                    offset++;
                }
            }

            // nickname field
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_NICKNAME)
            {
                for(j = 0; j < 11; j++)
                {
                    nickname[j] = buf[offset] | (buf[offset+1] << 8);
                    offset += 2;
                }
            }
        }

        // ball seal field
        ballseal = buf[offset] | (buf[offset+1] << 8);
        offset += 2;

        // now set mon data
        try_force_gender_maybe(species, form_no, abilityslot, &rnd_tmp);
        rnd = pow + level + species + bp->trainer_id[num];
        gf_srand(rnd);
        for (j = 0; j < bp->trainer_data[num].tr_type; j++)
        {
            rnd = gf_rand();
        }
        rnd = (rnd << 8) + rnd_tmp;
        pow = pow * 31 / 255;
        PokeParaSet(mons[i], species, level, pow, 1, rnd, 2, 0);
        SetMonData(mons[i], MON_DATA_FORM, &form_no);

        //set default abilities
        adjustedSpecies = PokeOtherFormMonsNoGet(species, form_no);
        ab1 = PokePersonalParaGet(adjustedSpecies, PERSONAL_ABILITY_1);
        ab2 = PokePersonalParaGet(adjustedSpecies, PERSONAL_ABILITY_2);
        if (ab2 != 0)
        {
            if (abilityslot & 1 || abilityslot == 32) // abilityslot 32 gives second slot in vanilla
            {
                SetMonData(mons[i], MON_DATA_ABILITY, (u8 *)&ab2);
            }
            else{
                SetMonData(mons[i], MON_DATA_ABILITY, (u8 *)&ab1);
            }
        }
        else
        {
            SetMonData(mons[i], MON_DATA_ABILITY, (u8 *)&ab1);
        }

        // if abilityslot is 2 force hidden ability with the bit set.  this specifically to cover darmanitan with zen mode switching between forms and such.
        if (abilityslot == 2)
        {
            u16 hiddenability = GetMonHiddenAbility(species, form_no);
            SET_MON_HIDDEN_ABILITY_BIT(mons[i]);
            SetMonData(mons[i], MON_DATA_ABILITY, (u8 *)&hiddenability);
        }

        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_ITEMS)
        {
            SetMonData(mons[i], MON_DATA_HELD_ITEM, &item);
        }
        if (!trainerMonWasRemapped && (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_MOVES))
        {
            for (j = 0; j < 4; j++)
            {
                SetPartyPokemonMoveAtPos(mons[i], moves[j], j);
            }
        }
#ifdef RANDOMIZER_SMART_TRAINER_MOVES
        else if (trainerMonWasRemapped)
        {
            Randomizer_SetSmartTrainerMoves(mons[i], species, form_no, level, heapID);
        }
#endif
        TrainerCBSet(ballseal, mons[i], heapID);
        if (!trainerMonWasRemapped && (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_ABILITY))
        {
            SetMonData(mons[i], MON_DATA_ABILITY, &ability);
        }
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_BALL)
        {
            SetMonData(mons[i], MON_DATA_POKEBALL, &ball);
        }
        if (!trainerMonWasRemapped && (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_IV_EV_SET))
        {
            for(j = 0; j < 6; j++)
            {
                SetMonData(mons[i],MON_DATA_HP_IV + j, &ivnums[j]);
            }

            for(j = 0; j < 6; j++)
            {
                SetMonData(mons[i],MON_DATA_HP_EV + j, &evnums[j]);
            }
        }
        if (!trainerMonWasRemapped && (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_NATURE_SET))
        {
            u32 pid = GetMonData(mons[i], MON_DATA_PERSONALITY, NULL);
            u8 currentNature = pid % 25;
            pid = pid + nature - currentNature;
            SetMonData(mons[i], MON_DATA_PERSONALITY, &pid);
        }
        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_SHINY_LOCK)
        {
            u32 pid = GetMonData(mons[i], MON_DATA_PERSONALITY, NULL);
            if (shinylock != 0)
            {
                do {
                    id = (gf_rand() | (gf_rand() << 16));
                } while (!SHINY_CHECK(id, pid));
                SetMonData(mons[i], MON_DATA_OTID, &id);
            }
        }

        ChangeToBattleForm(mons[i]);

        RecalcPartyPokemonStats(mons[i]); // recalculate stats here

        if (bp->trainer_data[num].data_type & TRAINER_DATA_TYPE_ADDITIONAL_FLAGS)
        {
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_STATUS)
            {
                SetMonData(mons[i],MON_DATA_STATUS, &status);
            }
            // Drop authored competitive stat/PP/nickname overrides for remapped mons.
            if (!trainerMonWasRemapped)
            {
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_HP)
            {
                SetMonData(mons[i],MON_DATA_MAXHP, &hp);
                SetMonData(mons[i],MON_DATA_HP, &hp);
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_ATK)
            {
                SetMonData(mons[i],MON_DATA_ATTACK, &atk);
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_DEF)
            {
                SetMonData(mons[i],MON_DATA_DEFENSE, &def);
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_SPEED)
            {
                SetMonData(mons[i],MON_DATA_SPEED, &speed);
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_SP_ATK)
            {
                SetMonData(mons[i],MON_DATA_SPECIAL_ATTACK, &spatk);
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_SP_DEF)
            {
                SetMonData(mons[i],MON_DATA_SPECIAL_DEFENSE, &spdef);
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_PP_COUNTS)
            {
                for(j = 0; j < 4; j++)
                {
                    SetMonData(mons[i],MON_DATA_MOVE1PP+j, &ppcounts[j]);
                }
            }
            if (additionalflags & TRAINER_DATA_EXTRA_TYPE_NICKNAME)
            {
                u32 one = 1;

                SetMonData(mons[i],MON_DATA_HAS_NICKNAME, &one);
                SetMonData(mons[i],MON_DATA_NICKNAME, nickname);
            }
            } // end !trainerMonWasRemapped
        }
        TrainerMonHandleFrustration(mons[i]);
    }

    for (i = 0; i < pokecount; i++)
    {
        PokeParty_Add(bp->poke_party[num], mons[partyOrder[i]]);
        sys_FreeMemoryEz(mons[i]);
    }

    sys_FreeMemoryEz(buf);
    sys_FreeMemoryEz(nickname);

    gf_srand(seed_tmp);
}

extern u32 space_for_setmondata;

/**
 *  @brief add a PartyPokemon to the "wild battler"'s party
 *
 *  @param inTarget battler whose party to add to
 *  @param encounterInfo various encounter information structure
 *  @param encounterPartyPokemon PartyPokemon to modify and add
 *  @param encounterBattleParam battle param
 *  @return TRUE if PokeParty_Add was successful
 */
BOOL LONG_CALL AddWildPartyPokemon(int inTarget, EncounterInfo *encounterInfo, struct PartyPokemon *encounterPartyPokemon, struct BATTLE_PARAM *encounterBattleParam)
{
    int range = 0;
    u8 change_form = 0;
    u8 form_no;
    u16 species;

    if (encounterInfo->isEgg == 0 && encounterInfo->ability == ABILITY_COMPOUND_EYES)
    {
        range = 1;
    }

    species = GetMonData(encounterPartyPokemon, MON_DATA_SPECIES, NULL);

    if (space_for_setmondata != 0)
    {
        change_form = 1;
        form_no = space_for_setmondata;//(species & 0xF800) >> 11;
        space_for_setmondata = 0;
    }

    WildMonSetRandomHeldItem(encounterPartyPokemon, encounterBattleParam->fight_type, range);

    if (species == SPECIES_UNOWN)
    {
        change_form = 1;
        form_no = GrabAndRegisterUnownForm(encounterInfo);
    }
    else if (species == SPECIES_DEERLING || species == SPECIES_SAWSBUCK)
    {
        UpdatePassiveForms(encounterPartyPokemon);
    }

    if (CheckScriptFlag(HIDDEN_ABILITIES_FLAG) == 1)
    {
        SET_MON_HIDDEN_ABILITY_BIT(encounterPartyPokemon)
        ClearScriptFlag(HIDDEN_ABILITIES_FLAG);
        ResetPartyPokemonAbility(encounterPartyPokemon);
    }

    if (change_form)
    {
        SetMonData(encounterPartyPokemon, MON_DATA_FORM, (u8 *)&form_no);
        RecalcPartyPokemonStats(encounterPartyPokemon);
        ResetPartyPokemonAbility(encounterPartyPokemon);
        InitBoxMonMoveset(&encounterPartyPokemon->box);
    }

    ChangeToBattleForm(encounterPartyPokemon);

    return PokeParty_Add(encounterBattleParam->poke_party[inTarget], encounterPartyPokemon);
}
