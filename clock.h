#pragma once
#include <furi.h>
#include <gui/gui.h>

#define OFS_LEFT_X          31
#define FACE_RADIUS         31
#define CLOCK_HOURS         12 // Dial is always a real 12-hour clock face
#define MAX_TIMER_HOURS     24 // Max configurable shift length, in hours
#define DEFAULT_TIMER_HOURS 8

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

typedef struct {
    Line minutes[CLOCK_HOURS * 4]; // 4 minor ticks between each hour mark (every 5 minutes)
    Line hour_marks[CLOCK_HOURS];
} ClockFace;

#define CONFIG_VERSION 8
typedef struct {
    uint8_t version;
    uint8_t timer_duration_hours; // Timer duration in hours (1-24)
    bool fill_enabled; // Fill rendering enabled/disabled
} TimerConfig;

void calc_clock_face(ClockFace* face);
void draw_timer(
    Canvas* canvas,
    ClockFace* face,
    uint8_t timer_duration_hours,
    uint32_t elapsed_seconds,
    uint16_t ms,
    bool running,
    bool has_been_started,
    bool fill_enabled,
    uint32_t now_wallclock_secs,
    uint32_t start_wallclock_secs);

void init_timer_config(TimerConfig* cfg);
void modify_timer_up(TimerConfig* cfg);
void modify_timer_down(TimerConfig* cfg);
