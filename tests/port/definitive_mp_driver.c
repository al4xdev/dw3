/* tests/port/definitive_mp.py's driver for port/game/definitive_mp.c: records_digimon is a stand-in loaded from stdin.
 * Prints one "name start gain" line per rookie, in
 * the order Kotemon..Patamon, after def_mp_apply; the Python side compares them with the PRD and the converter. */
#include <stdio.h>

#include "records.h"
#include "definitive.h"

RecordsDigimon records_digimon[8];

int main(void) {
    int i, mp, gain;

    /* stdin: the eight rookies' "start_mp gain" as src/main/records.c has them (the Python side parses them) */
    for (i = 0; i < 8; i++) {
        if (scanf("%d %d", &mp, &gain) != 2) {
            return 2;
        }
        records_digimon[i].start_mp = mp;
        records_digimon[i].hp_mp_gains[1] = gain;
    }
    def_mp_apply();
    for (i = 0; i < 8; i++) {
        printf("%d %d %d\n", i, records_digimon[i].start_mp, records_digimon[i].hp_mp_gains[1]);
    }
    return 0;
}
