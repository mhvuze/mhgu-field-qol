// Field QoL: quality-of-life changes on quests, as a Forge plugin for MHGU Recomp.
//
// Its features, each with a switch (Options > Mod Options > Field QoL with Purple-lib, Forge's menu, fieldqol.ini):
//   - Gathering:  one press at a gathering spot gathers everything the spot has left (Hold R: one item), one notification
//                 per spot, and a full pouch asks what to replace once the spot is done.
//   - Delivery:   the Item Pouch's Deliver works anywhere on the map, not only in the base camp.
//   - Rewards:    "Send all to the Item Box & Exit" on the reward screens, the appraisal and the trade cart, its menu row
//                 made at runtime (no layout files).
//   - Auto-sort:  (off by default) the Item Pouch sorted whenever items are added.
//
// The plugin runs in the recomp's interpreter as AArch32 code and calls the game's own functions. Addresses are
// offsets into the game's main module (MHGU 1.4.0); at runtime they are added to forge_mem_getMainTextAddr(). Each
// feature's section below explains the game code it relies on.
#include "forge.h"
#include "purplelib.h"

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "dev"
#endif

// ================================================================== game addresses

// Gathering
#define GATHER 0x2E07C4           // uPlayer: one gather (hunter, empty_if_none, notify) -> 1 when an item was given
#define GIVE 0x2E04D4             // uPlayer: give a gathered item (hunter, item, quantity, notify, 0)
#define PLAYER_ADD 0x2880B4       // uPlayerBase: put an item in the pouch (hunter, item, quantity, flags, 0) -> 0-2: taken
#define PLAYER_ADD_QUEST 0x301784 // uPlayerQuest's override of it (the hunter on a quest)
#define IS_LOCAL 0x27BCD0         // uPlayer: is this the hunter played on this console
#define POP_USE 0x31B228          // sPop: use up one gather (spop, id, &pop) -> 1: more left, 2: that was the last
#define POP_CAN_GATHER 0x319244   // cPop: has a gather left and isn't going away
#define POP_ENEMY 0x319588        // cPop: the monster it belongs to, else 0 (most carcasses don't say)
#define EM009_SET_ACTION 0xD565F8 // uEm009_00 (Gypceros), vtable +0x204: start an action (gypceros, kind, action)
#define POP_ITEM 0x319318         // cPop: roll its next item (pop, &item, &quantity) -> 1
#define IS_CARRIED 0x1943BC       // sItem: is the item carried in the hands (items, item)
#define MESSAGE 0x65DE34          // add a line to the quest's message log (kind, message, item, quantity, 0)
#define MSG_BEGIN 0x65DCE4        // the log's next lines go in one notification box...
#define MSG_END 0x65DD24          // ...which this shows
#define POUCH_PROMPT 0x670500     // open the "Item Pouch is full" window for an item to take (item, quantity)
#define PROMPT_OPEN 0x670584      // is that window open
#define PROMPT_ITEM 0x6705C0      // the window's item to take (u16 item, u16 quantity), 0 without the window; the byte
                                  // 8 before it is set while an item waits for the window to open
#define GOT_SPOP 0x1838D94        // the game's sPop (gathering spots)
#define GOT_ITEMS 0x1838BD0       // the game's sItem (item data)

// Delivery
#define DELIVERABLE 0x188C84      // cItemPouch: is Deliver open for an item (slot, item)
#define CLEAR_ACCOUNT 0x187A60    // cItemPouch: is "Clear out Account Items" open
#define LOCAL_HUNTER 0x276278     // the player manager's hunter for this console (players)
#define GOT_PLAYERS 0x1838D80     // the game's player manager

// Rewards
#define RESULT_SECTION 0x388EE8   // uUIResult: run one section (menu, item grids, selling) for a frame (ui, section)
#define SEND_TO_BOX 0x38A2DC      // uUIResult: send one slot to the Item Box (ui, slot) -> 7: the box had no room
#define RESULT_SPECIAL 0x3C6C6C   // hand over a slot of an item marked special in its data (result, slot, 0)
#define ITEM_DATA 0x194228        // sItem: an item's data (items, item)
#define UI_SOUND 0x54F260         // play a menu sound (kind, id); Send to the Item Box plays (0, 0x1F), or (0, 3) when full
#define MENU_ROWS 0x624BF8        // cUIOReward: set the menu's rows from a list of texts (uio, list, flag)
#define MENU_LOOKS 0x624D00       // cUIOReward: set the rows' looks (uio, enabled, cursor row)
#define REWARD_UPDATE 0x622FE4    // cUIOReward: per-frame update, places the highlight bar (uio)
#define UIO_SET_TEXT 0x56E808     // cUIO: set an object's text (uio, instance, object, text)
#define UIO_SET_VISIBLE 0x56E530  // cUIO: show or hide an object (uio, instance, object, on)
#define UIO_SET_LOOK 0x56E6FC     // cUIO: set an object's look (uio, instance, object, look)
#define ROW_LOOK 0x550354         // a menu row's look (enabled, selected, 0, 0) -> look
#define ENTRY_TEXT 0x54FA20       // the string of a text list entry (entry)
#define GUI_GET_Y 0x545AF8        // cGUI: an instance's y (gui, instance)
#define GUI_SET_Y 0x545AC0        // cGUI: set an instance's y (gui, instance, y)
#define CURSOR_SYNC 0x551D58      // move a cursor's drawing to an instance (cursor, uio, instance)
#define BOX_HAS_ROOM 0x199390     // sItemBox: has room for an item (box, item)
#define BOX_ADD 0x19945C          // sItemBox: add an item (box, item, quantity, 0, &left over)
#define MONNYAN_INIT 0x1B600C     // Meownster Hunters' reward list: set up with its menu (list, uio, ...)
#define MONNYAN_UPDATE 0x1B607C   // ...every frame (list)
#define MONNYAN_EVENT 0x1ADAEC    // ...tell the screen (list, 0xB, event, info)
#define MONNYAN_INFO 0x1BBE04     // ...an event's info, made empty (info)
#define EQUIP_ADD 0x143160        // the equipment box: add a piece (box, 0, &piece) -> its slot, 0xFFFF: full
#define RESULT_ITEM_SPECIAL 0x3C6B64 // an appraised item marked at item data +0x12 (result, slot)
#define APPRAISAL_UPDATE 0x3850B0 // uUIResult: run the appraisal for a frame (ui) -> 1: done
#define APPRAISAL_HELP 0x390AAC   // uUIResult: the appraisal's help line (ui); in the menu message 0x85 + cursor row
// The trade cart (Neko, uUITradeCenter): its Claim Items screen and its menu (cUIOTradeCenterBoxMenu)
#define TRADE_CLAIM 0x7A3480      // uUITradeCenter: Claim Items for a frame (ui) -> 2: closed by B
#define TRADE_MENU_DRAW 0x63ED84  // cUIOTradeCenterBoxMenu: draw what changed (uio)
#define TRADE_CART 0x195448       // a trade cart's inbox (the GOT_ITEMS object, cart 0-2) -> cart, 0 past the last
#define TRADE_CHANGED 0x1955B8    // (the GOT_ITEMS object) after the inbox changed, as the game's Send calls it
#define TRADE_REDRAW 0x7A0B74     // uUITradeCenter: draw the inbox again (ui, 1)
#define BOX_PUT 0x1990DC          // sItemBox: put an item in (box, item, quantity, 0), as the trade's Send does
#define MSG_SET 0x56FC58          // cUIO: an object's text from a message (uio, instance, object, 0, 1, message)
#define INST_VISIBLE 0x56E528     // cUIO: show or hide an instance (uio, instance, on)
#define LOOK_OF 0x560E90          // a text look (the GOT_LOOKS object, kind): 0 as usual, 0xB greyed
#define GOT_LOOKS 0x1838BD8       // (also the screens' manager: SCREEN(it, 0) is the screen holding the help line)
#define HELP_SET 0x65E5BC         // the help line: show a message (flags, a, table, message, x, format, args...)
#define SCREEN 0x55D2B4           // (GOT_LOOKS object, 0) -> the screen whose component 4 is the help line
#define HELP_UIO_INDEX 4
#define HELP_TEXT_VFUNC 0x64      // the help line's cUIO: show a string (uio, text, 0)
#define HELP_LAST_MESSAGE 0x56    // (u16) the message it shows, which a request for the same is skipped for
#define GH_UPDATE 0x710A64        // uUIGuestHunterResult (Hunters for Hire's results): every frame (ui)
#define MENU_SET 0x624664         // cUIOReward: set the menu's rows and looks (uio, rows, ...)
#define UI_COMPONENT 0x65CB3C     // uUIBase: one of its cUIO (ui, index); the reward screen's menu is index 0
#define GOT_PAD 0x183931C         // the game's pad state
#define GOT_RESULT 0x1838DE0      // the game's quest result
#define GOT_BOX 0x183A81C         // the game's sItemBox
#define GOT_EQUIP_BOX 0x1839394   // the game's equipment box

// ================================================================== game structures

// sPop and cPop: the active gathering spots are a list from sPop+0x5C, linked by +0x2C
#define SPOP_ACTIVE 0x5C
#define POP_NEXT 0x2C
#define POP_ID 0x4C               // (u16) the spot's id
#define POP_LEFT 0x4E             // (u16) gathers left
#define LEFT_UNLIMITED 0xFFFF
// uEnemy, a spot's monster
#define ENEMY_CARVE_STATE 0x338   // (vtable) how a carve affects it: 0 not at all (a carcass); 1 it is told, 2 refused
#define EM009_DATA 0xCAC0         // uEm009_00: its own data...
#define EM009_DEAD_POP 0x4C       // ...(s32) the id of its playing-dead carve spot, -1: none
#define MAX_PLAYING_DEAD 8        // the Gypceros remembered at once
// the hunter
#define HUNTER_STATE 0x2500
#define STATE_CARRYING 0x10       // set while carrying something in the hands
#define HUNTER_AWAY 0x1054        // (u8) 0 in the base camp, 1 out on the map
#define HUNTER_POP_ID 0x32AE      // (s16) the id of the spot being gathered
// the pouch's answers
#define ADD_NONE 0xFFFFFFFF       // (the plugin's own: no answer seen)
#define ADD_TAKEN_MAX 2           // 0-2: the pouch took it (2: some of it, the item reached its limit)
#define ADD_AT_LIMIT 3            // not taken: the item is at its limit ("can't carry any more")
#define ADD_NO_ROOM 5             // not taken: no free slot; the pouch opens the replace window (POUCH_PROMPT)
// the message log
#define MSG_OBTAINED 9            // kind 0, message 9: "<item> xN"

// uUIResult, the reward screen
#define RESULT_STATE 0x44
#define STATE_MENU 2              // the menu is open
#define RESULT_REDRAW 0x55        // (u8) set: draw the menu and grids again
#define RESULT_LISTS 0x5C         // per section {u16 *slots, u32 count}; a slot is {u16 item, s16 quantity}
#define RESULT_CURSOR 0xA0        // the menu's cursor
#define CURSOR_ROW 0xD            // (u8) its row
#define CURSOR_ROWS 0xE           // (u8) its rows
#define CURSOR_COUNT 0x14         // (u16) its entries (the moves stop at the last)
#define SEND_FULL 7
#define ITEM_SPECIAL 0x16         // (u8, item data) handed over by RESULT_SPECIAL instead of sent
// the pad
#define PAD_PRESSED 0x6BFC        // the buttons pressed this frame
#define PAD_A 0x2000
#define PAD_B 0x4000
// Meownster Hunters' reward list (uUIMonNyan's): its own menu logic over the same reward menu (mode 7)
#define MN_UIO 0x08
#define MN_STATE 0x10             // 1: the menu; 2: the list is empty, the menu shows Done
#define MN_LIST 0x14              // {u16 *slots, u32 count}, a slot as the reward screen's
#define MN_CURSOR 0x20            // the menu's cursor (+0xD its row, +0xE its rows, +0x10 its page)
#define MN_MENU 1
#define MN_DONE 2
#define MN_EV 0xB
#define MN_EV_SENT 2
#define MN_EV_EMPTY 0x10
#define MN_ROWS_DONE 8            // (MENU_SET: the menu with Done)
#define CURSOR_PAGE 0x10
// the reward screen's appraisal: its menu is state 4 (A: state 5 + row), its list {entry *, count} at +0x7C, an entry
// 0x34 bytes: u32 kind (0 none, 1 an item, 2 a piece of equipment), the item's slot at +4, the piece at +8
#define APPRAISAL_MENU 4
#define APPRAISAL_SELL 6         // (Sell Items & Exit's confirmation: cancelled, the menu with the cursor on its row 1)
#define APPRAISAL_LIST 0x7C
#define APPRAISAL_ENTRY 0x34
#define ENTRY_ITEM 1
#define ENTRY_EQUIP 2
#define ITEM_SPECIAL_APPRAISED 0x12
#define EQUIP_FULL 0xFFFF
// uUIGuestHunterResult, Hunters for Hire's results: the same reward menu (mode 6, its component 1)
#define GH_STATE 0x4C             // 2: the menu (3 Take, 4 Send, 5 Sell)
#define GH_MENU 2
#define GH_REDRAW 0x55
#define GH_LIST 0x3C              // {u16 *slots, u32 count}
#define GH_CURSOR 0x58
#define GH_UIO_INDEX 1
// the Claim Items screen: its sub-state (u8) 0 the menu (A: 1 + row: Take, Send's grid, Sell), its own copy of the pad
#define TRADE_SUBSTATE 7
#define TRADE_PAD 0x80
#define TRADE_CURSOR 0x178
#define TRADE_CARTS 3
#define CART_CELLS 0x10           // 3 rows of 10 cells {u16 item, s16 quantity}
#define CART_CELL_COUNT 30
// its menu: rows at instances 5-7 (messages 0x1A + row, 0x1D for Exit), the fourth row (the former layout tool) 119 or a runtime one
#define TM_FLAGS 0x4C             // what to draw: 1 the highlight, 4 the rows, 8 their visibility
#define TM_MODE 0x58              // 1: nothing to claim (Take and Send greyed, Exit)
#define TM_CURSOR 0x50
#define TM_ROW_SEND 6
#define TM_ROW_THIRD 7
#define TM_ROW_FOURTH 119         // (the former layout tool; without it, a runtime row: g_tm_row4)
#define TM_INSTANCES 119          // shop_trade's own instances
#define TM_ROW_PARENT 4           // Nul_mes_sub
#define TM_LINE 10                // Anm_line_sub
#define TM_LINE_PARENT 8          // Nul_line_sub
#define TM_WINDOW 3               // Anm_Window_sub (7.6, its flow 7.45)
#define TM_CART_INFO 29           // Nul_main0 (275)
#define TM_HIGHLIGHT 18
#define TM_SPACING 14
#define TM_MSG_SELL 0x1C
#define TM_MSG_EXIT 0x1D
#define LOOK_GREYED 0xB
#define SOUND_SENT 0x1F           // (UI_SOUND kind 0)
#define SOUND_FULL 3
#define SOUND_BACK 4             // (B moving the cursor to the last row)
// cUIOReward, the reward menu, and its layout
#define UIO_GUI 0x24
#define UIO_MODE 0x4C             // which screen uses the menu: the reward screen's sections are 1, 2 and 7
#define UIO_CURSOR 0x50
#define RESULT_MODES 0xC6         // (bits 1, 2, 6 Hunters for Hire, 7 Meownster Hunters; other screens use others)
#define MODE_APPRAISAL 3          // the reward screen's appraisal: two rows, Send to Boxes and Sell Items & Exit
#define GUI_INSTANCES 0x298       // the layout's instance count
#define ROW_TAKE 6                // layout instances: the first row
#define ROW_SEND 7                // the second row (Send to the Item Box)
#define ROW_THIRD 8               // the third row, which the plugin turns into "Send all..."
#define ROW_FOURTH 89             // the fourth row in the layout from the former layout tool (0.5-0.7)
// The added row and its separator are made at runtime as copies of the layout's own (a layout with the row from the
// former layout tool is still used when it's installed)
#define GUI_INST_ARRAY 0xF8       // cGUI: the layout's instances (objects)
#define GUI_CREATE_INST 0x54501C  // cGUI: a new instance (gui, resource, animation, parent, type DTI) -> its index
#define GUI_EXTRA_COUNT 0x288     // cGUI: instances made at runtime
#define INST_RESOURCE 0xD0        // cGUIInstAnimation: ResourceId (-1: the layout's own), AnimationId
#define INST_ANIMATION 0xD4
#define INST_SCL_X 0xA0           // cGUIInstance: scale (x, y)
#define INST_SCL_Y 0xA4
#define INST_SET_SCL_Y 0xA368DC   // cGUIInstance: set the y scale (instance, s0)
#define ROW_PARENT 5              // Nul_mes_sub
#define LINE 11                   // Anm_line_sub
#define LINE_PARENT 9             // Nul_line_sub
#define WINDOW 4                  // Anm_Window_sub
#define TIME_LEFT 26              // Nul_sub1
#define APPRAISAL_BOX 48          // Nul_sub3 (the appraisal's Time Left and box slots)
#define ROW_THIRD_Y 108.0f
#define ROW_FOURTH_Y 142.0f
#define ROW_STEP_Y 34.0f
#define ROW_OBJECT 1              // a row's text object
#define INST_SPACING 0x10         // its y is the spacing of the rows
#define INST_HIGHLIGHT 0x2F       // the highlight bar
#define MSG_RESULT 7              // the message table of the menu's texts...
#define MSG_SEND 1                // ...and "Send to the Item Box" in it

// ================================================================== plugin state and helpers

#define MAX_EXTRA 64              // the most extra gathers per press (spots have a handful)
#define MAX_QUEUED 24             // the most messages held back per press
#define BOX_LINES 6               // the lines a notification box takes
#define MAX_LEFT 16               // the items waiting for the replace window
#define PROMPT_TRIES 600          // frames without a replace window before the waiting items are dropped (about 10 s)
#define BUTTON_R 7                // (forge_input_isDown)
#define SINGLE_FRAMES 60          // R held this many frames ago or less: a single gather (the gather comes partway into
                                  // the gathering motion, so R can be let go by then)
#define PROMPT_PENDING 8          // (PROMPT_ITEM - 8: an item waits for the window)
#define MAX_SOUNDS 4              // the most distinct sounds remembered during one Send all

