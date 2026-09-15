#include <math.h>

#include "clock.h"

#ifndef M_TWOPI
#define M_TWOPI (2.0 * M_PI)
#endif
#define M_TWOPI_F ((float)(2.0 * M_PI))
#define M_PI_2_F  ((float)(M_PI / 2.0))

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

void calc_clock_face(TimerConfig* cfg, ClockFace* face) {
    // Square face (width = height = FACE_RADIUS)
    uint8_t width = FACE_RADIUS;
    uint8_t height = FACE_RADIUS;

    float short_ofs = 2.0;
    float long_ofs = 7.0;
    float hour_ofs = 12.5;

    uint8_t num_hours = cfg->timer_duration_hours;
    if(num_hours < 1) num_hours = 1;
    if(num_hours > MAX_TIMER_HOURS) num_hours = MAX_TIMER_HOURS;

    // Create hour marks based on timer duration (every M_TWOPI / num_hours degrees)
    // Start at top (0 degrees) and go clockwise
    float hour_angle_step = (float)M_TWOPI / (float)num_hours;

    // Create hour mark lines (long marks) and label positions
    for(uint8_t hour = 0; hour < num_hours; hour++) {
        float ang = (float)hour * hour_angle_step;

        // Normalize angle to [0, 2π)
        while(ang >= (float)M_TWOPI)
            ang -= (float)M_TWOPI;
        while(ang < 0.0f)
            ang += (float)M_TWOPI;

        // Handle special cases for cardinal directions
        // Use a wider tolerance to catch angles that are exactly 90°, 180°, 270° when divisible by 4
        bool is_top = (ang < 0.02f || ang > (float)M_TWOPI - 0.02f);
        bool is_right = (ang > (float)M_PI_2 - 0.05f && ang < (float)M_PI_2 + 0.05f);
        bool is_bottom = (ang > (float)M_PI - 0.05f && ang < (float)M_PI + 0.05f);
        bool is_left =
            (ang > (float)(3.0 * M_PI_2) - 0.05f && ang < (float)(3.0 * M_PI_2) + 0.05f);

        if(is_top) {
            // Top (0°)
            face->hour_marks[hour].start.x = 0;
            face->hour_marks[hour].start.y = height;
            face->hour_marks[hour].end.x = 0;
            face->hour_marks[hour].end.y = height - long_ofs;
            face->hours[hour].x = 0;
            face->hours[hour].y = height - hour_ofs;
        } else if(is_right) {
            // Right (90°)
            face->hour_marks[hour].start.x = width;
            face->hour_marks[hour].start.y = 0;
            face->hour_marks[hour].end.x = width - long_ofs;
            face->hour_marks[hour].end.y = 0;
            face->hours[hour].x = width - hour_ofs;
            face->hours[hour].y = 0;
        } else if(is_bottom) {
            // Bottom (180°)
            face->hour_marks[hour].start.x = 0;
            face->hour_marks[hour].start.y = -height;
            face->hour_marks[hour].end.x = 0;
            face->hour_marks[hour].end.y = -(height - long_ofs);
            face->hours[hour].x = 0;
            face->hours[hour].y = -(height - hour_ofs);
        } else if(is_left) {
            // Left (270°)
            face->hour_marks[hour].start.x = -width;
            face->hour_marks[hour].start.y = 0;
            face->hour_marks[hour].end.x = -(width - long_ofs);
            face->hour_marks[hour].end.y = 0;
            face->hours[hour].x = -(width - hour_ofs);
            face->hours[hour].y = 0;
        } else {
            // Check if angle is very close to 90° or 270° - handle specially to avoid issues
            float ang_mod = fmodf(ang, (float)M_PI_2);
            bool near_90_or_270 = (ang_mod < 0.05f || ang_mod > (float)M_PI_2 - 0.05f);

            if(near_90_or_270 && (ang > (float)M_PI_2 - 0.1f && ang < (float)M_PI_2 + 0.1f)) {
                // Very close to 90° - handle directly as horizontal line to the right
                face->hour_marks[hour].start.x = width;
                face->hour_marks[hour].start.y = 0;
                face->hour_marks[hour].end.x = width - long_ofs;
                face->hour_marks[hour].end.y = 0;
                face->hours[hour].x = width - hour_ofs;
                face->hours[hour].y = 0;
            } else if(
                near_90_or_270 &&
                (ang > (float)(3.0 * M_PI_2) - 0.1f && ang < (float)(3.0 * M_PI_2) + 0.1f)) {
                // Very close to 270° - handle directly as horizontal line to the left
                face->hour_marks[hour].start.x = -width;
                face->hour_marks[hour].start.y = 0;
                face->hour_marks[hour].end.x = -(width - long_ofs);
                face->hour_marks[hour].end.y = 0;
                face->hours[hour].x = -(width - hour_ofs);
                face->hours[hour].y = 0;
            } else {
                // Calculate intersection with square face for any angle
                // Use set_point to get direction, then scale to fit square
                float sin_a = sinf(ang);
                float cos_a = cosf(ang);

                // Get a point on unit circle in the direction of the angle
                float dir_x = sin_a;
                float dir_y = cos_a;

                // Calculate scale factor to fit square (find which edge is hit first)
                float scale_x = (dir_x > 0.001f || dir_x < -0.001f) ? (float)width / fabsf(dir_x) :
                                                                      width + height;
                float scale_y = (dir_y > 0.001f || dir_y < -0.001f) ?
                                    (float)height / fabsf(dir_y) :
                                    width + height;
                float scale = (scale_x < scale_y) ? scale_x : scale_y;

                // Calculate start point (at edge of square)
                Point start_p;
                start_p.x = (int8_t)round((double)(dir_x * scale));
                start_p.y = (int8_t)round((double)(dir_y * scale));

                // Calculate end point (at edge minus long_ofs)
                float scale_end = scale - long_ofs;
                if(scale_end < 0) scale_end = 0;
                Point end_p;
                end_p.x = (int8_t)round((double)(dir_x * scale_end));
                end_p.y = (int8_t)round((double)(dir_y * scale_end));

                // Clamp end point to square bounds minus long_ofs
                int8_t max_x = width - long_ofs;
                int8_t max_y = height - long_ofs;
                if(end_p.x > max_x) end_p.x = max_x;
                if(end_p.x < -max_x) end_p.x = -max_x;
                if(end_p.y > max_y) end_p.y = max_y;
                if(end_p.y < -max_y) end_p.y = -max_y;

                // Calculate hour label position (at edge minus hour_ofs)
                float scale_hour = scale - hour_ofs;
                if(scale_hour < 0) scale_hour = 0;
                Point hour_p;
                hour_p.x = (int8_t)round((double)(dir_x * scale_hour));
                hour_p.y = (int8_t)round((double)(dir_y * scale_hour));

                // Clamp hour point to square bounds minus hour_ofs
                int8_t max_x_hour = width - hour_ofs;
                int8_t max_y_hour = height - hour_ofs;
                if(hour_p.x > max_x_hour) hour_p.x = max_x_hour;
                if(hour_p.x < -max_x_hour) hour_p.x = -max_x_hour;
                if(hour_p.y > max_y_hour) hour_p.y = max_y_hour;
                if(hour_p.y < -max_y_hour) hour_p.y = -max_y_hour;

                face->hour_marks[hour].start = start_p;
                face->hour_marks[hour].end = end_p;
                face->hours[hour] = hour_p;
            }
        }
    }

    // Initialize all minute marks to zero
    for(uint8_t i = 0; i < 60; i++) {
        face->minutes[i].start.x = 0;
        face->minutes[i].start.y = 0;
        face->minutes[i].end.x = 0;
        face->minutes[i].end.y = 0;
    }

    // Create minute marks dynamically between hour marks
    // Keep the same total number of marks as 8 hours (8 * 4 = 32 marks)
    // Scale marks per interval inversely with number of hours to maintain consistent density
    // Special cases: 1 hour = 60 marks, 2 hours = double the normal amount
    uint8_t marks_per_hour_interval;

    if(num_hours == 1) {
        // 1 hour: 60 marks total (60 marks per interval)
        marks_per_hour_interval = 60;
    } else if(num_hours == 2) {
        // 2 hours: between 1 hour (60) and 3 hours (~33), use 24 marks per interval (48 total)
        marks_per_hour_interval = 24;
    } else {
        // For 3+ hours: use the standard calculation
        const uint8_t base_hours = 8;
        const uint8_t base_marks_per_interval = 4;
        const uint16_t target_total_marks = base_hours * base_marks_per_interval; // 32 marks

        // Calculate marks per interval to maintain similar total count
        // Use rounding for better distribution (e.g., 32/12 ≈ 2.67 → 3 marks)
        marks_per_hour_interval = (uint8_t)roundf((float)target_total_marks / (float)num_hours);
        if(marks_per_hour_interval < 1)
            marks_per_hour_interval = 1; // At least 1 mark per interval
        if(marks_per_hour_interval > 10) marks_per_hour_interval = 10; // Cap at reasonable maximum
    }

    uint8_t minute_mark_index = 0;
    float hour_ang_step = (float)M_TWOPI / (float)num_hours;
    float minute_ang_step =
        hour_ang_step / (float)(marks_per_hour_interval + 1); // +1 to skip hour mark position

    for(uint8_t hour = 0; hour < num_hours && minute_mark_index < 60; hour++) {
        float hour_ang = (float)hour * hour_ang_step;

        // Place minute marks between this hour and the next
        for(uint8_t m = 1; m <= marks_per_hour_interval && minute_mark_index < 60; m++) {
            float min_ang = hour_ang + (float)m * minute_ang_step;

            // Normalize angle to [0, 2π)
            while(min_ang >= (float)M_TWOPI)
                min_ang -= (float)M_TWOPI;
            while(min_ang < 0.0f)
                min_ang += (float)M_TWOPI;

            // Calculate intersection with square face using the same method as hour marks
            float sin_a = sinf(min_ang);
            float cos_a = cosf(min_ang);

            // Get a point on unit circle in the direction of the angle
            float dir_x = sin_a;
            float dir_y = cos_a;

            // Calculate scale factor to fit square (find which edge is hit first)
            float scale_x = (dir_x > 0.001f || dir_x < -0.001f) ? (float)width / fabsf(dir_x) :
                                                                  width + height;
            float scale_y = (dir_y > 0.001f || dir_y < -0.001f) ? (float)height / fabsf(dir_y) :
                                                                  width + height;
            float scale = (scale_x < scale_y) ? scale_x : scale_y;

            // Calculate start point (at edge of square)
            Point start_p;
            start_p.x = (int8_t)round((double)(dir_x * scale));
            start_p.y = (int8_t)round((double)(dir_y * scale));

            // Calculate end point (at edge minus short_ofs)
            float scale_end = scale - short_ofs;
            if(scale_end < 0) scale_end = 0;
            Point end_p;
            end_p.x = (int8_t)round((double)(dir_x * scale_end));
            end_p.y = (int8_t)round((double)(dir_y * scale_end));

            // Clamp end point to square bounds minus short_ofs
            int8_t max_x = width - short_ofs;
            int8_t max_y = height - short_ofs;
            if(end_p.x > max_x) end_p.x = max_x;
            if(end_p.x < -max_x) end_p.x = -max_x;
            if(end_p.y > max_y) end_p.y = max_y;
            if(end_p.y < -max_y) end_p.y = -max_y;

            face->minutes[minute_mark_index].start = start_p;
            face->minutes[minute_mark_index].end = end_p;
            minute_mark_index++;
        }
    }
}

