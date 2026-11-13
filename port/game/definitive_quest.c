/* The Guide's rules: what the player is to do next, and the auctions of the Cargo Tower.
 *
 * The game already keeps the story as one number, gamestate_data.progress (0..45, set by the stages' event ends), and
 * that is the trigger: a quest is a row shown while the progress is inside its range and, when it names one, while a
 * game flag has a given value (an owned key item, mostly: flag 0x8000 | item). The main quest is the first row of
 * def_quests that matches, so a step's variants go before its plain row; the missions are every row of def_missions
 * that matches. Nothing is stored: the save's own progress and flags say it all, so an old save opens on the right
 * step.
 *
 * The auctions are the game's fieldstg_flag_events (stage 0x210, the Cargo Tower): auction N runs when the progress is
 * exactly its step, flag 0x400 + N is clear and the player has the money, and either answer sets the flag. Past its
 * step with the flag clear it was missed. tests/port/definitive_quest.py checks this table against FIELDSTG's.
 *
 * The notices (a step that changed, an auction that opened) are polled by the field (definitive_quest_screen.c); what
 * was last announced is the session's, not the save's: a loaded game announces where it stands once. */
#include <stdlib.h>

#include "common.h"

#include "gamestate.h"

#include "psxstack/savestate.h"

#include "definitive.h"

extern int port_script_active;
extern s32 gamestate_money_check[];

#define ITEM(id) (0x8000 | (id)) /* the flag of an item owned or equipped (gamestate_get_flag, types 0x80..0x8E) */
#define ANY DEF_QUEST_NO_FLAG

extern const unsigned char def_str_Q0[], def_str_Q0T[], def_str_Q1[], def_str_Q1T[], def_str_Q2[], def_str_Q2T[],
    def_str_Q3[], def_str_Q3T[], def_str_Q4[], def_str_Q4T[], def_str_Q5[], def_str_Q5T[], def_str_Q5b[],
    def_str_Q5bT[], def_str_Q5c[], def_str_Q5cT[], def_str_Q6[], def_str_Q6T[], def_str_Q7[], def_str_Q7T[],
    def_str_Q8[], def_str_Q8T[], def_str_Q9[], def_str_Q9T[], def_str_Q9b[], def_str_Q9bT[], def_str_Q10[],
    def_str_Q10T[], def_str_Q11[], def_str_Q11T[], def_str_Q12[], def_str_Q12T[], def_str_Q12b[], def_str_Q12bT[],
    def_str_Q13[], def_str_Q13T[], def_str_Q14[], def_str_Q14T[], def_str_Q14b[], def_str_Q14bT[], def_str_Q15[],
    def_str_Q15T[], def_str_Q16[], def_str_Q16T[], def_str_Q17[], def_str_Q17T[], def_str_Q18[], def_str_Q18T[],
    def_str_Q19[], def_str_Q19T[], def_str_Q20[], def_str_Q20T[], def_str_Q21[], def_str_Q21T[], def_str_Q22[],
    def_str_Q22T[], def_str_Q23[], def_str_Q23T[], def_str_Q24[], def_str_Q24T[], def_str_Q25[], def_str_Q25T[],
    def_str_Q26[], def_str_Q26T[], def_str_Q27[], def_str_Q27T[], def_str_Q28[], def_str_Q28T[], def_str_Q29[],
    def_str_Q29T[], def_str_Q30[], def_str_Q30T[], def_str_Q31[], def_str_Q31T[], def_str_Q32[], def_str_Q32T[],
    def_str_Q33[], def_str_Q33T[], def_str_Q34[], def_str_Q34T[], def_str_Q35[], def_str_Q35T[], def_str_Q36[],
    def_str_Q36T[], def_str_Q37[], def_str_Q37T[], def_str_Q38[], def_str_Q38T[], def_str_Q39[], def_str_Q39T[],
    def_str_Q40[], def_str_Q40T[], def_str_Q41[], def_str_Q41T[], def_str_Q43[], def_str_Q43T[], def_str_Q45[],
    def_str_Q45T[];
extern const unsigned char def_str_M0[], def_str_M0T[], def_str_M1[], def_str_M1T[], def_str_M2[], def_str_M2T[],
    def_str_M3[], def_str_M3T[];
