#if defined(__vita__) || defined(__3DS__)

#include "vitapath.hpp"

#include <cstdlib>

// Per-platform root locations. On Vita: ux0:data/openmw (user) + app0: (VPK,
// read-only). On 3DS: sdmc:/3ds/openmw (user) + romfs:/ (CIA, read-only).
#ifdef __3DS__
#define OPENMW_USER_ROOT "sdmc:/3ds/openmw"
#define OPENMW_APP_ROOT "romfs:/"
#else
#define OPENMW_USER_ROOT "ux0:data/openmw"
#define OPENMW_APP_ROOT "app0:"
#endif

namespace Files
{
    VitaPath::VitaPath(const std::string& applicationName)
        : mName(applicationName)
    {
    }

    std::filesystem::path VitaPath::getUserConfigPath() const
    {
        return std::filesystem::path(OPENMW_USER_ROOT "/config");
    }

    std::filesystem::path VitaPath::getUserDataPath() const
    {
        return std::filesystem::path(OPENMW_USER_ROOT);
    }

    std::filesystem::path VitaPath::getGlobalConfigPath() const
    {
        // Same as user config — no system-wide config location
        return std::filesystem::path(OPENMW_USER_ROOT "/config");
    }

    std::filesystem::path VitaPath::getLocalPath() const
    {
        // Read-only application directory (VPK / romfs)
        return std::filesystem::path(OPENMW_APP_ROOT);
    }

    std::filesystem::path VitaPath::getGlobalDataPath() const
    {
        return std::filesystem::path(OPENMW_USER_ROOT);
    }

    std::filesystem::path VitaPath::getCachePath() const
    {
        return std::filesystem::path(OPENMW_USER_ROOT "/cache");
    }

    std::vector<std::filesystem::path> VitaPath::getInstallPaths() const
    {
        return {};
    }

} /* namespace Files */

#endif /* __vita__ */
