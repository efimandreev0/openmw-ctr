#ifndef OPENMW_COMPONENTS_VFS_RECURSIVEDIRECTORYITERATOR_H
#define OPENMW_COMPONENTS_VFS_RECURSIVEDIRECTORYITERATOR_H

#include <string>

#include "filemap.hpp"
#include "pathutil.hpp"

namespace VFS
{
    class RecursiveDirectoryIterator
    {
    public:
        // Path::Normalized on desktop, Path::NormalizedView on 3DS (the index
        // stores views into archive-owned name storage there).
        using KeyType = FileMap::key_type;

        RecursiveDirectoryIterator(FileMap::const_iterator it)
            : mIt(it)
        {
        }

        const KeyType& operator*() const { return mIt->first; }

        const KeyType* operator->() const { return &mIt->first; }

        RecursiveDirectoryIterator& operator++()
        {
            ++mIt;
            return *this;
        }

        friend bool operator==(const RecursiveDirectoryIterator& lhs, const RecursiveDirectoryIterator& rhs) = default;

    private:
        FileMap::const_iterator mIt;
    };

    class RecursiveDirectoryRange
    {
    public:
        RecursiveDirectoryRange(RecursiveDirectoryIterator first, RecursiveDirectoryIterator last)
            : mBegin(first)
            , mEnd(last)
        {
        }

        RecursiveDirectoryIterator begin() const { return mBegin; }

        RecursiveDirectoryIterator end() const { return mEnd; }

    private:
        RecursiveDirectoryIterator mBegin;
        RecursiveDirectoryIterator mEnd;
    };
}

#endif