extern const unsigned char def_str_AucWhen0[], def_str_AucWhen1[], def_str_AucWhen2[], def_str_AucWhen3[],
    def_str_AucWhen4[], def_str_AucWhen5[], def_str_AucWhen6[], def_str_AucWhen7[], def_str_AucWhen8[],
    def_str_AucWhen9[], def_str_AucWhen10[], def_str_AucWhen11[], def_str_AucWhen12[], def_str_AucWhen13[],
    def_str_AucWhen14[], def_str_AucWhen15[];

/* The main quest, a row per story step; `map` is where the step ends (its name is the game's own map title). The steps
 * are the stages' own: each WSTAG file's `gamestate_data.progress = N` is where N begins. */
const DefQuest def_quests[] = {
    { 0, 0, ANY, 0, 0x2D8, def_str_Q0, def_str_Q0T },
    { 1, 1, ANY, 0, 0x207, def_str_Q1, def_str_Q1T },
    { 2, 2, ANY, 0, 0x204, def_str_Q2, def_str_Q2T },
    { 3, 3, ANY, 0, 0x200, def_str_Q3, def_str_Q3T },
    { 4, 4, ANY, 0, 0x22E, def_str_Q4, def_str_Q4T },
    { 5, 5, ITEM(398), 1, 0x22C, def_str_Q5c, def_str_Q5cT }, /* the Blue Card */
    { 5, 5, ITEM(399), 1, 0x224, def_str_Q5b, def_str_Q5bT }, /* only the 8lue Card */
    { 5, 5, ANY, 0, 0x22F, def_str_Q5, def_str_Q5T },
    { 6, 6, ANY, 0, 0x232, def_str_Q6, def_str_Q6T },
    { 7, 7, ANY, 0, 0x23A, def_str_Q7, def_str_Q7T },
    { 8, 8, ANY, 0, 0x227, def_str_Q8, def_str_Q8T },
    { 9, 9, ITEM(21), 1, 0x239, def_str_Q9b, def_str_Q9bT }, /* the Sepik Mask */
    { 9, 9, ANY, 0, 0x21B, def_str_Q9, def_str_Q9T },
    { 10, 10, ANY, 0, 0x240, def_str_Q10, def_str_Q10T },
    { 11, 11, ANY, 0, 0x204, def_str_Q11, def_str_Q11T },
    { 12, 12, ITEM(15), 1, 0x20B, def_str_Q12b, def_str_Q12bT }, /* the Agumon Suit */
    { 12, 12, ANY, 0, 0x21B, def_str_Q12, def_str_Q12T },
    { 13, 13, ANY, 0, 0x217, def_str_Q13, def_str_Q13T },
    { 14, 14, ITEM(14), 1, 0x235, def_str_Q14b, def_str_Q14bT }, /* the TNT Chip */
    { 14, 14, ANY, 0, 0x235, def_str_Q14, def_str_Q14T },
    { 15, 15, ANY, 0, 0x25D, def_str_Q15, def_str_Q15T },
    { 16, 16, ANY, 0, 0x25A, def_str_Q16, def_str_Q16T },
    { 17, 17, ANY, 0, 0x252, def_str_Q17, def_str_Q17T },
    { 18, 18, ANY, 0, 0x254, def_str_Q18, def_str_Q18T },
    { 19, 19, ANY, 0, 0x255, def_str_Q19, def_str_Q19T },
    { 20, 20, ANY, 0, 0x21C, def_str_Q20, def_str_Q20T },
    { 21, 21, ANY, 0, 0x21C, def_str_Q21, def_str_Q21T },
    { 22, 22, ANY, 0, 0x218, def_str_Q22, def_str_Q22T },
    { 23, 23, ANY, 0, 0x203, def_str_Q23, def_str_Q23T },
    { 24, 24, ANY, 0, 0x260, def_str_Q24, def_str_Q24T },
    { 25, 25, ANY, 0, 0x240, def_str_Q25, def_str_Q25T },
    { 26, 26, ANY, 0, 0x246, def_str_Q26, def_str_Q26T },
    { 27, 27, ANY, 0, 0x29E, def_str_Q27, def_str_Q27T },
    { 28, 28, ANY, 0, 0x2AD, def_str_Q28, def_str_Q28T },
    { 29, 29, ANY, 0, 0x2AE, def_str_Q29, def_str_Q29T },
    { 30, 30, ANY, 0, 0x26E, def_str_Q30, def_str_Q30T },
    { 31, 31, ANY, 0, 0x26D, def_str_Q31, def_str_Q31T },
    { 32, 32, ANY, 0, 0, def_str_Q32, def_str_Q32T },
    { 33, 33, ANY, 0, 0x218, def_str_Q33, def_str_Q33T },
    { 34, 34, ANY, 0, 0x2C7, def_str_Q34, def_str_Q34T },
    { 35, 35, ANY, 0, 0x2D5, def_str_Q35, def_str_Q35T },
    { 36, 36, ANY, 0, 0x272, def_str_Q36, def_str_Q36T },
    { 37, 37, ANY, 0, 0x288, def_str_Q37, def_str_Q37T },
    { 38, 38, ANY, 0, 0x219, def_str_Q38, def_str_Q38T },
    { 39, 39, ANY, 0, 0x288, def_str_Q39, def_str_Q39T },
    { 40, 40, ANY, 0, 0x2DD, def_str_Q40, def_str_Q40T },
    { 41, 41, ANY, 0, 0x2DF, def_str_Q41, def_str_Q41T },
    { 42, 44, ANY, 0, 0, def_str_Q43, def_str_Q43T },
    { 45, 45, ANY, 0, 0, def_str_Q45, def_str_Q45T }, /* 0x2D: the game after the ending */
};
const int def_quest_count = (int)(sizeof(def_quests) / sizeof(def_quests[0]));

