#include "registerarchives.hpp"

#include <filesystem>
#include <set>
#include <stdexcept>

#include <components/debug/debuglog.hpp>

#include <components/vfs/bsaarchive.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>

namespace VFS
{

    void registerArchives(VFS::Manager* vfs, const Files::Collections& collections,
        const std::vector<std::string>& archives, bool useLooseFiles, const ToUTF8::StatelessUtf8Encoder* encoder,
        const std::filesystem::path& vfsCacheDir)
    {
        const Files::PathContainer& dataDirs = collections.getPaths();

        for (std::vector<std::string>::const_iterator archive = archives.begin(); archive != archives.end(); ++archive)
        {
            if (collections.doesExist(*archive))
            {
                // Last BSA has the highest priority
                const auto archivePath = collections.getPath(*archive);
                Log(Debug::Info) << "Adding BSA archive " << archivePath;
                vfs->addArchive(makeBsaArchive(archivePath, encoder));
            }
            else
            {
                throw std::runtime_error("Archive '" + *archive + "' not found");
            }
        }

        if (useLooseFiles)
        {
            std::set<std::filesystem::path> seen;
            int dirIdx = 0;
            for (const auto& dataDir : dataDirs)
            {
                if (seen.insert(dataDir).second)
                {
                    Log(Debug::Info) << "Adding data directory " << dataDir;
                    std::filesystem::path cacheFile;
#if defined(__vita__) || defined(__3DS__)
                    if (!vfsCacheDir.empty())
                        cacheFile = vfsCacheDir / ("vfs_dir_" + std::to_string(dirIdx) + ".bin");
#endif
                    vfs->addArchive(std::make_unique<FileSystemArchive>(dataDir, cacheFile));
                }
                else
                    Log(Debug::Info) << "Ignoring duplicate data directory " << dataDir;
                ++dirIdx;
            }
        }

        vfs->buildIndex();
    }

}
