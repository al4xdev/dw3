# Digimon World 3: Definitive Edition

*Digimon World 3* as it could have been: battles rebuilt around elemental weaknesses in the style of *Persona*, a
progression that respects your time, and the comforts a 2002 game never had. It runs natively, with no emulator, on
RK3326 handhelds (the R36S, the XF35H and their clones) and on PC.

- **Fights you think through.** Find the weakness, stagger the enemy, earn one more action, pass the baton.
- **Less grinding, same journey.** The late game arrives around level 40-50, where most players used to stop.
- **Nothing hidden.** Enemy weaknesses, digivolution requirements and what each technique really does are on screen.
- **Your old save still works.**

It is built on the [Digimon World 2003 decompilation](https://github.com/gascarcella/dw2003recomp) and its PC port,
which are the upstream project's work; this fork adds the handheld path and the Definitive Edition.

> [!IMPORTANT]
> **This repository contains no game data.** You need your own copy of *Digimon World 2003 (Europe) (En,Fr,De,Es,It)*,
> raw `.bin` + `.cue`, MODE2/2352, 692,146,560 bytes, SHA-1 `457cb233349ba841e03b33d8060f8fbcadd45cb3`.
> This project is not affiliated with or endorsed by Bandai, Bandai Namco or Sony.

<!-- IMAGE: docs/images/definitive/banner.svg - title banner -->

## Quick start

1. Download `dw3.zip` from the [releases](https://github.com/al4xdev/dw3/releases) and unpack it into your PortMaster
   `ports` directory.
2. Copy your disc image into `ports/dw3/game/`.
3. Start *Digimon World 3* from the ports menu.

Install details, tested devices and known issues are on the player page, [`portmaster/README.md`](portmaster/README.md).

**Coming from an older version?** Your save opens and plays. To give it the new MP and TP tables, run
`tools/convert_save_mp.py card1.mcd` once (it keeps a backup).

## Where it runs

The work is focused on **RK3326 handhelds** for now: that is where everything is measured and tuned, on an XF35H. The
PC build works too. Once the Definitive Edition is finished, the focus moves to opening it up to more devices. If you
try it on another handheld, a report in the [issue tracker](https://github.com/al4xdev/dw3/issues) helps a lot.

## Combat

### Weaknesses

Seven elements: Fire, Water, Ice, Wind, Thunder, Machine and Dark. Every enemy has its own weaknesses and resistances:

- most enemies are weak to two elements and resist one;
- a few early "walls" are weak to a single element and resist two: find the weakness or struggle;
- late-game enemies resist two elements, bosses three, the final boss four;
- Dark is rare on both sides: only a handful of enemies are weak to it, and as few resist it.

Weaknesses are not fixed: a buff or a debuff can create or remove one in the middle of a fight.

<!-- IMAGE: docs/images/definitive/weakness.svg - the seven elements; weak / neutral / resisted; how the median rule picks two -->

### Stagger and One More

Hitting a weakness with an elemental technique **staggers** the enemy and gives you **one more** action. Each weakness
element gives its extra action once per round, so two weaknesses can chain into several actions in a row. The stagger
ends when the enemy takes its next blow (that blow still gets the bonus) or when it acts.

- **Baton Pass**: while the enemy is staggered, the Tag menu offers a free switch that keeps your turn (once per round).
  A plain switch still costs the turn.
- **You open the fight** unless you are ambushed. Speed no longer decides who starts; a much faster side acts twice,
  and the HUD shows "Enemy x2" when it is the enemy.

<!-- IMAGE: docs/images/definitive/stagger.svg - hit a weakness -> stagger -> one more -> baton pass -->

### Damage

For your elemental techniques: `1.0 + 0.5 (weakness) + 0.2 (staggered target) + affinity`, and half damage against a
resisted element. A blow with no element still gets +0.2 on a staggered target.

**Affinity** is what your Digimon's old elemental "tolerance" stats became: they no longer defend, they strengthen your
attacks of that element. Fire and Wind count on their own, as does Dark; Water with Ice and Thunder with Machine count
as pairs.

One stat defends against everything: enemy magic is resisted by your **Defense**, and Spirit is purely your magic
attack.

<!-- IMAGE: docs/images/definitive/damage.svg - the multiplier, the affinity pairs -->

### Buffs and debuffs

- An **attack buff** raises physical and magic damage alike; a **defense buff or debuff** counts against both kinds of
  hit. A magic party gets as much from them as a physical one.
- **Bosses are immune to status** (poison, sleep and the like) **but not to debuffs**: lowering a boss's defense or
  elemental resistance is part of the plan.
- **Resist Down** and **Mega Break** lower the enemy's resistance to your best elements for a few turns, which can open
  a weakness that was not there.
- The HUD shows every active buff and debuff as an up or down arrow beside the Digimon, on both sides.

### Knowing your enemy

- **Inspect** (a seventh button on the battle HUD, L2 by default) and **Start** show the enemy's seven elements, but
  only what you have found out: weaknesses and resistances are learnt by hitting them, everything else shows `???`.
- **Libra** reveals all of an enemy's elements at once.
- The **Bestiary** (field menu) keeps what you learnt of every species, saved with your game.
- The technique menu shows each technique's element.

![Bestiary, the weakness panel in battle and the technique menu's element](docs/images/definitive/definitive_ui.png)

### Techniques and MP

- Remade techniques: **Libra** (was Speed Up), **Relay** (was Mega Boost: the next switch is free), **Resist Down** (was
  Slow Down) and **Mega Break**.
- More techniques carry a secondary effect, so each form has its own role in a fight.
- Every technique description was rewritten from what the technique actually does, effect first: "Fire magic. Sleep
  chance."
- MP matters now, so the rookies that were starved of it are not any more: Kotemon, Kumamon, Monmon and Guilmon start
  with 140 MP and grow at the same pace as the others.

### Battle HUD

A direct-button HUD replaces the battle's list menu: each action (Fight, Tech, Digivolve, Tag, Item, Run, Inspect) has
its own button in a pad-shaped layout, rebindable from the launcher. It remembers the last technique and item, and
buffers a press made just before the menu opens.

![Persona-style Direct Battle HUD](docs/images/battle_hud.png)

## Progression

The original is known for ending at level 40-50 after exhausting grinding. The Definitive Edition brings the late game
to that same range without the grind.

### Training points

| Level | TP per level | Original |
|---|---|---|
| 2-20 | 7 | 5 |
| 21-40 | 9 | 5 |
| 41-50 | 30 | 5 |
| 51 and up | 0 | 5 |

By level 50 a Digimon has earned more TP than the original gives by level 99. Levels 41 to 50 are where you shape its
stats the way you want. TP still caps at 99 in hand, so you train along the way instead of hoarding for the last gym.

### Training

- **Guaranteed training**: after three sessions without a perfect result, the fourth succeeds on every round. A perfect
  session resets the count, and the training screen shows where you stand.
- **Resistance training no longer costs a stat.**

### Experience

- A rookie levels at the original pace; **digivolution forms level 14 times faster**, so trying a new form is fun
  instead of a chore.
- **The whole roster grows together**: rookies that stayed out of the fight still earn experience, and so do the forms
  that did not fight.

### Digivolution

- **Requirements on screen**: the Digi-Lab shows what each next form needs and how far you are, for example
  "Greymon Lv 5 (3/5)" or "Strength 180 (142/180)". No guide needed.
- **Short animations**: a form's digivolution plays in full the first time, then a short version; hold a button to skip.

## Comfort and fixes

- **The Blast Gauge is saved.** The original reset it every time the game was closed, even after saving.
- **Battle text that does not stop the fight**: message boxes no longer wait for a button.
- **Fast travel** from the map, a **map objective marker**, **fast-forward**, button glyphs that match your pad, and
  full remapping.
- **A gamepad menu** for every setting, with no keyboard needed.

## Building and documentation

| For | Command | Details |
|---|---|---|
| The ARM handheld binary and package | `tools/build_arm.sh` | [`docs/HANDHELD.md`](docs/HANDHELD.md#building) |
| The PortMaster zip | `portmaster/make_release.sh` | [`portmaster/PUBLISHING.md`](portmaster/PUBLISHING.md) |
| The PC port and the tests | see the guide | [`docs/BUILDING.md`](docs/BUILDING.md) |

The ARM build runs in Docker, so nothing is installed on the host. Everything technical (how the handheld port reaches
full speed, the repository layout, the design documents and every other guide) is indexed in
[`docs/README.md`](docs/README.md).

## Contributing and support

Read [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) first. Report bugs and ask questions in the
[issue tracker](https://github.com/al4xdev/dw3/issues).

## Credits and license

The decompilation and the PC port are the work of the [upstream project](https://github.com/gascarcella/dw2003recomp).
This fork adds the Definitive Edition and the handheld path: the direct framebuffer present, the Cortex-A35 code generation, the vector span work,
the VRAM row layout, the frame meter and the handheld extras, the GLES renderer, the audio work and the package. The
project's own work is under the [MIT licence](LICENSE); the game remains the property of its publisher.