/* The missions: from the step the field's menu opens at (4) on, while the key item is missing. */
const DefQuest def_missions[DEF_QUEST_MISSIONS] = {
    { 4, 45, ITEM(8), 0, 0x213, def_str_M0, def_str_M0T },   /* the El Dorado ID */
    { 4, 45, ITEM(402), 0, 0x211, def_str_M1, def_str_M1T }, /* the Folder Bag */
    { 4, 45, ITEM(4), 0, 0x220, def_str_M2, def_str_M2T },   /* the Tree Boots */
    { 4, 45, ITEM(5), 0, 0x21F, def_str_M3, def_str_M3T },   /* the Fishing Pole */
};

/* The auctions, in fieldstg_flag_events' order: the story step, the item the winning bid gives (the first answer's
 * 0x80.. flag) and the index of the price in gamestate_money_check (the event's money condition, 0x74 + it). */
const DefAuction def_auctions[DEF_QUEST_AUCTIONS] = {
    { 5, 281, 0, def_str_AucWhen0 },   /* Divine Barrier */
    { 8, 287, 0, def_str_AucWhen1 },   /* Hazard Shield */
    { 12, 282, 1, def_str_AucWhen2 },  /* Sniper Shield */
    { 14, 288, 1, def_str_AucWhen3 },  /* Dramon Shield */
    { 16, 137, 2, def_str_AucWhen4 },  /* Yin Yang Wand */
    { 22, 149, 2, def_str_AucWhen5 },  /* Hazard Fang */
    { 24, 124, 3, def_str_AucWhen6 },  /* Sniper Cannon */
    { 26, 98, 3, def_str_AucWhen7 },   /* Ronin Blade */
    { 28, 222, 4, def_str_AucWhen8 },  /* Sniper Goggle */
    { 30, 232, 4, def_str_AucWhen9 },  /* Divine Crown */
    { 31, 244, 5, def_str_AucWhen10 }, /* Brave Helmet */
    { 34, 243, 5, def_str_AucWhen11 }, /* Ronin Helmet */
    { 36, 257, 6, def_str_AucWhen12 }, /* Yin Yang Suit */
    { 37, 269, 7, def_str_AucWhen13 }, /* Ronin Armor */
    { 38, 258, 8, def_str_AucWhen14 }, /* Wild Suit */
    { 39, 271, 9, def_str_AucWhen15 }, /* Brave Armor */
};

/* Off under a replay script (the scripts are shared with the emulator, and a notice or a ninth menu entry would move
 * what their hashes cover): DW3_GUIDE turns it on there, DW3_GUIDE=0 off everywhere. Not DW3_DEFINITIVE's: the combat
 * tests that set it never asked for a new menu entry. */
