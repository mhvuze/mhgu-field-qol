// Field QoL: quality-of-life changes on quests, as a Forge plugin for MHGU Recomp.
//
// Three features, each with a switch in Forge's menu (Insert > Field QoL), all on by default:
//   - Gathering: one press at a gathering spot gathers everything the spot has left.
//   - Delivery:  the Item Pouch's Deliver works anywhere on the map, not only in the base camp.
//   - Rewards:   "Send all to Item Box & Exit" on the quest reward screen.
//
// The plugin runs in the recomp's interpreter as AArch32 code and calls the game's own functions. Addresses are
// offsets into the game's main module (MHGU 1.4.0); at runtime they are added to forge_mem_getMainTextAddr(). Each
// feature's section below explains the game code it relies on.
#include "forge.h"

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
#define POP_ITEM 0x319318         // cPop: roll its next item (pop, &item, &quantity) -> 1
#define IS_CARRIED 0x1943BC       // sItem: is the item carried in the hands (items, item)
#define MESSAGE 0x65DE34          // add a line to the quest's message log (kind, message, item, quantity, 0)
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
#define UI_COMPONENT 0x65CB3C     // uUIBase: one of its cUIO (ui, index); the reward screen's menu is index 0
#define GOT_PAD 0x183931C         // the game's pad state
#define GOT_RESULT 0x1838DE0      // the game's quest result

// ================================================================== game structures

// sPop and cPop: the active gathering spots are a list from sPop+0x5C, linked by +0x2C
#define SPOP_ACTIVE 0x5C
#define POP_NEXT 0x2C
#define POP_ID 0x4C               // (u16) the spot's id
#define POP_LEFT 0x4E             // (u16) gathers left
#define LEFT_UNLIMITED 0xFFFF
// the hunter
#define HUNTER_STATE 0x2500
#define STATE_CARRYING 0x10       // set while carrying something in the hands
#define HUNTER_AWAY 0x1054        // (u8) 0 in the base camp, 1 out on the map
#define HUNTER_POP_ID 0x32AE      // (s16) the id of the spot being gathered
// the pouch's answers
#define ADD_NONE 0xFFFFFFFF       // (the plugin's own: no answer seen)
#define ADD_TAKEN_MAX 2           // 0-2: the pouch took it; more: refused (full, at the stack limit)
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
#define SEND_FULL 7
#define ITEM_SPECIAL 0x16         // (u8, item data) handed over by RESULT_SPECIAL instead of sent
// the pad
#define PAD_PRESSED 0x6BFC        // the buttons pressed this frame
#define PAD_A 0x2000
#define PAD_B 0x4000
// cUIOReward, the reward menu, and its layout
#define UIO_GUI 0x24
#define UIO_MODE 0x4C             // which screen uses the menu: the reward screen's sections are 1, 2 and 7
#define UIO_CURSOR 0x50
#define RESULT_MODES 0x86         // (bits 1, 2 and 7; the Meowcenaries' screens use other modes)
#define GUI_INSTANCES 0x298       // the layout's instance count
#define ROW_SEND 7                // layout instances: the second row (Send to the Item Box)
#define ROW_THIRD 8               // the third row, which the plugin turns into "Send all..."
#define ROW_FOURTH 89             // the fourth row, added by tools/make_arcs.py
#define ROW_OBJECT 1              // a row's text object
#define INST_SPACING 0x10         // its y is the spacing of the rows
#define INST_HIGHLIGHT 0x2F       // the highlight bar
#define MSG_RESULT 7              // the message table of the menu's texts...
#define MSG_SEND 1                // ...and "Send to the Item Box" in it

// ================================================================== plugin state and helpers

#define MAX_EXTRA 64              // the most extra gathers per press (spots have a handful)
#define MAX_QUEUED 24             // the most messages held back per press
#define MAX_SOUNDS 4              // the most distinct sounds remembered during one Send all

