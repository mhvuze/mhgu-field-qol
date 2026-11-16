/* Purple-lib API version 4, for C++ (and C) Forge plugins.
 * Copyright (c) 2026 PurplefinNeptuna, MIT license.
 *
 * Purple-lib puts other mods' settings in the game's own Options window, under
 * Options > Mod Options, and adds their topics to the game's help screens. Your plugin does not
 * link against it: it exports purplelib_on_register, which Purple-lib calls with a table of its
 * functions (example.cpp shows how), so it still loads and works when Purple-lib is not installed.
 * We recommend keeping Purple-lib optional: your mod should do its whole job without it, with its
 * settings in its own file and the Mod Options page as an easier way to change them.
 *
 * The rules, in short (Purple-lib's API.md has them in full):
 *
 * - Register inside your purplelib_on_register, the only time Purple-lib accepts it. It is called
 *   once, from Purple-lib's first forge_onUpdate, after every plugin's forge_onInit. The plugins'
 *   load order does not matter. When it has not been called by your second forge_onUpdate,
 *   Purple-lib is not installed or is older than API version 3.
 * - Register when the table's `version` is PURPLELIB_API_VERSION (4) or more. A later Purple-lib
 *   only adds entries at the end of the table and fields at the end of a record, and reads your
 *   record by its `size`, so this header keeps working with it.
 * - Each table entry is the Purple-lib function purplelib_ plus the entry's name: add_options is
 *   purplelib_add_options, which is the name the rules below and API.md use.
 * - The add functions copy your record. They return false when they refuse it: a `size` below
 *   this header's, a missing required callback, a value out of range, a full registry (128
 *   option sets, 255 help topics, 255 page help topics), a call after registration closed, or a
 *   second set from the same PurpleLibOptions.
 * - Give each Mod Options page its own PurpleLibOptions and keep it where it is: its address is
 *   how purplelib_options_shown, purplelib_options_row and purplelib_add_set_help know the page.
 * - Every string a callback returns, except value_text's, must stay valid for as long as the game
 *   runs: the game's interface keeps the pointers. Use string literals or strings you never free.
 *   value_text's string is copied at once. Strings are UTF-8.
 * - `get` and `set` run inside the game's update: keep them short, with no file access, no locks
 *   and no logging. `closed`, `action` and `open` run from Purple-lib's forge_onUpdate.
 * - While your own window is open, call purplelib_hold_input every frame: the game then gets no
 *   controller input, and your window, which reads Forge's input, still does. When the calls stop,
 *   the game gets its input back once every button is up.
 * - purplelib_options_shown(&OPTIONS) says whether the Options window is showing your page right
 *   now, and purplelib_options_row(&OPTIONS) which row the cursor is on (-1 when not shown). Pass
 *   the same record you registered. Ask every frame from your ImGui callback to draw something
 *   beside the page, such as a preview. Stop drawing it when shown turns false: the player switched
 *   to another mod's page or closed the window. purplelib_help_shown(&OPTIONS) is true while your
 *   page's own help screen covers it: hide the preview then too.
 * - purplelib_add_set_help(&OPTIONS, &topic) gives your page its own help screen. While the page is
 *   on screen, the game's help button opens it: (−), or (+) if the player swapped the two. It is
 *   titled with your page's title and holds only your topics. Call it after purplelib_add_options,
 *   with category 0.
 * - Read your own window's buttons with Forge's input (forge_input_isDown in forge/input.h), not
 *   with ImGui's controller navigation: on MHGURecomp, ImGui gets no controller input while its
 *   overlay (its settings or the Forge menu) is closed, and Forge's input works on every host.
 * - Leave Y out of your window's controls. Original Forge (Switch, Eden, Ryujinx) feeds the
 *   controller to ImGui every frame, and Y is ImGui's menu key: a tap toggles the menu layer,
 *   holding it opens ImGui's window switcher, where L and R move the focus to another window.
 *
 * Limits:
 *
 * - Up to 255 rows a set, 8 to a page.
 * - A labels, numbers or text row has 2 to 65535 values.
 * - About 11 columns a value, a Chinese character counting two: the game's own values are at most
 *   that wide. A value's text is cut at 63 bytes.
 * - Help topics go in help screens 1 to 18 (API.md lists them), up to 16 pages each, five lines a
 *   page and about 40 columns a line.
 * - 128 option sets, 255 help topics and 255 page help topics from all mods together, and 255
 *   topics a help screen with the game's own. Topics past that are left out, the first registered
 *   kept. Purple-lib r17 holds 128 of each kind of topic and 64 a screen or page.
 */
