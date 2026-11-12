# Field QoL

Quality-of-life changes for quests in **Monster Hunter Generations Ultimate**, as a Forge plugin for
[MHGU Recomp](https://github.com/morerokk/MHGURecomp).

> Only tested on MHGU Recomp (macOS), with the English game text. It has not been tested on a Switch or an emulator.

## Features

**Gather a whole spot with one press.** At a mining, bone, bug, herb or mushroom spot, one press gathers everything
the spot has left. Carving works the same way: one press takes all of a carcass's carves. Each item gets its own roll
as usual, and skills and early breaks work the same. It stops:

- before eggs, powderstone and other items you carry in your hands: those still take a press each;
- when you carve Gypceros while it plays dead: you get one carve, as usual, before it gets up.

Hold **R** as you start gathering to gather a single item instead, for spots like Khezu Welps where you can only
carry one and gathering everything would use up every chance.

A full pouch doesn't stop it: every gather is still rolled, and the pouch takes what fits (items you already have
stack as usual). Items at their stack limit are lost, as in the game. For items that need a free slot, the game's
"Item Pouch is full" window opens once the spot is done, one item after the other, so you choose what to replace in
one go instead of after every press.

All of a spot's messages come in one notification, with one line per item and its total (`Iron Ore x3`). A spot with
more than 6 lines continues in a second notification.

**Deliver from anywhere.** The Item Pouch's **Deliver** and **Clear out Account Items** work anywhere on the map, not
only in the base camp. Which items can be delivered doesn't change: the quest's target items and account items, but
not carried items such as eggs.

**Send all to Item Box & Exit.** The quest reward screen gets a new menu row under "Send to the Item Box". It sends
every item of the section to your Item Box and moves on to the next section. This works for the reward box, your
Palicoes' gathered items, the rewards from Meownster Hunters and Hunters for Hire, and the Item Appraisal ("Send all to
Item Box & Exit" under "Send to Boxes": items to the Item Box, appraised equipment and charms to the Equipment Box).
The trade cart's Claim Items gets the same row: it sends all three carts' items to the Item Box and closes Claim Items.
If the box fills up first, the menu stays open with what's left.

Every feature can be turned on and off in Forge's menu (press **Insert** in game, then open **Field QoL**). They're all
on by default, and the switches reset each time the game starts.

## Install

1. Download `field-qol-<version>.zip` from [Releases](../../releases).
2. Extract it into the recomp's `mods` folder, so that you get `mods/field-qol/`:
   - macOS: `~/Library/Application Support/MHGURecomp/mods/`
   - Windows and Linux: the `mods` folder next to the recomp's executable

   Gathering and delivering work right away.
3. For **Send all to Item Box & Exit**, run the layout tool once. It builds the reward screen's new menu row from
   your own game files, because the mod can't ship them. You need [Python 3](https://www.python.org/downloads/).
   Open a terminal in the `field-qol` folder and run:
   - macOS and Linux: `python3 tools/make_arcs.py`
   - Windows: `py tools\make_arcs.py`
4. Start the game. `log.txt` should list three `field-qol:` lines: `one press gathers the whole spot`,
   `pouch deliveries work anywhere` and `Send all to Item Box & Exit on the reward screen`.

## Uninstall

Delete the `field-qol` folder, or turn the mod off in the recomp's Mods tab. That restores everything: the layout tool
never changes your game files, it only writes new copies into the mod's own folder, which the recomp applies on top of
the game's data.

To keep the gathering and delivery features but get the game's original menus back, delete just the `.arc` files in
`field-qol/romfs/nativeNX/` (the layout tool's output). The plugin then leaves the menus alone.

## Compatibility

- **Game version:** MHGU 1.4.0, which the recomp needs anyway.
- **Language:** the new reward screen row needs the English menu layout. With another game language, gathering and
  delivering still work, but the reward screen keeps its usual menu.
- **Other mods:** the layout tool writes the mod's own copies of `result_eng.arc`, `common_eng.arc` and
  `shoptrade_eng.arc`. If another
  mod also changes those archives, the recomp merges the changes of both.

## Building

You need an `arm-none-eabi` GCC toolchain (on macOS: `brew install arm-none-eabi-gcc`) and Python 3.

```sh
sh build.sh            # builds out/field_qol.nro
sh build.sh install    # builds, installs into the recomp's mods/field-qol/ and runs the layout tool there
sh build.sh release    # builds dist/field-qol-<version>.zip (no game data)
```

The reward screen layout is made from your own game files by `tools/make_arcs.py`, so neither this repository nor
the release zip holds game data. `install` reads them from the recomp's folder: set `RECOMP=<path>` if yours isn't
macOS's default `~/Library/Application Support/MHGURecomp`.

### Layout of the repository

| Path | What it is |
| --- | --- |
| `src/plugin.c` | The plugin: game addresses, hooks, and how each feature works |
| `src/forge.h` | The part of Forge's plugin API the plugin uses |
| `src/start.S`, `build/` | NRO start code, linker script and ELF to NRO converter |
| `tools/make_arcs.py` | Adds the "Send all" row to the reward screen's and the trade cart's menu layouts |
| `tools/mtgui.py`, `tools/patchgui.py` | A reader and an append-only editor for MHGU GUI files |
| `release/README.txt` | The readme that goes in the release zip |

## Credits

- `src/start.S`, `build/nro.ld` and `build/elf2nro.py` come from lick-mee's
  [mhgu-gathering-map](https://github.com/lick-mee/mhgu-gathering-map) (MIT).
- The plugin API is [Forge](https://github.com/Fexty12573/forge) by Fexty12573.
- The GUI file tools follow Fexty12573's [mhw-gui-editor](https://github.com/Fexty12573/mhw-gui-editor).

## License

[MIT](LICENSE).
