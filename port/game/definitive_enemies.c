/* The port's enemy table (definitive combat PRD, layer 3). EMPTY until the owner approves
 * .dumps/tasks/persona/fraquezas.md; tools/gen_enemy_weaknesses.py writes the proposed table beside it, and approving
 * it means generating this file (--apply). With no rows no species is overridden and none has a Dark mark. */
#include "definitive.h"

const DefEnemyRow def_enemy_rows[] = { { 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 } }; /* one unused row: an empty array is not C */
const int def_enemy_row_count = 0;