typedef u32 (*Gather)(u32 hunter, u32 empty_if_none, u32 notify);
typedef void (*Give)(u32 hunter, u32 item, u32 quantity, u32 notify, u32 zero);
typedef u32 (*Add)(u32 hunter, u32 item, u32 quantity, u32 flags, u32 zero);
typedef u32 (*Is)(u32 self);
typedef u32 (*PopUse)(u32 spop, u32 id, u32 *pop);
typedef u32 (*PopItem)(u32 pop, u16 *item, u8 *quantity);
typedef u32 (*IsCarried)(u32 items, u32 item);
typedef u32 (*Message)(u32 kind, u32 id, u32 item, u32 quantity, u32 extra);
typedef u32 (*Query)(void);
typedef u32 (*Deliverable)(u32 slot, u32 item);
typedef u32 (*Section)(u32 ui, u32 section);
typedef u32 (*Component)(u32 ui, u32 index);
typedef u32 (*Send)(u32 ui, u32 slot);
typedef void (*Special)(u32 result, u32 slot, u32 zero);
typedef u32 (*Data)(u32 items, u32 item);
typedef u32 (*Sound)(u32 kind, u32 id);
typedef void (*Rows)(u32 uio, u32 list, u32 flag);
typedef void (*Looks)(u32 uio, u32 enabled, u32 row);
typedef void (*Method)(u32 self);
typedef void (*SetText)(u32 uio, u32 inst, u32 obj, const char *text);
typedef void (*SetInt)(u32 uio, u32 inst, u32 obj, u32 v);
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

static ForgeHook g_gather_hook, g_add_hook, g_add_quest_hook, g_message_hook;
static ForgeHook g_deliverable_hook, g_clear_account_hook;
static ForgeHook g_section_hook, g_rows_hook, g_looks_hook, g_reward_update_hook, g_sound_hook;
static bool g_gather_hooked, g_deliver_hooked, g_rewards_hooked;

// The switches in Forge's menu
static bool g_gather_all = true, g_merge = true, g_deliver_anywhere = true, g_send_all = true;

// The compiler may emit memset calls; there is no C library to provide one
void *memset(void *d, int c, u32 n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }

// ================================================================== Gathering
//
// The game's gather (GATHER) runs once per gathering motion. It asks sPop to use up one gather of the spot the hunter
// faces (POP_USE), takes the spot's item (POP_ITEM: a fixed item, or a roll of the spot's table) and gives it to the
// hunter (GIVE: skill bonuses, the pouch, the "obtained" message, or the carry motion for eggs and the like).
//
// The plugin hooks GATHER. After a gather that put an item in the pouch, it repeats those three steps until the spot
// is used up, following the same rules as pressing the button again. It stops:
//   - at a spot with an unlimited count (it would never run out);
//   - before an item carried in the hands (eggs, powderstone...): that gather isn't used, so the next press gathers
//     it as usual; likewise when the hunter is already carrying something;
//   - when the pouch refuses an item (full, at the stack limit);
//   - when the spot runs out or breaks early (sPop's own count and early-break roll).
// The plugin rolls the item before using up the gather (the game does it the other way round), so stopping for a
// carried item costs nothing. Every item still gets its own roll.
//
// Messages: the pouch posts an "obtained" line per item (MESSAGE, kind 0, message 9). While a spot is gathered they
// are held back in order, with the lines of the same item merged into one with the summed quantity ("Iron Ore x3").
// They are posted when the spot is done. Other messages (pouch full...) keep their place in the queue.

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
    if (kind == 0 && id == MSG_OBTAINED) {
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
    g_holding = on && g_merge && g_message_hook.original;
    g_queued = 0;
}

static void post_messages(void) {
    g_holding = false;
    for (u32 i = 0; i < g_queued; i++)
        ORIGINAL(Message, g_message_hook)(g_queue[i].kind, g_queue[i].id, g_queue[i].item, g_queue[i].quantity,
                                          g_queue[i].extra);
    g_queued = 0;
}

static u32 find_pop(u32 spop, u32 id) {
    for (u32 pop = *(u32 *)(spop + SPOP_ACTIVE); pop; pop = *(u32 *)(pop + POP_NEXT))
        if (*(u16 *)(pop + POP_ID) == id) return pop;
    return 0;
}

static bool pouch_took_it(void) { return g_add_result != ADD_NONE && g_add_result <= ADD_TAKEN_MAX; }

static bool carrying(u32 hunter) { return (*(u32 *)(hunter + HUNTER_STATE) & STATE_CARRYING) != 0; }

// Gathers the rest of the spot after the game's own gather. Returns why it stopped (shown in Forge's menu).
static const char *gather_rest(u32 hunter, u32 notify, u32 *extra) {
    u32 spop = GLOBAL(GOT_SPOP), items = GLOBAL(GOT_ITEMS);
    s16 sid = *(s16 *)(hunter + HUNTER_POP_ID);
    if (!spop || sid < 0) return "no spot";
    u32 id = (u16)sid;
    for (*extra = 0; *extra < MAX_EXTRA;) {
        u32 pop = find_pop(spop, id);
        if (!pop) return "spot gone";
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
        if (!pouch_took_it()) return "pouch refused";
        if (left == 2) return "used up";
    }
    return "limit";
}

