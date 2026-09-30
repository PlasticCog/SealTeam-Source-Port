#include "platform/crash.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#endif

namespace st {

namespace {

std::string g_reportPath;

#if defined(_WIN32)

const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    default: return "exception";
    }
}

void writeTrace(FILE* f, EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    std::fprintf(f, "SEAL Team source port crash: %s (0x%08lx) at %p\n", exceptionName(r->ExceptionCode),
                 (unsigned long)r->ExceptionCode, r->ExceptionAddress);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        std::fprintf(f, "  %s address %p\n", r->ExceptionInformation[0] ? "writing" : "reading",
                     (void*)r->ExceptionInformation[1]);

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 frame;
    std::memset(&frame, 0, sizeof frame);
#if defined(_M_X64) || defined(__x86_64__)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

    char symBuf[sizeof(SYMBOL_INFO) + 512];
    for (int depth = 0; depth < 40; ++depth) {
        if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        const char* name = SymFromAddr(process, frame.AddrPC.Offset, &disp, sym) ? sym->Name : "?";
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof line;
        DWORD lineDisp = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line))
            std::fprintf(f, "  #%-2d %s + 0x%llx  (%s:%lu)\n", depth, name, (unsigned long long)disp, line.FileName,
                         (unsigned long)line.LineNumber);
        else
            std::fprintf(f, "  #%-2d %s + 0x%llx\n", depth, name, (unsigned long long)disp);
    }
    std::fflush(f);
}

LONG WINAPI onException(EXCEPTION_POINTERS* ep) {
    writeTrace(stderr, ep);
    if (FILE* f = std::fopen(g_reportPath.c_str(), "w")) {
        writeTrace(f, ep);
        std::fclose(f);
        std::fprintf(stderr, "crash report written to %s\n", g_reportPath.c_str());
    }
    return EXCEPTION_CONTINUE_SEARCH;  // let Windows finish (dialog / exit code)
}

#endif

} // namespace

void installCrashHandler(const std::string& reportDir) {
    g_reportPath = reportDir;
    if (!g_reportPath.empty() && g_reportPath.back() != '/' && g_reportPath.back() != '\\') g_reportPath += '/';
    g_reportPath += "sealteam-crash.txt";
#if defined(_WIN32)
    SetUnhandledExceptionFilter(onException);
#endif
}

} // namespace st
