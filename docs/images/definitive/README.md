# Definitive Edition images

The pictures the top-level README shows. A planned one is listed here and in the README as an HTML comment
(`<!-- IMAGE: ... -->`), so nothing broken shows until it exists; when it is added, replace its comment with the image.

| File | What it explains | Numbers it shows |
|---|---|---|
| `banner.svg` | the title banner, with the WEAK! and ONE MORE! callouts and the five element families | none |
| `weakness.svg` | the seven elements as five families (Water + Ice, Thunder + Machine); weak / neutral / resisted / not learnt yet; what Inspect shows after a hit; how one value per family decides the state, and what Resist Down's −80 does to a neutral and a resisted family | `DEF_WEAK_MAX`, `DEF_RESIST_MIN`, `DEF_BREAK_SLOW_DELTA`, the enemy table's 60 and 100–115, `DEF_MULT_WEAK`, `DEF_MULT_RESISTED` |
| `stagger.svg` | weakness hit -> stagger -> one more (once per weak family a round) -> hit a weakness again, Baton Pass or any other blow; OVERFINISH | `DEF_MULT_STAGGER`, `DEF_OVERFINISH_BITS_PCT` |
| `damage.svg` | the multiplier, part by part, and each family's affinity ceiling | `DEF_MULT_*`, `DEF_AFF_*` |
| `damage-pipeline.svg` | where the multiplier sits among the game's own damage steps | the stagger bonus with no element, the game's caps |
| `hunt.svg` | PRD 4: the regional chain and its chance curve, the Rare Target with its region's numeral, its rewards (the region's numbered rare item, one R-Booster, more experience), the bestiary's collection | `DEF_HUNT_CHAIN_MIN`, `DEF_HUNT_CHANCE_*`; the owner's example "Datamon IV", "Datamon Core IV", "4/13" |
| `hunt-materials.svg` | common materials (any battle, by level band) against rare items (Rare Targets only, numbered); held and shown in the bestiary; where they go (forge, Black Market, DRI Gauntlet, all planned) | the owner's example only |
| `definitive_ui.png` | a capture: the bestiary, the weakness panel in battle and the technique menu's element. Taken before the five-family screens and the bestiary's Hunt line | |
| `inspect.png` | planned capture: the weakness panel in battle, by family | |
| `bestiary.png` | planned capture: the bestiary, with its Hunt line | |

The numbers come from `port/game/definitive.h`: when a constant in the last column changes, the picture changes with
it. The hunt's reward numbers (drop chance, how many materials and rares, the XP multiple, the R-Booster levels) stay
out of the pictures and live in the README's text. The Datamon example in the two hunt pictures follows the approved
target table (Datamon leads 13 regions): redraw it if the table changes.

The SVGs are written by hand for GitHub, which shows them through `<img>`: no scripts, no `foreignObject`, no external
fonts or images, a `viewBox` with a width and height, a system font stack, and their own dark background so they read
on the light and the dark theme alike. The README shows them 800 px wide, so no text is smaller than 13.5 px in a
900 px drawing. Check a change parses with `python3 -c "import sys, xml.etree.ElementTree as E; E.parse(sys.argv[1])" <file>.svg`,
then look at it in a browser at 800 px.
