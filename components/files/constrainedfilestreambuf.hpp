#ifndef OPENMW_CONSTRAINEDFILESTREAMBUF_H
#define OPENMW_CONSTRAINEDFILESTREAMBUF_H

#include <filesystem>
#include <streambuf>

#include <components/platform/file.hpp>

namespace Files
{
    /// A file streambuf constrained to a specific region in the file, specified by the 'start' and 'length' parameters.
    class ConstrainedFileStreamBuf final : public std::streambuf
    {
    public:
        ConstrainedFileStreamBuf(const std::filesystem::path& fname, std::size_t start, std::size_t length);

        int_type underflow() final;

        pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode) final;

        pos_type seekpos(pos_type pos, std::ios_base::openmode mode) final;

    private:
        pos_type seekTo(std::size_t position);

        std::size_t mOrigin;
        std::size_t mSize;
        std::size_t mFilePos = 0;
        std::size_t mBufferPos = 0;
        Platform::File::ScopedHandle mFile;
#if defined(__vita__) || defined(__3DS__)
        char mBuffer[32768]{ 0 };
#else
        char mBuffer[8192]{ 0 };
#endif
    };
}

#endif
