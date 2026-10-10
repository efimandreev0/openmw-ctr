#include "ctrstrpool.hpp"

#include <cstdio>
#include <cstring>

#include <components/debug/debuglog.hpp>

namespace ESM
{
    namespace
    {
        constexpr char sMagic[8] = { 'O', 'M', 'W', 'S', 'P', 'O', 'L', '2' };
        constexpr uint32_t sHeaderSize = 24; // magic + fingerprint + completed size (0 while building)
        constexpr long sCompletedSizeOffset = 16;
    }

    CtrStrPool& CtrStrPool::instance()
    {
        static CtrStrPool pool;
        return pool;
    }

    void CtrStrPool::init(const std::string& path, uint64_t fingerprint)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mMode != Mode::Disabled)
            return;

        // Try to reuse an existing pool with a matching fingerprint.
        mPath = path;
        if (FILE* f = fopen(path.c_str(), "rb"))
        {
            char magic[8];
            uint64_t fp = 0;
            uint64_t completedSize = 0;
            bool valid = fread(magic, 1, 8, f) == 8 && fread(&fp, 8, 1, f) == 1
                && fread(&completedSize, 8, 1, f) == 1 && memcmp(magic, sMagic, 8) == 0 && fp == fingerprint
                && completedSize >= sHeaderSize;
            if (valid)
            {
                valid = fseek(f, 0, SEEK_END) == 0 && static_cast<uint64_t>(ftell(f)) == completedSize;
                if (!valid)
                    Log(Debug::Warning) << "[CtrStrPool] " << path << " is incomplete, rebuilding it";
            }
            if (valid)
            {
                mFile = f;
                mCursor = sHeaderSize;
                mReuseSize = static_cast<uint32_t>(completedSize);
                mMode = Mode::Reuse;
                mFingerprint = fingerprint;
                Log(Debug::Info) << "[CtrStrPool] Reusing string pool " << path;
                return;
            }
            fclose(f);
        }

        FILE* f = fopen(path.c_str(), "w+b");
        if (!f)
        {
            Log(Debug::Warning) << "[CtrStrPool] Cannot create " << path << " - strings stay in RAM";
            return;
        }
        const uint64_t notCompleted = 0;
        if (fwrite(sMagic, 1, 8, f) != 8 || fwrite(&fingerprint, 8, 1, f) != 1 || fwrite(&notCompleted, 8, 1, f) != 1)
        {
            Log(Debug::Warning) << "[CtrStrPool] Header write failed - strings stay in RAM";
            fclose(f);
            return;
        }
        mFile = f;
        mCursor = sHeaderSize;
        mMode = Mode::Build;
        mFingerprint = fingerprint;
        Log(Debug::Info) << "[CtrStrPool] Building string pool " << path;
    }

    void CtrStrPool::finalize()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mMode == Mode::Build)
        {
            const uint64_t completedSize = mCursor;
            if (fflush(mFile) != 0 || fseek(mFile, sCompletedSizeOffset, SEEK_SET) != 0
                || fwrite(&completedSize, 8, 1, mFile) != 1 || fflush(mFile) != 0)
                Log(Debug::Warning) << "[CtrStrPool] Could not mark " << mPath << " complete; it will be rebuilt";
        }
        else if (mMode == Mode::Reuse && mCursor != mReuseSize)
        {
            Log(Debug::Error) << "[CtrStrPool] " << mPath << " does not match this build (" << mCursor
                              << " != " << mReuseSize << " bytes); it will be rebuilt on the next start";
            fclose(mFile);
            mFile = nullptr;
            std::remove(mPath.c_str());
        }
        if (mMode == Mode::Build || mMode == Mode::Reuse)
        {
            Log(Debug::Info) << "[CtrStrPool] Finalized: " << (mCursor / 1024) << "KB of strings kept on SD";
            mMode = Mode::ReadOnly;
        }
    }

    bool CtrStrPool::put(std::string_view s, uint32_t& ofs, uint32_t& len)
    {
        if (s.empty())
            return false;
        std::lock_guard<std::mutex> lock(mMutex);
        if (mMode == Mode::Build)
        {
            if (fseek(mFile, mCursor, SEEK_SET) != 0 || fwrite(s.data(), 1, s.size(), mFile) != s.size())
            {
                // SD write failure mid-build: everything already deferred has
                // valid offsets, but stop deferring new strings.
                Log(Debug::Warning) << "[CtrStrPool] Write failed at " << mCursor << " - pool frozen";
                mMode = Mode::ReadOnly;
                return false;
            }
        }
        else if (mMode != Mode::Reuse)
            return false;

        ofs = mCursor;
        len = static_cast<uint32_t>(s.size());
        mCursor += static_cast<uint32_t>(s.size());
        return true;
    }

    std::string CtrStrPool::get(uint32_t ofs, uint32_t len) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mFile || len == 0)
            return {};
        std::string out;
        out.resize(len);
        if (fseek(mFile, ofs, SEEK_SET) != 0 || fread(out.data(), 1, len, mFile) != len)
        {
            Log(Debug::Error) << "[CtrStrPool] Read failed at " << ofs << "+" << len;
            return {};
        }
        return out;
    }
}
