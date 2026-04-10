// direction_merge_debug.cpp - 支持可配置 check_time
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include "interception.h"
#include <windows.h>
#pragma comment(lib, "winmm.lib")

// 方向键扫描码
#define SCANCODE_UP    0x48
#define SCANCODE_DOWN  0x50
#define SCANCODE_LEFT  0x4B
#define SCANCODE_RIGHT 0x4D

// 默认参数
static const double DEFAULT_BUFFER_TIME = 0.010;   // 10ms
static const double DEFAULT_CHECK_TIME  = 0.100;   // 100ms

inline double now_seconds() {
    using namespace std::chrono;
    static const steady_clock::time_point epoch = steady_clock::now();
    return duration<double>(steady_clock::now() - epoch).count();
}

static bool debug_mode = false;

const char* get_key_name(int code) {
    switch (code) {
        case SCANCODE_UP:    return "Up";
        case SCANCODE_DOWN:  return "Down";
        case SCANCODE_LEFT:  return "Left";
        case SCANCODE_RIGHT: return "Right";
        default:             return "Unknown";
    }
}

int code_to_index(int code) {
    switch (code) {
        case SCANCODE_UP:    return 0;
        case SCANCODE_DOWN:  return 1;
        case SCANCODE_LEFT:  return 2;
        case SCANCODE_RIGHT: return 3;
        default:             return -1;
    }
}

bool are_adjacent(int code1, int code2) {
    int idx1 = code_to_index(code1);
    int idx2 = code_to_index(code2);
    return (idx1 / 2) != (idx2 / 2);
}

void log_event(const char* key_name, bool is_press, bool is_delayed,
               double orig_time, double send_time) {
    const char* action = is_press ? "press " : "release";
    if (!debug_mode) return;
    if (is_delayed) {
        printf("[%s] %s delayed: orig %.3f, sent %.3f (diff %.3f ms)\n",
               key_name, action, orig_time, send_time, (send_time - orig_time) * 1000.0);
    } else {
        printf("[%s] %s at %.3f\n", key_name, action, orig_time);
    }
}

struct DelayedKey {
    int device;
    InterceptionKeyStroke stroke;
    int code;
    double deadline;
    double orig_time;
    bool is_press;
};

