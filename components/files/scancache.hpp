#ifndef COMPONENTS_FILES_SCANCACHE_HPP
#define COMPONENTS_FILES_SCANCACHE_HPP

#include <filesystem>
#include <map>
#include <string>

#include <components/misc/strings/algorithm.hpp>

#include "multidircollection.hpp"

namespace Files
{
    // Cache of Files::Collections per-extension scan results. Keyed by directory list;
    // whole-cache validity only. Vita-only optimization to skip filesystem walks on boot.

    using CollectionsMap = std::map<std::string, MultiDirCollection, Misc::StringUtils::CiComp>;

    bool loadScanCache(const std::filesystem::path& cachePath,
        const PathContainer& directories,
        CollectionsMap& collections);

    bool saveScanCache(const std::filesystem::path& cachePath,
        const PathContainer& directories,
        const CollectionsMap& collections);

    void clearScanCache(const std::filesystem::path& cachePath);
}

#endif
