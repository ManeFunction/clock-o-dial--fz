#include <math.h>

#include "clock.h"
#include "crock_o_dial_icons.h"

#ifndef M_TWOPI
#define M_TWOPI (2.0 * M_PI)
#endif
#define M_TWOPI_F ((float)(2.0 * M_PI))

#define OFS_LEFT_X  31
#define OFS_MID_X   63
#define OFS_RIGHT_X 96
#define OFS_Y       31

#define H_RAD   17
#define M_RAD   26
#define S_RAD   29
#define HMS_OFS 8

#define FACE_RADIUS        31
#define FACE_DEFAULT_WIDTH 54

void draw_line(Canvas* c, uint8_t ofs_x, Line* l, LineType type) { // Bresenham-Algorithm
    int8_t x = l->start.x, y = l->start.y;
    int8_t dx = abs(l->end.x - x), sx = x < l->end.x ? 1 : -1;
    int8_t dy = -abs(l->end.y - y), sy = y < l->end.y ? 1 : -1;
    int8_t error = dx + dy, e2;
    while(true) {
        if(type == Thick)
            canvas_draw_disc(c, ofs_x + x, OFS_Y - y, 1);
        else {
            canvas_draw_dot(c, ofs_x + x, OFS_Y - y);
            if(type & 1) canvas_draw_dot(c, ofs_x - x, OFS_Y - y); // copy hor or both
            if(type & 2) canvas_draw_dot(c, ofs_x + x, OFS_Y + y); // copy ver or both
            if(type == CopyBoth) canvas_draw_dot(c, ofs_x - x, OFS_Y + y);
        }
        if((x == l->end.x) && (y == l->end.y)) break;
        e2 = 2 * error;
        if(e2 > dy) x += sx, error += dy;
        if(e2 < dx) y += sy, error += dx;
    }
}

void set_point(Point* p, float ang, float radius) {
    p->x = (int8_t)round(sin((double)ang) * (double)radius);
    p->y = (int8_t)round(cos((double)ang) * (double)radius);
}

void intersect(Point* p, float ang, uint8_t width, uint8_t height) { // Quadrant I only
    double t = tan((double)ang);
    double x = (double)height * t;
    double y = (double)width / t;
    p->x = (int8_t)round(x > width ? width : x);
    p->y = (int8_t)round(y > height ? height : y);
}

void copy_point(Point* p, Point* from, bool flip_x, bool flip_y) {
    p->x = flip_x ? -from->x : from->x;
    p->y = flip_y ? -from->y : from->y;
}

void set_line(Line* l, float ang, float start_rad, float end_rad) {
    set_point(&l->start, ang, start_rad);
    set_point(&l->end, ang, end_rad);
}

void copy_line(Line* l, Line* from, float flip_x, float flip_y) {
    copy_point(&l->start, &from->start, flip_x, flip_y);
    copy_point(&l->end, &from->end, flip_x, flip_y);
}

void draw_hand(Canvas* canvas, uint8_t ofs_x, float ang, int radius, bool thick) {
    Line l;
    set_line(&l, ang, thick ? HMS_OFS : -HMS_OFS, radius);
    draw_line(canvas, ofs_x, &l, thick ? Thick : Normal);
    if(thick) {
        // Draw thin line from end back to center
        l.end.x = 0, l.end.y = 0;
        draw_line(canvas, ofs_x, &l, Normal);
        // Draw thin line at the end of the hand (extending just a few pixels beyond the bold part)
        set_line(&l, ang, radius, radius + 3);
        draw_line(canvas, ofs_x, &l, Normal);
    }
}

// Finds where a ray at angle `ang` (from center, clockwise from top) exits the square face.
static void square_intersect(float ang, uint8_t width, uint8_t height, float ofs, Point* out) {
    float dir_x = sinf(ang);
    float dir_y = cosf(ang);

    float scale_x = (fabsf(dir_x) > 0.001f) ? (float)width / fabsf(dir_x) : width + height;
    float scale_y = (fabsf(dir_y) > 0.001f) ? (float)height / fabsf(dir_y) : width + height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    scale -= ofs;
    if(scale < 0) scale = 0;

    float max_x = width - ofs;
    float max_y = height - ofs;
    float x = dir_x * scale;
    float y = dir_y * scale;
    if(x > max_x) x = max_x;
    if(x < -max_x) x = -max_x;
    if(y > max_y) y = max_y;
    if(y < -max_y) y = -max_y;

    out->x = (int8_t)roundf(x);
    out->y = (int8_t)roundf(y);
}

