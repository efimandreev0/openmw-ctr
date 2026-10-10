#include "constrainedfilestreambuf.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>

namespace Files
{
    namespace File = Platform::File;

    ConstrainedFileStreamBuf::ConstrainedFileStreamBuf(
        const std::filesystem::path& fname, std::size_t start, std::size_t length)
        : mOrigin(start)
    {
        mFile = File::open(fname);
        mSize = length != std::numeric_limits<std::size_t>::max() ? length : File::size(mFile) - start;

        if (start != 0)
            File::seek(mFile, start);

        setg(nullptr, nullptr, nullptr);
    }

    std::streambuf::int_type ConstrainedFileStreamBuf::underflow()
    {
        if (gptr() == egptr())
        {
            const std::size_t toRead = std::min(mSize - mFilePos, sizeof(mBuffer));
            // Read in the next chunk of data, and set the read pointers on success
            // Failure will throw exception.
            const std::size_t got = File::read(mFile, mBuffer, toRead);
            mBufferPos = mFilePos;
            mFilePos += got;
            setg(mBuffer, mBuffer, mBuffer + got);
        }
        if (gptr() == egptr())
            return traits_type::eof();

        return traits_type::to_int_type(*gptr());
    }

    std::streambuf::pos_type ConstrainedFileStreamBuf::seekTo(std::size_t position)
    {
        if (position > mSize)
            return traits_type::eof();

        if (eback() != nullptr && position >= mBufferPos
            && position <= mBufferPos + static_cast<std::size_t>(egptr() - eback()))
        {
            setg(eback(), eback() + (position - mBufferPos), egptr());
            return position;
        }

        File::seek(mFile, mOrigin + position);
        mFilePos = position;

        setg(nullptr, nullptr, nullptr);
        return position;
    }

    std::streambuf::pos_type ConstrainedFileStreamBuf::seekoff(
        off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode)
    {
        if ((mode & std::ios_base::out) || !(mode & std::ios_base::in))
            return traits_type::eof();

        // new file position, relative to mOrigin
        size_t newPos;
        switch (whence)
        {
            case std::ios_base::beg:
                newPos = offset;
                break;
            case std::ios_base::cur:
                newPos = (eback() != nullptr ? mBufferPos + (gptr() - eback()) : mFilePos) + offset;
                break;
            case std::ios_base::end:
                newPos = mSize + offset;
                break;
            default:
                return traits_type::eof();
        }

        return seekTo(newPos);
    }

    std::streambuf::pos_type ConstrainedFileStreamBuf::seekpos(pos_type pos, std::ios_base::openmode mode)
    {
        if ((mode & std::ios_base::out) || !(mode & std::ios_base::in))
            return traits_type::eof();

        return seekTo(static_cast<std::size_t>(pos));
    }
}
