// Mods on the console (mods_switch.c): ftl.nro applies changed mods at startup,
// and its mod list (L on the start screen) switches them on/off and orders them.
#pragma once
#include <stdbool.h>

#define SWITCH_MAX_MODS 256

typedef struct {
    char name[256];
    bool on;
} switch_mod;

// The mods in /switch/ftl/mods/ in the order they're applied, each on or off:
// mods/order.txt has a file name per line, "-name" for a mod that's off; mods it
// doesn't name are on and follow by name. Hyperspace comes first when it's on.
int switch_mods_list(switch_mod *mods, int max);
// Writes that order and state to mods/order.txt. 0 on success.
int switch_mods_save(const switch_mod *mods, int n);

// Applies the mods that are on (ftlman, from ftl.dat.vanilla), or puts the
// original ftl.dat back when none is, and records what was applied. print gets
// progress lines. Returns 0 on success; on failure the original is back and err
// says why.
int switch_mods_apply(const switch_mod *mods, int n, void (*print)(const char *line), char *err, int errlen);
// Whether the mods that are on differ from what was applied last.
bool switch_mods_changed(const switch_mod *mods, int n);
// The mod list on the console: on/off, order, all off; saves mods/order.txt.
// Returns whether anything changed (startup then applies it as usual).
bool switch_mods_menu(void);
