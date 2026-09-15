#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "clock.h"

#define CFG_FILENAME APP_DATA_PATH("timer.cfg")

// Adaptive frame rates for battery optimization
#define FRAME_MS_RUNNING       1000 // 1 FPS when timer is running
#define MUTEX_TIMEOUT_IDLE     100 // Short timeout when idle (draw callback won't be called often)
#define FINISH_CHECK_INTERVAL  1000 // Check finish every second when close
#define FINISH_CHECK_THRESHOLD 60 // Only check frequently within last 60 seconds

// Forward declarations
static void play_rick_roll_melody(NotificationApp* notification);
static void play_hour_chime(NotificationApp* notification);

// Calculate skip amount based on hold duration
// Short press: 1 minute
// Medium hold: 5 minutes
// Long hold: 20 minutes
static uint32_t calculate_skip_amount(uint32_t press_duration_ms) {
    if(press_duration_ms < 2000) {
        return 60; // 1 minute
    } else if(press_duration_ms < 5000) {
        return 300; // 5 minutes
    } else {
        return 1200; // 20 minutes
    }
}

typedef struct {
    FuriMutex* mutex;
    TimerConfig cfg;
    ClockFace face; // Runtime-only, calculated from timer_duration_hours
    bool running;
    bool has_been_started;
    uint32_t start_tick;
    uint32_t elapsed_seconds;
    uint16_t ms_adjust;
    bool fill_enabled; // Toggle for segment fill visibility
    bool finish_sound_played; // Track if finish sound has been played
    uint32_t skip_left_press_tick; // Track when left skip button was pressed
    uint32_t skip_right_press_tick; // Track when right skip button was pressed
    bool skip_left_active; // Track if left skip button is currently held
    bool skip_right_active; // Track if right skip button is currently held
    uint32_t last_hour_played; // Track last hour that played chime (to avoid repeats)
} AppData;

static void app_draw_callback(Canvas* canvas, void* ctx) {
    furi_assert(ctx);
    AppData* app = (AppData*)ctx;

    // Use timeout based on timer state for battery optimization
    // When not running, use short timeout since draw callback won't be called often anyway
    uint32_t timeout_ms = app->running ? FRAME_MS_RUNNING : MUTEX_TIMEOUT_IDLE;

    if(furi_mutex_acquire(app->mutex, timeout_ms) != FuriStatusOk) return;

    uint32_t current_tick = furi_get_tick();
    uint32_t elapsed_seconds = app->elapsed_seconds;
    uint16_t ms = 0;

    if(app->running) {
        uint32_t elapsed_ms = current_tick - app->start_tick;
        elapsed_seconds = app->elapsed_seconds + (elapsed_ms / 1000);
        ms = elapsed_ms % 1000;
    } else {
        // When paused, use stored milliseconds
        ms = app->ms_adjust;
    }

    // Only check finish in draw callback if very close (optimization)
    // Main loop handles finish detection for better battery life
    if(app->running && !app->finish_sound_played) {
        uint32_t timer_duration_seconds = app->cfg.timer_duration_hours * 3600;
        uint32_t remaining = timer_duration_seconds > elapsed_seconds ?
                                 timer_duration_seconds - elapsed_seconds :
                                 0;

        // Only check finish if within threshold (saves CPU)
        if(remaining <= FINISH_CHECK_THRESHOLD) {
            float total_ms = elapsed_seconds * 1000.0f + ms;
            float progress = total_ms / (timer_duration_seconds * 1000.0f);
            bool is_finished = (progress >= 1.0f && app->has_been_started);

            if(is_finished) {
                // Accumulate final elapsed time and cap at timer duration
                uint32_t elapsed_ms = current_tick - app->start_tick;
                app->elapsed_seconds += (elapsed_ms / 1000);
                app->ms_adjust = elapsed_ms % 1000;
                if(app->elapsed_seconds > timer_duration_seconds) {
                    app->elapsed_seconds = timer_duration_seconds;
                    app->ms_adjust = 0;
                }
                app->finish_sound_played = true;
                app->running = false;
            }
        }
    }

    draw_timer(
        canvas,
        &app->face,
        app->cfg.timer_duration_hours,
        elapsed_seconds,
        ms,
        app->running,
        app->has_been_started,
        app->fill_enabled);
    furi_mutex_release(app->mutex);
}

