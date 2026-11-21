// Timestamped logging to stderr: "<wall ISO time> <mono ms> <LEVEL> [component] message".
// Thread-safe, printf-style, never throws. On QNX stderr can be redirected to slog2 later.
// TODO(qnx-check): consider slog2f() (libslog2) so logs show in `slog2info` on the Pi.
#pragma once

#include <cstdarg>

namespace brim::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void set_level(Level l);
void set_process_name(const char* name);
void write(Level l, const char* component, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

}  // namespace brim::log

#define BRIM_LOGD(comp, ...) ::brim::log::write(::brim::log::Level::Debug, comp, __VA_ARGS__)
#define BRIM_LOGI(comp, ...) ::brim::log::write(::brim::log::Level::Info, comp, __VA_ARGS__)
#define BRIM_LOGW(comp, ...) ::brim::log::write(::brim::log::Level::Warn, comp, __VA_ARGS__)
#define BRIM_LOGE(comp, ...) ::brim::log::write(::brim::log::Level::Error, comp, __VA_ARGS__)
