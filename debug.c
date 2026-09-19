#include <furi_hal_version.h>
#include <string.h>

#include "debug.h"

#define DEBUG_DEVICE_NAME "Pelavicr"

bool is_debug_device(void) {
    return strcmp(furi_hal_version_get_name_ptr(), DEBUG_DEVICE_NAME) == 0;
}

// Left, Down, Right, Down, Left - entered as plain presses while a shift is active.
static const InputKey debug_combo[] = {
    InputKeyLeft,
    InputKeyDown,
    InputKeyRight,
    InputKeyDown,
    InputKeyLeft,
};
#define DEBUG_COMBO_LENGTH (sizeof(debug_combo) / sizeof(debug_combo[0]))

static InputKey debug_combo_buffer[DEBUG_COMBO_LENGTH];
static bool debug_mode_unlocked = false;

bool is_debug_mode_active(void) {
    return is_debug_device() && debug_mode_unlocked;
}

bool debug_feed_combo_key(InputKey key) {
    if(!is_debug_device()) return false; // the combo means nothing anywhere else

    memmove(
        &debug_combo_buffer[0],
        &debug_combo_buffer[1],
        (DEBUG_COMBO_LENGTH - 1) * sizeof(InputKey));
    debug_combo_buffer[DEBUG_COMBO_LENGTH - 1] = key;

    if(memcmp(debug_combo_buffer, debug_combo, sizeof(debug_combo)) == 0) {
        debug_mode_unlocked = true;
        return true;
    }
    return false;
}

#define DEBUG_TIME_TRAVEL_SECONDS 3600

// Shifts a wall-clock (seconds-of-day) value by delta, wrapping around midnight.
static uint32_t wrap_add_seconds(uint32_t base, int32_t delta) {
    int32_t result = ((int32_t)base + delta) % 86400;
    if(result < 0) result += 86400;
    return (uint32_t)result;
}

void debug_time_travel(AppData* app) {
    // Simulates the shift genuinely having started an hour earlier and having been worked
    // continuously since then: the start point moves back, and that hour is credited as worked
    // time too, pulling the predicted finish earlier. Every already-logged break, plus the live
    // one if a break is in progress, shifts by the same amount so the whole timeline - including
    // its breaks - stays internally consistent.
    int32_t delta = -DEBUG_TIME_TRAVEL_SECONDS;

    for(uint8_t i = 0; i < app->break_count; i++) {
        app->breaks[i].start_wallclock_secs =
            wrap_add_seconds(app->breaks[i].start_wallclock_secs, delta);
        app->breaks[i].end_wallclock_secs =
            wrap_add_seconds(app->breaks[i].end_wallclock_secs, delta);
    }

    if(!app->running) {
        // A break is currently live - move its bookkeeping too (both the wall-clock anchor used
        // to log it once it ends, and the monotonic one used for the sub-minute fold-in check)
        app->pause_start_wallclock = wrap_add_seconds(app->pause_start_wallclock, delta);
        app->pause_start_tick += (uint32_t)(delta * 1000);
    }

    app->elapsed_seconds = (uint32_t)((int32_t)app->elapsed_seconds - delta);
    app->start_wallclock_secs = wrap_add_seconds(app->start_wallclock_secs, delta);
}
