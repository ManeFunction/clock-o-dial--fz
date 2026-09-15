#pragma once
#include <furi.h>
#include <gui/gui.h>

#define OFS_LEFT_X          31
#define FACE_RADIUS         31
#define MAX_TIMER_HOURS     24
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
    Line minutes[60];
    Line hour_marks[MAX_TIMER_HOURS]; // Variable hour marks (1-24 hours)
    Point hours[MAX_TIMER_HOURS]; // Variable hour label positions
} ClockFace;

#define CONFIG_VERSION 8
typedef struct {
    uint8_t version;
    uint8_t timer_duration_hours; // Timer duration in hours (1-24)
    bool fill_enabled; // Fill rendering enabled/disabled
} TimerConfig;

void calc_clock_face(TimerConfig* cfg, ClockFace* face);
void draw_timer(
    Canvas* canvas,
    ClockFace* face,
    uint8_t timer_duration_hours,
    uint32_t elapsed_seconds,
    uint16_t ms,
    bool running,
    bool has_been_started,
    bool fill_enabled);

void init_timer_config(TimerConfig* cfg);
void modify_timer_up(TimerConfig* cfg);
void modify_timer_down(TimerConfig* cfg);