typedef u32 (*Gather)(u32 hunter, u32 empty_if_none, u32 notify);
typedef void (*Give)(u32 hunter, u32 item, u32 quantity, u32 notify, u32 zero);
typedef u32 (*Add)(u32 hunter, u32 item, u32 quantity, u32 flags, u32 zero);
typedef u32 (*Is)(u32 self);
typedef u32 (*Is2)(u32 self, u32 a);
typedef u32 (*PopUse)(u32 spop, u32 id, u32 *pop);
typedef u32 (*PopItem)(u32 pop, u16 *item, u8 *quantity);
typedef u32 (*IsCarried)(u32 items, u32 item);
typedef u32 (*Message)(u32 kind, u32 id, u32 item, u32 quantity, u32 extra);
typedef u32 (*Query)(void);
typedef u32 (*Prompt)(u32 item, u32 quantity);
typedef u32 (*Deliverable)(u32 slot, u32 item);
typedef u32 (*Section)(u32 ui, u32 section);
typedef u32 (*Component)(u32 ui, u32 index);
typedef u32 (*Send)(u32 ui, u32 slot);
typedef void (*Special)(u32 result, u32 slot, u32 zero);
typedef u32 (*Data)(u32 items, u32 item);
typedef u32 (*Sound)(u32 kind, u32 id);
typedef u32 (*EquipAdd)(u32 box, u32 zero, u32 piece);
typedef u32 (*BoxAdd)(u32 box, u32 item, s32 quantity, u32 zero, s32 *left);
typedef u32 (*Event)(u32 self, u32 kind, u32 event, void *info);
typedef void (*MenuSet)(u32 uio, u32 rows, u32 a, u32 b);
typedef void (*Rows)(u32 uio, u32 list, u32 flag);
typedef void (*Looks)(u32 uio, u32 enabled, u32 row);
typedef void (*Method)(u32 self);
typedef u32 (*Method4)(u32 self, u32 a1, u32 a2, u32 a3);
typedef void (*SetText)(u32 uio, u32 inst, u32 obj, const char *text);
typedef void (*SetInt)(u32 uio, u32 inst, u32 obj, u32 v);
typedef void (*SetInt3)(u32 uio, u32 inst, u32 v);
typedef void (*MsgSet)(u32 uio, u32 inst, u32 obj, u32 zero, u32 one, u32 message);
typedef u32 (*Look)(u32 enabled, u32 selected, u32 a, u32 b);
typedef const char *(*EntryText)(u32 entry);
typedef float (*GetY)(u32 gui, u32 inst);
typedef void (*SetY)(u32 gui, u32 inst, float y);
typedef void (*Sync)(u32 cursor, u32 uio, u32 inst);

static u32 g_main; // the game's main module

// A game function, and a pointer read through the game's GOT
#define FN(type, off) ((type)(g_main + (off)))
#define GLOBAL(got) (*(u32 *)*(u32 *)(g_main + (got)))
// A hook's trampoline to the original function
#define ORIGINAL(type, hook) ((type)(hook).original)

static ForgeHook g_gather_hook, g_add_hook, g_add_quest_hook, g_message_hook, g_play_dead_hook, g_prompt_hook;
static ForgeHook g_deliverable_hook, g_clear_account_hook;
static ForgeHook g_section_hook, g_rows_hook, g_looks_hook, g_reward_update_hook, g_sound_hook;
static ForgeHook g_help_hook, g_monnyan_init_hook, g_monnyan_hook, g_gh_hook, g_appraisal_hook, g_appraisal_help_hook, g_trade_hook, g_trade_menu_hook;
static bool g_gather_hooked, g_deliver_hooked, g_rewards_hooked;

// The switches in Forge's menu
static bool g_gather_all = true, g_merge = true, g_one_box = true, g_roll_all = true, g_hold_single = true;
static bool g_deliver_anywhere = true, g_send_all = true, g_auto_sort = false, g_meld_leftovers = true;
static bool g_meld_jackpot = false;

// The compiler may emit memset/memcpy calls; there is no C library to provide them
void *memset(void *d, int c, u32 n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, u32 n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }

// ================================================================== Gathering
//
// The game's gather (GATHER) runs once per gathering motion. It asks sPop to use up one gather of the spot the hunter
// faces (POP_USE), takes the spot's item (POP_ITEM: a fixed item, or a roll of the spot's table) and gives it to the
// hunter (GIVE: skill bonuses, the pouch, the "obtained" message, or the carry motion for eggs and the like).
//
// Carving uses the same spots: a carcass's carves are a spot's gathers (made from the monster's carve table), so they
// are taken at once too.
//
// The plugin hooks GATHER. After a gather that put an item in the pouch, it repeats those three steps until the spot
// is used up, following the same rules as pressing the button again. It stops at the carve spots of a living monster,
// where the carves are meant to be limited by the monster getting up, not by the spot's count:
//   - Gypceros playing dead (uEm009_00): starting that action (its vtable +0x204, EM009_SET_ACTION) makes a spot from
//     its carve table, which it keeps at its data +0x4C and removes when it gets up. Nothing tells it about carves; it
//     gets up when the action ends, so in the game there is time for about one carve. Taken at once, every carve of
//     the spot came out, each time it played dead. The plugin notes the spot when the action starts;
//   - a spot that belongs to a monster that is told about carves (POP_ENEMY, and its uEnemy virtual 0x338 isn't 0):
//     the hunter's carve tells the monster, which may end the spot. Only uEm056_00 has one, made while it is alive;
//   - at a spot with an unlimited count (it would never run out);
//   - before an item carried in the hands (eggs, powderstone...): that gather isn't used, so the next press gathers
//     it as usual; likewise when the hunter is already carrying something;
//   - when the spot runs out or breaks early (sPop's own count and early-break roll).
// Holding R as the gathering starts gathers a single item, as the game does (for a spot whose items can only be carried
// one at a time, such as Khezu Welps, where gathering at once would use up every chance): forge_onUpdate notes the
// last frame R was down.
//
// A full pouch doesn't stop it: every gather is rolled, the pouch takes what it can (an item it holds already stacks),
// an item at its limit is lost as in the game ("can't carry any more"), and an item with no free slot waits. The game
// would open the replace window for it (POUCH_PROMPT), one item a press: the plugin holds those back while the spot
// is gathered and opens the window for them once it's done, one after the other as each is closed (forge_onUpdate).
// With that switch off it stops at the first item the pouch refuses, as it did before.
// The plugin rolls the item before using up the gather (the game does it the other way round), so stopping for a
// carried item costs nothing. Every item still gets its own roll.
//
// Messages: the pouch posts an "obtained" line per item (MESSAGE, kind 0, message 9), and a line when it can't take
// one (message 12, the pouch is full or the item at its limit). While a spot is gathered they are held back in order,
// with the lines of the same item merged into one with the summed quantity ("Iron Ore x3"). They are posted when the
// spot is done, all in one notification: the log puts the lines posted between MSG_BEGIN and MSG_END in one box, as
// the game does for its own lists. A box takes 6 lines (the log drops a 7th), so a longer list goes on in a new box.
// The pouch's line stays where it came, after the items the pouch took.

static u32 g_add_result = ADD_NONE; // the pouch's answer to the last item given
static u32 g_last_extra, g_total;   // for Forge's menu: extra items from the last spot, and this session
static const char *g_last_stop = "";

// The quest hunter's override calls the base function, and returns last, so the outer answer is the one kept.
static u32 add_hook(u32 hunter, u32 item, u32 quantity, u32 flags, u32 zero) {
    return g_add_result = ORIGINAL(Add, g_add_hook)(hunter, item, quantity, flags, zero);
}

static u32 add_quest_hook(u32 hunter, u32 item, u32 quantity, u32 flags, u32 zero) {
    return g_add_result = ORIGINAL(Add, g_add_quest_hook)(hunter, item, quantity, flags, zero);
}

// The messages held back while a spot is gathered
static bool g_holding;
static u32 g_queued;
static struct { u32 kind, id, item, quantity, extra; } g_queue[MAX_QUEUED];

static u32 message_hook(u32 kind, u32 id, u32 item, u32 quantity, u32 extra) {
    if (!g_holding) return ORIGINAL(Message, g_message_hook)(kind, id, item, quantity, extra);
    if (id != MSG_OBTAINED) // (the same warning for each gather of an item at its limit: once)
        for (u32 i = 0; i < g_queued; i++)
            if (g_queue[i].kind == kind && g_queue[i].id == id && g_queue[i].item == item &&
                g_queue[i].quantity == quantity)
                return 0;
    if (g_merge && kind == 0 && id == MSG_OBTAINED) {
        for (u32 i = 0; i < g_queued; i++) {
            if (g_queue[i].kind == 0 && g_queue[i].id == MSG_OBTAINED && g_queue[i].item == item) {
                u32 sum = g_queue[i].quantity + quantity;
                g_queue[i].quantity = sum > 0xFF ? 0xFF : sum; // (the game passes it as a byte)
                return 0;
            }
        }
    }
    if (g_queued == MAX_QUEUED) return ORIGINAL(Message, g_message_hook)(kind, id, item, quantity, extra);
    g_queue[g_queued].kind = kind, g_queue[g_queued].id = id, g_queue[g_queued].item = item;
    g_queue[g_queued].quantity = quantity, g_queue[g_queued].extra = extra;
    g_queued++;
    return 0; // (the pouch doesn't look at the result)
}

static void hold_messages(bool on) {
    g_holding = on && (g_merge || g_one_box) && g_message_hook.original;
    g_queued = 0;
}

static void post_messages(void) {
    g_holding = false;
    bool boxed = g_one_box && g_queued > 1;
    for (u32 i = 0; i < g_queued; i++) {
        if (boxed && i % BOX_LINES == 0) {
            if (i) FN(Query, MSG_END)();
            FN(Query, MSG_BEGIN)();
        }
        ORIGINAL(Message, g_message_hook)(g_queue[i].kind, g_queue[i].id, g_queue[i].item, g_queue[i].quantity,
                                          g_queue[i].extra);
    }
    if (boxed) FN(Query, MSG_END)();
    g_queued = 0;
}

static u32 find_pop(u32 spop, u32 id) {
    for (u32 pop = *(u32 *)(spop + SPOP_ACTIVE); pop; pop = *(u32 *)(pop + POP_NEXT))
        if (*(u16 *)(pop + POP_ID) == id) return pop;
    return 0;
}

static bool pouch_took_it(void) { return g_add_result != ADD_NONE && g_add_result <= ADD_TAKEN_MAX; }

// The pouch's answer lets the spot go on: taken, or (rolling every gather) at its limit or waiting for a free slot
static bool keep_rolling(void) {
    if (pouch_took_it()) return true;
    return g_roll_all && g_prompt_hook.original && (g_add_result == ADD_AT_LIMIT || g_add_result == ADD_NO_ROOM);
}

// The items waiting for the replace window: noted while a spot is gathered, shown once it's done
static bool g_spot;
static u32 g_lefts, g_prompt_wait;
static struct { u16 item; u8 quantity; } g_left[MAX_LEFT];

static u32 prompt_hook(u32 item, u32 quantity) {
    if (!g_spot) return ORIGINAL(Prompt, g_prompt_hook)(item, quantity);
    for (u32 i = 0; i < g_lefts; i++) {
        if (g_left[i].item == item) {
            u32 sum = g_left[i].quantity + quantity;
            g_left[i].quantity = sum > 0xFF ? 0xFF : sum;
            return 0;
        }
    }
    if (g_lefts < MAX_LEFT) g_left[g_lefts].item = item, g_left[g_lefts].quantity = quantity, g_lefts++;
    return 0; // (the pouch doesn't look at it)
}

// Opens the replace window for the next waiting item when it's free. Called after a spot and every frame.
static void prompt_next(void) {
    if (!g_lefts || g_spot) return;
    u32 item = FN(Query, PROMPT_ITEM)();
    if (!item) { // (no window: the quest is over)
        if (++g_prompt_wait > PROMPT_TRIES) {
            forge_log(LOG_WARN, "field-qol: no replace window, %u waiting items left behind", g_lefts);
            g_lefts = g_prompt_wait = 0;
        }
        return;
    }
    g_prompt_wait = 0;
    u8 *pending = (u8 *)(item - PROMPT_PENDING);
    if (*pending || FN(Query, PROMPT_OPEN)()) return; // (one is waiting or open: the next waits for it)
    ORIGINAL(Prompt, g_prompt_hook)(g_left[0].item, g_left[0].quantity);
    if (!*pending) return; // (not taken: again next frame)
    for (u32 i = 1; i < g_lefts; i++) g_left[i - 1] = g_left[i];
    g_lefts--;
}

static bool carrying(u32 hunter) { return (*(u32 *)(hunter + HUNTER_STATE) & STATE_CARRYING) != 0; }

// The carve spots of Gypceros playing dead, noted each time one starts or ends an action
static struct { u32 gypceros; s32 pop; } g_playing_dead[MAX_PLAYING_DEAD];

static u32 play_dead_hook(u32 gypceros, u32 kind, u32 action, u32 a3) {
    u32 r = ORIGINAL(Method4, g_play_dead_hook)(gypceros, kind, action, a3);
    u32 data = *(u32 *)(gypceros + EM009_DATA);
    s32 pop = data ? *(s32 *)(data + EM009_DEAD_POP) : -1;
    u32 slot = MAX_PLAYING_DEAD;
    for (u32 i = 0; i < MAX_PLAYING_DEAD && slot == MAX_PLAYING_DEAD; i++)
        if (g_playing_dead[i].gypceros == gypceros) slot = i;
    for (u32 i = 0; i < MAX_PLAYING_DEAD && slot == MAX_PLAYING_DEAD; i++)
        if (!g_playing_dead[i].gypceros || g_playing_dead[i].pop < 0) slot = i; // (a free entry)
    if (slot < MAX_PLAYING_DEAD) g_playing_dead[slot].gypceros = gypceros, g_playing_dead[slot].pop = pop;
    return r;
}

// A carve spot of a living monster: Gypceros playing dead, or a monster that is told about carves
static bool living_monster(u32 pop) {
    u16 id = *(u16 *)(pop + POP_ID);
    for (u32 i = 0; i < MAX_PLAYING_DEAD; i++)
        if (g_playing_dead[i].gypceros && g_playing_dead[i].pop >= 0 && (u16)g_playing_dead[i].pop == id) return true;
    u32 enemy = FN(Is, POP_ENEMY)(pop);
    if (!enemy) return false;
    Is carve_state = *(Is *)(*(u32 *)enemy + ENEMY_CARVE_STATE);
    return carve_state(enemy) != 0;
}

// Gathers the rest of the spot after the game's own gather. Returns why it stopped (shown in Forge's menu).
static const char *gather_rest(u32 hunter, u32 notify, u32 *extra) {
    u32 spop = GLOBAL(GOT_SPOP), items = GLOBAL(GOT_ITEMS);
    s16 sid = *(s16 *)(hunter + HUNTER_POP_ID);
    if (!spop || sid < 0) return "no spot";
    u32 id = (u16)sid;
    for (*extra = 0; *extra < MAX_EXTRA;) {
        u32 pop = find_pop(spop, id);
        if (!pop) return "spot gone";
        if (living_monster(pop)) return "living monster";
        if (*(u16 *)(pop + POP_LEFT) == LEFT_UNLIMITED) return "unlimited spot";
        if (!FN(Is, POP_CAN_GATHER)(pop)) return "used up";
        if (carrying(hunter)) return "carrying";

        // roll first: a carried item is left for a press of its own
        u16 item = 0;
        u8 quantity = 0;
        if (FN(PopItem, POP_ITEM)(pop, &item, &quantity) != 1 || !item) return "no item";
        if (FN(IsCarried, IS_CARRIED)(items, item)) return "carried item";

        u32 used = 0, left = FN(PopUse, POP_USE)(spop, id, &used);
        if (left != 1 && left != 2) return "used up";
        g_add_result = ADD_NONE;
        FN(Give, GIVE)(hunter, item, quantity, notify, 0);
        ++*extra;
        if (!keep_rolling()) return "pouch refused";
        if (left == 2) return "used up";
    }
    return "limit";
}

static u32 g_since_r = SINGLE_FRAMES + 1; // frames since R was last down

static u32 gather_hook(u32 hunter, u32 empty_if_none, u32 notify) {
    bool single = g_hold_single && g_since_r <= SINGLE_FRAMES;
    bool on = g_gather_all && !single && hunter && FN(Is, IS_LOCAL)(hunter) == 1;
    g_add_result = ADD_NONE;
    hold_messages(on);
    g_spot = on && g_roll_all;
    u32 r = ORIGINAL(Gather, g_gather_hook)(hunter, empty_if_none, notify);
    // (when the first item became a carried item, or the pouch refused it, the press ends as usual)
    if (on && r == 1 && keep_rolling()) {
        u32 extra = 0;
        g_last_stop = gather_rest(hunter, notify, &extra);
        g_last_extra = extra;
        g_total += extra;
        if (extra)
            forge_log(LOG_INFO, "field-qol: %u more from spot %d (%s)", extra, *(s16 *)(hunter + HUNTER_POP_ID),
                      g_last_stop);
    }
    g_spot = false;
    post_messages();
    prompt_next();
    return r;
}

// ================================================================== Delivery
//
// The Item Pouch delivers in two ways:
//   - an item's Deliver action (DELIVERABLE), open for what the game calls deliverable: a quest in progress that has
//     deliveries, the pouch usable, not a carried item, and a main or sub target item or an account item. The game
//     asks this every frame the item's actions are shown, and again when Deliver is chosen;
//   - the "Clear out Account Items" button (CLEAR_ACCOUNT), while the pouch holds account items that aren't targets.
// Both also want the hunter in the base camp, which the game reads from the hunter's byte +0x1054 (0 in the camp).
// The plugin sets that byte to 0 only while the game asks those two questions, and restores it right after, so
// nothing else sees the change.

// The hunter's "away from camp" byte when it needs changing (null when the switch is off or the hunter is in camp)
static u8 *away_flag(void) {
    if (!g_deliver_anywhere) return 0;
    u32 players = GLOBAL(GOT_PLAYERS), hunter = players ? FN(Is, LOCAL_HUNTER)(players) : 0;
    u8 *away = hunter ? (u8 *)(hunter + HUNTER_AWAY) : 0;
    return away && *away ? away : 0;
}

