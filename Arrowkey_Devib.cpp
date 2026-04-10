#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <unordered_map>
#include <string>
#include "interception.h"
#include <windows.h>
#pragma comment(lib, "winmm.lib")

// 默认参数
static const double DEFAULT_BUFFER_TIME = 0.010;   //单位秒
static const double DEFAULT_CHECK_TIME  = 0.080;   

inline double now_seconds() {
    using namespace std::chrono;
    static const steady_clock::time_point epoch = steady_clock::now();
    return duration<double>(steady_clock::now() - epoch).count();
}

static bool debug_mode = false;

// 方向索引 → 显示名称
const char* dir_names[] = { "Up", "Down", "Left", "Right" };

// 内置键位表 (key_name -> {scancode, extended})
// 基于提供的 JSON 文件生成，key_name 统一存储为小写以便不区分大小写查找
static const std::unordered_map<std::string, std::pair<int, bool>> build_key_map() {
    return {
        {"escape", {0x01, false}},
        {"1", {0x02, false}},
        {"2", {0x03, false}},
        {"3", {0x04, false}},
        {"4", {0x05, false}},
        {"5", {0x06, false}},
        {"6", {0x07, false}},
        {"7", {0x08, false}},
        {"8", {0x09, false}},
        {"9", {0x0A, false}},
        {"0", {0x0B, false}},
        {"minus", {0x0C, false}},
        {"equals", {0x0D, false}},
        {"backspace", {0x0E, false}},
        {"tab", {0x0F, false}},
        {"q", {0x10, false}},
        {"w", {0x11, false}},
        {"e", {0x12, false}},
        {"r", {0x13, false}},
        {"t", {0x14, false}},
        {"y", {0x15, false}},
        {"u", {0x16, false}},
        {"i", {0x17, false}},
        {"o", {0x18, false}},
        {"p", {0x19, false}},
        {"leftbracket", {0x1A, false}},
        {"rightbracket", {0x1B, false}},
        {"enter", {0x1C, false}},
        {"leftcontrol", {0x1D, false}},
        {"a", {0x1E, false}},
        {"s", {0x1F, false}},
        {"d", {0x20, false}},
        {"f", {0x21, false}},
        {"g", {0x22, false}},
        {"h", {0x23, false}},
        {"j", {0x24, false}},
        {"k", {0x25, false}},
        {"l", {0x26, false}},
        {"semicolon", {0x27, false}},
        {"apostrophe", {0x28, false}},
        {"grave", {0x29, false}},
        {"leftshift", {0x2A, false}},
        {"backslash", {0x2B, false}},
        {"z", {0x2C, false}},
        {"x", {0x2D, false}},
        {"c", {0x2E, false}},
        {"v", {0x2F, false}},
        {"b", {0x30, false}},
        {"n", {0x31, false}},
        {"m", {0x32, false}},
        {"comma", {0x33, false}},
        {"period", {0x34, false}},
        {"slash", {0x35, false}},
        {"rightshift", {0x36, false}},
        {"multiply", {0x37, false}},
        {"leftalt", {0x38, false}},
        {"space", {0x39, false}},
        {"capslock", {0x3A, false}},
        {"f1", {0x3B, false}},
        {"f2", {0x3C, false}},
        {"f3", {0x3D, false}},
        {"f4", {0x3E, false}},
        {"f5", {0x3F, false}},
        {"f6", {0x40, false}},
        {"f7", {0x41, false}},
        {"f8", {0x42, false}},
        {"f9", {0x43, false}},
        {"f10", {0x44, false}},
        {"numlock", {0x45, false}},
        {"scrolllock", {0x46, false}},
        {"numpad7", {0x47, false}},
        {"numpad8", {0x48, false}},
        {"numpad9", {0x49, false}},
        {"numpadminus", {0x4A, false}},
        {"numpad4", {0x4B, false}},
        {"numpad5", {0x4C, false}},
        {"numpad6", {0x4D, false}},
        {"numpadplus", {0x4E, false}},
        {"numpad1", {0x4F, false}},
        {"numpad2", {0x50, false}},
        {"numpad3", {0x51, false}},
        {"numpad0", {0x52, false}},
        {"numpaddelete", {0x53, false}},
        {"f11", {0x57, false}},
        {"f12", {0x58, false}},
        {"rightcontrol", {0x1D, true}},
        {"printscreen", {0x37, true}},
        {"rightalt", {0x38, true}},
        {"pause", {0x45, true}},
        {"home", {0x47, true}},
        {"up", {0x48, true}},
        {"pageup", {0x49, true}},
        {"left", {0x4B, true}},
        {"right", {0x4D, true}},
        {"end", {0x4F, true}},
        {"down", {0x50, true}},
        {"pagedown", {0x51, true}},
        {"insert", {0x52, true}},
        {"delete", {0x53, true}},
        {"leftwindows", {0x5B, true}},
        {"rightwindows", {0x5C, true}},
        {"menu", {0x5D, true}},
        {"numpadenter", {0x1C, true}},
        {"numpadslash", {0x35, true}}
    };
}

