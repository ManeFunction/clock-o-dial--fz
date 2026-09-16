#pragma once
#include <furi.h>
#include <gui/gui.h>

#define OFS_LEFT_X          31
#define FACE_RADIUS         31
#define CLOCK_HOURS         12 // Dial is always a real 12-hour clock face
#define MAX_TIMER_HOURS     12 // Max configurable shift length, in hours
#define DEFAULT_TIMER_HOURS 8

// How long the OK/Back button must be held before it triggers reset/close. Closing from Set
// mode (nothing running yet, so nothing to lose) only needs half as long a hold.
#define HOLD_CONFIRM_MS 3000
// Hold overlay (progress bar + label) only appears once the hold has run this long
#define HOLD_SHOW_MS 300
// How long the sound-on and backlight icons flash after their state changes
#define ICON_FLASH_MS 5000
// Breaks shorter than this are folded into worked time instead of leaving a visible gap
#define BREAK_FOLD_MS 60000
// Max number of individually-logged breaks per shift; further breaks past this just aren't logged
#define MAX_BREAKS 16

// Segment fill geometry: a square inset from the dial's edge, sized so marks stay uncovered.
#define FILL_MARGIN    5
#define FILL_HALF_SIZE (FACE_RADIUS - FILL_MARGIN)
#define FILL_GRID_SIDE (2 * FILL_HALF_SIZE + 1)
// Half the pixels in the fill square pass the worked-pattern's dither (only those are stored)
#define FILL_PIXEL_CAPACITY ((FILL_GRID_SIDE * FILL_GRID_SIDE + 1) / 2)

typedef enum {
    Normal = 0,
    CopyHor,
    CopyVer,
    CopyBoth,
    Thick
} LineType;

typedef struct {
    int8_t x;
    int8_t y;
} Point;

typedef struct {
    Point start;
    Point end;
} Line;

// A pixel eligible for the worked-segment dither, with its angle on the dial precomputed once
// at startup instead of every frame - only its arc containment needs checking per redraw.
typedef struct {
    int8_t x;
    int8_t y;
    float angle;
} FillPixel;

typedef struct {
    Line minutes[CLOCK_HOURS * 4]; // 4 minor ticks between each hour mark (every 5 minutes)
    Line hour_marks[CLOCK_HOURS];
    FillPixel fill_pixels[FILL_PIXEL_CAPACITY];
    uint16_t fill_pixel_count;
} ClockFace;

#define CONFIG_VERSION 10
typedef struct {
    uint8_t version;
    uint8_t timer_duration_hours; // Shift duration in hours (1-12)
    bool sound_enabled; // Hour chime + finish melody on/off
    bool backlight_on; // Manual backlight toggle, available in every mode
} TimerConfig;

// Chrome/overlay state that isn't part of the clock's own timekeeping, prepared by the app
// and handed to the renderer each frame.
typedef struct {
    bool sound_enabled;
    bool show_sound_icon; // sound-off shows always while muted; sound-on flashes briefly
    bool backlight_on;
    bool show_backlight_icon; // flashes briefly after a backlight toggle, either state
    bool hold_active; // a qualifying reset/close hold is in progress, past HOLD_SHOW_MS
    float hold_fraction; // 0..1 linear progress toward HOLD_CONFIRM_MS (eased at draw time)
    const char* hold_label; // "RESETTING" or "CLOSING"
} UiOverlay;

// A single completed break, logged only once it's run at least BREAK_FOLD_MS.
typedef struct {
    uint32_t start_wallclock_secs;
    uint32_t end_wallclock_secs;
} BreakInterval;

// The break history for the current shift, handed to the renderer so it can carve each break
// out of the worked arc individually instead of showing one lumped trailing gap.
typedef struct {
    const BreakInterval* items;
    uint8_t count;
    bool live_active; // true if currently on a break that hasn't been logged yet (still ongoing)
    uint32_t live_start_wallclock_secs; // valid only when live_active
} BreakLog;

void calc_clock_face(ClockFace* face);
void draw_timer(
    Canvas* canvas,
    ClockFace* face,
    uint8_t timer_duration_hours,
    uint32_t elapsed_seconds,
    uint16_t ms,
    bool running,
    bool has_been_started,
    uint32_t now_wallclock_secs,
    uint32_t start_wallclock_secs,
    const BreakLog* break_log,
    const UiOverlay* ui);

void init_timer_config(TimerConfig* cfg);
void modify_timer_up(TimerConfig* cfg);
void modify_timer_down(TimerConfig* cfg);
