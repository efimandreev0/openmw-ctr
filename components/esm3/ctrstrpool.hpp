#ifndef OPENMW_ESM3_CTRSTRPOOL_H
#define OPENMW_ESM3_CTRSTRPOOL_H

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace ESM
{
    // 3DS: SD-backed side pool for bulky record strings (dialogue responses,
    // dialogue result scripts, book texts). During content load the loader
    // hands each string to put() and keeps only {offset,len}; get() reads the
    // string back on demand. Original Morrowind streamed this data from disk
    // too — OpenMW's keep-everything-resident store does not fit in the 3DS's
    // ~125MB arena.
    //
    // First boot writes the pool file; later boots with the same content
    // fingerprint skip the writes (single-threaded parse order is
    // deterministic, so recomputed offsets match the file). Inactive unless
    // init() ran, so desktop builds and dynamic (save-game) records keep
    // their strings inline.
    class CtrStrPool
    {
    public:
        static CtrStrPool& instance();

        void init(const std::string& path, uint64_t fingerprint);
        // Stop accepting strings; the pool stays readable.
        void finalize();

        bool active() const { return mMode == Mode::Build || mMode == Mode::Reuse; }

        // Content-set fingerprint passed to init(); 0 until init() succeeds.
        // Other SD caches key on it too (e.g. the ref-count cache).
        uint64_t fingerprint() const { return mFingerprint; }

        // Returns false (and stores nothing) when inactive or s is empty —
        // the caller keeps the string inline.
        bool put(std::string_view s, uint32_t& ofs, uint32_t& len);
        std::string get(uint32_t ofs, uint32_t len) const;

    private:
        enum class Mode
        {
            Disabled,
            Build, // writing pool file while loading
            Reuse, // valid pool on disk; advance offsets without writing
            ReadOnly // after finalize()
        };

        Mode mMode = Mode::Disabled;
        mutable std::mutex mMutex;
        mutable FILE* mFile = nullptr;
        uint32_t mCursor = 0;
        uint32_t mReuseSize = 0;
        uint64_t mFingerprint = 0;
        std::string mPath;
    };
}

#endif
