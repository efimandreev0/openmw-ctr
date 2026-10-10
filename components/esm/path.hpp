#ifndef OPENMW_COMPONENTS_ESM_PATH_H
#define OPENMW_COMPONENTS_ESM_PATH_H

#include <components/vfs/pathutil.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace ESM
{
    class Path
    {
    public:
#ifdef __3DS__
        const std::string& getOriginal() const { return mNormalized.value(); }

        const VFS::Path::Normalized& getNormalized() const { return mNormalized; }

        bool empty() const { return mNormalized.empty(); }

        void set(std::string&& value) { mNormalized = VFS::Path::Normalized(std::move(value)); }

        void set(std::string_view value) { mNormalized = VFS::Path::Normalized(value); }

        void clear() { mNormalized.clear(); }
#else
        const std::string& getOriginal() const { return mOriginal; }

        const VFS::Path::Normalized& getNormalized() const { return mNormalized; }

        bool empty() const { return mOriginal.empty(); }

        void set(std::string&& value)
        {
            mOriginal = std::move(value);
            mNormalized = VFS::Path::Normalized(mOriginal);
        }

        void set(std::string_view value)
        {
            mOriginal = value;
            mNormalized = VFS::Path::Normalized(mOriginal);
        }

        void clear()
        {
            mOriginal.clear();
            mNormalized.clear();
        }
#endif

        Path& operator=(const std::string& value)
        {
            set(std::string_view(value));
            return *this;
        }

        Path& operator=(std::string&& value)
        {
            set(std::move(value));
            return *this;
        }

        Path& operator=(const char* value)
        {
            set(std::string_view(value));
            return *this;
        }

        Path& operator=(std::string_view value)
        {
            set(value);
            return *this;
        }

    private:
#ifndef __3DS__
        std::string mOriginal;
#endif
        VFS::Path::Normalized mNormalized;
    };
}

#endif
