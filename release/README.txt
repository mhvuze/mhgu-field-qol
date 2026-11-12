Field QoL @VERSION@ for MHGU Recomp
===================================

Quality-of-life changes for quests:

  - One press at a gathering spot gathers everything it has left, and one carve
    takes all of a carcass's carves (eggs and other carried items still take a
    press each, and Gypceros playing dead still gives one carve). All of a
    spot's items come in one notification, and with a full pouch you choose
    what to replace once the spot is done.
  - The Item Pouch's Deliver works anywhere on the map, not only in the base camp.
  - "Send all to Item Box & Exit" on the quest reward screen (reward box,
    Palicoes' items, Item Appraisal), Meownster Hunters, Hunters for Hire and
    the trade cart's Claim Items.

Only tested on MHGU Recomp, with the English game text.

Install
-------
1. Put this "field-qol" folder in the recomp's "mods" folder:
     - macOS: ~/Library/Application Support/MHGURecomp/mods/
     - Windows and Linux: the "mods" folder next to the recomp's executable
   Gathering and delivering work right away.

2. For "Send all to Item Box & Exit", run the layout tool once. It makes the
   reward screen's new menu row from your own game files (the mod can't ship
   them), and needs Python 3 (python.org; macOS and Linux usually have it).
   Open a terminal in the "field-qol" folder and run:
     - macOS and Linux: python3 tools/make_arcs.py
     - Windows:         py tools\make_arcs.py
   It writes romfs/nativeNX/eng/arc/result_eng.arc, village/common_eng.arc and
   facility/shoptrade_eng.arc into this folder. Run it again after updating
   the mod.

Settings
--------
Press Insert in game to open Forge's menu, then open "Field QoL". Every feature
can be turned off there. They're all on by default.

Uninstall
---------
Delete the "field-qol" folder.

Source code, issues and more details: https://github.com/mhvuze/mhgu-field-qol