static u32 deliverable_hook(u32 slot, u32 item) {
    u8 *away = away_flag(), was = away ? *away : 0;
    if (away) *away = 0;
    u32 r = ORIGINAL(Deliverable, g_deliverable_hook)(slot, item);
    if (away) *away = was;
    return r;
}

static u32 clear_account_hook(void) {
    u8 *away = away_flag(), was = away ? *away : 0;
    if (away) *away = 0;
    u32 r = ORIGINAL(Query, g_clear_account_hook)();
    if (away) *away = was;
    return r;
}

// ================================================================== Rewards
//
// The reward screen (uUIResult) shows one section at a time (the reward box, the Palicoes' items...) beside a menu:
// Take Items (or Receive from Palico), Send to the Item Box, Sell Items & Exit (or Done). RESULT_SECTION runs it each
// frame. A section's slots are one list (all its Palicoes' items in one), and Send to the Item Box sends a slot with
// SEND_TO_BOX (as much as the box takes) or hands it to RESULT_SPECIAL when the item is marked special.
//
// The menu is drawn by cUIOReward: MENU_ROWS sets the rows' texts (layout instances 6, 7, 8), MENU_LOOKS their looks,
// and REWARD_UPDATE places the highlight bar at the cursor's row times the row spacing. The plugin adds a fourth row
// to the layout at runtime (a copy of the third, see clone_inst) and makes the window taller, puts "Send all..." on
// the third row and moves the game's third entry to the fourth.
//
// The game's cursor keeps its three rows. "Send all" is an extra row between Send (1) and the third (2) that only the
// plugin knows about: it is entered by moving down from Send or up from the third row, while the game's cursor stays
// on Send. Pressing A there sends every slot of the section. When that empties the section, the cursor goes to the
// game's third row (Done, by then) and the game gets the A, so the section ends as if Done was chosen. When the box
// fills up first, the menu stays with what is left, and the game never sees the A.
//
// Meownster Hunters' rewards use the same menu (mode 7) with logic of their own (MONNYAN_UPDATE, state 1 the menu, its
// cursor at +0x20, its list at [+0x14]): Send to the Item Box adds a slot to the box with BOX_ADD (after BOX_HAS_ROOM)
// and tells the screen (MONNYAN_EVENT, sent); when the list is empty it shows Done (MENU_SET, the cursor on it, state
// 2), where A closes the screen. Send all does the same for every slot, then, when the list is empty, sets up Done the
// same way and leaves the A to the game, which closes the screen.
//
// Hunters for Hire's results (uUIGuestHunterResult, GH_UPDATE) use the same menu (mode 6) too, with their own logic:
// state 2 the menu, its cursor at +0x58, its list at +0x3C; Send to the Item Box as above; A on the menu with an empty
// list closes the screen. Send all sends every slot, plays the sound once, and leaves that A to the game.
//
// Sending plays a sound per slot. The game's sound manager starts each one on its own voice, so several in one frame
// add up to a louder sound: during a Send all, each sound plays only once.

static const char SEND_ALL_TEXT[] = "Send all to the Item Box & Exit";
static const char SEND_ALL_BOXES_TEXT[] = "Send all to Boxes & Exit"; // (the appraisal, beside its "Send to Boxes")
// Send all's help lines
static const char HELP_SEND_ALL[] = // (with the reward screens' own hint, as their lines have it)
    "Send every item to your Item Box and exit.\r\n<COL FF0000FF>(X Button: Show/hide menu)</COL>";
static const char HELP_TRADE[] = "Send every item from your trades to\r\nthe Item Box and exit.";
static const char HELP_APPRAISAL[] = "Send every appraised item and piece of\r\nequipment to its box and exit.";
static u32 g_menu_uio;     // the menu showing the added row (0: none)
static u32 g_result_uio;   // the reward screen's menu
static u32 g_monnyan_uio;  // Meownster Hunters' reward menu
static u32 g_gh_uio;       // Hunters for Hire's
static bool g_on_send_all; // the cursor is on the added row (the game's cursor stays on Send)

// The sounds already played during this Send all
static bool g_one_sound_each;
static u32 g_sounds, g_sound[MAX_SOUNDS];

static u32 sound_hook(u32 kind, u32 id) {
    if (g_one_sound_each) {
        u32 key = kind << 16 | (id & 0xFFFF);
        for (u32 i = 0; i < g_sounds; i++)
            if (g_sound[i] == key) return 0;
        if (g_sounds < MAX_SOUNDS) g_sound[g_sounds++] = key;
    }
    return ORIGINAL(Sound, g_sound_hook)(kind, id);
}

static u32 ROW_INSTANCES[4]; // (below)
typedef u32 (*CreateInst)(u32 gui, u32 resource, u32 animation, u32 parent, u32 dti);
typedef void (*SetF)(u32 instance, float v);
static u32 g_rt_gui, g_rt_line, g_rt_row3; // the layout given runtime rows, its separator, and a third
                                           // row for the appraisal (whose state hides the layout's)
static bool g_rt;               // the fourth row is a runtime one

static u32 inst_obj(u32 gui, u32 i) { return *(u32 *)(*(u32 *)(gui + GUI_INST_ARRAY) + 4 * i); }

// A runtime copy of layout instance src (same type, resource and animation) under parent, at y
static u32 clone_inst(u32 gui, u32 src, u32 parent, float y) {
    u32 obj = inst_obj(gui, src);
    if (!obj) return 0;
    u32 dti = ((u32(*)(u32))(*(u32 *)(*(u32 *)obj + 0x14)))(obj);
    u32 idx = FN(CreateInst, GUI_CREATE_INST)(gui, *(u32 *)(obj + INST_RESOURCE), *(u32 *)(obj + INST_ANIMATION),
                                             parent, dti);
    FN(SetY, GUI_SET_Y)(gui, idx, y);
    forge_log(LOG_INFO, "field-qol: menu row %u made as a copy of %u", idx, src);
    return idx;
}

static bool has_fourth_row(u32 uio) {
    u32 gui = *(u32 *)(uio + UIO_GUI);
    if (!gui) return false;
    if (*(u32 *)(gui + GUI_INSTANCES) > ROW_FOURTH) { // (the former layout tool's layout is loaded)
        ROW_INSTANCES[3] = ROW_FOURTH, g_rt = false;
        return true;
    }
    if (*(u32 *)(gui + GUI_INSTANCES) <= ROW_FOURTH - 60) return false; // (not the reward layout)
    if (gui != g_rt_gui || !*(u32 *)(gui + GUI_EXTRA_COUNT)) {
        u32 row = clone_inst(gui, ROW_THIRD, ROW_PARENT, ROW_FOURTH_Y);
        if (!row) return false;
        g_rt_line = clone_inst(gui, LINE, LINE_PARENT, ROW_STEP_Y);
        g_rt_row3 = clone_inst(gui, ROW_THIRD, ROW_PARENT, ROW_THIRD_Y);
        g_rt_gui = gui, ROW_INSTANCES[3] = row;
    }
    g_rt = true;
    return true;
}

// Moves instance i's y (or, with scale, its y scale) from `from` to `to` when it's at `from` (the layout's own value)
static void nudge(u32 gui, u32 i, bool scale, float from, float to) {
    if (scale) {
        u32 obj = inst_obj(gui, i);
        float v = *(float *)(obj + INST_SCL_Y);
        if (v > from - 0.02f && v < from + 0.02f) FN(SetF, INST_SET_SCL_Y)(obj, to);
    } else {
        float v = FN(GetY, GUI_GET_Y)(gui, i);
        if (v > from - 0.5f && v < from + 0.5f) FN(SetY, GUI_SET_Y)(gui, i, to);
    }
}

// The window one row taller and the box under it one row lower, as the former layout tool made the layout:
// for a three-row menu (1) or the appraisal's two rows (2); the last one's changes undone when it's gone
enum { RT_NONE, RT_FOUR, RT_APPRAISAL };
static void runtime_layout(u32 gui, u32 mode) {
    static u32 applied;
    if (mode != applied) {
        if (applied == RT_FOUR) nudge(gui, WINDOW, true, 9.2f, 7.5f), nudge(gui, TIME_LEFT, false, 256.0f, 222.0f);
        if (applied == RT_APPRAISAL)
            nudge(gui, WINDOW, true, 7.6f, 5.9f), nudge(gui, APPRAISAL_BOX, false, 222.0f, 188.0f);
        applied = mode;
    }
    if (mode == RT_FOUR) nudge(gui, WINDOW, true, 7.5f, 9.2f), nudge(gui, TIME_LEFT, false, 222.0f, 256.0f);
    if (mode == RT_APPRAISAL)
        nudge(gui, WINDOW, true, 5.9f, 7.6f), nudge(gui, APPRAISAL_BOX, false, 188.0f, 222.0f);
}

// The menu showing the added row: its rows (3, or 2 for the appraisal) and the row of Send, which "Send all" follows.
// With three rows the game's cursor stays on Send while "Send all" is highlighted (g_on_send_all); the appraisal's
// cursor is given a third row instead (two rows wrap, so up and down couldn't be told apart), which the plugin maps.
static u32 g_menu_rows = 3, g_send_row = 1;
static bool g_cursor_extended;
static u32 ROW_INSTANCES[4] = {ROW_TAKE, ROW_SEND, ROW_THIRD, ROW_FOURTH};

// A menu left through Send all: it keeps Send all highlighted while the screen closes, as the game keeps Sell Items &
// Exit highlighted (the plugin closes it through another row or button of the game's), until the game's cursor is back
// on the first row (the menu opened again)
static u32 g_exit_uio;
static bool g_trade_exit;

// The row the player sees highlighted: the game's rows with "Send all" after Send
static u32 shown_row(u32 game_row) {
    if (g_cursor_extended) return game_row;
    return g_on_send_all ? g_send_row + 1 : game_row > g_send_row ? game_row + 1 : game_row;
}

static u32 *section_list(u32 ui, u32 section) {
    return (u32 *)(ui + RESULT_LISTS + 8 * (section <= 3 ? section : 0)); // (as the game picks it)
}

// Sends every slot of the section to the Item Box, the way Send to the Item Box does. Returns the slots sent.
static u32 send_all(u32 ui, u32 section) {
    u32 *list = section_list(ui, section);
    u16 *slots = (u16 *)list[0];
    u32 items = GLOBAL(GOT_ITEMS), result = GLOBAL(GOT_RESULT), sent = 0;
    g_one_sound_each = true, g_sounds = 0;
    for (u32 i = 0; slots && i < list[1]; i++) {
        u16 *slot = slots + 2 * i;
        if (!slot[0]) continue;
        u32 data = FN(Data, ITEM_DATA)(items, slot[0]);
        if (!data) continue;
        if (*(u8 *)(data + ITEM_SPECIAL)) {
            FN(Special, RESULT_SPECIAL)(result, (u32)slot, 0);
            slot[0] = slot[1] = 0;
        } else if (FN(Send, SEND_TO_BOX)(ui, (u32)slot) == SEND_FULL) {
            break;
        }
        sent++;
    }
    g_one_sound_each = false;
    *(u8 *)(ui + RESULT_REDRAW) = 1;
    return sent;
}

static bool section_empty(u32 ui, u32 section) {
    const u32 *list = section_list(ui, section);
    const u16 *slots = (const u16 *)list[0];
    for (u32 i = 0; slots && i < list[1]; i++)
        if (slots[2 * i]) return false;
    return true;
}

static void track_cursor(u8 *cursor, u32 before, u32 was, u32 screen, const char *help);
static void help_show(const char *text);

// The appraisal: Send to Boxes as the game does it (items to the Item Box as the other sections', items marked at
// +0x12 to RESULT_ITEM_SPECIAL, equipment to the equipment box), every entry
static bool appraisal_empty(u32 ui) {
    const u32 *list = (const u32 *)(ui + APPRAISAL_LIST);
    for (u32 i = 0; list[0] && i < list[1]; i++)
        if (*(u32 *)(list[0] + APPRAISAL_ENTRY * i)) return false;
    return true;
}

static u32 appraisal_send_all(u32 ui) {
    const u32 *list = (const u32 *)(ui + APPRAISAL_LIST);
    u32 items = GLOBAL(GOT_ITEMS), sent = 0;
    g_one_sound_each = true, g_sounds = 0;
    for (u32 i = 0; list[0] && i < list[1]; i++) {
        u32 *entry = (u32 *)(list[0] + APPRAISAL_ENTRY * i);
        u16 *slot = (u16 *)(entry + 1);
        if (entry[0] == ENTRY_ITEM && slot[0]) {
            u32 data = FN(Data, ITEM_DATA)(items, slot[0]);
            if (!data) continue;
            if (*(u8 *)(data + ITEM_SPECIAL_APPRAISED)) {
                FN(Special, RESULT_ITEM_SPECIAL)(GLOBAL(GOT_RESULT), (u32)slot, 0);
                entry[0] = entry[1] = 0;
            } else if (FN(Send, SEND_TO_BOX)(ui, (u32)slot) == SEND_FULL) {
                continue; // (the Item Box is full: the equipment may still fit)
            } else if (!slot[1]) {
                entry[0] = 0;
            }
            sent++;
        } else if (entry[0] == ENTRY_EQUIP) {
            if (FN(EquipAdd, EQUIP_ADD)(GLOBAL(GOT_EQUIP_BOX), 0, (u32)(entry + 2)) == EQUIP_FULL) {
                FN(Sound, UI_SOUND)(0, SOUND_FULL);
                continue;
            }
            entry[0] = 0;
            FN(Sound, UI_SOUND)(0, SOUND_SENT);
            sent++;
        }
    }
    g_one_sound_each = false;
    *(u8 *)(ui + RESULT_REDRAW) = 1;
    return sent;
}

// The appraisal's menu (its own update, not a section's), its cursor given a third row, which the game's own moves then
// use (wrapping as before): 0 Send to Boxes, 1 Send all (the plugin's), 2 Sell Items & Exit (the game's row 1)
static u32 appraisal_hook(u32 ui) {
    u32 uio = FN(Component, UI_COMPONENT)(ui, 0);
    if (uio && uio != g_result_uio) {
        g_result_uio = uio;
        *(u8 *)(ui + RESULT_REDRAW) = 1;
    }
    static bool from_sell; // (Sell's confirmation was open: cancelled, the game's cursor is on its row 1)
    u8 *cursor = (u8 *)(ui + RESULT_CURSOR);
    u32 state = *(u32 *)(ui + RESULT_STATE);
    if (state == APPRAISAL_SELL) from_sell = true; // (until the menu is back; a new appraisal starts on row 0)
    bool menu = g_send_all && uio && uio == g_menu_uio && g_menu_rows == 2 &&
                state == APPRAISAL_MENU && (cursor[CURSOR_ROWS] == 2 || cursor[CURSOR_ROWS] == 3);
    if (!menu) {
        g_cursor_extended = false;
        return ORIGINAL(Is, g_appraisal_hook)(ui);
    }
    if (from_sell && cursor[CURSOR_ROW] == 1) cursor[CURSOR_ROW] = 2; // (back on Sell: the plugin's row 2)
    from_sell = false;
    u32 *pressed = (u32 *)(GLOBAL(GOT_PAD) + PAD_PRESSED), was = *pressed;
    if (!g_cursor_extended) *(u8 *)(ui + RESULT_REDRAW) = 1; // (the rows' looks for the third row)
    cursor[CURSOR_ROWS] = 3;
    *(u16 *)(cursor + CURSOR_COUNT) = 3;
    g_cursor_extended = true;
    u32 row = cursor[CURSOR_ROW];
    if (was & (PAD_A | PAD_B)) g_exit_uio = 0; // (the menu is in use again; Send all sets it below when it leaves)
    if (was & PAD_B) {
        // the game's B goes to its last row, which is Send all here: to Sell instead
        if (row != 2) {
            cursor[CURSOR_ROW] = 2;
            FN(Sound, UI_SOUND)(0, SOUND_BACK);
            *(u8 *)(ui + RESULT_REDRAW) = 1;
        }
        *pressed = was & ~(PAD_B | PAD_A);
    } else if ((was & PAD_A) && row == 1) {
        u32 sent = appraisal_send_all(ui);
        bool leaving = appraisal_empty(ui);
        forge_log(LOG_INFO, "field-qol: sent %u appraised entries to the boxes%s", sent,
                  leaving ? ", leaving" : ", a box is full");
        if (!leaving) *pressed = was & ~PAD_A; // (with entries left the game would sell them; with none its A ends
                                               // the appraisal, from any row)
        else g_exit_uio = uio;
    } else if ((was & PAD_A) && row == 2) {
        cursor[CURSOR_ROW] = 1; // (the game's Sell Items & Exit)
    }
    u32 r = ORIGINAL(Is, g_appraisal_hook)(ui);
    *pressed = was;
    // out of the menu (Send to Boxes' grid, Sell's confirmation): the game's rows again from this frame's drawing on,
    // or the highlight would show its row 1 as Send all for a frame
    if (*(u32 *)(ui + RESULT_STATE) != APPRAISAL_MENU) g_cursor_extended = false;
    return r;
}

// Its help line, for the row the game sees: Send all gets Send to Boxes' line, Sell Items & Exit its own
static void appraisal_help_hook(u32 ui) {
    u8 *cursor = (u8 *)(ui + RESULT_CURSOR);
    if (!g_cursor_extended || *(u32 *)(ui + RESULT_STATE) != APPRAISAL_MENU) {
        ORIGINAL(Method, g_appraisal_help_hook)(ui);
        return;
    }
    u8 row = cursor[CURSOR_ROW];
    cursor[CURSOR_ROW] = row == 2 ? 1 : 0;
    ORIGINAL(Method, g_appraisal_help_hook)(ui);
    cursor[CURSOR_ROW] = row;
    if (row == 1) help_show(HELP_APPRAISAL);
}

