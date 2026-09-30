// Developer commands: test entry points selected on the command line
// (e.g. `sealteam --view-world 3 --shot out.bmp`). Modules register them with
// a static DevCommand object in their own .cpp file, so adding one never
// requires editing game.cpp. Commands run after the normal start-up (archives,
// graphics, fonts, cursor, input, sound) and their return value becomes the
// exit code.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace st::game {

using DevArgs = std::vector<std::string>;  // arguments following the command name

struct DevCommand {
    DevCommand(const char* name, const char* help, std::function<int(const DevArgs&)> fn);
};

// Returns true and sets `rc` if `args` selects a registered command.
bool runDevCommand(const std::vector<std::string>& args, int& rc);
void printDevCommands();

} // namespace st::game
