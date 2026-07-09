#include "filesystemarchive.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>

#include "pathutil.hpp"

#include <components/debug/debuglog.hpp>
#include <components/files/constrainedfilestream.hpp>
#include <components/files/conversion.hpp>

namespace VFS
{

    FileSystemArchive::FileSystemArchive(const std::filesystem::path& path,
        const std::filesystem::path& cacheFile)
        : mPath(path)
    {
        if (!cacheFile.empty() && loadCache(cacheFile))
            return;

        walkDirectory();

        if (!cacheFile.empty())
            saveCache(cacheFile);
    }

    void FileSystemArchive::walkDirectory()
    {
        const auto str = mPath.u8string();
        std::size_t prefix = str.size();

        if (prefix > 0 && str[prefix - 1] != '\\' && str[prefix - 1] != '/')
            ++prefix;

        std::filesystem::recursive_directory_iterator iterator(mPath);

        for (auto it = std::filesystem::begin(iterator), end = std::filesystem::end(iterator); it != end;)
        {
            const std::filesystem::directory_entry& entry = *it;

            if (!entry.is_directory())
            {
                const std::filesystem::path& filePath = entry.path();
                const std::string proper = Files::pathToUnicodeString(filePath);
                VFS::Path::Normalized searchable(std::string_view{ proper }.substr(prefix));
                FileSystemArchiveFile file(filePath);

                const auto inserted = mIndex.emplace(std::move(searchable), std::move(file));
                if (!inserted.second)
                    Log(Debug::Warning)
                        << "Found duplicate file for '" << proper
                        << "', please check your file system for two files with the same name in different cases.";
            }

            const std::filesystem::path prevPath = entry.path();
            std::error_code ec;
            it.increment(ec);
            if (ec != std::error_code())
                throw std::runtime_error("Failed to recursively iterate over \"" + Files::pathToUnicodeString(mPath)
                    + "\" when incrementing to the next item from \"" + Files::pathToUnicodeString(prevPath)
                    + "\": " + ec.message());
        }
    }

    bool FileSystemArchive::loadCache(const std::filesystem::path& cacheFile)
    {
        std::ifstream is(cacheFile, std::ios::binary);
        if (!is)
            return false;

        constexpr std::uint32_t kMagic = 0x43534656; // "VFSC"
        constexpr std::uint32_t kSchema = 1;
        std::uint32_t magic, schema, numEntries;

        auto readU32 = [&](std::uint32_t& v) {
            is.read(reinterpret_cast<char*>(&v), 4);
            return is.good();
        };
        auto readStr = [&](std::string& s) {
            std::uint32_t len;
            if (!readU32(len) || len > 4096) return false;
            s.resize(len);
            is.read(s.data(), len);
            return is.good();
        };

        if (!readU32(magic) || magic != kMagic) return false;
        if (!readU32(schema) || schema != kSchema) return false;

        std::string cachedDir;
        if (!readStr(cachedDir) || cachedDir != Files::pathToUnicodeString(mPath))
            return false;

        if (!readU32(numEntries)) return false;

        for (std::uint32_t i = 0; i < numEntries; ++i)
        {
            std::string key, filepath;
            if (!readStr(key) || !readStr(filepath))
                return false;
            mIndex.emplace(VFS::Path::Normalized(std::move(key)),
                FileSystemArchiveFile(std::filesystem::path(filepath)));
        }

        Log(Debug::Info) << "VFS cache loaded: " << mIndex.size() << " files for " << mPath;
        return true;
    }

    void FileSystemArchive::saveCache(const std::filesystem::path& cacheFile) const
    {
        std::error_code ec;
        std::filesystem::create_directories(cacheFile.parent_path(), ec);

        std::ofstream os(cacheFile, std::ios::binary | std::ios::trunc);
        if (!os) return;

        auto writeU32 = [&](std::uint32_t v) {
            os.write(reinterpret_cast<const char*>(&v), 4);
        };
        auto writeStr = [&](std::string_view s) {
            writeU32(static_cast<std::uint32_t>(s.size()));
            os.write(s.data(), s.size());
        };

        writeU32(0x43534656); // magic
        writeU32(1);          // schema
        writeStr(Files::pathToUnicodeString(mPath));
        writeU32(static_cast<std::uint32_t>(mIndex.size()));

        for (const auto& [key, file] : mIndex)
        {
            writeStr(key.value());
            writeStr(Files::pathToUnicodeString(file.getPath()));
        }

        Log(Debug::Info) << "VFS cache saved: " << mIndex.size() << " files for " << mPath;
    }

    void FileSystemArchive::listResources(FileMap& out)
    {
        for (auto& [k, v] : mIndex)
            out[k] = &v;
    }

    bool FileSystemArchive::contains(Path::NormalizedView file) const
    {
        return mIndex.find(file) != mIndex.end();
    }

    std::string FileSystemArchive::getDescription() const
    {
        return "DIR: " + Files::pathToUnicodeString(mPath);
    }

    // ----------------------------------------------------------------------------------

    FileSystemArchiveFile::FileSystemArchiveFile(const std::filesystem::path& path)
        : mPath(path)
    {
    }

    Files::IStreamPtr FileSystemArchiveFile::open()
    {
        return Files::openConstrainedFileStream(mPath);
    }

    std::filesystem::file_time_type FileSystemArchiveFile::getLastModified() const
    {
        return std::filesystem::last_write_time(mPath);
    }

    std::string FileSystemArchiveFile::getStem() const
    {
        return Files::pathToUnicodeString(mPath.stem());
    }

}