static u32 section_hook(u32 ui, u32 section) {
    // the reward screen's menu: known from its first frame (the rows are drawn again to show the added one)
    u32 uio = FN(Component, UI_COMPONENT)(ui, 0);
    if (uio && uio != g_result_uio) {
        g_result_uio = uio;
        *(u8 *)(ui + RESULT_REDRAW) = 1;
    }

    u8 *cursor = (u8 *)(ui + RESULT_CURSOR);
    g_cursor_extended = false;
    bool menu = g_send_all && g_menu_uio && *(u32 *)(ui + RESULT_STATE) == STATE_MENU && cursor[CURSOR_ROWS] == 3;
    if (!menu) {
        g_on_send_all = false;
        return ORIGINAL(Section, g_section_hook)(ui, section);
    }

    // A on "Send all": send, then either exit through the game's third row or hide the A from the game
    u32 *pressed = (u32 *)(GLOBAL(GOT_PAD) + PAD_PRESSED), was = *pressed;
    bool leaving = false;
    if (g_on_send_all && (was & PAD_A)) {
        u32 sent = send_all(ui, section);
        leaving = section_empty(ui, section);
        forge_log(LOG_INFO, "field-qol: sent %u reward slots to the Item Box (section %u)%s", sent, section,
                  leaving ? ", leaving" : ", the box is full");
        if (leaving) {
            cursor[CURSOR_ROW] = 2;
            g_on_send_all = false, g_exit_uio = uio;
        } else {
            *pressed = was & ~PAD_A; // (the game would open Send's grid)
        }
    }

    u32 before = cursor[CURSOR_ROW];
    u32 r = ORIGINAL(Section, g_section_hook)(ui, section);
    *pressed = was;
    if (leaving || *(u32 *)(ui + RESULT_STATE) != STATE_MENU) {
        g_on_send_all = false;
        return r;
    }

    track_cursor(cursor, before, was, uio, HELP_SEND_ALL);
    return r;
}

// The help line. While "Send all" is highlighted it shows Send all's own line (g_help_text, per screen), whatever the
// game asks for. The game's requests are recorded per row of its cursor, so that when the plugin puts the cursor back
// on Send (Up from Send all: the game already moved to the first row and showed its line) Send's line is shown again.
typedef void (*HelpSet)(u32 flags, u32 a, u32 table, u32 message, u32 x, u32 format, u32 arg0, u32 arg1);
static u32 g_help_last[8], g_help_rows[3][8];
static bool g_help_asked, g_help_known[3];
static const char *g_help_text;
static u32 g_help_screen; // (the screen the rows were recorded for)

static void help_show(const char *text) {
    u32 screen = FN(Is2, SCREEN)(GLOBAL(GOT_LOOKS), 0);
    u32 uio = screen ? FN(Component, UI_COMPONENT)(screen, HELP_UIO_INDEX) : 0;
    if (!uio) return;
    ((void (*)(u32, const char *, u32))(*(u32 *)(*(u32 *)uio + HELP_TEXT_VFUNC)))(uio, text, 0);
    *(u16 *)(uio + HELP_LAST_MESSAGE) = 0xFFFF; // (the game's next request is shown, even if it's the last one)
}

// Meld Result (below): its menu's cursor while the game runs its frame, and the help lines of its rows
static u8 *g_mr_cursor;
#define ALCHEMY_CURSOR_SUB (0xC4 - 5) // (the cursor's distance from the screen's sub-state byte)
#define MR_MENU_SUB 3
#define MR_HELP_SELL 0x3D         // the menu's help lines by row: Send 0x3C, Sell 0x3D, and 0x3E for a third
#define MR_HELP_THIRD 0x3E
static const char HELP_MELD_RESULT[] = "Send every melded Talisman to the\r\nItem Box and exit.";
// View Talismans' R+ZL (below): the sale question shows the leftovers' count and total instead
static bool g_sell_asking;
static char g_sell_text[96];
static void help_hook(u32 flags, u32 a, u32 table, u32 message, u32 x, u32 format, u32 arg0, u32 arg1) {
    if (g_sell_asking && table == 0x16 && (message == 0x7C || message == 0x90)) {
        ORIGINAL(HelpSet, g_help_hook)(flags, a, table, message, x, format, arg0, arg1);
        help_show(g_sell_text);
        return;
    }
    // Meld Result's menu (below): ours on row 1, Sell's line on row 2 (the game asks for row 2's); only while the menu
    // is up with three rows (not Sell's question, whose text goes there too: its A passes the game's two rows)
    if (g_mr_cursor && g_mr_cursor[CURSOR_ROWS] == 3 && g_mr_cursor[-ALCHEMY_CURSOR_SUB] == MR_MENU_SUB) {
        u32 row = g_mr_cursor[CURSOR_ROW];
        if (row == 1) {
            help_show(HELP_MELD_RESULT);
            return;
        }
        if (row == 2 && message == MR_HELP_THIRD) message = MR_HELP_SELL;
    }
    u32 args[8] = {flags, a, table, message, x, format, arg0, arg1};
    for (u32 i = 0; i < 8; i++) g_help_last[i] = args[i];
    g_help_asked = true;
    if (g_on_send_all && g_help_text) {
        help_show(g_help_text);
        return;
    }
    ORIGINAL(HelpSet, g_help_hook)(flags, a, table, message, x, format, arg0, arg1);
}

