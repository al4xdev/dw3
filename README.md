# Digimon World 3: Definitive Edition

A remake of *Digimon World 3*'s battles in the style of *Persona*, running natively on RK3326 handhelds (the R36S, the
XF35H and their clones) and on PC. It is built on the [Digimon World 2003 decompilation](https://github.com/gascarcella/dw2003recomp)
and its PC port: the decompilation and the port are the upstream project's work; this fork adds the handheld path and
the Definitive Edition's combat.

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

Saves from earlier versions open and play. The rookies' new MP is not applied to an old save by the game:
`tools/convert_save_mp.py card1.mcd` converts a card (keep a copy first).

The player page, with install details, the tested devices and known issues, is
[`portmaster/README.md`](portmaster/README.md). How the launcher runs the game, the environment variables and the
controls are in [`docs/HANDHELD.md`](docs/HANDHELD.md).

## The Definitive Edition's combat

The design and every number behind it are in [`.plan/tasks/definitive-combat-prd.md`](.plan/tasks/definitive-combat-prd.md);
every tunable number is a constant in [`port/game/definitive.h`](port/game/definitive.h). It is always on: there is no
option to turn it off.

### Weaknesses

Seven elements: Fire, Water, Ice, Wind, Thunder, Machine and Dark. Every enemy has its own weaknesses and resistances,
set by a table the port applies over the disc's records (the disc is never edited):

- most enemies are weak to two elements and resist one;
- a few early "walls" are weak to a single element and resist two: find the weakness or grind;
- late-game enemies resist two elements, bosses three, the final boss four;
- Dark is rare on both sides: only a handful of enemies are weak to it, and as few resist it.

A weakness is decided at the moment of the hit, from the target's resists as they stand then, so a buff or a debuff can
create or remove one.

<!-- IMAGE: docs/images/definitive/weakness.svg - the seven elements; weak / neutral / resisted; how the median rule picks two -->

### Stagger and One More

Hitting a weakness with an elemental technique **staggers** the enemy (it takes the tired pose) and gives you **one
more** action. Each weakness element gives its extra action once per round, so two weaknesses can chain into several
actions in a row. The stagger ends when the enemy takes its next blow (that blow still gets the bonus) or when it acts.

- **Baton Pass**: while the enemy is staggered, the Tag menu offers Baton Pass, a free switch that keeps your turn (once
  per round). A plain switch still costs the turn.
- **You open the fight** (unless you are ambushed), and speed matters from the first turn: a much faster party acts twice
  before the enemy does. The HUD shows "Enemy x2" when the enemy will act twice before you.

<!-- IMAGE: docs/images/definitive/stagger.svg - hit a weakness -> stagger -> one more -> baton pass -->

### Damage

For your elemental techniques: `1.0 + 0.5 (weakness) + 0.2 (staggered target) + affinity` for each of the two, and half
damage against a resisted element. **Affinity** is what your Digimon's old elemental "tolerance" stats became: they no
longer defend, they strengthen your attacks of that element. Fire and Wind count on their own; Water with Ice and Thunder
with Machine count as pairs (the affinity uses the pair's sum), Dark on its own. A blow with no element on a staggered
target gets +0.2.

Enemy magic is now resisted by your **Defense** (it was Spirit), so one stat covers both kinds of hit; Spirit stays your
magic attack.

<!-- IMAGE: docs/images/definitive/damage.svg - the multiplier, the affinity pairs -->

### Knowing your enemy

- **Inspect** (a seventh button on the battle HUD, L2 by default) and **Start** show the enemy's seven elements, but
  only what you have found out: weaknesses and resistances are learnt by hitting them, everything else shows `???`.
- **Libra** reveals all of an enemy's elements at once.
- The **Bestiary** (field menu) keeps what you learnt of every species, saved with your game, per slot.
- The technique menu shows each technique's element.

<!-- IMAGE: docs/images/definitive/inspect.png - the weakness panel in battle -->
<!-- IMAGE: docs/images/definitive/bestiary.png - the bestiary -->

### Techniques and MP

- Remade techniques: **Libra** (was Speed Up), **Relay** (was Mega Boost: the next switch is free), **Resist Down** (was
  Slow Down) and Mega Break, which lower the enemy's resistance to your best elements for a few turns (bosses are immune).
- Every technique description was rewritten from what the technique actually does (effect first: "Fire magic. Sleep
  chance.").
- The rookies' MP: Kotemon, Kumamon and Monmon start with 140 MP; their MP growth per level is 57, as is Veemon's.

### Battle HUD

A direct-button HUD replaces the battle's list menu: each action (Fight, Tech, Digivolve, Tag, Item, Run, Inspect) has
its own button in a pad-shaped layout, rebindable from the launcher; it remembers the last technique and item, and
buffers a press made just before the menu opens.

![Persona-style Direct Battle HUD](docs/images/battle_hud.png)

### Balance

The numbers are a first pass, tuned by playing. A battle log (`DW3_BATTLE_LOG=<file>`) writes every turn, action and
damage with the multiplier's terms, to see where a fight goes wrong.

## The handheld port

- **Battle text that does not stop the fight** (`battle_text`): the battle's message boxes no longer wait for a button.
  In `async` (the default) the action carries on while the text stays up for a moment; `skip` removes the boxes, `off`
  is the original.
- **Direct framebuffer present**: the PS1's VRAM expanded straight into `/dev/fb0` with NEON, which is what makes the 2D
  game and the movies run at full speed.
- **A second renderer on the Mali** (`--renderer gles`): the VRAM as a texture, faster than the CPU rasteriser in heavy
  3D battles. The software renderer stays as the reference and the fallback.
- **A gamepad menu** (`handheld/dw3-menu.py`) for the game, XP and video settings, with no keyboard needed.
- **Mods**: fast travel from the map, a map objective marker, button glyphs that match the pad and remapping, an XP and
  bits boost, an evolution buff and fast-forward. They are described in [`docs/LAUNCHER.md`](docs/LAUNCHER.md).
- **Cortex-A35 code generation**, NEON span drawing, a cache-friendly VRAM row layout and a frame meter that splits a
  frame into the parts you can act on ([`docs/PERFORMANCE.md`](docs/PERFORMANCE.md)).
- **A checked PortMaster package**: `portmaster/make_release.sh` builds `dist/dw3.zip` and refuses to build it with a
  missing file or with game data inside.

## Building

| For | Command | Details |
|---|---|---|
| The ARM handheld binary and package | `tools/build_arm.sh` | [`docs/HANDHELD.md`](docs/HANDHELD.md#building) |
| The PortMaster zip | `portmaster/make_release.sh` | [`portmaster/PUBLISHING.md`](portmaster/PUBLISHING.md) |
| The PC port and the tests | see the guide | [`docs/BUILDING.md`](docs/BUILDING.md) |

The ARM build runs in Docker, so nothing is installed on the host.

## Repository layout

| Path | Contents |
|---|---|
| `src/`, `include/` | the game's C, decompiled and matching the original build |
| `port/` | the PC port's adapter for the game ([`port/README.md`](port/README.md)) |
| `psxstack/` | the runtime stack, a git submodule ([fork](https://github.com/al4xdev/dw3-psxstack)) |
| `handheld/` | the launcher, the menu and the LED daemon, the source of what runs on the card |
| `portmaster/` | PortMaster metadata, `make_release.sh` and the publishing checklist |
| `packaging/` | the desktop AppImage and Windows packages |
| `tools/`, `scripts/` | build, analysis and check scripts |
| `tests/` | the reference tests ([`tests/README.md`](tests/README.md)) |
| `docs/` | the documentation, indexed below |
| `.plan/` | engineering plans with their measurements and what failed, kept as a historical log |

## Documentation

**Using and building**

| Document | Contents |
|---|---|
| [`HANDHELD.md`](docs/HANDHELD.md) | running on a handheld, building and deploying the ARM package, environment variables, limitations |
| [`PERFORMANCE.md`](docs/PERFORMANCE.md) | measured frame costs and how to measure them |
| [`BUILDING.md`](docs/BUILDING.md) | building and testing the PC port |
| [`RELEASE.md`](docs/RELEASE.md) | desktop releases (AppImage, Windows) |
| [`LAUNCHER.md`](docs/LAUNCHER.md) | the launcher, the mods, the settings file and the LED bar |
| [`XF35H.md`](docs/XF35H.md) | the XF35H's panel, LED bar and rumble |

**Project**

| Document | Contents |
|---|---|
| [`CONTRIBUTING.md`](docs/CONTRIBUTING.md) | the rules, naming, CI checks and how to report issues |
| [`STATUS.md`](docs/STATUS.md) | where the project stands |
| [`PROGRESS.md`](docs/PROGRESS.md) | matching progress, generated by `tools/progress.py` |
| [`PORT.md`](docs/PORT.md) | the design of the PC port |
| [`DECISIONS.md`](docs/DECISIONS.md) | what was decided and why |
| [`THIRD_PARTY.md`](docs/THIRD_PARTY.md) | what the project borrows, and under which licences |

**The game and the decompilation**

| Document | Contents |
|---|---|
| [`MATCHING.md`](docs/MATCHING.md) | matching functions to the original build |
| [`TOOLCHAIN.md`](docs/TOOLCHAIN.md) | what built the original game |
| [`MECHANICS.md`](docs/MECHANICS.md) | game mechanics reference for the tests |
| [`FORMATS.md`](docs/FORMATS.md) | file formats |
| [`DISC_LAYOUT.md`](docs/DISC_LAYOUT.md) | the disc's layout |
| [`SOUND.md`](docs/SOUND.md) | what the game asks of the sound library and how the port reproduces it |

**The GPU renderer**: [`gpu/README.md`](docs/gpu/README.md) indexes the study and the build log of the `gles` renderer;
[`bench/`](docs/bench) holds the span-loop microbenchmarks. The stack's own fork-specific notes are in
[`psxstack/docs/HANDHELD.md`](psxstack/docs/HANDHELD.md).

## Plans

`.plan/` holds the engineering plans of the handheld work, each with its measurements and a "what failed" section. They
are a historical record, not instructions.

## Contributing and support

Read [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) first. Report bugs and ask questions in the
[issue tracker](https://github.com/al4xdev/dw3/issues).

## Credits and license

The decompilation and the PC port are the work of the [upstream project](https://github.com/gascarcella/dw2003recomp).
This fork adds the handheld path: the direct framebuffer present, the Cortex-A35 code generation, the vector span work,
the VRAM row layout, the frame meter and the handheld extras, the GLES renderer, the audio work and the package. The
project's own work is under the [MIT licence](LICENSE); the game remains the property of its publisher.