static u32 gather_hook(u32 hunter, u32 empty_if_none, u32 notify) {
    bool on = g_gather_all && hunter && FN(Is, IS_LOCAL)(hunter) == 1;
    g_add_result = ADD_NONE;
    hold_messages(on);
    u32 r = ORIGINAL(Gather, g_gather_hook)(hunter, empty_if_none, notify);
    // (when the first item didn't fit, or became a carried item, the press ends as usual)
    if (on && r == 1 && pouch_took_it()) {
        u32 extra = 0;
        g_last_stop = gather_rest(hunter, notify, &extra);
        g_last_extra = extra;
        g_total += extra;
        if (extra)
            forge_log(LOG_INFO, "field-qol: %u more from spot %d (%s)", extra, *(s16 *)(hunter + HUNTER_POP_ID),
                      g_last_stop);
    }
    post_messages();
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
// and REWARD_UPDATE places the highlight bar at the cursor's row times the row spacing. tools/make_arcs.py adds a
// fourth row to the layout (instance 89) and makes the window taller. The plugin puts "Send all..." on the third row
// and moves the game's third entry to the fourth.
//
// The game's cursor keeps its three rows. "Send all" is an extra row between Send (1) and the third (2) that only the
// plugin knows about: it is entered by moving down from Send or up from the third row, while the game's cursor stays
// on Send. Pressing A there sends every slot of the section. When that empties the section, the cursor goes to the
// game's third row (Done, by then) and the game gets the A, so the section ends as if Done was chosen. When the box
// fills up first, the menu stays with what is left, and the game never sees the A.
//
// Sending plays a sound per slot. The game's sound manager starts each one on its own voice, so several in one frame
// add up to a louder sound: during a Send all, each sound plays only once.

static const char SEND_ALL_TEXT[] = "Send all to Item Box & Exit";
static u32 g_menu_uio;     // the menu showing the added row (0: none)
static u32 g_result_uio;   // the reward screen's menu
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

static bool has_fourth_row(u32 uio) {
    u32 gui = *(u32 *)(uio + UIO_GUI);
    return gui && *(u32 *)(gui + GUI_INSTANCES) > ROW_FOURTH; // (the layout from tools/make_arcs.py is loaded)
}

// The row the player sees highlighted: 0 Take, 1 Send, 2 Send all, 3 the game's third (Sell or Done)
static u32 shown_row(u32 game_row) { return g_on_send_all ? 2 : game_row == 2 ? 3 : game_row; }

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

static u32 section_hook(u32 ui, u32 section) {
    // the reward screen's menu: known from its first frame (the rows are drawn again to show the added one)
    u32 uio = FN(Component, UI_COMPONENT)(ui, 0);
    if (uio && uio != g_result_uio) {
        g_result_uio = uio;
        *(u8 *)(ui + RESULT_REDRAW) = 1;
    }

    u8 *cursor = (u8 *)(ui + RESULT_CURSOR);
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
            g_on_send_all = false;
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

    // the cursor's moves, with the extra row between Send (1) and the third (2)
    u32 after = cursor[CURSOR_ROW];
    if (was & PAD_B) {
        g_on_send_all = false; // (the game moved its cursor to the third row)
    } else if (g_on_send_all) {
        if (before == 1 && after == 0) { // up: back to Send
            cursor[CURSOR_ROW] = 1;
            g_on_send_all = false;
        } else if (after != before) {    // down: the game is on its third row already
            g_on_send_all = false;
        }
    } else if (before == 1 && after == 2) { // down from Send: stop on Send all
        cursor[CURSOR_ROW] = 1;
        g_on_send_all = true;
    } else if (before == 2 && after == 1) { // up from the third row: stop on Send all
        g_on_send_all = true;
    }
    return r;
}

// The rows: the added one on the reward screen's three-row menu with Send second
static void rows_hook(u32 uio, u32 list, u32 flag) {
    ORIGINAL(Rows, g_rows_hook)(uio, list, flag);
    if (!has_fourth_row(uio)) return;
    u32 entries = *(u32 *)list, count = *(u32 *)(list + 4), mode = *(u32 *)(uio + UIO_MODE);
    bool ours = g_send_all && flag && entries && count == 3 && uio == g_result_uio &&
                mode < 32 && (RESULT_MODES >> mode & 1) &&
                *(u16 *)(entries + 12 + 6) == MSG_RESULT && *(u16 *)(entries + 12 + 8) == MSG_SEND;
    if (!ours) {
        if (g_menu_uio == uio) g_menu_uio = 0;
        FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_FOURTH, ROW_OBJECT, 0);
        return;
    }
    g_menu_uio = uio;
    const char *third = FN(EntryText, ENTRY_TEXT)(entries + 24);
    FN(SetText, UIO_SET_TEXT)(uio, ROW_FOURTH, ROW_OBJECT, third);
    FN(SetInt, UIO_SET_VISIBLE)(uio, ROW_FOURTH, ROW_OBJECT, 1);
    FN(SetText, UIO_SET_TEXT)(uio, ROW_THIRD, ROW_OBJECT, SEND_ALL_TEXT);
}

