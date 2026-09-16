#pragma once

#include "app_data.h"

// Hidden debug feature, gated to one specific device by its user-set name so it can never
// activate by accident on anyone else's Flipper.
bool is_debug_device(void);

// Debug-only: simulate the shift (and its logged breaks) having started an hour earlier, with
// that hour credited as worked time too. Only reachable via is_debug_device().
void debug_time_travel(AppData* app);
