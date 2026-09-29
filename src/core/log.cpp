#include "core/common.h"

#include <cstdarg>
#include <cstdio>

namespace st {

static void vlog(FILE* f, const char* tag, const char* fmt, va_list ap) {
    std::fprintf(f, "[%s] ", tag);
    std::vfprintf(f, fmt, ap);
    std::fputc('\n', f);
    std::fflush(f);
}

void logInfo(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stdout, "info", fmt, ap);
    va_end(ap);
}

void logWarn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "warn", fmt, ap);
    va_end(ap);
}

void logError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "error", fmt, ap);
    va_end(ap);
}

void fatal(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    throw FatalError(buf);
}

} // namespace st
