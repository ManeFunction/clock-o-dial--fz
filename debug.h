#pragma once

#include <input/input.h>

#include "app_data.h"

// Hidden debug feature, gated to one specific device by its user-set name so it can never
// activate by accident on anyone else's Flipper.
bool is_debug_device(void);

// True once the secret button combo has been entered this session, on the debug device. Debug
// features stay unreachable - even on the debug device - until this unlocks.
bool is_debug_mode_active(void);

// Feed one button press (while a shift is active) into the combo tracker. Returns true exactly
// once: the moment the combo completes correctly, and only on the debug device. Does nothing
// (and always returns false) on any other device.
bool debug_feed_combo_key(InputKey key);

// Debug-only: simulate the shift (and its logged breaks) having started an hour earlier, with
// that hour credited as worked time too. Only reachable via is_debug_mode_active().
void debug_time_travel(AppData* app);
