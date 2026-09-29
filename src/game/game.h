// Top level of the ported game (original main() in segment 1000).
#pragma once

#include <string>
#include <vector>

namespace st::game {

// Equivalent of the original main(argc, argv). Returns the process exit code.
int run(const std::vector<std::string>& args);

} // namespace st::game