// 将键名（如 "W"、"Up"）转换为扫描码和扩展标志（不区分大小写）
bool keyname_to_scancode(const char* name, int* scancode, bool* extended) {
    static const auto key_map = build_key_map();
    std::string key = name;
    for (auto& c : key) c = tolower(c);
    auto it = key_map.find(key);
    if (it != key_map.end()) {
        *scancode = it->second.first;
        *extended = it->second.second;
        return true;
    }
    return false;
}

// 方向键映射表 [上, 下, 左, 右]
struct {
    int scancode;
    bool extended;
} mapping[4];

// 初始化默认映射（使用内置键位表中的原始方向键）
void init_default_mapping() {
    // 方向键原始扫描码在表中的 key_name 为 "up","down","left","right"
    int sc;
    bool ext;
    if (keyname_to_scancode("up", &sc, &ext)) {
        mapping[0] = {sc, ext};
    } else {
        mapping[0] = {0x48, true}; // fallback
    }
    if (keyname_to_scancode("down", &sc, &ext)) {
        mapping[1] = {sc, ext};
    } else {
        mapping[1] = {0x50, true};
    }
    if (keyname_to_scancode("left", &sc, &ext)) {
        mapping[2] = {sc, ext};
    } else {
        mapping[2] = {0x4B, true};
    }
    if (keyname_to_scancode("right", &sc, &ext)) {
        mapping[3] = {sc, ext};
    } else {
        mapping[3] = {0x4D, true};
    }
}

// 根据扫描码和扩展标志获取方向索引（0-3），若不是方向键则返回 -1
int get_direction_index(int scancode, bool is_extended) {
    for (int i = 0; i < 4; ++i) {
        if (mapping[i].scancode == scancode && mapping[i].extended == is_extended)
            return i;
    }
    return -1;
}

// 日志输出（使用方向索引）
void log_event(int dir_idx, bool is_press, bool is_delayed,
               double orig_time, double send_time) {
    if (!debug_mode) return;
    const char* action = is_press ? "press " : "release";
    const char* dir_name = dir_names[dir_idx];
    if (is_delayed) {
        printf("[%s] %s delayed: orig %.3f, sent %.3f (diff %.3f ms)\n",
               dir_name, action, orig_time, send_time, (send_time - orig_time) * 1000.0);
    } else {
        printf("[%s] %s at %.3f\n", dir_name, action, orig_time);
    }
}

// 判断两个方向是否属于不同组（上下 vs 左右）
bool is_opposite_group(int idx1, int idx2) {
    return (idx1 / 2) != (idx2 / 2);
}

struct DelayedKey {
    int device;
    InterceptionKeyStroke stroke;
    int dir_idx;
    double deadline;
    double orig_time;
    bool is_press;
};