#ifndef PURPLELIB_H
#define PURPLELIB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PURPLELIB_API_VERSION 4u

/* Row kinds (PurpleLibOptionRow.kind). In every kind a value is 0-based, below the row's value
 * count. A labels row shows labels[value]; a numbers row shows first + value * step, then suffix;
 * a text row shows what PurpleLibOptions.value_text returns for the value. */
enum {
    PURPLELIB_ROW_LABELS = 0,
    PURPLELIB_ROW_NUMBERS = 1,
    PURPLELIB_ROW_TEXT = 2,
    /* A row that calls PurpleLibOptions.action when the player presses A on it; the Options window
     * stays open. It has no value: `get` and `set` are never called for it. */
    PURPLELIB_ROW_ACTION = 3,
};

/* PurpleLibOptionRow.flags: stop at the first and last value instead of wrapping around. */
enum { PURPLELIB_ROW_NO_WRAP = 1 };

/* The game's language byte, as the callbacks receive it. Any other value is English. Return your
 * English text for any language you do not translate: Purple-lib shows whatever you return. */
enum {
    PURPLELIB_FRENCH = 2,
    PURPLELIB_SPANISH = 3,
    PURPLELIB_GERMAN = 4,
    PURPLELIB_ITALIAN = 5,
    PURPLELIB_CHINESE_TRADITIONAL = 7,
    PURPLELIB_CHINESE_SIMPLIFIED = 8,
    /* A Korean patch over the game's English text (Purple-lib r21 and later; English before). */
    PURPLELIB_KOREAN = 6,
};

/* One row of your Mod Options page, filled by your `row` callback. Purple-lib zeroes it before
 * each call, so fields you leave alone are 0: a row that sets only the first four fields is a
 * labels row that wraps. */
typedef struct {
    /* The row's name, on the left. */
    const char *name;
    /* The help-bar line while the cursor is on the row, or NULL for none. */
    const char *help;
    /* Labels rows: label_count strings, one per value. Action rows: one string, the text on the
     * right (such as "Open"), or none. */
    const char *const *labels;
    /* Labels rows: 2..65535, the same at every call. Action rows: 0 or 1. */
    uint32_t label_count;
    /* PURPLELIB_ROW_LABELS (0), PURPLELIB_ROW_NUMBERS, PURPLELIB_ROW_TEXT or PURPLELIB_ROW_ACTION. */
    uint32_t kind;
    /* Numbers and text rows: 2..65535 values, the same at every call. */
    uint32_t value_count;
    /* Numbers rows: the number value 0 shows. */
    int32_t first;
    /* Numbers rows: added for each value, not 0 (negative counts down). */
    int32_t step;
    /* Numbers rows: text after the number, such as "%", or NULL. */
    const char *suffix;
    /* PURPLELIB_ROW_NO_WRAP or 0, the same at every call; 0 for action rows. */
    uint32_t flags;
} PurpleLibOptionRow;

/* Your Mod Options page set. The window shows eight rows a page; Mod Options lists every
 * registered set by its title, and A on that row opens it. */
typedef struct {
    /* sizeof(PurpleLibOptions): 40 on the Switch. */
    uint32_t size;
    /* Passed back unchanged to every callback. */
    void *context;
    /* The window title and the set's row in the list, usually your mod's name. */
    const char *(*title)(void *context, uint32_t language);
    /* 1..255 rows, fixed at registration. */
    uint32_t row_count;
    /* Describe row `index`; return false only for an index you do not have. */
    bool (*row)(void *context, uint32_t language, uint32_t index, PurpleLibOptionRow *out);
    /* The row's current value, below its label or value count. Runs inside the game's update. */
    uint32_t (*get)(void *context, uint32_t index);
    /* Apply a value at once. Called for every step while the player changes the row, held steps
     * included, and with the old value when they cancel with B; there is no confirm step. Runs
     * inside the game's update. */
    void (*set)(void *context, uint32_t index, uint32_t value);
    /* Optional: called after the window closes, only when `set` ran while it was open. Runs from
     * Purple-lib's forge_onUpdate: save your settings here, or flag your own update to. */
    void (*closed)(void *context);
    /* Required when a row is PURPLELIB_ROW_TEXT: the text of `value` in row `index`. Purple-lib
     * copies it at once, so it need only stay valid until your next call. Purple-lib's first
     * update asks for each text row's first and last value and drops the set if either is NULL. */
    const char *(*value_text)(void *context, uint32_t language, uint32_t index, uint32_t value);
    /* Required when a row is PURPLELIB_ROW_ACTION: the player pressed A on row `index`. Runs from
     * Purple-lib's forge_onUpdate. */
    void (*action)(void *context, uint32_t index);
} PurpleLibOptions;