static void help_again(u32 row) {
    if (row >= 3 || !g_help_known[row]) return;
    const u32 *h = g_help_rows[row];
    ORIGINAL(HelpSet, g_help_hook)(h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
}

// The cursor's moves after the game's frame, with the extra row between Send (1) and the third (2); screen tells the
// screens apart (their help lines are recorded per screen), help is Send all's line there
static void track_cursor(u8 *cursor, u32 before, u32 was, u32 screen, const char *help) {
    u32 after = cursor[CURSOR_ROW];
    bool on = g_on_send_all;
    if (after != before || (was & (PAD_A | PAD_B))) g_exit_uio = 0, g_trade_exit = false; // (the menu is in use again)
    if (screen != g_help_screen) {
        g_help_screen = screen;
        for (u32 i = 0; i < 3; i++) g_help_known[i] = false;
    }
    g_help_text = help;
    if (g_help_asked && after != before && after < 3) { // (the game showed the line of the row it moved to)
        for (u32 i = 0; i < 8; i++) g_help_rows[after][i] = g_help_last[i];
        g_help_known[after] = true;
    }
    g_help_asked = false;
    if (was & PAD_B) {
        g_on_send_all = false; // (the game moved its cursor to the third row)
    } else if (g_on_send_all) {
        if (before == 1 && after == 0) { // up: back to Send
            cursor[CURSOR_ROW] = 1;
            g_on_send_all = false;
            help_again(1);
        } else if (after != before) {    // down: the game is on its third row already
            g_on_send_all = false;
        }
    } else if (before == 1 && after == 2) { // down from Send: stop on Send all
        cursor[CURSOR_ROW] = 1;
        g_on_send_all = true;
    } else if (before == 2 && after == 1) { // up from the third row: stop on Send all
        g_on_send_all = true;
    }
    if (g_on_send_all && !on && help) help_show(help);
}

// Meownster Hunters: the menu is known before the setup draws its rows
static u32 monnyan_init_hook(u32 list, u32 uio, u32 a2) {
    if (uio) g_monnyan_uio = uio;
    return ORIGINAL(Method4, g_monnyan_init_hook)(list, uio, a2, 0);
}

static bool monnyan_empty(u32 list) {
    const u32 *l = *(const u32 **)(list + MN_LIST);
    const u16 *slots = l ? (const u16 *)l[0] : 0;
    for (u32 i = 0; slots && i < l[1]; i++)
        if (slots[2 * i]) return false;
    return true;
}

// Sends every slot to the Item Box as Send to the Item Box does. Returns the slots sent; stops when the box is full.
static u32 monnyan_send_all(u32 list, bool *full) {
    u32 *l = *(u32 **)(list + MN_LIST), box = GLOBAL(GOT_BOX), sent = 0;
    u16 *slots = l ? (u16 *)l[0] : 0;
    *full = false;
    g_one_sound_each = true, g_sounds = 0;
    for (u32 i = 0; slots && i < l[1]; i++) {
        u16 *slot = slots + 2 * i;
        if (!slot[0]) continue;
        if (!FN(Is2, BOX_HAS_ROOM)(box, slot[0])) { *full = true; break; }
        u32 info[4] = {0};
        FN(Is, MONNYAN_INFO)((u32)info);
        info[1] = slot[0] | (u32)slot[1] << 16;
        s32 left = 0;
        FN(BoxAdd, BOX_ADD)(box, slot[0], (s16)slot[1], 0, &left);
        if (left < 1) slot[0] = 0, left = 0;
        slot[1] = (u16)left;
        FN(Event, MONNYAN_EVENT)(list, MN_EV, MN_EV_SENT, info);
        sent++;
    }
    g_one_sound_each = false;
    return sent;
}

static u32 monnyan_hook(u32 list) {
    u32 uio = *(u32 *)(list + MN_UIO);
    u8 *cursor = (u8 *)(list + MN_CURSOR);
    bool menu = g_send_all && uio && uio == g_monnyan_uio && uio == g_menu_uio &&
                *(u32 *)(list + MN_STATE) == MN_MENU && cursor[CURSOR_ROWS] == 3;
    if (!menu) {
        g_on_send_all = false;
        return ORIGINAL(Is, g_monnyan_hook)(list);
    }
    u32 *pressed = (u32 *)(GLOBAL(GOT_PAD) + PAD_PRESSED), was = *pressed;
    bool leaving = false;
    if (g_on_send_all && (was & PAD_A)) {
        bool full;
        u32 sent = monnyan_send_all(list, &full);
        leaving = monnyan_empty(list);
        forge_log(LOG_INFO, "field-qol: sent %u Meownster Hunters slots to the Item Box%s", sent,
                  leaving ? ", leaving" : ", the box is full");
        if (leaving) { // (as the game does after the last slot: Done, which the A then closes)
            FN(Event, MONNYAN_EVENT)(list, MN_EV, MN_EV_EMPTY, 0);
            FN(MenuSet, MENU_SET)(uio, MN_ROWS_DONE, 1, 6);
            cursor[CURSOR_ROW] = 2, *(u16 *)(cursor + CURSOR_PAGE) = 0;
            *(u32 *)(list + MN_STATE) = MN_DONE;
            g_on_send_all = false, g_exit_uio = uio;
        } else {
            *pressed = was & ~PAD_A; // (the game would open Send's grid)
        }
    }
    u32 before = cursor[CURSOR_ROW];
    u32 r = ORIGINAL(Is, g_monnyan_hook)(list);
    *pressed = was;
    if (leaving || *(u32 *)(list + MN_STATE) != MN_MENU) {
        g_on_send_all = false;
        return r;
    }
    track_cursor(cursor, before, was, uio, HELP_SEND_ALL);
    return r;
}

// Hunters for Hire: Send to the Item Box as the game's (an item with data, BOX_HAS_ROOM, BOX_ADD), one sound for all.
// Its menu closes the screen on A when the list is empty, from any row: Send all leaves it the A then.
static bool gh_empty(u32 ui) {
    const u32 *l = (const u32 *)(ui + GH_LIST);
    const u16 *slots = (const u16 *)l[0];
    for (u32 i = 0; slots && i < l[1]; i++)
        if (slots[2 * i]) return false;
    return true;
}

static u32 gh_send_all(u32 ui, bool *full) {
    u32 *l = (u32 *)(ui + GH_LIST), box = GLOBAL(GOT_BOX), items = GLOBAL(GOT_ITEMS), sent = 0;
    u16 *slots = (u16 *)l[0];
    *full = false;
    for (u32 i = 0; slots && i < l[1]; i++) {
        u16 *slot = slots + 2 * i;
        if (!slot[0] || !FN(Data, ITEM_DATA)(items, slot[0])) continue;
        if (!FN(Is2, BOX_HAS_ROOM)(box, slot[0])) { *full = true; break; }
        s32 left = 0;
        FN(BoxAdd, BOX_ADD)(box, slot[0], (s16)slot[1], 0, &left);
        if (left < 1) slot[0] = 0, left = 0;
        slot[1] = (u16)left;
        sent++;
    }
    if (sent || *full) FN(Sound, UI_SOUND)(0, *full ? SOUND_FULL : SOUND_SENT);
    *(u8 *)(ui + GH_REDRAW) = 1;
    return sent;
}

static u32 gh_hook(u32 ui) {
    u32 uio = FN(Component, UI_COMPONENT)(ui, GH_UIO_INDEX);
    if (uio) g_gh_uio = uio; // (before the screen's first frame draws its rows)
    u8 *cursor = (u8 *)(ui + GH_CURSOR);
    bool menu = g_send_all && uio && uio == g_menu_uio && *(u32 *)(ui + GH_STATE) == GH_MENU &&
                cursor[CURSOR_ROWS] == 3;
    if (!menu) {
        g_on_send_all = false;
        return ORIGINAL(Is, g_gh_hook)(ui);
    }
    u32 *pressed = (u32 *)(GLOBAL(GOT_PAD) + PAD_PRESSED), was = *pressed;
    bool leaving = false;
    if (g_on_send_all && (was & PAD_A)) {
        bool full;
        u32 sent = gh_send_all(ui, &full);
        leaving = gh_empty(ui);
        forge_log(LOG_INFO, "field-qol: sent %u Hunters for Hire slots to the Item Box%s", sent,
                  leaving ? ", leaving" : ", the box is full");
        if (leaving) g_on_send_all = false, g_exit_uio = uio; // (the game closes the screen on this A)
        else *pressed = was & ~PAD_A;
    }
    u32 before = cursor[CURSOR_ROW];
    u32 r = ORIGINAL(Is, g_gh_hook)(ui);
    *pressed = was;
    if (leaving || *(u32 *)(ui + GH_STATE) != GH_MENU) {
        g_on_send_all = false;
        return r;
    }
    track_cursor(cursor, before, was, uio, HELP_SEND_ALL);
    return r;
}

// The trade cart's Claim Items (Neko): every cell of the three carts' inboxes to the Item Box as its Send does (room
// for the whole quantity, BOX_PUT, the cell emptied), then the inbox saved and drawn once, with one sound.
static bool g_trade_row; // (its menu has the fourth row: the former layout tool's, or runtime rows)

static bool trade_empty(void) {
    u32 items = GLOBAL(GOT_ITEMS);
    for (u32 cart = 0; cart < TRADE_CARTS; cart++) {
        u32 c = FN(Is2, TRADE_CART)(items, cart);
        if (!c) continue;
        const u16 *cell = (const u16 *)(c + CART_CELLS);
        for (u32 i = 0; i < CART_CELL_COUNT; i++)
            if (cell[2 * i]) return false;
    }
    return true;
}

static u32 trade_send_all(u32 ui, bool *full) {
    u32 items = GLOBAL(GOT_ITEMS), box = GLOBAL(GOT_BOX), sent = 0;
    *full = false;
    for (u32 cart = 0; cart < TRADE_CARTS; cart++) {
        u32 c = FN(Is2, TRADE_CART)(items, cart);
        if (!c) continue;
        u16 *cell = (u16 *)(c + CART_CELLS);
        for (u32 i = 0; i < CART_CELL_COUNT; i++) {
            u16 item = cell[2 * i];
            s16 qty = (s16)cell[2 * i + 1];
            if (!item) continue;
            if ((s32)FN(Is2, BOX_HAS_ROOM)(box, item) < qty) { *full = true; continue; }
            FN(Method4, BOX_PUT)(box, item, (u32)(s32)qty, 0);
            cell[2 * i] = cell[2 * i + 1] = 0;
            sent++;
        }
    }
    if (sent) {
        FN(Method, TRADE_CHANGED)(items);
        FN(Is2, TRADE_REDRAW)(ui, 1);
    }
    if (sent || *full) FN(Sound, UI_SOUND)(0, *full ? SOUND_FULL : SOUND_SENT);
    return sent;
}

static u32 trade_hook(u32 ui) {
    u8 *cursor = (u8 *)(ui + TRADE_CURSOR);
    bool menu = g_send_all && g_trade_row && *(u8 *)(ui + TRADE_SUBSTATE) == 0 && cursor[CURSOR_ROWS] == 3;
    if (!menu) {
        g_on_send_all = false;
        return ORIGINAL(Is, g_trade_hook)(ui);
    }
    u32 *pad = (u32 *)(ui + TRADE_PAD), was = *pad;
    bool leaving = false;
    if (g_on_send_all && (was & PAD_A) && !trade_empty()) { // (with nothing to claim, the A goes to Send, greyed)
        bool full;
        u32 sent = trade_send_all(ui, &full);
        leaving = trade_empty();
        forge_log(LOG_INFO, "field-qol: sent %u trade cart slots to the Item Box%s", sent,
                  leaving ? ", leaving" : ", the box is full");
        if (leaving) {
            *pad = PAD_B; // (the game's B closes Claim Items)
            g_on_send_all = false, g_trade_exit = true;
        } else {
            *pad = was & ~PAD_A; // (the game would open Send's grid)
        }
    }
    u32 before = cursor[CURSOR_ROW];
    u32 r = ORIGINAL(Is, g_trade_hook)(ui);
    *pad = was;
    if (leaving || *(u8 *)(ui + TRADE_SUBSTATE) != 0) {
        g_on_send_all = false;
        return r;
    }
    track_cursor(cursor, before, was, ui, HELP_TRADE);
    return r;
}

// Its menu: Send all after Send (on the third row), Sell Items & Exit on the fourth, the highlight on the row the
// player sees. Turning the option off draws the game's rows again.
static u32 g_tm_gui, g_tm_row4 = TM_ROW_FOURTH, g_tm_line; // runtime rows for the trade's layout

// The trade's layout with the fourth row: the former layout tool's, or runtime copies of row 3 and the separator
static bool trade_row(u32 gui) {
    if (!gui) return false;
    u32 n = *(u32 *)(gui + GUI_INSTANCES);
    if (n > TM_ROW_FOURTH) return g_tm_row4 = TM_ROW_FOURTH, g_tm_line = 0, g_tm_gui = 0, true;
    if (n != TM_INSTANCES) return false;
    if (gui != g_tm_gui || !*(u32 *)(gui + GUI_EXTRA_COUNT)) {
        u32 row = clone_inst(gui, TM_ROW_THIRD, TM_ROW_PARENT, 140.0f);
        if (!row) return false;
        g_tm_line = clone_inst(gui, TM_LINE, TM_LINE_PARENT, ROW_STEP_Y);
        g_tm_gui = gui, g_tm_row4 = row;
    }
    return true;
}

// Runtime rows: the window one row taller and Cart Info one row lower while ours, as before otherwise
static void trade_layout(u32 uio, u32 gui, bool ours) {
    if (!g_tm_line) return;
    FN(SetInt3, INST_VISIBLE)(uio, g_tm_line, ours);
    if (ours) {
        nudge(gui, TM_WINDOW, true, 7.45f, 9.15f), nudge(gui, TM_WINDOW, true, 7.6f, 9.3f);
        nudge(gui, TM_CART_INFO, false, 275.0f, 309.0f);
    } else {
        nudge(gui, TM_WINDOW, true, 9.15f, 7.45f), nudge(gui, TM_WINDOW, true, 9.3f, 7.6f);
        nudge(gui, TM_CART_INFO, false, 309.0f, 275.0f);
    }
}

static void trade_menu_hook(u32 uio) {
    static bool was_ours;
    u32 gui = *(u32 *)(uio + UIO_GUI);
    g_trade_row = trade_row(gui);
    bool ours = g_send_all && g_trade_row;
    if (ours != was_ours) *(u32 *)(uio + TM_FLAGS) |= 1 | 4 | 8;
    u32 flags = *(u32 *)(uio + TM_FLAGS);
    ORIGINAL(Method, g_trade_menu_hook)(uio);
    if (!g_trade_row) return;
    trade_layout(uio, gui, ours);
    if (!ours) {
        if (was_ours) FN(SetInt3, INST_VISIBLE)(uio, g_tm_row4, 0);
        was_ours = false;
        return;
    }
    was_ours = true;
    bool nothing = *(u32 *)(uio + TM_MODE) == 1;
    if (flags & 4) {
        FN(SetText, UIO_SET_TEXT)(uio, TM_ROW_THIRD, ROW_OBJECT, SEND_ALL_TEXT);
        FN(SetInt, UIO_SET_LOOK)(uio, TM_ROW_THIRD, ROW_OBJECT,
                                 FN(Is2, LOOK_OF)(GLOBAL(GOT_LOOKS), nothing ? LOOK_GREYED : 0));
        FN(MsgSet, MSG_SET)(uio, g_tm_row4, ROW_OBJECT, 0, 1, nothing ? TM_MSG_EXIT : TM_MSG_SELL);
        FN(SetInt, UIO_SET_LOOK)(uio, g_tm_row4, ROW_OBJECT, FN(Is2, LOOK_OF)(GLOBAL(GOT_LOOKS), 0));
    }
    u32 cursor = *(u32 *)(uio + TM_CURSOR);
    if (flags & 8) // (the fourth row as the game's third: shown with three entries)
        FN(SetInt3, INST_VISIBLE)(uio, g_tm_row4, cursor && *(u16 *)(cursor + CURSOR_COUNT) > 2);
    if (cursor) {
        float spacing = FN(GetY, GUI_GET_Y)(gui, TM_SPACING);
        u32 row = *(u8 *)(cursor + CURSOR_ROW); // (Send is its second row: Send all and Sell one lower)
        if (g_trade_exit && !row) g_trade_exit = false; // (opened again)
        u32 shown = g_on_send_all || g_trade_exit ? 2 : row >= 2 ? row + 1 : row;
        FN(SetY, GUI_SET_Y)(gui, TM_HIGHLIGHT, spacing * (float)shown);
    }
}

// The Melding Pot's Meld Result (Claim Talismans, uUIAlchemy state 3): its menu, Send to Item Box and Sell Items &
// Exit, gets "Send all to Item Box & Exit" between them. The game's cursor is given a third row while the menu is up
// (so the mouse and the moves see three: Send 0, ours 1, Sell 2), and its A, B and help line are mapped back to the
// game's two rows. The window (cUIOAlchemyReceive, component
// 8) shows the three rows: Send, ours on the game's second row (instance 31), and a runtime copy of it one row lower
// with Sell's text. Choosing ours opens Send's grid (sub-state 4) and presses A on each talisman in it, one a frame,
// as the player would. With the grid empty the game shows its Done menu, where the plugin chooses Done, which closes
// Meld Result. If the Item Box is full, it goes back to the menu.
#define CLAIM_UPDATE 0x6DF3E8     // uUIAlchemy: Claim Talismans for a frame (ui)
#define ALCHEMY_STATE 4           // u8: 3 claiming; +5 its sub-state: 3 the menu, 4 Send's grid
#define ALCHEMY_CLAIM 3
#define CLAIM_MENU 3
#define CLAIM_GRID 4
#define CLAIM_DONE 8              // (everything claimed: Send greyed and Done, its second row, which leaves)
#define PAD_REPEAT 0x6C34         // the buttons pressed or repeating this frame (the cursor moves by them)
#define PAD_UP 0x10
#define PAD_DOWN 0x40
#define GRID_COL 0x120            // u8 col, row, cols, rows; u16 page, pages at +0x124
#define GRID_PAGE 0x124
#define GRID_PAGES 0x126
#define GRID_CELLS 0x440          // 0x3C a cell: u32 order, entry, state (2: a talisman), item
#define GRID_CELL 0x3C
#define GRID_FULL 2
#define MAX_GRID_CELLS 280
#define RECEIVE_UIO_INDEX 8       // cUIOAlchemyReceive: the Meld Result window
#define RECEIVE_FLAGS 0x4C        // (|= 1: the highlight drawn again, at the row +0x50)
#define RECEIVE_ROW 0x50
#define RECEIVE_SET_TEXT 0x56EA08 // cUIO: an object's text (uio, instance, object, text), as the window sets its rows
#define MR_INSTANCES 59           // the Melding Pot's layout: Anm_Window_main02, Null_text01 and its second row
#define MR_WINDOW 25
#define MR_ROW_PARENT 29
#define MR_ROW_SECOND 31
#define MR_ITEMBOX 32             // Null_sub_itembox (Free space), under the window
#define MR_ROW_Y 72.0f
#define MR_ROW_STEP 36.0f
#define MR_WINDOW_SCL 6.0f        // the window's y scale for two rows, and one row more
#define MR_ROW_SCL 1.7f
#define MR_ITEMBOX_Y 202.0f
#define MR_GROW 8                 // frames the window takes to grow a row
#define CLAIM_LAST 7              // (sub-states 3-7: the menu, Send's grid, Sell's question and sale)
#define MR_MSG_TABLE 1            // Sell Items & Exit: message 0x1C of table 1 (the message manager, GOT_MESSAGES)
#define MR_MSG_SELL 0x1C
#define GOT_MESSAGES 0x1838BD4
#define MSG_TEXT 0x55BC4C         // a message's text (manager, table, number)
#define ALCHEMY_CURSOR 0xC4       // uUIAlchemy: its menus' cursor
static const char SEND_ALL_MR_TEXT[] = "Send all to Item Box & Exit"; // (beside the game's "Send to Item Box")
typedef const char *(*MsgLookup)(u32 manager, u32 table, u32 number);
static ForgeHook g_claim_hook;
#define RECEIVE_DRAW 0x564F5C     // cUIOAlchemyReceive: draw what changed (uio): +0x4C flags 1 the highlight, 4 texts
static ForgeHook g_receive_draw_hook;
static bool g_mr_sending, g_mr_shown, g_mr_active;
static u32 g_frame; // (frames since the start, counted in forge_onUpdate)
static u32 g_mr_frame; // (the frame Meld Result last ran: the window's draw only does ours right after)
static u32 g_mr_grow, g_mr_row_shown; // (the highlight's row: kept while Sell's question uses the cursor)
static u32 g_mr_gui, g_mr_row3, g_mr_last = 0xFFFFFFFF;

static u32 mr_next_cell(u32 ui) {
    u8 *g = (u8 *)(ui + GRID_COL);
    u32 per = (u32)g[2] * g[3], pages = *(u16 *)(ui + GRID_PAGES), n = per * (pages ? pages : 1);
    if (n > MAX_GRID_CELLS) n = MAX_GRID_CELLS;
    for (u32 i = 0; i < n; i++)
        if (*(u32 *)(ui + GRID_CELLS + i * GRID_CELL + 8) == GRID_FULL) return i;
    return 0xFFFFFFFF;
}

// The window's three rows (ours shown) or the game's two
static void mr_layout(u32 ui, bool ours) {
    u32 uio = FN(Component, UI_COMPONENT)(ui, RECEIVE_UIO_INDEX);
    u32 gui = uio ? *(u32 *)(uio + UIO_GUI) : 0;
    if (!gui || *(u32 *)(gui + GUI_INSTANCES) != MR_INSTANCES) return;
    if (ours && (gui != g_mr_gui || !*(u32 *)(gui + GUI_EXTRA_COUNT))) {
        g_mr_row3 = clone_inst(gui, MR_ROW_SECOND, MR_ROW_PARENT, MR_ROW_Y);
        g_mr_gui = gui, g_mr_shown = false;
    }
    if (!g_mr_row3 || gui != g_mr_gui) return;
    if (ours) { // (the window grows a row over MR_GROW frames, eased, the box under it moving along)
        if (!g_mr_shown) *(u32 *)(uio + RECEIVE_FLAGS) |= 1 | 4, g_mr_grow = 0; // (our texts and highlight, below)
        if (g_mr_grow < MR_GROW) g_mr_grow++;
        float t = (float)g_mr_grow / MR_GROW, e = 1.0f - (1.0f - t) * (1.0f - t);
        FN(SetF, INST_SET_SCL_Y)(inst_obj(gui, MR_WINDOW), MR_WINDOW_SCL + MR_ROW_SCL * e);
        FN(SetY, GUI_SET_Y)(gui, MR_ITEMBOX, MR_ITEMBOX_Y + MR_ROW_STEP * e);
        FN(SetInt3, INST_VISIBLE)(uio, g_mr_row3, g_mr_grow * 2 >= MR_GROW); // (once the window holds it)
        g_mr_shown = true;
    } else if (g_mr_shown) {
        FN(SetF, INST_SET_SCL_Y)(inst_obj(gui, MR_WINDOW), MR_WINDOW_SCL);
        FN(SetY, GUI_SET_Y)(gui, MR_ITEMBOX, MR_ITEMBOX_Y);
        FN(SetInt3, INST_VISIBLE)(uio, g_mr_row3, 0);
        // (no redraw asked for here: the window may not be drawn again until it's freed, and the game asks for its
        // texts itself whenever it shows the window again)
        g_mr_shown = false;
    }
}

// The window's draw: with ours, the highlight on the row the player sees and the texts of rows 2 and 3
static void receive_draw_hook(u32 uio) {
    if (!g_mr_active || !g_mr_shown || g_frame - g_mr_frame > 2 || *(u32 *)(uio + UIO_GUI) != g_mr_gui) {
        ORIGINAL(Method, g_receive_draw_hook)(uio);
        return;
    }
    u32 shown = g_mr_row_shown;
    if (*(u32 *)(uio + RECEIVE_ROW) != shown) *(u32 *)(uio + RECEIVE_ROW) = shown, *(u32 *)(uio + RECEIVE_FLAGS) |= 1;
    u32 flags = *(u32 *)(uio + RECEIVE_FLAGS);
    ORIGINAL(Method, g_receive_draw_hook)(uio);
    if (flags & 7) {
        FN(SetText, RECEIVE_SET_TEXT)(uio, MR_ROW_SECOND, ROW_OBJECT, SEND_ALL_MR_TEXT);
        const char *sell = FN(MsgLookup, MSG_TEXT)(GLOBAL(GOT_MESSAGES), MR_MSG_TABLE, MR_MSG_SELL);
        if (sell && g_mr_row3) FN(SetText, RECEIVE_SET_TEXT)(uio, g_mr_row3, ROW_OBJECT, sell);
    }
}

static u32 claim_hook(u32 ui) {
    g_mr_frame = g_frame;
    u8 state = *(u8 *)(ui + ALCHEMY_STATE), sub = *(u8 *)(ui + ALCHEMY_STATE + 1);
    bool claiming = state == ALCHEMY_CLAIM, menu = claiming && sub == CLAIM_MENU;
    u8 *cursor = (u8 *)(ui + ALCHEMY_CURSOR);
    u32 pad = GLOBAL(GOT_PAD), *pressed = (u32 *)(pad + PAD_PRESSED), *repeat = (u32 *)(pad + PAD_REPEAT);
    u32 was = *pressed, was_rep = *repeat;
    if (g_send_all && claiming && sub == CLAIM_DONE && g_mr_sending) { // (all sent: Done)
        cursor[CURSOR_ROW] = 1;
        *pressed = *repeat = PAD_A;
        g_mr_sending = false, g_one_sound_each = false;
        forge_log(LOG_INFO, "field-qol: Meld Result: every Talisman sent, leaving");
        u32 r = ORIGINAL(Is, g_claim_hook)(ui);
        *pressed = was, *repeat = was_rep;
        return r;
    }
    if (!g_send_all || !claiming || (sub != CLAIM_MENU && sub != CLAIM_GRID)) {
        if (g_mr_sending) g_one_sound_each = false;
        g_mr_sending = false, g_mr_last = 0xFFFFFFFF;
        u32 r = ORIGINAL(Is, g_claim_hook)(ui);
        u8 now = *(u8 *)(ui + ALCHEMY_STATE + 1); // (Sell's question and sale keep the rows, as the menu shows)
        g_mr_active = g_send_all && *(u8 *)(ui + ALCHEMY_STATE) == ALCHEMY_CLAIM && now >= CLAIM_MENU && now <= CLAIM_LAST;
        mr_layout(ui, g_mr_active);
        return r;
    }
    u32 before = cursor[CURSOR_ROW];
    if (menu && !g_mr_sending && (cursor[CURSOR_ROWS] == 2 || cursor[CURSOR_ROWS] == 3)) {
        // the cursor given a third row (the mouse and the moves see three): Send 0, ours 1, Sell 2 (the game's 1)
        if (cursor[CURSOR_ROWS] == 2) {
            if (cursor[CURSOR_ROW] == 1) cursor[CURSOR_ROW] = 2;
            cursor[CURSOR_ROWS] = 3, *(u16 *)(cursor + CURSOR_COUNT) = 3;
        }
        before = cursor[CURSOR_ROW];
        if ((was & PAD_A) && before == 1) { // ours: the game's A on Send opens the grid, then the presses
            g_mr_sending = true, g_mr_last = 0xFFFFFFFF;
            g_one_sound_each = true, g_sounds = 0;
            cursor[CURSOR_ROW] = 0, cursor[CURSOR_ROWS] = 2, *(u16 *)(cursor + CURSOR_COUNT) = 2;
            forge_log(LOG_INFO, "field-qol: Meld Result: sending every Talisman to the Item Box");
        } else if ((was & PAD_A) && before == 2) { // Sell: the game's row 1
            cursor[CURSOR_ROW] = 1, cursor[CURSOR_ROWS] = 2, *(u16 *)(cursor + CURSOR_COUNT) = 2;
        }
        g_mr_cursor = cursor;
    }
    if (sub == CLAIM_GRID && g_mr_sending) {
        u32 next = mr_next_cell(ui);
        if (next == 0xFFFFFFFF) {
            g_mr_sending = false, g_one_sound_each = false; // (the game shows its Done menu when it's empty)
        } else if (next == g_mr_last) { // (not sent: the Item Box is full) back to the menu
            g_mr_sending = false, g_one_sound_each = false;
            *pressed = *repeat = PAD_B;
            forge_log(LOG_INFO, "field-qol: Meld Result: the Item Box is full, back to the menu");
        } else {
            u8 *g = (u8 *)(ui + GRID_COL);
            u32 per = (u32)g[2] * g[3], in = next % per;
            *(u16 *)(ui + GRID_PAGE) = (u16)(next / per), g[1] = (u8)(in / g[2]), g[0] = (u8)(in % g[2]);
            *pressed = *repeat = PAD_A;
            g_mr_last = next;
        }
    }
    u32 r = ORIGINAL(Is, g_claim_hook)(ui);
    *pressed = was, *repeat = was_rep;
    g_mr_cursor = 0;
    u8 now = *(u8 *)(ui + ALCHEMY_STATE + 1);
    // the game's B on Send goes to its last row, ours here: to Sell instead
    if (now == CLAIM_MENU && (was & PAD_B) && before == 0 && cursor[CURSOR_ROW] == 1 && cursor[CURSOR_ROWS] == 3)
        cursor[CURSOR_ROW] = 2;
    g_mr_active = *(u8 *)(ui + ALCHEMY_STATE) == ALCHEMY_CLAIM && ((now >= CLAIM_MENU && now <= CLAIM_LAST) || g_mr_sending);
    if (now == CLAIM_MENU) g_mr_row_shown = cursor[CURSOR_ROWS] == 3 ? cursor[CURSOR_ROW] : cursor[CURSOR_ROW] ? 2 : 0;
    else if (g_mr_sending) g_mr_row_shown = 1;
    mr_layout(ui, g_mr_active);
    return r;
}

// The rows: the added one after Send, on a three-row menu with Send second (the reward screen's sections, Meownster
// Hunters, Hunters for Hire) or the appraisal's two rows (Send to Boxes, Sell Items & Exit)
static void rows_hook(u32 uio, u32 list, u32 flag) {
    ORIGINAL(Rows, g_rows_hook)(uio, list, flag);
    if (!has_fourth_row(uio)) return;
    u32 entries = *(u32 *)list, count = *(u32 *)(list + 4), mode = *(u32 *)(uio + UIO_MODE);
    bool known = uio == g_result_uio || uio == g_monnyan_uio || uio == g_gh_uio;
    bool three = count == 3 && mode < 32 && (RESULT_MODES >> mode & 1) && entries &&
                 *(u16 *)(entries + 12 + 6) == MSG_RESULT && *(u16 *)(entries + 12 + 8) == MSG_SEND;
    bool two = count == 2 && mode == MODE_APPRAISAL && uio == g_result_uio && entries;
    bool ours = g_send_all && flag && known && (three || two);
    if (!ours) {
        if ((uio == g_monnyan_uio || uio == g_gh_uio) && g_send_all && flag && entries && count == 3) // (why it stays)
            forge_log(LOG_INFO, "field-qol: %s menu kept (mode %u, second %u:%u)",
                      uio == g_gh_uio ? "Hunters for Hire" : "Meownster Hunters", mode,
                      *(u16 *)(entries + 12 + 6), *(u16 *)(entries + 12 + 8));
        if (g_menu_uio == uio) {
            g_menu_uio = 0;
            if (count < 3) FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_THIRD, ROW_OBJECT, 0); // (the appraisal's third row)
        }
        FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_INSTANCES[3], ROW_OBJECT, 0);
        if (g_rt) {
            if (g_rt_line) FN(SetInt3, INST_VISIBLE)(uio, g_rt_line, 0);
            if (g_rt_row3) FN(SetInt, UIO_SET_VISIBLE)(uio, g_rt_row3, ROW_OBJECT, 0);
        }
        ROW_INSTANCES[2] = ROW_THIRD;
        return;
    }
    g_menu_uio = uio, g_menu_rows = count, g_send_row = two ? 0 : 1;
    // the appraisal's state hides the layout's third row: with runtime rows, a runtime one stands in for it
    ROW_INSTANCES[2] = g_rt && two && g_rt_row3 ? g_rt_row3 : ROW_THIRD;
    if (g_rt && g_rt_row3 && !two) FN(SetInt, UIO_SET_VISIBLE)(uio, g_rt_row3, ROW_OBJECT, 0);
    if (three) g_cursor_extended = false;
    // the rows after Send move one down, "Send all" goes after Send
    for (u32 k = count - 1; k > g_send_row; k--) {
        FN(SetText, UIO_SET_TEXT)(uio, ROW_INSTANCES[k + 1], ROW_OBJECT, FN(EntryText, ENTRY_TEXT)(entries + 12 * k));
        FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_INSTANCES[k + 1], ROW_OBJECT, 1);
    }
    FN(SetText, UIO_SET_TEXT)(uio, ROW_INSTANCES[g_send_row + 1], ROW_OBJECT, two ? SEND_ALL_BOXES_TEXT : SEND_ALL_TEXT);
    FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_INSTANCES[g_send_row + 1], ROW_OBJECT, 1);
    if (count == 2) FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_INSTANCES[3], ROW_OBJECT, 0);
    if (g_rt && g_rt_line) FN(SetInt3, INST_VISIBLE)(uio, g_rt_line, 1);
}

