#include "game/devtools.h"

#include <cstdio>
#include <map>

namespace st::game {

namespace {

struct Entry {
    std::string help;
    std::function<int(const DevArgs&)> fn;
};

std::map<std::string, Entry>& registry() {
    static std::map<std::string, Entry> r;
    return r;
}

} // namespace

DevCommand::DevCommand(const char* name, const char* help, std::function<int(const DevArgs&)> fn) {
    registry()[name] = Entry{help, std::move(fn)};
}

bool runDevCommand(const std::vector<std::string>& args, int& rc) {
    for (size_t i = 1; i < args.size(); ++i) {
        const auto it = registry().find(args[i]);
        if (it == registry().end()) continue;
        const DevArgs rest(args.begin() + long(i) + 1, args.end());
        rc = it->second.fn(rest);
        return true;
    }
    return false;
}

void printDevCommands() {
    for (const auto& [name, e] : registry()) std::printf("  %-18s %s\n", name.c_str(), e.help.c_str());
}

} // namespace st::game