void draw_timer(
    Canvas* canvas,
    ClockFace* face,
    uint8_t timer_duration_hours,
    uint32_t elapsed_seconds,
    uint16_t ms,
    bool running,
    bool has_been_started,
    bool fill_enabled) {
    // Draw square clock face on left side with variable hour marks
    static char time_buf[10];

    // Calculate timer progress (0.0 to 1.0)
    uint32_t timer_duration_seconds = timer_duration_hours * 3600;
    uint32_t timer_duration_ms = timer_duration_seconds * 1000;
    float total_ms = elapsed_seconds * 1000.0f + ms;
    float progress = total_ms / (float)timer_duration_ms;
    if(progress > 1.0f) progress = 1.0f;
    float hand_angle = progress * M_TWOPI_F; // Start at 0 (top), go clockwise

    // Draw filled black segment using optimized scanlines (every 2 pixels)
    // Only draw if timer has been started, has progress, and fill is enabled
    if(has_been_started && progress > 0.0f && fill_enabled) {
        // Reduce fill area by 4 pixels on each side to avoid covering marks
        uint8_t fill_margin = 5;
        uint8_t width = FACE_RADIUS - fill_margin;
        uint8_t height = FACE_RADIUS - fill_margin;

        // Draw diagonal scanlines at 45 degrees (from top-left to bottom-right)
        // Iterate along diagonal lines where x + y = constant
        // We'll iterate along the sum (x + y) from -2*width to 2*width, every 2 units
        int16_t min_sum = -(int16_t)width - (int16_t)height;
        int16_t max_sum = (int16_t)width + (int16_t)height;

        for(int16_t sum = min_sum; sum <= max_sum; sum += 2) {
            // For diagonal line x + y = sum, find intersection with square and sector
            // Iterate along this diagonal line
            int8_t start_x = -width;
            int8_t end_x = width;

            // Clamp to square bounds: x + y = sum, so y = sum - x
            // y must be between -height and height
            if(sum - start_x > height) start_x = sum - height;
            if(sum - start_x < -height) start_x = sum + height;
            if(sum - end_x < -height) end_x = sum + height;
            if(sum - end_x > height) end_x = sum - height;

            // Draw pixels along this diagonal that are within the sector
            // We need to check each pixel individually because the sector boundary is curved
            for(int8_t x = start_x; x <= end_x; x++) {
                int8_t y = sum - x;

                // Check if pixel is within square bounds
                if(abs(x) > width || abs(y) > height) continue;

                // Calculate pixel angle
                float pixel_angle = atan2f((float)x, (float)y);
                if(pixel_angle < 0.0f) pixel_angle += M_TWOPI_F;

                // Check if pixel is within the elapsed time sector
                // Sector goes from 0 (top) clockwise to hand_angle
                bool should_fill = (pixel_angle >= 0.0f && pixel_angle <= hand_angle);

                if(should_fill) {
                    canvas_draw_dot(canvas, OFS_LEFT_X + x, OFS_Y - y);
                }
            }
        }
    }

    // Draw minute marks (short marks) - skip marks that would create center lines
    for(uint8_t i = 0; i < 60; i++) {
        if(face->minutes[i].start.x != 0 || face->minutes[i].start.y != 0 ||
           face->minutes[i].end.x != 0 || face->minutes[i].end.y != 0) {
            // Skip marks that are too close to center (would create unwanted lines)
            int16_t start_dist = abs(face->minutes[i].start.x) + abs(face->minutes[i].start.y);
            int16_t end_dist = abs(face->minutes[i].end.x) + abs(face->minutes[i].end.y);
            if(start_dist > 2 && end_dist > 2) { // Only draw if both points are away from center
                draw_line(canvas, OFS_LEFT_X, &face->minutes[i], Normal);
            }
        }
    }

    // Draw hour marks (long marks) - skip marks that would create center lines
    // timer_duration_hours is already validated in calc_clock_face, so we can trust it
    for(uint8_t i = 0; i < timer_duration_hours; i++) {
        // Skip marks that are too close to center or have invalid coordinates
        int16_t start_dist = abs(face->hour_marks[i].start.x) + abs(face->hour_marks[i].start.y);
        int16_t end_dist = abs(face->hour_marks[i].end.x) + abs(face->hour_marks[i].end.y);

        // Check if this is a horizontal line at y=0 (90° or 270° mark)
        bool is_horizontal =
            (abs(face->hour_marks[i].start.y) <= 1 && abs(face->hour_marks[i].end.y) <= 1);

        // For horizontal lines, ensure start point is at the edge, not near center
        // This prevents drawing lines that extend from center outward
        if(is_horizontal) {
            // Start point must be at or very close to the edge (width or -width)
            if(abs(face->hour_marks[i].start.x) < FACE_RADIUS - 2) {
                // Start point is too close to center, skip this mark
                continue;
            }
            // Also ensure end point is closer to center than start point
            if(abs(face->hour_marks[i].end.x) >= abs(face->hour_marks[i].start.x)) {
                // End point is not closer to center, skip this mark
                continue;
            }
        }

        // Skip if line passes through or near center
        bool passes_through_center =
            ((face->hour_marks[i].start.x >= 0 && face->hour_marks[i].end.x <= 0) ||
             (face->hour_marks[i].start.x <= 0 && face->hour_marks[i].end.x >= 0)) &&
            ((face->hour_marks[i].start.y >= 0 && face->hour_marks[i].end.y <= 0) ||
             (face->hour_marks[i].start.y <= 0 && face->hour_marks[i].end.y >= 0));

        // Only draw if both points are away from center AND line doesn't pass through center
        if(start_dist > 2 && end_dist > 2 && !passes_through_center) {
            draw_line(canvas, OFS_LEFT_X, &face->hour_marks[i], Normal);
        }
    }

    // Hour labels are permanently hidden (removed digits feature)

    // Draw minutes hand showing timer progress (always draw, even in Set mode)
    draw_hand(canvas, OFS_LEFT_X, hand_angle, M_RAD, true);

    canvas_draw_disc(canvas, OFS_LEFT_X, OFS_Y, 2);

    // Draw digital timer and status on right side
    uint32_t total_elapsed_ms = elapsed_seconds * 1000 + ms;
    uint32_t remaining_ms =
        timer_duration_ms > total_elapsed_ms ? timer_duration_ms - total_elapsed_ms : 0;

    // Convert to hours, minutes, seconds
    uint32_t remaining_seconds = remaining_ms / 1000;
    uint8_t hours = remaining_seconds / 3600;
    uint8_t minutes = (remaining_seconds % 3600) / 60;
    uint8_t seconds = remaining_seconds % 60;

    // Format time string
    snprintf(time_buf, 10, "%u:%02u:%02u", hours, minutes, seconds);

    // Determine status
    const char* status;
    if(!has_been_started) {
        status = "Set";
    } else if(progress >= 1.0f) {
        status = "Finished";
    } else if(running) {
        status = "Working";
    } else {
        status = "Paused";
    }

    // Draw digital timer on right side (monospace font to prevent shifting)
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, OFS_RIGHT_X, OFS_Y - 5, AlignCenter, AlignCenter, time_buf);

    // Draw status below timer
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, OFS_RIGHT_X, OFS_Y + 8, AlignCenter, AlignCenter, status);
}

void init_timer_config(TimerConfig* cfg) {
    cfg->version = CONFIG_VERSION;
    cfg->timer_duration_hours = DEFAULT_TIMER_HOURS; // Default 8 hours
    cfg->fill_enabled = true; // Fill enabled by default
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
