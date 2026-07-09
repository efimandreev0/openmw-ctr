#ifndef OPENMW_COMPONENTS_RESOURCE_FILESYSTEMARCHIVE_H
#define OPENMW_COMPONENTS_RESOURCE_FILESYSTEMARCHIVE_H

#include "archive.hpp"
#include "file.hpp"

#include <filesystem>
#include <string>

namespace VFS
{

    class FileSystemArchiveFile : public File
    {
    public:
        FileSystemArchiveFile(const std::filesystem::path& path);

        Files::IStreamPtr open() override;

        std::filesystem::file_time_type getLastModified() const override;

        std::string getStem() const override;

        const std::filesystem::path& getPath() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    class FileSystemArchive : public Archive
    {
    public:
        /// Walk `path` recursively. If `cacheFile` is set, load that instead and
        /// write it on cache miss.
        FileSystemArchive(const std::filesystem::path& path,
            const std::filesystem::path& cacheFile = {});

        void listResources(FileMap& out) override;

        bool contains(Path::NormalizedView file) const override;

        std::string getDescription() const override;

    private:
        bool loadCache(const std::filesystem::path& cacheFile);
        void saveCache(const std::filesystem::path& cacheFile) const;
        void walkDirectory();

        std::map<VFS::Path::Normalized, FileSystemArchiveFile, std::less<>> mIndex;
        std::filesystem::path mPath;
    };

}

#endif