/* A help topic. In one of the game's help screens it comes after the game's own topics. Its pages
 * turn with Left and Right like the game's. */
typedef struct {
    /* sizeof(PurpleLibHelp): 28 on the Switch. */
    uint32_t size;
    /* Passed back unchanged to every callback. */
    void *context;
    /* The help screen, 1..18: 18 is the Help in the village and the hub, 5 the Smithy's, 16 the
     * Courier's (API.md lists all). 0, the touch panels' own help, is refused; for
     * purplelib_add_set_help it must be 0. */
    uint32_t category;
    /* 1..16 pages. */
    uint32_t page_count;
    /* The topic's name in the topic list and the page title. */
    const char *(*title)(void *context, uint32_t language);
    /* One page: up to five lines joined with "\r\n", about 40 columns a line. */
    const char *(*page)(void *context, uint32_t language, uint32_t index);
    /* Optional: when set, choosing the topic closes the help and calls this instead of showing
     * pages, for a mod with its own window. Runs from Purple-lib's forge_onUpdate. */
    void (*open)(void *context);
} PurpleLibHelp;

/* Purple-lib's functions, as it passes them to your purplelib_on_register. The table stays valid
 * for as long as the game runs. */
typedef struct {
    /* The API version of the Purple-lib that is installed: 4 or more once you registered. */
    uint32_t version;
    /* Copies your page set; false when it refuses the record. */
    bool (*add_options)(const PurpleLibOptions *options);
    /* Copies a help topic for one of the game's help screens; false when it refuses the record. */
    bool (*add_help)(const PurpleLibHelp *help);
    /* Call every frame your own window is open. */
    void (*hold_input)(void);
    /* Whether the Options window shows the page registered with this record now. */
    bool (*options_shown)(const PurpleLibOptions *options);
    /* The cursor's row on that page, or -1 when it is not on screen. */
    int32_t (*options_row)(const PurpleLibOptions *options);
    /* Copies a topic of that page's own help screen; false when it refuses the record. */
    bool (*add_set_help)(const PurpleLibOptions *options, const PurpleLibHelp *help);
    /* Version 4: whether that page's own help screen is open now. */
    bool (*help_shown)(const PurpleLibOptions *options);
} PurpleLibApi;

/* The records' sizes on the Switch, where pointers are 4 bytes. */
#if defined(__arm__) && defined(__cplusplus)
static_assert(sizeof(PurpleLibOptions) == 40, "Purple-lib API 3 record size");
static_assert(sizeof(PurpleLibOptionRow) == 40, "Purple-lib API 3 record size");
static_assert(sizeof(PurpleLibHelp) == 28, "Purple-lib API 3 record size");
static_assert(sizeof(PurpleLibApi) == 32, "Purple-lib API 4 table size");
#elif defined(__arm__)
_Static_assert(sizeof(PurpleLibOptions) == 40, "Purple-lib API 3 record size");
_Static_assert(sizeof(PurpleLibOptionRow) == 40, "Purple-lib API 3 record size");
_Static_assert(sizeof(PurpleLibHelp) == 28, "Purple-lib API 3 record size");
_Static_assert(sizeof(PurpleLibApi) == 32, "Purple-lib API 4 table size");
#endif

/* Your plugin defines and exports this function; Purple-lib finds it by name in your plugin's
 * symbol table and calls it once, from its first forge_onUpdate. Add the name to the symbols your
 * plugin's linker script exports, beside forge_onInit. */
void purplelib_on_register(const PurpleLibApi *api);

#ifdef __cplusplus
}
#endif

#endif
