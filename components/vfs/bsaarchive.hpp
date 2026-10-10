#ifndef VFS_BSAARCHIVE_HPP_
#define VFS_BSAARCHIVE_HPP_

#include "archive.hpp"
#include "file.hpp"
#include "pathutil.hpp"

#ifndef OPENMW_CTR_NATIVE
#include <components/bsa/ba2dx10file.hpp>
#include <components/bsa/ba2gnrlfile.hpp>
#endif
#include <components/bsa/bsafile.hpp>
#include <components/bsa/compressedbsafile.hpp>

#include <components/toutf8/toutf8.hpp>

#include <algorithm>
#include <deque>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace VFS
{
    template <typename BSAFileType>
    class BsaArchive;

    template <typename FileType>
    class BsaArchiveFile : public File
    {
    public:
        BsaArchiveFile(const Bsa::BSAFile::FileStruct* info, const BsaArchive<FileType>* bsa)
            : mInfo(info)
            , mFile(bsa)
        {
        }

        Files::IStreamPtr open() override { return mFile->getFile()->getFile(mInfo); }

        std::filesystem::file_time_type getLastModified() const override
        {
            return std::filesystem::last_write_time(mFile->getFile()->getPath());
        }

        std::string getStem() const override
        {
            std::string_view name = mInfo->name();
            auto index = name.find_last_of("\\/");
            if (index != std::string_view::npos)
                name = name.substr(index + 1);
            index = name.find_last_of('.');
            if (index != std::string_view::npos && index != 0)
                name = name.substr(0, index);
            std::string out;
            std::string_view utf8 = mFile->getUtf8(name, out);
            if (out.data() == utf8.data())
                out.resize(utf8.size());
            else
                out = utf8;
            return out;
        }

        const Bsa::BSAFile::FileStruct* mInfo;
        const BsaArchive<FileType>* mFile;
    };

    template <typename BSAFileType>
    class BsaArchive : public Archive
    {
    public:
        BsaArchive(const std::filesystem::path& filename, const ToUTF8::StatelessUtf8Encoder* encoder)
            : Archive()
            , mEncoder(encoder)
        {
            mFile = std::make_unique<BSAFileType>();
            mFile->open(filename);

#ifdef __3DS__
            // Diet: normalize each name inside the BSA's own string table and
            // key the index off views into it, instead of owning every name
            // twice more (the sorted mFiles copy + the FileMap keys) — ~4MB
            // across a full Morrowind load order. Uncompressed BSAs only:
            // compressed/BA2 readers hash the original name on extraction.
            std::string buffer;
            const auto& list = mFile->getList();
            mResources.reserve(list.size());
            mKeys.reserve(list.size());
            for (const Bsa::BSAFile::FileStruct& file : list)
            {
                mResources.emplace_back(&file, this);
                mKeys.push_back(makeKey(file, buffer));
            }
            mFiles = mKeys;
            std::sort(mFiles.begin(), mFiles.end());
#else
            std::string buffer;
            for (const Bsa::BSAFile::FileStruct& file : mFile->getList())
            {
                mResources.emplace_back(&file, this);
                mFiles.emplace_back(getUtf8(file.name(), buffer));
            }

            std::sort(mFiles.begin(), mFiles.end());
#endif
        }

        void listResources(FileMap& out) override
        {
#ifdef __3DS__
            for (std::size_t i = 0; i < mResources.size(); ++i)
                out[mKeys[i]] = &mResources[i];
#else
            std::string buffer;
            for (auto& resource : mResources)
            {
                std::string_view path = getUtf8(resource.mInfo->name(), buffer);
                out[VFS::Path::Normalized(path)] = &resource;
            }
#endif
        }

        bool contains(Path::NormalizedView file) const override
        {
            return std::binary_search(mFiles.begin(), mFiles.end(), file);
        }

        std::string getDescription() const override { return std::string{ "BSA: " } + mFile->getFilename(); }

        BSAFileType* getFile() const { return mFile.get(); }

        std::string_view getUtf8(std::string_view input, std::string& buffer) const
        {
            if (mEncoder == nullptr)
                return input;
            return mEncoder->getUtf8(input, ToUTF8::BufferAllocationPolicy::UseGrowFactor, buffer);
        }

    private:
#ifdef __3DS__
        Path::NormalizedView makeKey(const Bsa::BSAFile::FileStruct& file, std::string& buffer)
        {
            if constexpr (std::is_same_v<BSAFileType, Bsa::BSAFile>)
            {
                // The name table stores each entry null-terminated (mNameSize
                // excludes the null), so an in-place normalized name is a
                // valid C string for NormalizedView.
                char* p = file.mNamesBuffer->data() + file.mNameOffset;
                bool ascii = true;
                for (uint32_t i = 0; i < file.mNameSize; ++i)
                {
                    if (static_cast<unsigned char>(p[i]) >= 0x80)
                    {
                        ascii = false;
                        break;
                    }
                }
                if (ascii)
                {
                    for (uint32_t i = 0; i < file.mNameSize; ++i)
                        p[i] = Path::normalize(p[i]);
                    if (Path::isNormalized(std::string_view(p, file.mNameSize)))
                        return Path::NormalizedView(static_cast<const char*>(p));
                }
            }
            // Fallback (non-ASCII names needing UTF-8 conversion, odd
            // separators, compressed archives): own a copy with a stable
            // address.
            const std::string_view utf8 = getUtf8(file.name(), buffer);
            return mOwnedNames.emplace_back(Path::Normalized(utf8));
        }
#endif

        std::unique_ptr<BSAFileType> mFile;
        std::vector<BsaArchiveFile<BSAFileType>> mResources;
#ifdef __3DS__
        std::vector<Path::NormalizedView> mKeys; // parallel to mResources
        std::vector<Path::NormalizedView> mFiles; // sorted, for contains()
        std::deque<Path::Normalized> mOwnedNames; // fallback key storage
#else
        std::vector<VFS::Path::Normalized> mFiles;
#endif
        const ToUTF8::StatelessUtf8Encoder* mEncoder;
    };

    inline std::unique_ptr<VFS::Archive> makeBsaArchive(
        const std::filesystem::path& path, const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        switch (Bsa::BSAFile::detectVersion(path))
        {
            case Bsa::BsaVersion::Unknown:
                break;
            case Bsa::BsaVersion::Uncompressed:
                return std::make_unique<BsaArchive<Bsa::BSAFile>>(path, encoder);
            case Bsa::BsaVersion::Compressed:
                return std::make_unique<BsaArchive<Bsa::CompressedBSAFile>>(path, encoder);
#ifndef OPENMW_CTR_NATIVE
            case Bsa::BsaVersion::BA2GNRL:
                return std::make_unique<BsaArchive<Bsa::BA2GNRLFile>>(path, encoder);
            case Bsa::BsaVersion::BA2DX10:
                return std::make_unique<BsaArchive<Bsa::BA2DX10File>>(path, encoder);
#endif
        }

        throw std::runtime_error("Unknown archive type '" + Files::pathToUnicodeString(path) + "'");
    }
}

#endif
