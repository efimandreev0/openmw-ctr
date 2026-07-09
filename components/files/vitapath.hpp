#ifndef COMPONENTS_FILES_VITAPATH_H
#define COMPONENTS_FILES_VITAPATH_H

#if defined(__vita__) || defined(__3DS__)

#include <filesystem>
#include <vector>

namespace Files
{
    struct VitaPath
    {
        explicit VitaPath(const std::string& applicationName);

        std::filesystem::path getUserConfigPath() const;
        std::filesystem::path getUserDataPath() const;
        std::filesystem::path getGlobalConfigPath() const;
        std::filesystem::path getLocalPath() const;
        std::filesystem::path getGlobalDataPath() const;
        std::filesystem::path getCachePath() const;
        std::vector<std::filesystem::path> getInstallPaths() const;

        std::string mName;
    };

} /* namespace Files */

#endif /* __vita__ */

#endif /* COMPONENTS_FILES_VITAPATH_H */
