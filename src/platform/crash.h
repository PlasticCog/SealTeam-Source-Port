// Crash reporting: on an unhandled fault, write a stack trace with symbols
// to sealteam-crash.txt next to the settings file (and to stderr), so a
// crash can be reported with something more useful than "it crashed".
#pragma once

#include <string>

namespace st {

void installCrashHandler(const std::string& reportDir);

} // namespace st