// The rows' looks (enabled, highlighted) for the rows the player sees
static void looks_hook(u32 uio, u32 enabled, u32 row) {
    ORIGINAL(Looks, g_looks_hook)(uio, enabled, row);
    if (uio != g_menu_uio || *(u32 *)(enabled + 4) < g_menu_rows) return;
    const u8 *on = *(const u8 **)enabled;
    u32 shown = uio == g_exit_uio ? g_send_row + 1 : shown_row(row);
    Look look = FN(Look, ROW_LOOK);
    SetInt set = FN(SetInt, UIO_SET_LOOK);
    for (u32 k = 0; k < g_menu_rows; k++) {
        u32 v = k <= g_send_row ? k : k + 1;
        set(uio, ROW_INSTANCES[v], ROW_OBJECT, look(on[k], shown == v, 0, 0));
    }
    set(uio, ROW_INSTANCES[g_send_row + 1], ROW_OBJECT, look(on[g_send_row], shown == g_send_row + 1, 0, 0));
}

// The highlight bar, on the row the player sees
static void reward_update_hook(u32 uio) {
    ORIGINAL(Method, g_reward_update_hook)(uio);
    u32 gui = *(u32 *)(uio + UIO_GUI), cursor = *(u32 *)(uio + UIO_CURSOR);
    if (g_rt && gui && gui == g_rt_gui)
        runtime_layout(gui, uio != g_menu_uio ? RT_NONE : g_menu_rows == 3 ? RT_FOUR : RT_APPRAISAL);
    if (uio != g_menu_uio) return;
    if (!gui || !cursor) return;
    float spacing = FN(GetY, GUI_GET_Y)(gui, INST_SPACING);
    if (uio == g_exit_uio && !*(u8 *)(cursor + CURSOR_ROW)) g_exit_uio = 0; // (opened again)
    u32 shown = uio == g_exit_uio ? g_send_row + 1 : shown_row(*(u8 *)(cursor + CURSOR_ROW));
    FN(SetY, GUI_SET_Y)(gui, INST_HIGHLIGHT, spacing * (float)shown);
    FN(Sync, CURSOR_SYNC)(cursor, uio, INST_HIGHLIGHT);
}

// ================================================================== Melding: Y picks leftover talismans
//
// The melding screen (uUIAlchemy, its main state MELD_UPDATE) lists the talismans for the chosen melding a page of 9
// rows at a time (MELD_FILL fills a page from the equipment box and flags each row); A on a usable row chooses it (box
// index into MELD_CHOSEN, MELD_COUNT up) and the third opens "Meld?". With Y the plugin collects every usable row of
// every page, keeps those another talisman beats or equals (dominates: at least the points in every skill either has,
// negative ones too, and at least the slots; of identical ones the first is kept), never the worn one or one in a saved
// equipment set, and chooses the least valuable through the game's own A, one a frame.
#define MELD_UPDATE 0x6DDF38      // uUIAlchemy: the melding screen's main state, every frame (ui)
#define MELD_FILL 0x6E026C        // uUIAlchemy: fill a page of the talisman list (ui, page, reset the cursor)
#define MELD_DRAW_CHOSEN 0x6E03F8 // uUIAlchemy: draw the chosen talismans (ui)
#define MELD_DRAW_DETAIL 0x6E04C0 // uUIAlchemy: draw the talisman under the cursor (ui)
#define EQUIP_ENTRY 0x143894      // the equipment box: an entry (box, 0, index) -> entry, 0 if none
#define MELD_SUBSTATE 5           // (u8) 1: the talisman list, 2: "Meld?"
#define MELD_LIST 1
#define MELD_COUNT 0x54           // talismans chosen (of MELD_NEEDED)
#define MELD_NEEDED 3
#define MELD_CURSOR 0xC4          // the list's cursor (row +0xD, page +0x10)
#define MELD_TOTAL 0x44           // rows in the list, all pages
#define MELD_ROWS 0x174           // the page's rows: flags, +8 the box index (0xFFFF: none)
#define MELD_ROW_SIZE 0x18
#define MELD_PAGE_ROWS 9
#define MELD_REDRAW 0x16C         // (|= 0x80: the rows are drawn again)
#define ROW_USABLE_MASK 0x19      // (flags & it) == 1: can be chosen
#define ROW_CHOSEN 4
#define ROW_WORN 8
#define ROW_IN_SET 0x20           // in a saved equipment set
#define PAD_Y 0x8000
#define EQUIP_SLOTS 2000
#define KIND_CHARM 6              // entry +4 (u16) & 31
#define CHARM_TREE 0x10           // u8 x2, then s8 points x2 at +0x12, u8 slots at +0x14
#define MAX_CANDIDATES 400

typedef u32 (*EquipEntry)(u32 box, u32 zero, u32 index);
typedef u32 (*Fill)(u32 ui, u32 page, u32 reset);

typedef struct { u8 tree[2]; s8 pts[2]; u8 slots; } Charm;
typedef struct { u16 index, page; u8 row; } Pick;

static Charm g_charm[EQUIP_SLOTS];
static bool g_is_charm[EQUIP_SLOTS];
static u16 g_charms[EQUIP_SLOTS], g_charm_count; // (the box indices of the talismans)
static Pick g_picks[MELD_NEEDED];
static u32 g_picks_left, g_picks_next;
static bool g_meld_restore;
static u16 g_meld_page0;
static u8 g_meld_row0;

static s32 charm_points(const Charm *c, u8 tree) {
    s32 n = 0;
    for (u32 k = 0; k < 2; k++)
        if (c->tree[k] == tree && c->pts[k]) n += c->pts[k];
    return n;
}

// Whether talisman a (box index ai) beats or equals b (bi): identical ones are ordered by their box index
static bool dominates(const Charm *a, u32 ai, const Charm *b, u32 bi) {
    if (a->slots < b->slots) return false;
    bool same = a->slots == b->slots;
    const Charm *both[2] = {a, b};
    for (u32 w = 0; w < 2; w++)
        for (u32 k = 0; k < 2; k++) {
            u8 t = both[w]->tree[k];
            if (!t || !both[w]->pts[k]) continue;
            s32 pa = charm_points(a, t), pb = charm_points(b, t);
            if (pa < pb) return false;
            if (pa != pb) same = false;
        }
    return !same || ai < bi;
}

static u32 charm_value(const Charm *c) { // (the least valuable leftovers are chosen first)
    u32 v = 3u * c->slots;
    for (u32 k = 0; k < 2; k++)
        if (c->tree[k] && c->pts[k] > 0) v += (u32)c->pts[k];
    return v;
}

static void read_charms(void) {
    u32 box = GLOBAL(GOT_EQUIP_BOX);
    g_charm_count = 0;
    for (u32 i = 0; i < EQUIP_SLOTS; i++) {
        u32 e = box ? FN(EquipEntry, EQUIP_ENTRY)(box, 0, i) : 0;
        g_is_charm[i] = e && (*(u16 *)(e + 4) & 31) == KIND_CHARM;
        if (!g_is_charm[i]) continue;
        const u8 *d = (const u8 *)(e + CHARM_TREE);
        g_charm[i].tree[0] = d[0], g_charm[i].tree[1] = d[1];
        g_charm[i].pts[0] = (s8)d[2], g_charm[i].pts[1] = (s8)d[3];
        g_charm[i].slots = d[4];
        g_charms[g_charm_count++] = (u16)i;
    }
}

// Y: choose the leftovers this melding still needs; false when there are none
static bool meld_leftovers(u32 ui) {
    u32 count = *(u32 *)(ui + MELD_COUNT);
    if (count >= MELD_NEEDED) return false;
    u8 *cursor = (u8 *)(ui + MELD_CURSOR);
    u16 page0 = *(u16 *)(cursor + CURSOR_PAGE);
    u8 row0 = cursor[CURSOR_ROW];
    Fill fill = FN(Fill, MELD_FILL);

    // every row the game lets you choose, with where it is
    static Pick cand[MAX_CANDIDATES];
    u32 n = 0;
    fill(ui, 0, 0);
    u32 total = *(u32 *)(ui + MELD_TOTAL);
    for (u32 page = 0; page * MELD_PAGE_ROWS < total && n < MAX_CANDIDATES; page++) {
        if (page) fill(ui, page, 0);
        for (u32 r = 0; r < MELD_PAGE_ROWS && n < MAX_CANDIDATES; r++) {
            u32 row = ui + MELD_ROWS + r * MELD_ROW_SIZE, flags = *(u32 *)row, index = *(u32 *)(row + 8);
            if (index >= EQUIP_SLOTS || (flags & ROW_USABLE_MASK) != 1 || (flags & (ROW_CHOSEN | ROW_WORN | ROW_IN_SET)))
                continue;
            cand[n].index = (u16)index, cand[n].page = (u16)page, cand[n].row = (u8)r;
            n++;
        }
    }
    fill(ui, page0, 0);

    // the dominated ones (each checked once against every talisman), then the least valuable of them first
    read_charms();
    static bool left[MAX_CANDIDATES];
    u32 leftovers = 0;
    for (u32 c = 0; c < n; c++) {
        u32 bi = cand[c].index;
        left[c] = false;
        if (!g_is_charm[bi]) continue;
        for (u32 k = 0; k < g_charm_count && !left[c]; k++) {
            u32 j = g_charms[k];
            if (j != bi && dominates(&g_charm[j], j, &g_charm[bi], bi)) left[c] = true;
        }
        leftovers += left[c];
    }
    u32 need = MELD_NEEDED - count;
    g_picks_left = 0;
    while (g_picks_left < need) {
        u32 best = n, best_value = 0;
        for (u32 c = 0; c < n; c++) {
            if (!left[c]) continue;
            u32 v = charm_value(&g_charm[cand[c].index]);
            if (best == n || v < best_value) best = c, best_value = v;
        }
        if (best == n) break;
        left[best] = false;
        g_picks[g_picks_left++] = cand[best];
        u32 bi = cand[best].index, by = 0;
        for (u32 k = 0; k < g_charm_count; k++)
            if (g_charms[k] != bi && dominates(&g_charm[g_charms[k]], g_charms[k], &g_charm[bi], bi)) { by = g_charms[k]; break; }
        const Charm *a = &g_charm[bi], *b = &g_charm[by];
        forge_log(LOG_INFO, "field-qol: melding: box %u (skill %u %d, skill %u %d, %u slots) is beaten by box %u (skill %u %d, "
                  "skill %u %d, %u slots)", bi, a->tree[0], a->pts[0], a->tree[1], a->pts[1], a->slots, by, b->tree[0],
                  b->pts[0], b->tree[1], b->pts[1], b->slots);
    }
    forge_log(LOG_INFO, "field-qol: melding: %u of %u usable talismans are leftovers, choosing %u", leftovers, n,
              g_picks_left);
    g_picks_next = 0;
    g_meld_page0 = page0, g_meld_row0 = row0;
    return g_picks_left > 0;
}

// The button guide (bottom right; component 5 of the screen holding the help line): its first list, four entries
// {button icon, text}, shows "Help" here; the plugin adds "Select Leftovers" with Y's icon while the list is open
#define GUIDE_UIO_INDEX 5
#define GUIDE_ADD 0x644C34        // the guide: add an entry to the first free of four (guide, icon, text) -> 1: added
#define GUIDE_BUILD 0x6439EC      // the guide: lay it out again (guide, set, flags, 1)
#define GUIDE_SET 0x4C            // its set and flags (for GUIDE_BUILD)
#define GUIDE_FLAGS 0x50
#define GUIDE_FIRST 0x228         // 4 x {u32 icon (-1: none), const char *text}
#define ICON_Y 0                  // (icons: buttonName indices: Y 0, X 1, B 2, A 3, L 4, R 5)
static const char MELD_HINT[] = "Select Leftovers";
static bool g_meld_hint;
static u32 g_meld_frame;

typedef u32 (*Build)(u32 guide, u32 set, u32 flags, u32 one);
typedef u32 (*GuideAdd)(u32 guide, u32 icon, const char *text);

static u32 guide_uio(void) {
    u32 screen = FN(Is2, SCREEN)(GLOBAL(GOT_LOOKS), 0);
    return screen ? FN(Component, UI_COMPONENT)(screen, GUIDE_UIO_INDEX) : 0;
}

// Adds text with a button's icon to the guide's first list, or takes it out again; returns show
static bool guide_hint(const char *text, u32 icon, bool show) {
    u32 g = guide_uio();
    if (!g) return false;
    bool there = false;
    for (u32 i = 0; i < 4; i++) {
        u32 *e = (u32 *)(g + GUIDE_FIRST + 8 * i);
        if (e[1] != (u32)text) continue;
        there = true;
        if (!show) e[0] = 0xFFFFFFFF, e[1] = 0;
    }
    bool changed = show ? !there && FN(GuideAdd, GUIDE_ADD)(g, icon, text) : there;
    if (changed) FN(Build, GUIDE_BUILD)(g, *(u32 *)(g + GUIDE_SET), *(u32 *)(g + GUIDE_FLAGS), 1);
    return show;
}

static void meld_hint(bool show) { g_meld_hint = guide_hint(MELD_HINT, ICON_Y, show); }

// A notification in the game's message log, in the village too: a line of our own text (MESSAGE_TEXT, the way the
// game posts a line that isn't in its message tables), in a box of its own. Such a line is red unless its text says
// otherwise, so it's wrapped in white (NOTE_WHITE).
#define MESSAGE_TEXT 0x65DDC4     // add a line of text to the message log (kind, 0x63, text, 0) -> -1 when it can't
#define TEXT_LINE 0x63
#define NOTE_WHITE "<COL FFFFFFFF>"
typedef u32 (*MessageText)(u32 kind, u32 type, const char *text, u32 zero);

static char *put(char *q, const char *s);
static char *put_u(char *q, u32 v);

static void notify(const char *text) {
    FN(Query, MSG_BEGIN)();
    FN(MessageText, MESSAGE_TEXT)(0, TEXT_LINE, text, 0);
    FN(Query, MSG_END)();
}

static ForgeHook g_meld_hook;
static bool g_meld_quiet; // (the picks' A presses play the game's select sound once)

// Melding jackpot: an order's charms are rolled when it's placed (MELD_ROLL, by the order's kind). The game gives
// Mystery, Luminous, Ancient and Fabled Melding 1 charm (Mystery, Shining, Timeworn, Enduring), Halcyon Melding 0-7
// charms of those first three by its own roll, and Juju Melding 0-2 Enduring and 1-2 Timeworn by the talismans'
// rarities. After the game's roll, the order's 7 entries (the most it holds) are all filled, made as the game makes
// its own: 7 of the melding's charm; for Halcyon 7, each of a type rolled with the game's odds; for Juju 5 Enduring
// and 2 Timeworn (as the well-known cheat 04000000 00196A9C E3A0A007 / 00196AA0 E3A09005 does).
#define MELD_ROLL 0x196604        // roll an order's charms (pot, order)
#define POT_ORDERS 10
#define POT_ORDER 0x1F4           // an order's size: s8 kind at +0x103C, its charms in 7 entries at +0x10C4
#define ORDER_KIND 0x103C
#define ORDER_ENTRIES 0x10C4
#define ORDER_CHARMS 7
#define ENTRY_SIZE 0x34           // u32 state (2: holds a charm), u16 item at +4, the charm item (40 bytes) at +0xC
#define ENTRY_FULL 2
#define ENTRY_ID 4
#define ENTRY_CHARM 0xC
#define ITEM_MAKE 0x14BBA8        // make an item (out, items, id, 1): its 40 bytes at out+4
#define ITEM_FREE 0xDAD24         // (out)
#define GOT_ITEM_MAKER 0x183A834
#define ITEM_MYSTERY 0x161        // then Shining, Timeworn, Enduring: kinds 0-3 give one of these
#define ITEM_TIMEWORN 0x163
#define ITEM_ENDURING 0x164
#define KIND_HALCYON 4
#define KIND_JUJU 5
#define JUJU_ENDURING 5
typedef void (*Roll)(u32 pot, u32 order);
typedef void (*ItemMake)(u32 *out, u32 items, u32 id, u32 one);
typedef void (*ItemFree)(u32 *item);
static ForgeHook g_meld_roll_hook;
static u32 g_seed = 0x9E3779B9;

// Halcyon's odds: of 0-84, below 10 Mystery, below 55 Shining, else Timeworn
static u32 halcyon_charm(void) {
    g_seed ^= g_seed << 13, g_seed ^= g_seed >> 17, g_seed ^= g_seed << 5;
    u32 v = (g_seed + g_frame) % 85;
    return v < 10 ? ITEM_MYSTERY : v < 55 ? ITEM_MYSTERY + 1 : ITEM_TIMEWORN;
}