static void app_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static bool cfg_load(File* file, AppData* app) {
    size_t readed = 0;
    if(storage_file_open(file, CFG_FILENAME, FSAM_READ, FSOM_OPEN_EXISTING))
        readed = storage_file_read(file, &app->cfg, sizeof(TimerConfig));
    storage_file_close(file);

    if(readed == sizeof(TimerConfig)) {
        // Handle config version migration
        if(app->cfg.version < CONFIG_VERSION) {
            // Migrate from old versions
            if(app->cfg.version < 4) {
                // Old versions had digits_mod, migrate to timer_duration_hours
                app->cfg.timer_duration_hours = DEFAULT_TIMER_HOURS;
            }
            // Version 4 had 'width' field which was removed in version 5
            // Version 5 had 'ofs_x' field which was removed in version 6 (always OFS_LEFT_X)
            // Version 6 had 'face' field which was removed in version 7 (recalculated from timer_duration_hours)
            // Version 7 didn't have 'fill_enabled' field, added in version 8
            if(app->cfg.version < 8) {
                app->cfg.fill_enabled = true; // Default to enabled for old configs
            }
            // Old configs will fail to load due to size mismatch and be recreated
            app->cfg.version = CONFIG_VERSION;
            // Ensure valid timer duration
            if(app->cfg.timer_duration_hours < 1 ||
               app->cfg.timer_duration_hours > MAX_TIMER_HOURS) {
                app->cfg.timer_duration_hours = DEFAULT_TIMER_HOURS;
            }
        }
        return true;
    }
    return false;
}

static void cfg_save_internal(File* file, TimerConfig* cfg) {
    // Face is not saved - it's recalculated on load
    if(storage_file_open(file, CFG_FILENAME, FSAM_WRITE, FSOM_CREATE_ALWAYS))
        storage_file_write(file, cfg, sizeof(TimerConfig));
    storage_file_close(file);
}

static void cfg_save(File* file, AppData* app) {
    if(furi_mutex_acquire(app->mutex, FuriWaitForever) != FuriStatusOk) return;
    cfg_save_internal(file, &app->cfg);
    furi_mutex_release(app->mutex);
}

// Rick Roll melody - "Never gonna give you up" opening phrase
// Exact notes and timing to match the iconic melody
// Based on tracker.c note_to_freq() logic and actual song timing
static void play_rick_roll_melody(NotificationApp* notification) {
    UNUSED(notification);

// Frequency constants for clarity
#define NOTE_A4  440.0f
#define NOTE_B4  493.88f
#define NOTE_D5  587.33f
#define NOTE_E5  659.25f
#define NOTE_FS5 739.99f

    float notes[] = {
        NOTE_A4, // Ne-
        NOTE_B4, // -ver
        NOTE_D5, // gon-
        NOTE_B4, // -na
        NOTE_FS5, // give
        NOTE_FS5, // you
        NOTE_E5 // up
    };

    uint16_t durations[] = {130, 130, 130, 130, 260, 260, 450};

    uint16_t pauses[] = {30, 30, 30, 30, 40, 40, 0};

    // Acquire speaker (similar to tracker_speaker_init)
    if(furi_hal_speaker_acquire(1000)) {
        // Play each note with exact timing
        for(int i = 0; i < 7; i++) {
            furi_hal_speaker_start(notes[i], 0.5f); // frequency, volume (0.5 = 50%)
            furi_delay_ms(durations[i]);
            furi_hal_speaker_stop();
            if(i < 6) {
                furi_delay_ms(pauses[i]); // Pause between notes
            }
        }
        // Release speaker
        furi_hal_speaker_release();
    }
}

// Play satisfying "tu-tum" chime for hourly milestones
// Two low notes: ascending for positive feel
static void play_hour_chime(NotificationApp* notification) {
    UNUSED(notification);

    // Low satisfying notes - ascending "tu-tum" for positive feel
    // First note: A2 (110.00 Hz) - "tu"
    // Second note: C3 (130.81 Hz) - "tum" (higher, more positive)
    float note1 = 110.00f; // A2
    float note2 = 130.81f; // C3

    // Acquire speaker
    if(furi_hal_speaker_acquire(1000)) {
        // Play first note "tu"
        furi_hal_speaker_start(note1, 0.4f); // Lower volume for subtlety
        furi_delay_ms(150);
        furi_hal_speaker_stop();

        // Short pause between notes
        furi_delay_ms(50);

        // Play second note "tum" (ascending)
        furi_hal_speaker_start(note2, 0.4f);
        furi_delay_ms(200); // Slightly longer for the "tum"
        furi_hal_speaker_stop();

        // Release speaker
        furi_hal_speaker_release();
    }
}