int main(int argc, char* argv[]) {
    timeBeginPeriod(1);

    double buffer_time = DEFAULT_BUFFER_TIME;
    double check_time  = DEFAULT_CHECK_TIME;
    
    // 解析命令行参数：支持 [buffer_time] [check_time] [-d|--debug]
    int num_numeric = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else {
            char* endptr;
            double val = strtod(argv[i], &endptr);
            if (*endptr == '\0' && val > 0.0) {
                if (num_numeric == 0) {
                    buffer_time = val;
                    num_numeric++;
                } else if (num_numeric == 1) {
                    check_time = val;
                    num_numeric++;
                } else {
                    std::cerr << "Too many numeric arguments." << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Invalid argument: " << argv[i] << std::endl;
                std::cerr << "Usage: " << argv[0] << " [buffer_time_sec] [check_time_sec] [-d|--debug]" << std::endl;
                return 1;
            }
        }
    }

    int timeout_ms = static_cast<int>(buffer_time * 1000 / 2);
    if (timeout_ms < 1) timeout_ms = 1;

    std::cout << "Direction key merger started. Buffer time = " << buffer_time
              << " sec, Check time = " << check_time << " sec." << std::endl;
    if (debug_mode) std::cout << "Debug mode ON" << std::endl;

    InterceptionContext context = interception_create_context();
    if (!context) {
        std::cerr << "Failed to create Interception context. Is driver installed?" << std::endl;
        return 1;
    }
    interception_set_filter(context, interception_is_keyboard, INTERCEPTION_FILTER_KEY_ALL);

    double last_press[4]   = {0.0, 0.0, 0.0, 0.0};
    double last_release[4] = {0.0, 0.0, 0.0, 0.0};

    DelayedKey delayed;
    bool has_delayed = false;

    auto send_stroke = [&](int device, const InterceptionKeyStroke& stroke,
                           bool is_press, int code, double orig_time, double send_time, bool is_delayed_flag) {
        interception_send(context, device,
                          reinterpret_cast<const InterceptionStroke*>(&stroke), 1);
        log_event(get_key_name(code), is_press, is_delayed_flag, orig_time, send_time);
    };

    auto update_state = [&](int idx, bool is_press, double time_sec) {
        if (is_press) last_press[idx] = time_sec;
        else last_release[idx] = time_sec;
    };

    auto cancel_delayed = [&](double now_sec) {
        if (!has_delayed) return;
        send_stroke(delayed.device, delayed.stroke, delayed.is_press, delayed.code,
                    delayed.orig_time, now_sec, true);
        update_state(code_to_index(delayed.code), delayed.is_press, now_sec);
        has_delayed = false;
    };

    auto check_timeout = [&](double now_sec) {
        if (has_delayed && now_sec >= delayed.deadline) {
            if (debug_mode) printf("[Timeout] Sending delayed key %s\n", get_key_name(delayed.code));
            cancel_delayed(now_sec);
        }
    };

    // 使用可变的 check_time 捕获
    auto has_recent_opposite = [&](int idx, double now_sec, bool is_press) -> bool {
        if (is_press) {
            return (now_sec - last_release[idx]) <= check_time;
        } else {
            return (now_sec - last_press[idx]) <= check_time;
        }
    };

    auto should_delay = [&](int idx, bool is_press, double now_sec) -> bool {
        if (!has_recent_opposite(idx, now_sec, is_press)) return false;
        int group = idx / 2;
        int adj_start = (group == 0) ? 2 : 0;
        for (int adj = adj_start; adj < adj_start + 2; ++adj) {
            if (has_recent_opposite(adj, now_sec, is_press)) return true;
        }
        return false;
    };

    while (true) {
        int device = interception_wait_with_timeout(context, timeout_ms);
        double now_sec = now_seconds();
        check_timeout(now_sec);

        if (device == 0 || !interception_is_keyboard(device)) continue;

        InterceptionKeyStroke stroke;
        if (interception_receive(context, device,
                                 reinterpret_cast<InterceptionStroke*>(&stroke), 1) == 0) continue;

        bool is_extended = (stroke.state & INTERCEPTION_KEY_E0) != 0;
        bool is_arrow = is_extended && (
            stroke.code == SCANCODE_UP   ||
            stroke.code == SCANCODE_DOWN ||
            stroke.code == SCANCODE_LEFT ||
            stroke.code == SCANCODE_RIGHT);
        bool is_press = (stroke.state & INTERCEPTION_KEY_UP) == 0;

        if (!is_arrow) {
            interception_send(context, device,
                              reinterpret_cast<InterceptionStroke*>(&stroke), 1);
            continue;
        }

        int idx = code_to_index(stroke.code);
        int code = stroke.code;

        if (has_delayed) {
            if (delayed.code == code) {
                cancel_delayed(now_sec);
            }
            else if (are_adjacent(delayed.code, code) &&
                     delayed.is_press == is_press &&
                     has_recent_opposite(idx, now_sec, is_press)) {
                if (debug_mode) {
                    printf("[Combine] Sending delayed %s and current %s together\n",
                           get_key_name(delayed.code), get_key_name(code));
                }
                send_stroke(delayed.device, delayed.stroke, delayed.is_press, delayed.code,
                            delayed.orig_time, now_sec, true);
                update_state(code_to_index(delayed.code), delayed.is_press, now_sec);
                send_stroke(device, stroke, is_press, code, now_sec, now_sec, false);
                update_state(idx, is_press, now_sec);
                has_delayed = false;
                continue;
            }
            else {
                send_stroke(device, stroke, is_press, code, now_sec, now_sec, false);
                update_state(idx, is_press, now_sec);
                continue;
            }
        }

        if (should_delay(idx, is_press, now_sec)) {
            delayed = {device, stroke, code, now_sec + buffer_time, now_sec, is_press};
            has_delayed = true;
            if (debug_mode) {
                printf("[Delay] %s %s will be delayed until %.3f\n",
                       get_key_name(code), is_press ? "press" : "release", delayed.deadline);
            }
        } else {
            send_stroke(device, stroke, is_press, code, now_sec, now_sec, false);
            update_state(idx, is_press, now_sec);
        }
    }

    interception_destroy_context(context);
    timeEndPeriod(1);
    return 0;
}