void calc_clock_face(ClockFace* face) {
    // Square face representing a real 12-hour clock (width = height = FACE_RADIUS)
    uint8_t width = FACE_RADIUS;
    uint8_t height = FACE_RADIUS;

    float short_ofs = 2.0;
    float long_ofs = 7.0;

    float hour_angle_step = (float)M_TWOPI / (float)CLOCK_HOURS;
    const uint8_t minor_ticks_per_hour = 4; // + the hour mark itself = 5 ticks/hour (every 5 min)
    float minute_angle_step = hour_angle_step / (float)(minor_ticks_per_hour + 1);

    uint8_t minute_mark_index = 0;
    for(uint8_t hour = 0; hour < CLOCK_HOURS; hour++) {
        float hour_ang = (float)hour * hour_angle_step;

        square_intersect(hour_ang, width, height, 0, &face->hour_marks[hour].start);
        square_intersect(hour_ang, width, height, long_ofs, &face->hour_marks[hour].end);

        for(uint8_t m = 1; m <= minor_ticks_per_hour; m++) {
            float min_ang = hour_ang + (float)m * minute_angle_step;
            square_intersect(min_ang, width, height, 0, &face->minutes[minute_mark_index].start);
            square_intersect(
                min_ang, width, height, short_ofs, &face->minutes[minute_mark_index].end);
            minute_mark_index++;
        }
    }
}

// Angle (radians, clockwise from top) for a point in time within a repeating 12-hour dial.
static float wallclock_angle(uint32_t seconds_of_day) {
    return fmodf((float)seconds_of_day, 12.0f * 3600.0f) / (12.0f * 3600.0f) * M_TWOPI_F;
}

// True if the forward sweep of `length` radians starting at `start` (clockwise) covers `angle`.
// A length >= 2*PI (an arc that has lapped the dial) covers every angle.
static bool angle_in_forward_arc(float angle, float start, float length) {
    if(length <= 0.0f) return false;
    float delta = fmodf(angle - start, M_TWOPI_F);
    if(delta < 0.0f) delta += M_TWOPI_F;
    return delta < length;
}