int def_guide_active(void) {
    static int mode = -1; /* 0 off, 1 on, 2 outside scripts only */

    if (mode < 0) {
        const char *v = getenv("DW3_GUIDE");

        mode = v == NULL || v[0] == '\0' ? 2 : v[0] != '0';
    }
    return mode == 2 ? !port_script_active : mode;
}

static int quest_matches(const DefQuest *q) {
    s32 progress = gamestate_data.progress;

    if (progress < q->first || progress > q->last) {
        return 0;
    }
    return q->flag == DEF_QUEST_NO_FLAG || gamestate_flags.get_flag(q->flag, q->value) != 0;
}

int def_quest_main_index(void) {
    int i;

    for (i = 0; i < def_quest_count; i++) {
        if (quest_matches(&def_quests[i])) {
            return i;
        }
    }
    return -1;
}

const DefQuest *def_quest_main(void) {
    int i = def_quest_main_index();

    return i >= 0 ? &def_quests[i] : NULL;
}

int def_quest_missions(const DefQuest **out, int max) {
    int i, n = 0;

    for (i = 0; i < DEF_QUEST_MISSIONS && n < max; i++) {
        if (quest_matches(&def_missions[i])) {
            out[n++] = &def_missions[i];
        }
    }
    return n;
}

int def_auction_state(int a) {
    s32 progress = gamestate_data.progress;

    if (a < 0 || a >= DEF_QUEST_AUCTIONS) {
        return DEF_AUCTION_SOON;
    }
    if (gamestate_flags.get_flag(DEF_AUCTION_FLAG + a, 1) != 0) {
        return DEF_AUCTION_DONE;
    }
    if (progress == def_auctions[a].progress) {
        return DEF_AUCTION_OPEN;
    }
    return progress > def_auctions[a].progress ? DEF_AUCTION_MISSED : DEF_AUCTION_SOON;
}

int def_auction_open(void) {
    int a;

    for (a = 0; a < DEF_QUEST_AUCTIONS; a++) {
        if (def_auction_state(a) == DEF_AUCTION_OPEN) {
            return a;
        }
    }
    return -1;
}

int def_auction_price(int a) {
    return a >= 0 && a < DEF_QUEST_AUCTIONS ? (int)gamestate_money_check[def_auctions[a].money] : 0;
}

/* The Guide's travel to El Dorado, the way into the Cargo Tower: for an auction open now, from a map the world map
 * knows (not the real world, not Gunslinger: the menu's MAP entry is greyed there too), not from El Dorado's own
 * three maps, and not out of Byakko City's trap, which the story walks the player out of. */
int def_guide_can_travel(int a, int map) {
    if (def_auction_state(a) != DEF_AUCTION_OPEN || map < 0x200 || map >= DEF_GUIDE_LAST_MAP) {
        return 0;
    }
    if (map == DEF_GUIDE_TRAVEL_MAP || map == 0x210 || map == 0x212) {
        return 0;
    }
    if (gamestate_data.progress == 16 && map >= 0x25E && map <= 0x260) {
        return 0;
    }
    return 1;
}

/* What the session last announced; -2: nothing yet. */
static int seen_quest = -2, seen_auction = -2;

/* The next notice the field owes the player: DEF_NOTICE_QUEST with the quest's row in *arg when the main quest's row
 * changed, else DEF_NOTICE_AUCTION with the auction's when one opened, else DEF_NOTICE_NONE. Each is returned once. */
int def_quest_notice(int *arg) {
    int quest, auction;

    if (!def_guide_active()) {
        return DEF_NOTICE_NONE;
    }
    quest = def_quest_main_index();
    if (quest != seen_quest) {
        seen_quest = quest;
        if (quest >= 0) {
            *arg = quest;
            return DEF_NOTICE_QUEST;
        }
    }
    auction = def_auction_open();
    if (auction != seen_auction) {
        seen_auction = auction;
        if (auction >= 0) {
            *arg = auction;
            return DEF_NOTICE_AUCTION;
        }
    }
    return DEF_NOTICE_NONE;
}

void def_quest_notice_reset(void) {
    seen_quest = seen_auction = -2;
}

void def_quest_savestate(struct PortState *s) {
    PORT_STATE_VAR(s, seen_quest);
    PORT_STATE_VAR(s, seen_auction);
}
