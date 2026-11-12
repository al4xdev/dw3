/* Definitive combat, layer 8: the rookies' MP, patched into records_digimon (def_mp_hook, definitive.c: at a new game and
 * at each battle's start, when def_on; not at boot, where a replay script is not yet known and its state, shared with
 * the emulator, must keep the disc's numbers). The reset restores the table, so the patch is written again each time.
 * src/main/records.c is matching source and is not edited.
 * tools/convert_save_mp.py (MP_TABLE) must carry the same numbers: it converts old saves, the game does not. */
#include "records.h"

#include "definitive.h"

/* Rookie index = records_digimon index = gamestate_data.digimon index: 0 Kotemon, 1 Kumamon, 2 Monmon, 4 Veemon,
 * 5 Guilmon (3 Agumon, 6 Renamon, 7 Patamon keep theirs). */
const DefMpRow def_mp_rows[] = {
    { 0, DEF_MP_START, DEF_MP_GAIN },
    { 1, DEF_MP_START, DEF_MP_GAIN },
    { 2, DEF_MP_START, DEF_MP_GAIN },
    { 4, DEF_MP_START_VEEMON, DEF_MP_GAIN },
    { 5, DEF_MP_START, DEF_MP_GAIN_GUILMON },
};
const int def_mp_row_count = sizeof(def_mp_rows) / sizeof(def_mp_rows[0]);

void def_mp_apply(void) {
    int i;

    for (i = 0; i < def_mp_row_count; i++) {
        RecordsDigimon *d = &records_digimon[def_mp_rows[i].digimon];

        d->start_mp = def_mp_rows[i].start_mp;
        d->hp_mp_gains[1] = def_mp_rows[i].gain;
    }
}