static void meld_roll_hook(u32 pot, u32 order) {
    ORIGINAL(Roll, g_meld_roll_hook)(pot, order);
    if (!g_meld_jackpot || order >= POT_ORDERS) return;
    s8 kind = *(s8 *)(pot + order * POT_ORDER + ORDER_KIND);
    if (kind < 0 || kind > KIND_JUJU) return;
    u32 e = pot + order * POT_ORDER + ORDER_ENTRIES;
    for (u32 i = 0; i < ORDER_CHARMS; i++, e += ENTRY_SIZE) {
        u32 id = kind == KIND_JUJU ? (i < JUJU_ENDURING ? ITEM_ENDURING : ITEM_TIMEWORN)
                 : kind == KIND_HALCYON ? halcyon_charm() : ITEM_MYSTERY + (u32)kind, item[11];
        FN(ItemMake, ITEM_MAKE)(item, GLOBAL(GOT_ITEM_MAKER), id, 1);
        *(u32 *)e = ENTRY_FULL;
        memcpy((void *)(e + ENTRY_CHARM), item + 1, 40);
        *(u16 *)(e + ENTRY_ID) = (u16)id;
        FN(ItemFree, ITEM_FREE)(item);
    }
    forge_log(LOG_INFO, "field-qol: melding jackpot: order %u (kind %d) gives 7 charms", order, kind);
}

static u32 meld_hook(u32 ui) {
    bool list = *(u8 *)(ui + MELD_SUBSTATE) == MELD_LIST;
    g_meld_frame = g_frame;
    if (list != g_meld_hint || (list && !g_meld_leftovers) ) meld_hint(list && g_meld_leftovers);
    if (g_meld_quiet && (!list || (!g_meld_restore && g_picks_next >= g_picks_left))) // (the picks are done)
        g_meld_quiet = false, g_one_sound_each = false;
    if (!list) {
        g_picks_left = 0, g_meld_restore = false;
        return ORIGINAL(Is, g_meld_hook)(ui);
    }
    u32 *pressed = (u32 *)(GLOBAL(GOT_PAD) + PAD_PRESSED), was = *pressed;
    u8 *cursor = (u8 *)(ui + MELD_CURSOR);
    if (g_picks_next < g_picks_left) {
        // the next leftover: the cursor on its row, and the game's A
        const Pick *p = &g_picks[g_picks_next++];
        *(u16 *)(cursor + CURSOR_PAGE) = p->page, cursor[CURSOR_ROW] = p->row;
        FN(Fill, MELD_FILL)(ui, p->page, 0);
        *pressed = PAD_A;
        if (g_picks_next == g_picks_left) g_meld_restore = true;
    } else if (g_meld_restore) {
        // fewer than needed (no "Meld?"): the cursor back where it was
        g_meld_restore = false, g_picks_left = g_picks_next = 0;
        *(u16 *)(cursor + CURSOR_PAGE) = g_meld_page0, cursor[CURSOR_ROW] = g_meld_row0;
        FN(Fill, MELD_FILL)(ui, g_meld_page0, 0);
        FN(Method, MELD_DRAW_CHOSEN)(ui);
        FN(Method, MELD_DRAW_DETAIL)(ui);
        *(u32 *)(ui + MELD_REDRAW) |= 0x80;
    } else if (g_meld_leftovers && (was & PAD_Y)) {
        u32 need = MELD_NEEDED - *(u32 *)(ui + MELD_COUNT);
        if (!meld_leftovers(ui)) FN(Sound, UI_SOUND)(0, SOUND_FULL);
        else g_meld_quiet = true, g_one_sound_each = true, g_sounds = 0; // (one select sound for all the picks)
        if (need <= MELD_NEEDED && g_picks_left < need) { // (too few: a notification says so)
            static char line[80];
            char *q = put(line, NOTE_WHITE);
            q = g_picks_left ? put(put_u(put(q, "Only "), g_picks_left),
                                   g_picks_left == 1 ? " leftover Talisman found." : " leftover Talismans found.")
                             : put(q, "No leftover Talismans found.");
            q = put(q, "</COL>");
            *q = 0;
            notify(line);
        }
        *pressed = was & ~PAD_Y;
    }
    u32 r = ORIGINAL(Is, g_meld_hook)(ui);
    *pressed = was;
    return r;
}

// ================================================================== View Talismans: R+ZL sells the leftovers
//
// Item Box > Manage Equipment > View Talismans (TAL_UPDATE, every frame; state +0x88: 0 the list, 3 the sale question,
// 4 sold). The game's sell button (TAL_SELL_BUTTON: R, or ZR in another button layout) asks "The total comes to Nz."
// for the Talisman under the cursor; Yes sells it (EQUIP_SELL, the zenny added with MONEY_ADD) and lays the list out
// again from the box (the list's order: TAL_ORDER, a box index per position). With R held, ZL collects the leftovers (as Meld leftovers chooses them, and never the worn one, one in a saved set, or one
// with Decorations attached) and opens the same question with their count and total. Yes sells all of them but one,
// then points the cursor's position at the last one, so the game's own Yes sells it and lays the list out again.
// The sell button acts when it's let go, so that R+ZL doesn't also ask about the Talisman under the cursor.
#define TAL_UPDATE 0x720FF8       // the View Talismans screen, every frame (ui)
#define TAL_STATE 0x88
#define TAL_LIST 0
#define TAL_QUESTION 3
#define TAL_SOLD 4
#define TAL_CURSOR 0x24C          // the list's cursor (row +0xD, rows per page +0xE, page +0x10)
#define TAL_ASK 0x6C              // (u8) the question's Yes/No cursor
#define TAL_SELL_BUTTON 0x65F914  // the game's sell button -> its pad bit
#define TAL_ORDER 0x3A1CC         // equipment box: u16 box index of each position of the list (8 bytes each)
#define TAL_ORDER_MAX 2000
#define EQUIP_PRICE 0x1437E0      // equipment box: an entry's sale price (box, 0, index) -> zenny, -1: none
#define EQUIP_SELL 0x143390       // equipment box: sell an entry (box, 0, index) -> zenny (its sets forget it)
#define EQUIP_WORN 0x1444B4       // equipment box: worn (box, 0, index) -> non-zero
#define EQUIP_IN_SET 0x148340     // equipment box: the first saved set using it (box, 0, index) -> 0xFF: none
#define EQUIP_DECORATED 0x144F28  // equipment box: Decorations attached (box, index) -> non-zero
#define MONEY_ADD 0x523150        // add zenny (the GOT_MONEY object, amount)
#define GOT_MONEY 0x1838D88
#define PAD_HELD 0x6BF4           // the buttons held this frame
#define TAL_MODE 0x84
#define TAL_PANEL 0x65CC0C        // (ui, 3): as the game's question does in modes 4 and 5
#define CURSOR_ACTIVATE 0x54ECC0  // a cursor: (cursor, 1) while a question is up, 0 after
#define SOUND_ASK 0x15
#define PAD_ZL 0x400
#define ICON_R 5
// The guide's "Sell Talisman" says ZL's too: the guide's second list names its entries by button-name message (set 17,
// View Talismans: 0x57 "Sell Talisman"), and looks each text up (MSG_TEXT) when it's laid out. While the list is up,
// that lookup gives the plugin's text, and the guide is laid out again when that starts and ends.
#define BUTTON_TABLE 0x12
#define BUTTON_SELL 0x57
static const char SELL_HINT[] = "Sell (ZL: Leftovers)";
static bool g_sell_hint;

static ForgeHook g_msg_hook;

static const char *msg_hook(u32 manager, u32 table, u32 number) {
    if (g_sell_hint && table == BUTTON_TABLE && number == BUTTON_SELL) return SELL_HINT;
    return ORIGINAL(MsgLookup, g_msg_hook)(manager, table, number);
}

static void sell_hint(bool show) {
    g_sell_hint = show;
    u32 g = guide_uio();
    if (g) FN(Build, GUIDE_BUILD)(g, *(u32 *)(g + GUIDE_SET), *(u32 *)(g + GUIDE_FLAGS), 1);
}
#define SELL_TABLE 0x16           // the question's message: 0x7C, or 0x90 with Decorations attached
#define SELL_MSG 0x7C
#define SELL_MSG_DECO 0x90

typedef u32 (*Box3)(u32 box, u32 zero, u32 index);
typedef u32 (*Box2)(u32 box, u32 index);
typedef u32 (*Button)(void);
typedef void (*AddMoney)(u32 money, s32 amount);
typedef void (*Activate)(u32 cursor, u32 on);

static ForgeHook g_tal_hook;
static u16 g_sell[EQUIP_SLOTS];
static u32 g_sell_count, g_sell_total, g_tal_frame;
static bool g_sell_held;

// The leftovers that can be sold: dominated (as for melding), not worn, in no saved set, no Decorations, with a price
static void sell_collect(void) {
    u32 box = GLOBAL(GOT_EQUIP_BOX);
    read_charms();
    g_sell_count = g_sell_total = 0;
    for (u32 k = 0; k < g_charm_count; k++) {
        u32 bi = g_charms[k];
        bool left = false;
        for (u32 j = 0; j < g_charm_count && !left; j++)
            if (j != k && dominates(&g_charm[g_charms[j]], g_charms[j], &g_charm[bi], bi)) left = true;
        if (!left || FN(Box3, EQUIP_WORN)(box, 0, bi) || (FN(Box3, EQUIP_IN_SET)(box, 0, bi) & 0xFF) != 0xFF ||
            FN(Box2, EQUIP_DECORATED)(box, bi))
            continue;
        s32 price = (s32)FN(Box3, EQUIP_PRICE)(box, 0, bi);
        if (price < 0) continue;
        g_sell[g_sell_count++] = (u16)bi;
        g_sell_total += (u32)price;
    }
}

static u32 tal_hook(u32 ui) {
    u32 pad = GLOBAL(GOT_PAD), *pressed = (u32 *)(pad + PAD_PRESSED), was = *pressed;
    u32 held = *(u32 *)(pad + PAD_HELD), sell = FN(Button, TAL_SELL_BUTTON)();
    u32 state = *(u32 *)(ui + TAL_STATE);
    if (g_frame - g_tal_frame > 2) g_sell_held = false, g_sell_asking = false; // (the screen was opened again)
    g_tal_frame = g_frame;
    bool hint = g_meld_leftovers && state == TAL_LIST;
    if (hint != g_sell_hint) sell_hint(hint);
    if (!g_meld_leftovers || (state != TAL_LIST && !g_sell_asking)) {
        g_sell_held = false;
        return ORIGINAL(Is, g_tal_hook)(ui);
    }
    if (state == TAL_LIST) {
        g_sell_asking = false;
        if (was & sell) g_sell_held = true, *pressed &= ~sell; // (decided when it's let go, or by ZL)
        if (g_sell_held && (was & PAD_ZL)) {
            g_sell_held = false;
            *pressed &= ~PAD_ZL;
            sell_collect();
            if (!g_sell_count) {
                FN(Sound, UI_SOUND)(0, SOUND_FULL);
                notify(NOTE_WHITE "No leftover Talismans to sell.</COL>");
            } else {
                char *q = put(put_u(put(g_sell_text, "Sell "), g_sell_count),
                              g_sell_count == 1 ? " leftover Talisman?" : " leftover Talismans?");
                q = put(put_u(put(q, "\r\nThe total comes to "), g_sell_total), "z.");
                *q = 0;
                g_sell_asking = true;
                forge_log(LOG_INFO, "field-qol: talismans: %u leftovers for %uz, asking", g_sell_count, g_sell_total);
                // the game's question, as its sell button opens it (it takes the Talisman under the cursor for its
                // text, which the plugin replaces)
                *(u8 *)(ui + TAL_ASK) = 1;
                *(u32 *)(ui + TAL_STATE) = TAL_QUESTION;
                u32 mode = *(u32 *)(ui + TAL_MODE);
                if (mode == 4 || mode == 5) FN(Method4, TAL_PANEL)(ui, 3, 0, 0);
                FN(Sound, UI_SOUND)(0, SOUND_ASK);
                FN(Activate, CURSOR_ACTIVATE)(ui + TAL_CURSOR, 1);
                FN(Activate, CURSOR_ACTIVATE)(ui + 0x198, 1);
                FN(Activate, CURSOR_ACTIVATE)(ui + 0x1BC, 1);
            }
        } else if (g_sell_held && !(held & sell)) {
            g_sell_held = false;
            *pressed |= sell; // (let go without ZL: the game's own sale of the Talisman under the cursor)
        }
    } else if (state == TAL_SOLD && g_sell_asking) {
        // Yes: all but one sold here, the last by the game at the cursor's position
        g_sell_asking = false;
        u32 box = GLOBAL(GOT_EQUIP_BOX), money = GLOBAL(GOT_MONEY), got = 0;
        u8 *cursor = (u8 *)(ui + TAL_CURSOR);
        u32 pos = (u32)*(u16 *)(cursor + CURSOR_PAGE) * cursor[CURSOR_ROWS] + cursor[CURSOR_ROW];
        for (u32 i = 0; i + 1 < g_sell_count; i++) got += FN(Box3, EQUIP_SELL)(box, 0, g_sell[i]);
        if (got) FN(AddMoney, MONEY_ADD)(money, (s32)got);
        if (pos < TAL_ORDER_MAX) *(u16 *)(box + TAL_ORDER + pos * 8) = g_sell[g_sell_count - 1];
        forge_log(LOG_INFO, "field-qol: talismans: sold %u leftovers for %uz, the last one by the game",
                  g_sell_count - 1, got);
    }
    u32 r = ORIGINAL(Is, g_tal_hook)(ui);
    *pressed = was;
    return r;
}

// ================================================================== Auto-sort (off by default)
//
// The Item Pouch's Sort (its top face button, then Yes) calls POUCH_SORT on the pouch. With the switch on, the plugin
// calls it whenever items were added to the pouch (an item's total in it went up), without the game's question and
// sound, once no menu is open and no "Item Pouch is full" window is waiting (those point at slots by number).
#define POUCH_SORT 0x19D740       // sort the Item Pouch (the GOT_POUCH object), as its Sort does after Yes
#define GOT_POUCH 0x183A828
#define POUCH_SLOT 0x558514       // a pouch slot {u16 item, u16 quantity} (the GOT_POUCH_SLOTS object, 0-39)
#define GOT_POUCH_SLOTS 0x1838DA8
#define POUCH_SLOTS 40

static u16 g_pouch_item[POUCH_SLOTS], g_pouch_qty[POUCH_SLOTS];
static bool g_pouch_known, g_sort_pending;

static bool pouch_read(u16 *item, u16 *qty) {
    u32 slots = GLOBAL(GOT_POUCH_SLOTS);
    if (!slots) return false;
    for (u32 i = 0; i < POUCH_SLOTS; i++) {
        const u16 *s = (const u16 *)FN(Is2, POUCH_SLOT)(slots, i);
        if (!s) return false;
        item[i] = s[0], qty[i] = item[i] ? s[1] : 0;
    }
    return true;
}

static u32 pouch_total(const u16 *item, const u16 *qty, u16 id) {
    u32 n = 0;
    for (u32 i = 0; i < POUCH_SLOTS; i++)
        if (item[i] == id) n += qty[i];
    return n;
}

// The screens that show the pouch's slots and put items in it (the Item Box, the quest's supply box, the shared box):
// their updates (vtable slot 9) note the frame, and the sort waits until none has run for a few frames
#define ITEM_BOX_UPDATE 0x713DB8
#define SUPPLY_BOX_UPDATE 0x679EB4
#define SHARE_BOX_UPDATE 0x66D588
#define BOX_QUIET_FRAMES 10
static ForgeHook g_item_box_hook, g_supply_box_hook, g_share_box_hook;
static u32 g_box_frame;

static u32 item_box_hook(u32 ui) { g_box_frame = g_frame; return ORIGINAL(Is, g_item_box_hook)(ui); }
static u32 supply_box_hook(u32 ui) { g_box_frame = g_frame; return ORIGINAL(Is, g_supply_box_hook)(ui); }
static u32 share_box_hook(u32 ui) { g_box_frame = g_frame; return ORIGINAL(Is, g_share_box_hook)(ui); }

static bool menu_open(void) { return g_frame - g_box_frame < BOX_QUIET_FRAMES; }

// Every frame: note added items, sort when nothing points at the pouch's slots
static void auto_sort_update(void) {
    if (!g_auto_sort) {
        g_pouch_known = g_sort_pending = false;
        return;
    }
    u16 item[POUCH_SLOTS], qty[POUCH_SLOTS];
    if (!pouch_read(item, qty)) return;
    if (g_pouch_known)
        for (u32 i = 0; i < POUCH_SLOTS && !g_sort_pending; i++)
            if (item[i] && pouch_total(item, qty, item[i]) > pouch_total(g_pouch_item, g_pouch_qty, item[i]))
                g_sort_pending = true;
    for (u32 i = 0; i < POUCH_SLOTS; i++) g_pouch_item[i] = item[i], g_pouch_qty[i] = qty[i];
    g_pouch_known = true;
    if (!g_sort_pending || g_spot || g_lefts || menu_open()) return;
    u32 prompt = FN(Query, PROMPT_ITEM)();
    if ((prompt && *(u8 *)(prompt - PROMPT_PENDING)) || FN(Query, PROMPT_OPEN)()) return;
    u32 pouch = GLOBAL(GOT_POUCH);
    if (!pouch) return;
    FN(Method, POUCH_SORT)(pouch);
    g_sort_pending = false;
    forge_log(LOG_INFO, "field-qol: the Item Pouch auto-sorted");
    pouch_read(g_pouch_item, g_pouch_qty); // (the sorted order is no change)
}

// ================================================================== Settings: the file, Purple-lib's page

static char *put(char *q, const char *s) { while (*s) *q++ = *s++; return q; }

// The switches, in the order of Purple-lib's page rows, with their defaults (Defaults puts them back)
typedef struct {
    const char *key;   // in fieldqol.ini
    const char *name;  // the Mod Options row
    const char *help;  // its help line
    bool *value;
    bool on;           // the default
} Setting;

