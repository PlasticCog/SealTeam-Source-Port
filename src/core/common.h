// Common types and helpers shared by the whole port.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace st {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

// Thrown from the platform layer when the user closes the window. The original
// game is written as nested blocking loops (menus wait for keys, the mission
// loop runs until extraction...), so unwinding with an exception is the
// simplest way to leave from anywhere without restructuring that control flow.
struct QuitRequested {};

// Fatal error equivalent to the original "Couldn't init ..." style aborts.
class FatalError : public std::runtime_error {
public:
    explicit FatalError(const std::string& msg) : std::runtime_error(msg) {}
};

void logInfo(const char* fmt, ...);
void logWarn(const char* fmt, ...);
void logError(const char* fmt, ...);
[[noreturn]] void fatal(const char* fmt, ...);

inline u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
inline s16 rds16(const u8* p) { return s16(rd16(p)); }
inline u32 rd32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

} // namespace st