int32_t clock_main(void* p) {
    UNUSED(p);

    AppData* app = malloc(sizeof(AppData));
    furi_assert(app);

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->running = false;
    app->has_been_started = false;
    app->elapsed_seconds = 0;
    app->start_tick = 0;
    app->ms_adjust = 0;
    app->finish_sound_played = false;
    app->skip_left_press_tick = 0;
    app->skip_right_press_tick = 0;
    app->skip_left_active = false;
    app->skip_right_active = false;
    app->last_hour_played = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(!cfg_load(file, app)) {
        init_timer_config(&app->cfg);
        calc_clock_face(&app->cfg, &app->face);
        app->fill_enabled = app->cfg.fill_enabled; // Sync from config
        cfg_save(file, app);
    } else {
        // Calculate face from loaded timer_duration_hours
        calc_clock_face(&app->cfg, &app->face);
        app->fill_enabled = app->cfg.fill_enabled; // Load fill_enabled from config
    }

    ViewPort* view_port = view_port_alloc();
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    view_port_draw_callback_set(view_port, app_draw_callback, app);
    view_port_input_callback_set(view_port, app_input_callback, event_queue);

    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message_block(notification, &sequence_display_backlight_enforce_on);

    InputEvent event;
    bool terminate = false;
    uint32_t last_finish_check = 0;
    uint32_t last_hour_check = 0;

    while(!terminate) {
        // Adaptive finish check - only check frequently when close to finish
        uint32_t current_tick = furi_get_tick();
        bool should_check_finish = false;

        if(furi_mutex_acquire(app->mutex, 0) == FuriStatusOk) {
            if(app->has_been_started && app->running && !app->finish_sound_played) {
                uint32_t timer_duration_seconds = app->cfg.timer_duration_hours * 3600;
                uint32_t elapsed_ms = current_tick - app->start_tick;
                uint32_t elapsed_seconds = app->elapsed_seconds + (elapsed_ms / 1000);
                uint32_t remaining = timer_duration_seconds > elapsed_seconds ?
                                         timer_duration_seconds - elapsed_seconds :
                                         0;

                // Check finish first (before hour check) to prioritize finish sound
                // Only check finish frequently when close (battery optimization)
                if(remaining <= FINISH_CHECK_THRESHOLD) {
                    // Check every FINISH_CHECK_INTERVAL ms when close
                    if((current_tick - last_finish_check) >= FINISH_CHECK_INTERVAL) {
                        should_check_finish = true;
                        last_finish_check = current_tick;
                    }
                } else {
                    // Check less frequently when far from finish
                    if((current_tick - last_finish_check) >= 5000) { // Every 5 seconds
                        should_check_finish = true;
                        last_finish_check = current_tick;
                    }
                }

                if(should_check_finish) {
                    float progress = (elapsed_seconds * 1000.0f + (elapsed_ms % 1000)) /
                                     (timer_duration_seconds * 1000.0f);

                    if(progress >= 1.0f) {
                        // Accumulate final elapsed time and cap at timer duration
                        app->elapsed_seconds += (elapsed_ms / 1000);
                        app->ms_adjust = elapsed_ms % 1000;
                        if(app->elapsed_seconds > timer_duration_seconds) {
                            app->elapsed_seconds = timer_duration_seconds;
                            app->ms_adjust = 0;
                        }
                        app->finish_sound_played = true;
                        app->running = false;
                        furi_mutex_release(app->mutex);
                        // Force update to show finished state
                        view_port_update(view_port);
                        // Play sound outside of mutex
                        play_rick_roll_melody(notification);
                        // Re-acquire mutex for next iteration
                        continue;
                    }
                }

                // Check for hour milestones more frequently (every second)
                // Only check if not finished (remaining > 0) to avoid playing chime at finish
                // This ensures timely chime playback without significant battery impact
                if(remaining > 0 &&
                   (current_tick - last_hour_check) >= 1000) { // Check every second
                    last_hour_check = current_tick;
                    uint32_t current_hour = elapsed_seconds / 3600;
                    if(current_hour > 0 && current_hour != app->last_hour_played) {
                        app->last_hour_played = current_hour;
                        furi_mutex_release(app->mutex);
                        // Play hour chime outside of mutex
                        play_hour_chime(notification);
                        // Re-acquire mutex for next iteration
                        continue;
                    }
                }
            }
            furi_mutex_release(app->mutex);
        }

        // Adaptive timeout based on timer state for battery optimization
        // When not running, we can wait longer since we're not updating display
        uint32_t queue_timeout = FRAME_MS_RUNNING;
        if(furi_mutex_acquire(app->mutex, 0) == FuriStatusOk) {
            if(!app->running) {
                // When paused/finished, wait longer for input (saves CPU cycles)
                queue_timeout =
                    5000; // 5 seconds - long enough to save battery, short enough to be responsive
            }
            furi_mutex_release(app->mutex);
        }

        if(furi_message_queue_get(event_queue, &event, queue_timeout) == FuriStatusOk) {
            // Handle long press for Back button to exit
            if(event.type == InputTypeLong && event.key == InputKeyBack) {
                terminate = true;
            } else if(event.type == InputTypeLong && event.key == InputKeyOk) {
                // Long press OK: reset to set mode
                if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                    app->has_been_started = false;
                    app->running = false;
                    app->elapsed_seconds = 0;
                    app->ms_adjust = 0;
                    app->start_tick = 0;
                    app->finish_sound_played = false;
                    furi_mutex_release(app->mutex);
                }
            } else if((event.type == InputTypePress) || (event.type == InputTypeRepeat)) {
                switch(event.key) {
                case InputKeyUp:
                    // Use timeout to avoid blocking if draw callback is holding mutex
                    if(furi_mutex_acquire(app->mutex, 100) == FuriStatusOk) {
                        if(!app->has_been_started) {
                            // Set mode: change timer duration
                            modify_timer_up(&app->cfg);
                            calc_clock_face(&app->cfg, &app->face);
                            cfg_save_internal(file, &app->cfg);
                        } else {
                            // Non-set mode: toggle fill visibility
                            app->fill_enabled = !app->fill_enabled;
                            app->cfg.fill_enabled = app->fill_enabled; // Sync to config
                            cfg_save_internal(file, &app->cfg); // Save config
                        }
                        furi_mutex_release(app->mutex);
                    }
                    break;
                case InputKeyDown:
                    // Use timeout to avoid blocking if draw callback is holding mutex
                    if(furi_mutex_acquire(app->mutex, 100) == FuriStatusOk) {
                        if(!app->has_been_started) {
                            // Set mode: change timer duration
                            modify_timer_down(&app->cfg);
                            calc_clock_face(&app->cfg, &app->face);
                            cfg_save_internal(file, &app->cfg);
                        } else {
                            // Non-set mode: toggle fill visibility
                            app->fill_enabled = !app->fill_enabled;
                            app->cfg.fill_enabled = app->fill_enabled; // Sync to config
                            cfg_save_internal(file, &app->cfg); // Save config
                        }
                        furi_mutex_release(app->mutex);
                    }
                    break;
                case InputKeyOk:
                    // Only handle initial press, not repeat events (to prevent repeated firing when held)
                    if(event.type == InputTypePress) {
                        if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
                            // Check if timer is finished
                            uint32_t timer_duration_seconds = app->cfg.timer_duration_hours * 3600;
                            uint32_t current_tick = furi_get_tick();
                            uint32_t elapsed_seconds = app->elapsed_seconds;
                            if(app->running) {
                                uint32_t elapsed_ms = current_tick - app->start_tick;
                                elapsed_seconds += (elapsed_ms / 1000);
                            }
                            float progress = (elapsed_seconds * 1000.0f + app->ms_adjust) /
                                             (timer_duration_seconds * 1000.0f);
                            bool is_finished = (progress >= 1.0f);

                            if(is_finished) {
                                // Timer finished: reset to set mode
                                app->has_been_started = false;
                                app->running = false;
                                app->elapsed_seconds = 0;
                                app->ms_adjust = 0;
                                app->start_tick = 0;
                                app->finish_sound_played = false;
                                app->last_hour_played = 0;
                            } else if(app->running) {
                                // Stop timer - accumulate elapsed time including milliseconds
                                uint32_t elapsed_ms = current_tick - app->start_tick;
                                app->elapsed_seconds += (elapsed_ms / 1000);
                                app->ms_adjust = elapsed_ms % 1000; // Store remaining milliseconds
                                app->running = false;
                            } else {
                                // Start timer - adjust start_tick to account for stored milliseconds
                                app->has_been_started = true;
                                // Subtract stored milliseconds from start_tick so timer continues from where it paused
                                app->start_tick = furi_get_tick() - app->ms_adjust;
                                app->ms_adjust = 0; // Reset milliseconds adjustment
                                app->running = true;
                                app->finish_sound_played = false; // Reset sound flag when starting
                            }
                            furi_mutex_release(app->mutex);
                            // Force update after state change (start/pause/finish)
                            view_port_update(view_port);
                        }
                    }
                    break;
                case InputKeyLeft:
                    // Skip backward with adaptive amount based on hold duration
                    // Use timeout to avoid blocking if draw callback is holding mutex
                    if(furi_mutex_acquire(app->mutex, 100) == FuriStatusOk) {
                        if(app->has_been_started) {
                            // Track button press start time
                            if(event.type == InputTypePress) {
                                app->skip_left_press_tick = furi_get_tick();
                                app->skip_left_active = true;
                            }

                            // Calculate hold duration from initial press
                            uint32_t hold_duration = 0;
                            if(app->skip_left_active && app->skip_left_press_tick > 0) {
                                hold_duration = furi_get_tick() - app->skip_left_press_tick;
                            }

                            // Calculate skip amount based on hold duration
                            uint32_t skip_seconds = calculate_skip_amount(hold_duration);

                            // Accumulate current elapsed time first if running
                            if(app->running) {
                                uint32_t current_tick = furi_get_tick();
                                uint32_t elapsed_ms = current_tick - app->start_tick;
                                app->elapsed_seconds += (elapsed_ms / 1000);
                                app->ms_adjust = elapsed_ms % 1000;
                            }

                            // Skip backward
                            if(app->elapsed_seconds >= skip_seconds) {
                                app->elapsed_seconds -= skip_seconds;
                            } else {
                                app->elapsed_seconds = 0;
                                app->ms_adjust = 0;
                            }

                            // Reset start_tick if running
                            if(app->running) {
                                app->start_tick = furi_get_tick() - app->ms_adjust;
                                app->ms_adjust = 0;
                            }

                            // Reset button tracking on release
                            if(event.type == InputTypeRelease) {
                                app->skip_left_active = false;
                                app->skip_left_press_tick = 0;
                            }
                        }
                        furi_mutex_release(app->mutex);
                    }
                    break;
                case InputKeyRight:
                    // Skip forward with adaptive amount based on hold duration
                    // Use timeout to avoid blocking if draw callback is holding mutex
                    if(furi_mutex_acquire(app->mutex, 100) == FuriStatusOk) {
                        if(app->has_been_started) {
                            // Track button press start time
                            if(event.type == InputTypePress) {
                                app->skip_right_press_tick = furi_get_tick();
                                app->skip_right_active = true;
                            }

                            // Calculate hold duration from initial press
                            uint32_t hold_duration = 0;
                            if(app->skip_right_active && app->skip_right_press_tick > 0) {
                                hold_duration = furi_get_tick() - app->skip_right_press_tick;
                            }

                            // Calculate skip amount based on hold duration
                            uint32_t skip_seconds = calculate_skip_amount(hold_duration);

                            // Accumulate current elapsed time first if running
                            if(app->running) {
                                uint32_t current_tick = furi_get_tick();
                                uint32_t elapsed_ms = current_tick - app->start_tick;
                                app->elapsed_seconds += (elapsed_ms / 1000);
                                app->ms_adjust = elapsed_ms % 1000;
                            }

                            // Skip forward
                            app->elapsed_seconds += skip_seconds;

                            // Check if we've exceeded the timer duration
                            uint32_t timer_duration_seconds = app->cfg.timer_duration_hours * 3600;
                            if(app->elapsed_seconds > timer_duration_seconds) {
                                app->elapsed_seconds = timer_duration_seconds;
                                app->ms_adjust = 0;
                            }

                            // Reset start_tick if running
                            if(app->running) {
                                app->start_tick = furi_get_tick() - app->ms_adjust;
                                app->ms_adjust = 0;
                            }

                            // Reset button tracking on release
                            if(event.type == InputTypeRelease) {
                                app->skip_right_active = false;
                                app->skip_right_press_tick = 0;
                            }
                        }
                        furi_mutex_release(app->mutex);
                    }
                    break;
                default:
                    break;
                }
            }
            // Force update after input events (user interaction requires immediate feedback)
            view_port_update(view_port);
        } else {
            // Battery optimization: Only update when timer is running
            // When paused/finished, display is static - no need to redraw
            bool needs_update = false;

            if(furi_mutex_acquire(app->mutex, 0) == FuriStatusOk) {
                needs_update = app->running; // Only update when timer is actively running
                furi_mutex_release(app->mutex);
            }

            if(needs_update) {
                // Timer is running - update at reduced frequency
                static uint32_t last_update = 0;
                uint32_t now = furi_get_tick();

                if((now - last_update) >= FRAME_MS_RUNNING) {
                    view_port_update(view_port);
                    last_update = now;
                }
            }
            // When paused/finished: no viewport update = zero battery drain for display
        }
    }

    notification_message_block(notification, &sequence_display_backlight_enforce_auto);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(event_queue);
    view_port_free(view_port);
    storage_file_free(file);

    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
