/* Copy of src/android/HyperspaceHost.h from Hyperspace (the commit in hyperspace/source.sh,
   the iPad game's Hyperspace; the hs_android_ names are its). Keep the two in step; HS_HOST_VERSION guards against mismatches. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HS_HOST_VERSION 1

typedef struct hs_host_info
{
    uint32_t version;           /* HS_HOST_VERSION */
    uint32_t size;              /* sizeof(hs_host_info) */

    /* The game: its Mach-O header as mapped, how far it moved from its link-time
       addresses (runtime = link-time + slide), and how many bytes are mapped from the
       header on. Hook trampolines are placed next to this range. */
    const void *mach_header;
    intptr_t slide;
    size_t image_size;

    /* zhl.log and FTL_HS.log go to log_dir; data_dir becomes the working directory,
       for files Hyperspace keeps next to the game on PC (settings_hs.ini). */
    const char *log_dir;
    const char *data_dir;

    /* The port's log (logcat and the session log). level: 0 info, 1 warning, 2 error */
    void (*log)(int level, const char *message);

    /* Unwind info for an address in the game, for the Mach-O aware libunwind both
       sides are built with. Returns 0 when pc is not in the game. */
    int (*find_macho_unwind)(uintptr_t pc, uintptr_t *mh, uintptr_t *text_len, uintptr_t *compact_unwind,
                             size_t *compact_unwind_len, uintptr_t *eh_frame, size_t *eh_frame_len);

    /* Optional (hosts may pass a smaller size without them, and NULL to leave it out):
       memory the port keeps free next to the game for the hook trampolines. The Android
       app passes it; the Switch gives alloc_code below instead. */
    void *hook_area;
    size_t hook_area_size;

    /* Optional: where code memory comes
       from on hosts that can't map or reprotect it themselves (the Switch). alloc_code
       returns size bytes of executable memory within +-128MB of the game, and
       write_code writes n bytes of code at dst (game or alloc_code memory) and makes
       them visible to instruction fetch, returning 0 on success. */
    void *(*alloc_code)(size_t size);
    int (*write_code)(void *dst, const void *src, size_t n);

    /* Optional: shows an error the way the PC builds show a message box (a mod's
       Hyperspace.ErrorMessage from Lua, a broken hyperspace.xml) and returns once the
       player closed it, the game waiting meanwhile as on PC. Called on the game's
       thread once Hyperspace is running; without it, errors only go to the log. */
    void (*show_error)(const char *title, const char *message);
} hs_host_info;

/* The size of the interface before the optional fields */
#define HS_HOST_INFO_SIZE_V1 offsetof(hs_host_info, hook_area)

/* Returns 0 once Hyperspace's hooks are in place. Otherwise writes the reason to
   error and returns nonzero; the game must not be started then, since some hooks may
   already be installed. */
int hs_android_init(const hs_host_info *info, char *error, size_t error_size);
typedef int (*hs_android_init_fn)(const hs_host_info *info, char *error, size_t error_size);

/* Right clicks for mods' scripts, which the port makes from a gesture (one finger held,
   a tap with another) and from a mouse's right button. Looked up with dlsym, so a
   library without them leaves both off. Both are called on the port's input thread:
   hs_android_wants_right_click says whether a script listened for the right button in
   the last frame, and hs_android_right_button queues the button going down, the cursor
   moving while it's held, or the button coming up, for the game's thread (x and y as
   the game's touch handlers get them). */
enum { HS_RIGHT_BUTTON_DOWN = 0, HS_RIGHT_BUTTON_MOVE = 1, HS_RIGHT_BUTTON_UP = 2 };
int hs_android_wants_right_click(void);
void hs_android_right_button(int action, int x, int y);
typedef int (*hs_android_wants_right_click_fn)(void);
typedef void (*hs_android_right_button_fn)(int action, int x, int y);

#ifdef __cplusplus
}
#endif
