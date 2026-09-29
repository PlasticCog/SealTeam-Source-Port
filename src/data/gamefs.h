// Access to the user's original SEAL Team installation. DOS file names are
// case-insensitive, so lookups ignore case on every host OS.
#pragma once

#include "core/common.h"

#include <string>
#include <vector>

namespace st {

class GameFS {
public:
    // The original game files live in a folder named "Game". It is looked up
    // next to the executable, one or two levels above it (build trees), then
    // in the working directory. `explicitDir` (--data) overrides the search.
    static constexpr const char* kFolderName = "Game";
    bool init(const std::string& explicitDir, const std::string& exeDir);

    const std::string& dir() const { return dir_; }

    // Full host path of a DOS file name, or empty if it doesn't exist.
    std::string resolve(const std::string& dosName) const;
    bool exists(const std::string& dosName) const { return !resolve(dosName).empty(); }
    bool readFile(const std::string& dosName, std::vector<u8>& out) const;
    bool writeFile(const std::string& dosName, const std::vector<u8>& data) const;

private:
    std::string dir_;
};

GameFS& gameFS();

} // namespace st
