#ifndef OPENMW_ESM3_CTRINFOBLOB_H
#define OPENMW_ESM3_CTRINFOBLOB_H

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace ESM
{
    struct DialInfo;

    // 3DS: compact storage for dialogue INFO records. After ESMStore::setUp
    // every Dialogue's ordered std::list<DialInfo> is serialized into one
    // flat byte blob and the lists are freed: ~32k DialInfo cost ~9-11MB as
    // lists (list nodes + 9 RefIds + std::strings + select vectors each), but
    // only ~2-3MB serialized — the bulky response/result text already lives
    // in the CtrStrPool on SD and is referenced by {offset,len}.
    //
    // Topics rematerialize on demand through Dialogue::readInfos(), backed by
    // a small LRU here. Entries are pinned by shared_ptr while any handle is
    // alive, so iteration and short-lived DialInfo pointers (Filter results
    // consumed in the same scope) stay valid; the LRU additionally keeps the
    // most recently fetched topics alive as a recency cushion.
    class CtrInfoBlob
    {
    public:
        using InfoList = std::list<DialInfo>;

        static constexpr uint32_t sNoSection = 0xFFFFFFFFu;

        static CtrInfoBlob& instance();

        // Serialize a topic's ordered infos and return its section handle.
        // Call once per topic during the bake pass.
        uint32_t bake(const InfoList& infos);

        // Rematerialize a topic. Never returns nullptr for a handle returned
        // by bake(); returns nullptr for sNoSection.
        std::shared_ptr<const InfoList> fetch(uint32_t section) const;

        // Release excess vector capacity after the last bake and log stats.
        void seal();

        size_t blobBytes() const { return mBlob.size(); }

    private:
        std::vector<uint8_t> mBlob;

        struct CacheEntry
        {
            std::shared_ptr<const InfoList> mList;
            uint64_t mStamp = 0;
        };

        mutable std::mutex mMutex;
        mutable std::map<uint32_t, CacheEntry> mCache;
        mutable uint64_t mStamp = 0;

        std::shared_ptr<const InfoList> materialize(uint32_t section) const;
        uint32_t sectionBytes(uint32_t section) const;
    };
}

#endif
