Field QoL @VERSION@ for MHGU Recomp
===================================

Quality-of-life changes for quests:

  - One press at a gathering spot gathers everything it has left, and one carve
    takes all of a carcass's carves (eggs and other carried items still take a
    press each, and Gypceros playing dead still gives one carve). Hold R as you
    start gathering for a single item. All of a spot's items come in one
    notification, and with a full pouch you choose what to replace once the
    spot is done.
  - The Item Pouch's Deliver works anywhere on the map, not only in the base camp.
  - "Send all to the Item Box & Exit" on the quest reward screen (reward box,
    Palicoes' items), Meownster Hunters, Hunters for Hire and the trade cart's
    Claim Items, "Send all to Boxes & Exit" on the Item Appraisal and "Send all
    to Item Box & Exit" on the Melding Pot's Meld Result.
  - Y on the Melding Pot's talisman list chooses talismans you own better ones
    of ("Select Leftovers"), never the one you wear or one in a saved set. A
    notification says so when there are fewer than it needs.
  - In Item Box > Manage Equipment > View Talismans, keep R held, then press
    ZL: it sells all those leftover talismans at once (never the worn one, one
    in a saved set or one with Decorations), after a question with their count
    and total. R has to be held down when ZL is pressed.
  - Auto-sort the Item Pouch whenever items are added (off by default).
  - Melding jackpot: every melding gives 7 charms, the most an order holds
    (off by default): 7 of its charm for Mystery, Luminous, Ancient and Fabled
    Melding, 7 random ones for Halcyon, 5 Enduring and 2 Timeworn for Juju.

Only tested on MHGU Recomp, with the English game text.

Install
-------
Put this "field-qol" folder in the recomp's "mods" folder:
  - macOS: ~/Library/Application Support/MHGURecomp/mods/
  - Windows and Linux: the "mods" folder next to the recomp's executable
That's all: no layout tool and no game files are needed anymore.

Updating from 0.5 to 0.7: those versions needed a layout tool, which wrote .arc
files into this folder. Delete them: the folder romfs/nativeNX/eng/arc/ (with
result_eng.arc, village/common_eng.arc and facility/shoptrade_eng.arc in it),
and the old "tools" folder.

Settings
--------
Every feature can be turned off. They're all on by default, except Auto-sort and
the Melding jackpot.
With Purple-lib (r19 or later, optional: https://gamebanana.com/mods/740376):
open the start menu, then Options > Mod Options > Field QoL.
Without it: press Insert in game to open Forge's menu, then open "Field QoL".

Changes are saved as you make them, in save_0/fieldqol.ini next to your save (by
default in the recomp's "save" folder), so they stay for the next start. You can
edit that file while the game is closed.

Uninstall
---------
Delete the "field-qol" folder. The mod never changes your game files. Your
settings stay in save_0/fieldqol.ini; delete it too if you want them gone.

To keep the other features but get the game's original menus back, turn off
"Send all to box" in the settings.

Source code, issues and more details: https://github.com/mhvuze/mhgu-field-qol