int main(int argc, char* argv[]) {
    timeBeginPeriod(1);

    double buffer_time = DEFAULT_BUFFER_TIME;
    double check_time  = DEFAULT_CHECK_TIME;
    init_default_mapping();
    
    // 解析命令行参数
    int num_numeric = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        }
        else if (strcmp(argv[i], "--up") == 0 && i+1 < argc) {
            if (!keyname_to_scancode(argv[++i], &mapping[0].scancode, &mapping[0].extended)) {
                std::cerr << "Invalid key name for --up: " << argv[i] << std::endl;
                return 1;
            }
        }
        else if (strcmp(argv[i], "--down") == 0 && i+1 < argc) {
            if (!keyname_to_scancode(argv[++i], &mapping[1].scancode, &mapping[1].extended)) {
                std::cerr << "Invalid key name for --down: " << argv[i] << std::endl;
                return 1;
            }
        }
        else if (strcmp(argv[i], "--left") == 0 && i+1 < argc) {
            if (!keyname_to_scancode(argv[++i], &mapping[2].scancode, &mapping[2].extended)) {
                std::cerr << "Invalid key name for --left: " << argv[i] << std::endl;
                return 1;
            }
        }
        else if (strcmp(argv[i], "--right") == 0 && i+1 < argc) {
            if (!keyname_to_scancode(argv[++i], &mapping[3].scancode, &mapping[3].extended)) {
                std::cerr << "Invalid key name for --right: " << argv[i] << std::endl;
                return 1;
            }
        }
        else {
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
                std::cerr << "Usage: " << argv[0] << " [buffer_time_sec] [check_time_sec] [-d|--debug]\n"
                          << "       [--up key] [--down key] [--left key] [--right key]\n"
                          << "Example: " << argv[0] << " --up W --down S --left A --right D" << std::endl;
                return 1;
            }
        }
    }

    int timeout_ms = static_cast<int>(buffer_time * 1000 / 2);
    if (timeout_ms < 1) timeout_ms = 1;

    std::cout << "Direction key merger started. Buffer time = " << buffer_time
              << " sec, Check time = " << check_time << " sec." << std::endl;
    std::cout << "Mapping: Up=" << mapping[0].scancode << (mapping[0].extended ? "(ext)" : "")
              << " Down=" << mapping[1].scancode << (mapping[1].extended ? "(ext)" : "")
              << " Left=" << mapping[2].scancode << (mapping[2].extended ? "(ext)" : "")
              << " Right=" << mapping[3].scancode << (mapping[3].extended ? "(ext)" : "") << std::endl;
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
                           int dir_idx, bool is_press, double orig_time, double send_time, bool is_delayed_flag) {
        interception_send(context, device,
                          reinterpret_cast<const InterceptionStroke*>(&stroke), 1);
        log_event(dir_idx, is_press, is_delayed_flag, orig_time, send_time);
    };

    auto update_state = [&](int idx, bool is_press, double time_sec) {
        if (is_press) last_press[idx] = time_sec;
        else last_release[idx] = time_sec;
    };

    auto cancel_delayed = [&](double now_sec) {
        if (!has_delayed) return;
        send_stroke(delayed.device, delayed.stroke, delayed.dir_idx, delayed.is_press,
                    delayed.orig_time, now_sec, true);
        update_state(delayed.dir_idx, delayed.is_press, now_sec);
        has_delayed = false;
    };

    auto check_timeout = [&](double now_sec) {
        if (has_delayed && now_sec >= delayed.deadline) {
            if (debug_mode) printf("[Timeout] Sending delayed key %s\n", dir_names[delayed.dir_idx]);
            cancel_delayed(now_sec);
        }
    };

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
        int dir_idx = get_direction_index(stroke.code, is_extended);
        bool is_press = (stroke.state & INTERCEPTION_KEY_UP) == 0;

        if (dir_idx == -1) {
            // 非方向键，直接发送
            interception_send(context, device,
                              reinterpret_cast<InterceptionStroke*>(&stroke), 1);
            continue;
        }

        // 方向键处理
        if (has_delayed) {
            if (delayed.dir_idx == dir_idx) {
                cancel_delayed(now_sec);
                if (!delayed.is_press == is_press){         //正常直角转弯 →按下↑按下→松开→按下↑松开 ，有可能会因为 “→按下↑按下” 使得  “→松开” 被delay，等待“↑松开”。这里检测”→按下“，直接取消delay，并发送自身
                    send_stroke(device, stroke, dir_idx, is_press, now_sec, now_sec, false);
                    continue;
                }
            }
            else if (is_opposite_group(delayed.dir_idx, dir_idx) &&
                     delayed.is_press == is_press &&
                     has_recent_opposite(dir_idx, now_sec, is_press)) {
                if (debug_mode) {
                    printf("[Combine] Sending delayed %s and current %s together\n",
                           dir_names[delayed.dir_idx], dir_names[dir_idx]);
                }
                send_stroke(delayed.device, delayed.stroke, delayed.dir_idx, delayed.is_press,
                            delayed.orig_time, now_sec, true);
                update_state(delayed.dir_idx, delayed.is_press, now_sec);
                send_stroke(device, stroke, dir_idx, is_press, now_sec, now_sec, false);
                update_state(dir_idx, is_press, now_sec);
                has_delayed = false;
                continue;
            }
            else {
                send_stroke(device, stroke, dir_idx, is_press, now_sec, now_sec, false);
                update_state(dir_idx, is_press, now_sec);
                continue;
            }
        }

        if (should_delay(dir_idx, is_press, now_sec)) {
            delayed = { device, stroke, dir_idx, now_sec + buffer_time, now_sec, is_press };
            has_delayed = true;
            if (debug_mode) {
                printf("[Delay] %s %s will be delayed until %.3f\n",
                       dir_names[dir_idx], is_press ? "press" : "release", delayed.deadline);
            }
        } else {
            send_stroke(device, stroke, dir_idx, is_press, now_sec, now_sec, false);
            update_state(dir_idx, is_press, now_sec);
        }
    }

    interception_destroy_context(context);
    timeEndPeriod(1);
    return 0;
}