static const Setting SETTINGS[] = {
    {"gather_all", "One-press gather", "One press gathers all a spot has left.", &g_gather_all, true},
    {"hold_single", "Hold R: single gather", "Hold R as you start gathering to gather\r\nonly a single item instead of all.",
     &g_hold_single, true},
    {"merge_messages", "One message per item", "\"Iron Ore x3\" instead of three lines.", &g_merge, true},
    {"one_notification", "One notification", "One notification for the whole spot.", &g_one_box, true},
    {"full_pouch", "Replacement prompt last", "Gather everything, then ask what to replace.", &g_roll_all, true},
    {"deliver_anywhere", "Deliver anywhere", "Item Pouch Deliver works away from camp.", &g_deliver_anywhere, true},
    {"auto_sort", "Auto-sort Item Pouch", "Sorts the Item Pouch whenever items are\r\nadded to it, as its Sort does.",
     &g_auto_sort, false},
    {"send_all", "Send all to box", "Adds \"Send all to the Item Box & Exit\".", &g_send_all, true},
    {"meld_leftovers", "Talisman management", "Leftover Talismans: Y on the melding\r\nlist chooses them. In View Talismans,\r\nhold R, then press ZL to sell them.",
     &g_meld_leftovers, true},
    {"meld_jackpot", "Melding jackpot", "Every melding gives 7 charms, the most\r\nan order holds.",
     &g_meld_jackpot, false},
};
#define SETTING_COUNT ((u32)(sizeof SETTINGS / sizeof *SETTINGS))

static bool g_prev[SETTING_COUNT];  // the values the last frame saw: a change starts the save countdown
static u32 g_save_delay;            // frames until a change is saved (a held step changes every frame)
static bool g_loaded;               // the file was read, or the save data had none

// ---------------------------------------------------------------- the settings file

// In the game's save data, next to the save (save_0/fieldqol.ini), as Forge's and other plugins' settings are. The save
// data is mounted for each read or write and unmounted (which commits) right after, as Forge's API asks.
#define FILE_NAME "fieldqol.ini"
static char g_path[64];

// Mounts the save data and makes g_path; false when it can't be mounted (yet)
static bool mount(void) {
    if (!forge_fs_mountSaveData()) return false;
    const char *mp = forge_fs_getSaveDataMountPoint();
    u32 i = 0;
    for (; mp && mp[i] && i < sizeof g_path - sizeof ":/" FILE_NAME; i++) g_path[i] = mp[i];
    for (const char *t = ":/" FILE_NAME; *t; t++) g_path[i++] = *t;
    g_path[i] = 0;
    return true;
}

static bool is_space(char c) { return c == ' ' || c == '\t'; }

// "gather_all = 0" lines (0 off, 1 on); anything else (comments, unknown keys, other values) is skipped
static void parse(const char *s, const char *end) {
    while (s < end) {
        const char *line = s;
        while (s < end && *s != '\n') s++;
        const char *eol = s++;
        while (line < eol && is_space(*line)) line++;
        for (u32 k = 0; k < SETTING_COUNT; k++) {
            const char *p = line, *key = SETTINGS[k].key;
            while (*key && p < eol && *p == *key) p++, key++;
            if (*key) continue;
            while (p < eol && is_space(*p)) p++;
            if (p == eol || *p++ != '=') continue;
            while (p < eol && is_space(*p)) p++;
            if (p < eol && (*p == '0' || *p == '1')) *SETTINGS[k].value = *p == '1';
        }
    }
}

static void remember(void) {
    for (u32 k = 0; k < SETTING_COUNT; k++) g_prev[k] = *SETTINGS[k].value;
}

// Reads the settings file; false when the save data couldn't be mounted (tried again later)
static bool load(void) {
    static char buf[1024];
    FsFile f;
    s64 size = 0;
    if (!mount()) return false;
    u32 n = 0;
    bool found = fs_open(&f, g_path, FS_READ) == 0;
    if (found) {
        if (fs_size(&size, f) == 0 && size > 0) {
            n = size < (s64)sizeof buf ? (u32)size : sizeof buf;
            if (fs_read(f, 0, buf, n) != 0) n = 0;
        }
        fs_close(f);
    }
    forge_fs_unmountSaveData();
    parse(buf, buf + n);
    remember();
    forge_log(LOG_INFO, "field-qol: %s", found ? "settings loaded from " FILE_NAME : "no " FILE_NAME " yet, the defaults");
    return true;
}

static void save(void) {
    static char buf[1024];
    char *q = put(buf, "; Field QoL " PLUGIN_VERSION ": 1 is on, 0 is off. Change them in game (Options > Mod Options, or\n"
                       "; Forge's menu) or here while the game is closed.\n");
    for (u32 k = 0; k < SETTING_COUNT; k++) {
        q = put(q, SETTINGS[k].key);
        q = put(q, " = ");
        *q++ = *SETTINGS[k].value ? '1' : '0';
        *q++ = '\n';
    }
    u32 n = (u32)(q - buf);

    FsFile f;
    if (!mount()) {
        forge_log(LOG_WARN, "field-qol: could not mount the save data, settings not saved");
        return;
    }
    bool ok = fs_open(&f, g_path, FS_WRITE | FS_APPEND) == 0;
    if (!ok) {
        fs_create(g_path, 0);
        ok = fs_open(&f, g_path, FS_WRITE | FS_APPEND) == 0;
    }
    if (ok) {
        static const FsWriteOption flush = {1};
        ok = fs_set_size(f, n) == 0 && fs_write(f, 0, buf, n, &flush) == 0;
        fs_close(f);
    }
    forge_fs_unmountSaveData();
    forge_log(ok ? LOG_INFO : LOG_WARN, "field-qol: settings %s", ok ? "saved" : "NOT saved");
}

// Called every frame: a change from either menu (Forge's checkboxes, Purple-lib's rows) is saved shortly after it stops
static void settings_update(void) {
    static u32 retry;
    if (!g_loaded) {
        if (++retry >= 60) retry = 0, g_loaded = load();  // the save data wasn't mountable at init
        return;
    }
    bool changed = false;
    for (u32 k = 0; k < SETTING_COUNT; k++)
        if (*SETTINGS[k].value != g_prev[k]) g_prev[k] = *SETTINGS[k].value, changed = true;
    if (changed) g_save_delay = 30;
    else if (g_save_delay && !--g_save_delay) save();
}

// ---------------------------------------------------------------- Purple-lib: Options > Mod Options

static const char *const ON_OFF[] = {"Off", "On"};
static const char *const RESET[] = {"Reset"};

static const char *title(void *ctx, u32 language) { (void)ctx, (void)language; return "Field QoL"; }

static bool row(void *ctx, u32 language, u32 index, PurpleLibOptionRow *out) {
    (void)ctx, (void)language;
    if (index < SETTING_COUNT) {
        out->name = SETTINGS[index].name;
        out->help = SETTINGS[index].help;
        out->labels = ON_OFF;
        out->label_count = 2;
        return true;
    }
    if (index == SETTING_COUNT) {
        out->name = "Defaults";
        out->help = "Puts every switch back to its default.";
        out->kind = PURPLELIB_ROW_ACTION;
        out->labels = RESET;
        out->label_count = 1;
        return true;
    }
    return false;
}

static u32 get(void *ctx, u32 index) { (void)ctx; return index < SETTING_COUNT && *SETTINGS[index].value; }

static void set(void *ctx, u32 index, u32 value) {
    (void)ctx;
    if (index < SETTING_COUNT) *SETTINGS[index].value = value != 0;
}

static void action(void *ctx, u32 index) {
    (void)ctx;
    if (index != SETTING_COUNT) return;
    for (u32 k = 0; k < SETTING_COUNT; k++) *SETTINGS[k].value = SETTINGS[k].on;
}

static void closed(void *ctx) { (void)ctx; g_save_delay = 1; }  // (the window closed: save without the wait)

static const char *const PAGES[] = {
    "Each row turns one Field QoL feature\r\non or off. Changes apply at once.",
    "They are saved to fieldqol.ini next\r\nto your save. Defaults puts every\r\nswitch back to its default.",
};
static const char *help_page(void *ctx, u32 language, u32 index) {
    (void)ctx, (void)language;
    return index < sizeof PAGES / sizeof *PAGES ? PAGES[index] : NULL;
}

static const PurpleLibOptions OPTIONS = {
    sizeof(PurpleLibOptions), NULL, title, SETTING_COUNT + 1, row, get, set, closed, NULL, action,
};
static const PurpleLibHelp SET_HELP = {
    sizeof(PurpleLibHelp), NULL, 0, sizeof PAGES / sizeof *PAGES, title, help_page, NULL,
};

EXPORT void purplelib_on_register(const PurpleLibApi *api) {
    if (!api || api->version < PURPLELIB_API_VERSION) {
        forge_log(LOG_WARN, "field-qol: Purple-lib is too old, no Mod Options page");
        return;
    }
    bool options = api->add_options(&OPTIONS);
    if (options) api->add_set_help(&OPTIONS, &SET_HELP);
    forge_log(options ? LOG_INFO : LOG_WARN, "field-qol: %s",
              options ? "settings under Options > Mod Options" : "Purple-lib refused the Mod Options page");
}

// ================================================================== Forge

EXPORT void forge_onLoad(ForgePluginInfo *info) {
    static const char name[] = "Field QoL " PLUGIN_VERSION;
    if (!info) return;
    info->major = 1, info->minor = 5, info->patch = 1; // (the Forge API version it was written for)
    memset(info->name, 0, sizeof info->name);
    for (u32 i = 0; i < sizeof name; i++) info->name[i] = name[i];
}

static bool hook(ForgeHook *h, u32 target, void *detour) {
    u32 original = 0; // (non-null asks Forge for a trampoline to the original)
    forge_hook_create(h, g_main + target, (u32)detour, &original);
    return h->original != 0;
}

EXPORT void forge_onInit(void) {
    g_main = forge_mem_getMainTextAddr();
    g_loaded = load();  // (the switches, before any hook reads them)

    // Gathering: the pouch results first, as the gather relies on them; without the message hook, a line per item
    bool pouch = hook(&g_add_hook, PLAYER_ADD, add_hook) & hook(&g_add_quest_hook, PLAYER_ADD_QUEST, add_quest_hook);
    if (!hook(&g_message_hook, MESSAGE, message_hook))
        forge_log(LOG_WARN, "field-qol: message hook FAILED, one line per gathered item");
    if (!hook(&g_prompt_hook, POUCH_PROMPT, prompt_hook))
        forge_log(LOG_WARN, "field-qol: pouch window hook FAILED, a full pouch stops the spot");
    if (!hook(&g_play_dead_hook, EM009_SET_ACTION, play_dead_hook))
        forge_log(LOG_WARN, "field-qol: Gypceros hook FAILED, its playing-dead carves are taken at once");
    g_gather_hooked = pouch && hook(&g_gather_hook, GATHER, gather_hook);
    forge_log(g_gather_hooked ? LOG_INFO : LOG_WARN, "field-qol: %s",
              g_gather_hooked ? "one press gathers the whole spot" : "gathering hook FAILED, gathering as usual");

    // Delivery
    g_deliver_hooked = hook(&g_deliverable_hook, DELIVERABLE, deliverable_hook) &
                       hook(&g_clear_account_hook, CLEAR_ACCOUNT, clear_account_hook);
    forge_log(g_deliver_hooked ? LOG_INFO : LOG_WARN, "field-qol: %s",
              g_deliver_hooked ? "pouch deliveries work anywhere" : "a delivery hook FAILED, deliveries may stay camp only");

    // Rewards: the menu's logic only along with its drawing; without the sound hook, a sound per slot sent
    bool drawn = hook(&g_rows_hook, MENU_ROWS, rows_hook) & hook(&g_looks_hook, MENU_LOOKS, looks_hook) &
                 hook(&g_reward_update_hook, REWARD_UPDATE, reward_update_hook);
    g_rewards_hooked = drawn && hook(&g_section_hook, RESULT_SECTION, section_hook);
    if (drawn && !(hook(&g_monnyan_init_hook, MONNYAN_INIT, monnyan_init_hook) &
                   hook(&g_monnyan_hook, MONNYAN_UPDATE, monnyan_hook)))
        forge_log(LOG_WARN, "field-qol: a Meownster Hunters hook FAILED, no Send all there");
    if (g_rewards_hooked && !(hook(&g_appraisal_help_hook, APPRAISAL_HELP, appraisal_help_hook) &&
                              hook(&g_appraisal_hook, APPRAISAL_UPDATE, appraisal_hook)))
        forge_log(LOG_WARN, "field-qol: the appraisal hook FAILED, no Send all there");
    // Melding: Y chooses leftover talismans (and says so when there are too few)
    if (!hook(&g_meld_hook, MELD_UPDATE, meld_hook))
        forge_log(LOG_WARN, "field-qol: the melding hook FAILED, no leftover talismans with Y");
    if (drawn && !(hook(&g_claim_hook, CLAIM_UPDATE, claim_hook) &&
                   hook(&g_receive_draw_hook, RECEIVE_DRAW, receive_draw_hook)))
        forge_log(LOG_WARN, "field-qol: the Meld Result hook FAILED, no Send all there");
    if (drawn && !hook(&g_msg_hook, MSG_TEXT, msg_hook))
        forge_log(LOG_WARN, "field-qol: the message hook FAILED, the guide shows only Sell Talisman");
    if (drawn && !hook(&g_tal_hook, TAL_UPDATE, tal_hook))
        forge_log(LOG_WARN, "field-qol: the View Talismans hook FAILED, no selling leftovers with R+ZL");
    if (!hook(&g_meld_roll_hook, MELD_ROLL, meld_roll_hook))
        forge_log(LOG_WARN, "field-qol: the melding roll hook FAILED, no melding jackpot");
    // Auto-sort: the screens it waits for
    if (!(hook(&g_item_box_hook, ITEM_BOX_UPDATE, item_box_hook) & hook(&g_supply_box_hook, SUPPLY_BOX_UPDATE, supply_box_hook) &
          hook(&g_share_box_hook, SHARE_BOX_UPDATE, share_box_hook)))
        forge_log(LOG_WARN, "field-qol: a box screen hook FAILED, auto-sort may sort while a box is open");
    if (drawn && !hook(&g_help_hook, HELP_SET, help_hook))
        forge_log(LOG_WARN, "field-qol: the help line hook FAILED, Send all shows the game's help lines");
    if (drawn && !(hook(&g_trade_menu_hook, TRADE_MENU_DRAW, trade_menu_hook) &&
                   hook(&g_trade_hook, TRADE_CLAIM, trade_hook)))
        forge_log(LOG_WARN, "field-qol: a trade hook FAILED, no Send all at the trade cart");
    if (drawn && !hook(&g_gh_hook, GH_UPDATE, gh_hook))
        forge_log(LOG_WARN, "field-qol: the Hunters for Hire hook FAILED, no Send all there");
    if (g_rewards_hooked && !hook(&g_sound_hook, UI_SOUND, sound_hook))
        forge_log(LOG_WARN, "field-qol: sound hook FAILED, Send all plays a sound per slot");
    forge_log(g_rewards_hooked ? LOG_INFO : LOG_WARN, "field-qol: %s",
              g_rewards_hooked ? "Send all to the Item Box & Exit on the reward screen" : "a rewards hook FAILED, no Send all");
}


EXPORT void forge_onUpdate(float dt) {
    (void)dt;
    if (g_main) g_frame++, auto_sort_update();
    if (g_meld_hint && g_frame - g_meld_frame > 2) meld_hint(false); // (the melding screen closed)
    if (g_sell_hint && g_frame - g_tal_frame > 2) sell_hint(false); // (View Talismans closed)
    g_since_r = forge_input_isDown(BUTTON_R) ? 0 : g_since_r + (g_since_r <= SINGLE_FRAMES);
    settings_update();
    if (g_main) prompt_next();
}

EXPORT void forge_onImGuiInit(void *ctx, void *alloc, void *freefn) {
    if (!ctx || !alloc || !freefn) return;
    igSetCurrentContext(ctx);
    igSetAllocatorFunctions(alloc, freefn, NULL);
}


// Writes a number in decimal without dividing (division would need libgcc)
static char *put_u(char *q, u32 v) {
    static const u32 tens[] = {1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
    bool started = false;
    for (u32 i = 0; i < sizeof tens / sizeof *tens; i++) {
        char d = '0';
        while (v >= tens[i]) { v -= tens[i]; d++; }
        if (d != '0' || started || tens[i] == 1) { *q++ = d; started = true; }
    }
    return q;
}

EXPORT void forge_onImGuiRender(void) {
    if (!igTreeNode_Str("Field QoL###field-qol")) return;

    igTextUnformatted("Gathering", NULL);
    if (!g_gather_hooked) igTextUnformatted("Hook failed: gathering as usual (see log.txt).", NULL);
    igCheckbox("One press gathers the whole spot", &g_gather_all);
    igCheckbox("Hold R while gathering: a single item", &g_hold_single);
    igTextUnformatted("Eggs and other carried items still take a press each.", NULL);
    igCheckbox("One message per item (\"Iron Ore x3\")", &g_merge);
    igCheckbox("One notification per spot", &g_one_box);
    igCheckbox("Full pouch: gather everything, then ask what to replace", &g_roll_all);
    static char line[96];
    char *q = put(line, "Last spot: ");
    q = put_u(q, g_last_extra);
    q = put(q, " extra (");
    q = put(q, *g_last_stop ? g_last_stop : "none yet");
    q = put(q, "), this session: ");
    q = put_u(q, g_total);
    *q = 0;
    igTextUnformatted(line, NULL);

    igTextUnformatted("Delivery", NULL);
    if (!g_deliver_hooked) igTextUnformatted("Hook failed: deliveries may stay camp only (see log.txt).", NULL);
    igCheckbox("Deliver from the Item Pouch anywhere", &g_deliver_anywhere);
    igTextUnformatted("Deliver and Clear out Account Items, for what the game allows.", NULL);

    igTextUnformatted("Item Pouch", NULL);
    igCheckbox("Auto-sort the Item Pouch when items are added (off by default)", &g_auto_sort);

    igTextUnformatted("Rewards", NULL);
    if (!g_rewards_hooked) igTextUnformatted("Hook failed: no Send all (see log.txt).", NULL);
    igCheckbox("\"Send all to the Item Box & Exit\" on the reward screen", &g_send_all);

    igTextUnformatted("Talismans", NULL);
    igCheckbox("Talisman management: Y on the melding list chooses leftover Talismans; in View Talismans, hold R, then press ZL to sell them",
               &g_meld_leftovers);
    igCheckbox("Melding jackpot: every melding gives 7 charms (off by default)", &g_meld_jackpot);

    igTextUnformatted("Saved to fieldqol.ini next to your save. With Purple-lib: Options > Mod Options.", NULL);
    igTreePop();
}