// Forward duration in seconds from `from` to `to`, both seconds-of-day, wrapping past midnight.
static uint32_t wallclock_span(uint32_t from, uint32_t to) {
    return to >= from ? to - from : to + 86400 - from;
}

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
    const UiOverlay* ui) {
    // Draw square 12-hour clock face on left side, real time always
    static char time_buf[10];

    uint32_t timer_duration_seconds = timer_duration_hours * 3600;
    uint32_t timer_duration_ms = timer_duration_seconds * 1000;

    // The hand always shows the real current time - one hand, no exceptions.
    float hand_angle = wallclock_angle(now_wallclock_secs);

    if(has_been_started) {
        // History span: everything from shift start to now, work and breaks both. Each logged
        // break gets carved out of it individually below, rather than showing one lumped gap.
        float history_start_angle = wallclock_angle(start_wallclock_secs);
        float history_length = (float)wallclock_span(start_wallclock_secs, now_wallclock_secs) /
                                (12.0f * 3600.0f) * M_TWOPI_F;

        // Predicted segment: from now, for however much work remains at the current pace.
        // While on a break this starts sliding forward with real time, which pushes the
        // predicted finish time forward too.
        uint32_t remaining_seconds =
            timer_duration_seconds > elapsed_seconds ? timer_duration_seconds - elapsed_seconds :
                                                        0;
        float predicted_start_angle = hand_angle;
        float predicted_length = (float)remaining_seconds / (12.0f * 3600.0f) * M_TWOPI_F;

        // Precompute every break's angle/length once per frame rather than per pixel
        float break_start_angles[MAX_BREAKS + 1];
        float break_lengths[MAX_BREAKS + 1];
        uint8_t break_angle_count = 0;
        for(uint8_t i = 0; i < break_log->count && break_angle_count < MAX_BREAKS + 1; i++) {
            break_start_angles[break_angle_count] =
                wallclock_angle(break_log->items[i].start_wallclock_secs);
            break_lengths[break_angle_count] =
                (float)wallclock_span(
                    break_log->items[i].start_wallclock_secs,
                    break_log->items[i].end_wallclock_secs) /
                (12.0f * 3600.0f) * M_TWOPI_F;
            break_angle_count++;
        }
        if(break_log->live_active && break_angle_count < MAX_BREAKS + 1) {
            break_start_angles[break_angle_count] =
                wallclock_angle(break_log->live_start_wallclock_secs);
            break_lengths[break_angle_count] =
                (float)wallclock_span(break_log->live_start_wallclock_secs, now_wallclock_secs) /
                (12.0f * 3600.0f) * M_TWOPI_F;
            break_angle_count++;
        }

        // Reduce fill area by a margin to avoid covering the tick marks
        uint8_t fill_margin = 5;
        int8_t width = FACE_RADIUS - fill_margin;
        int8_t height = FACE_RADIUS - fill_margin;

        for(int8_t y = -height; y <= height; y++) {
            for(int8_t x = -width; x <= width; x++) {
                if(abs(x) > width || abs(y) > height) continue;

                // Worked pattern: dense 45-degree checkerboard (half the pixels)
                bool worked_dither = ((x + y) & 1) == 0;
                if(!worked_dither) continue; // never eligible for either pattern

                float pixel_angle = atan2f((float)x, (float)y);
                if(pixel_angle < 0.0f) pixel_angle += M_TWOPI_F;

                if(angle_in_forward_arc(pixel_angle, history_start_angle, history_length)) {
                    bool in_break = false;
                    for(uint8_t bi = 0; bi < break_angle_count; bi++) {
                        if(angle_in_forward_arc(
                               pixel_angle, break_start_angles[bi], break_lengths[bi])) {
                            in_break = true;
                            break;
                        }
                    }
                    if(!in_break) canvas_draw_dot(canvas, OFS_LEFT_X + x, OFS_Y - y);
                } else if(angle_in_forward_arc(pixel_angle, predicted_start_angle, predicted_length)) {
                    // Predicted pattern: sparse grid dither (a quarter of the pixels) - lets more
                    // light through than the worked pattern, since this time hasn't happened yet.
                    bool predicted_dither = ((x & 1) == 0) && ((y & 1) == 0);
                    if(predicted_dither) canvas_draw_dot(canvas, OFS_LEFT_X + x, OFS_Y - y);
                }
            }
        }
    }

    // Draw minute marks (short ticks)
    for(uint8_t i = 0; i < CLOCK_HOURS * 4; i++) {
        draw_line(canvas, OFS_LEFT_X, &face->minutes[i], Normal);
    }

    // Draw hour marks (long ticks)
    for(uint8_t i = 0; i < CLOCK_HOURS; i++) {
        draw_line(canvas, OFS_LEFT_X, &face->hour_marks[i], Normal);
    }

    // Draw the single hand - always real time, never removed or duplicated
    draw_hand(canvas, OFS_LEFT_X, hand_angle, M_RAD, true);

    canvas_draw_disc(canvas, OFS_LEFT_X, OFS_Y, 2);

    // Draw digital timer and status on right side
    uint32_t total_elapsed_ms = elapsed_seconds * 1000 + ms;
    uint32_t remaining_ms =
        timer_duration_ms > total_elapsed_ms ? timer_duration_ms - total_elapsed_ms : 0;

    // Convert to hours and minutes (seconds aren't shown)
    uint32_t remaining_seconds = remaining_ms / 1000;
    uint8_t hours = remaining_seconds / 3600;
    uint8_t minutes = (remaining_seconds % 3600) / 60;

    // Format time string
    snprintf(time_buf, 10, "%u:%02u", hours, minutes);

    // Determine status
    bool is_finished = total_elapsed_ms >= timer_duration_ms;
    bool is_working = running && !is_finished;
    bool is_break = has_been_started && !running && !is_finished;
    const char* status;
    if(!has_been_started) {
        status = "Set shift";
    } else if(is_finished) {
        status = "Finished";
    } else if(running) {
        status = "Working";
    } else {
        status = "Break";
    }

    // Draw digital timer on right side (monospace font to prevent shifting)
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, OFS_RIGHT_X, OFS_Y - 5, AlignCenter, AlignCenter, time_buf);

    if(!has_been_started) {
        // Left/right arrows flank the shift-length readout, hinting at the controls that adjust
        // it. Positioned off a fixed reference width so they never move as the value changes.
        uint16_t half_w = canvas_string_width(canvas, "12:00") / 2;
        int32_t left_x = OFS_RIGHT_X - half_w - 6;
        int32_t right_x = OFS_RIGHT_X + half_w + 2;
        int32_t arrow_y = (OFS_Y - 5) - 3;
        canvas_draw_icon(canvas, left_x, arrow_y, &I_arrow_left);
        canvas_draw_icon(canvas, right_x, arrow_y, &I_arrow);
    }

    // Draw status below timer
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, OFS_RIGHT_X, OFS_Y + 8, AlignCenter, AlignCenter, status);

    // Both animations share a bottom edge regardless of their frame heights
    int32_t icon_bottom_y = OFS_Y + 15 + icon_get_height(&I_coffee_1);

    if(is_working) {
        // Alternate frames once a second - draw callback already ticks at ~1Hz, so no
        // separate animation timer is needed.
        const Icon* working_icon = (now_wallclock_secs % 2 == 0) ? &I_working_1 : &I_working_2;
        canvas_draw_icon(
            canvas,
            OFS_RIGHT_X - icon_get_width(working_icon) / 2,
            icon_bottom_y - icon_get_height(working_icon),
            working_icon);
    } else if(is_break) {
        // Alternate frames once a second - draw callback already ticks at ~1Hz, so no
        // separate animation timer is needed.
        const Icon* coffee_icon = (now_wallclock_secs % 2 == 0) ? &I_coffee_1 : &I_coffee_2;
        canvas_draw_icon(
            canvas,
            OFS_RIGHT_X - icon_get_width(coffee_icon) / 2,
            icon_bottom_y - icon_get_height(coffee_icon),
            coffee_icon);
    }

    if(ui->show_sound_icon) {
        const Icon* sound_icon = ui->sound_enabled ? &I_sound_on : &I_sound_off;
        canvas_draw_icon(canvas, 128 - 2 - icon_get_width(sound_icon), 2, sound_icon);
    }

    if(ui->show_backlight_icon) {
        const Icon* backlight_icon = ui->backlight_on ? &I_light_on : &I_light_off;
        int32_t bl_x = 128 - 2 - icon_get_width(backlight_icon);
        int32_t bl_y = 64 - 2 - icon_get_height(backlight_icon);
        canvas_draw_icon(canvas, bl_x, bl_y, backlight_icon);
    }

    if(ui->hold_active) {
        // Overlay the bottom-right quadrant with a hold-to-confirm progress bar
        float frac = ui->hold_fraction;
        if(frac < 0.0f) frac = 0.0f;
        if(frac > 1.0f) frac = 1.0f;
        float eased = 0.5f * (1.0f - cosf(frac * (float)M_PI)); // ease-in-out sine

        const uint8_t bar_y = 62;
        const uint8_t bar_margin = 2;
        int32_t bar_x0 = OFS_MID_X + 1 + bar_margin;
        int32_t bar_x1 = 128 - bar_margin;
        int32_t bar_width = bar_x1 - bar_x0;

        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, OFS_MID_X + 1, 44, 128 - (OFS_MID_X + 1), 64 - 44);
        canvas_set_color(canvas, ColorBlack);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, OFS_RIGHT_X, bar_y - 8, AlignCenter, AlignCenter, ui->hold_label);
        canvas_draw_box(canvas, bar_x0, bar_y, (int32_t)(bar_width * eased), 2);
    }
}

void init_timer_config(TimerConfig* cfg) {
    cfg->version = CONFIG_VERSION;
    cfg->timer_duration_hours = DEFAULT_TIMER_HOURS; // Default 8 hours
    cfg->sound_enabled = true; // Sound enabled by default
    cfg->backlight_on = true; // Backlight enforced on by default
}

void modify_timer_up(TimerConfig* cfg) {
    if(cfg->timer_duration_hours < MAX_TIMER_HOURS) {
        cfg->timer_duration_hours++;
    }
}

void modify_timer_down(TimerConfig* cfg) {
    if(cfg->timer_duration_hours > 1) {
        cfg->timer_duration_hours--;
    }
}