// The rows' looks (enabled, highlighted) for the four rows the player sees
static void looks_hook(u32 uio, u32 enabled, u32 row) {
    ORIGINAL(Looks, g_looks_hook)(uio, enabled, row);
    if (uio != g_menu_uio || *(u32 *)(enabled + 4) < 3) return;
    const u8 *on = *(const u8 **)enabled;
    u32 shown = shown_row(row);
    Look look = FN(Look, ROW_LOOK);
    SetInt set = FN(SetInt, UIO_SET_LOOK);
    set(uio, ROW_SEND, ROW_OBJECT, look(on[1], shown == 1, 0, 0));
    set(uio, ROW_THIRD, ROW_OBJECT, look(on[1], shown == 2, 0, 0)); // (Send all is open when Send is)
    set(uio, ROW_FOURTH, ROW_OBJECT, look(on[2], shown == 3, 0, 0));
}

// The highlight bar, on the row the player sees
static void reward_update_hook(u32 uio) {
    ORIGINAL(Method, g_reward_update_hook)(uio);
    if (uio != g_menu_uio) return;
    u32 gui = *(u32 *)(uio + UIO_GUI), cursor = *(u32 *)(uio + UIO_CURSOR);
    if (!gui || !cursor) return;
    float spacing = FN(GetY, GUI_GET_Y)(gui, INST_SPACING);
    FN(SetY, GUI_SET_Y)(gui, INST_HIGHLIGHT, spacing * (float)shown_row(*(u8 *)(cursor + CURSOR_ROW)));
    FN(Sync, CURSOR_SYNC)(cursor, uio, INST_HIGHLIGHT);
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

    // Gathering: the pouch results first, as the gather relies on them; without the message hook, a line per item
    bool pouch = hook(&g_add_hook, PLAYER_ADD, add_hook) & hook(&g_add_quest_hook, PLAYER_ADD_QUEST, add_quest_hook);
    if (!hook(&g_message_hook, MESSAGE, message_hook))
        forge_log(LOG_WARN, "field-qol: message hook FAILED, one line per gathered item");
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
    if (g_rewards_hooked && !hook(&g_sound_hook, UI_SOUND, sound_hook))
        forge_log(LOG_WARN, "field-qol: sound hook FAILED, Send all plays a sound per slot");
    forge_log(g_rewards_hooked ? LOG_INFO : LOG_WARN, "field-qol: %s",
              g_rewards_hooked ? "Send all to Item Box & Exit on the reward screen" : "a rewards hook FAILED, no Send all");
}

EXPORT void forge_onImGuiInit(void *ctx, void *alloc, void *freefn) {
    if (!ctx || !alloc || !freefn) return;
    igSetCurrentContext(ctx);
    igSetAllocatorFunctions(alloc, freefn, NULL);
}

static char *put(char *q, const char *s) { while (*s) *q++ = *s++; return q; }

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
    igTextUnformatted("Eggs and other carried items still take a press each.", NULL);
    igCheckbox("One message per item (\"Iron Ore x3\")", &g_merge);
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

    igTextUnformatted("Rewards", NULL);
    if (!g_rewards_hooked) igTextUnformatted("Hook failed: no Send all (see log.txt).", NULL);
    igCheckbox("\"Send all to Item Box & Exit\" on the reward screen", &g_send_all);

    igTreePop();
}
