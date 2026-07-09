#include "ctrinfoblob.hpp"

#include "loadinfo.hpp"

#include <components/debug/debuglog.hpp>

#include <cstring>

namespace ESM
{
    namespace
    {
        // The LRU holds rematerialized lists beyond the life of the handles
        // so short-lived DialInfo* results (Filter::Response consumed in the
        // caller's scope) survive a few unrelated fetches in between. Sized
        // in section bytes (in-RAM lists cost ~3-4x more, so ~1MB here is
        // ~3MB of lists at worst).
        constexpr size_t sCacheBudgetBytes = 768 * 1024;
        constexpr size_t sCacheMinEntries = 6;

        void putBytes(std::vector<uint8_t>& b, const void* p, size_t n)
        {
            const uint8_t* c = static_cast<const uint8_t*>(p);
            b.insert(b.end(), c, c + n);
        }

        template <class T>
        void putT(std::vector<uint8_t>& b, T v)
        {
            putBytes(b, &v, sizeof(v));
        }

        void putStr16(std::vector<uint8_t>& b, std::string_view s)
        {
            const uint16_t len = static_cast<uint16_t>(s.size());
            putT(b, len);
            putBytes(b, s.data(), len);
        }

        void putStr32(std::vector<uint8_t>& b, std::string_view s)
        {
            const uint32_t len = static_cast<uint32_t>(s.size());
            putT(b, len);
            putBytes(b, s.data(), len);
        }

        void putRefId(std::vector<uint8_t>& b, const RefId& id)
        {
            putStr16(b, id.empty() ? std::string() : id.toString());
        }

        struct Cursor
        {
            const uint8_t* mPtr;

            template <class T>
            T get()
            {
                T v;
                std::memcpy(&v, mPtr, sizeof(v));
                mPtr += sizeof(v);
                return v;
            }

            std::string getStr16()
            {
                const uint16_t len = get<uint16_t>();
                std::string s(reinterpret_cast<const char*>(mPtr), len);
                mPtr += len;
                return s;
            }

            std::string getStr32()
            {
                const uint32_t len = get<uint32_t>();
                std::string s(reinterpret_cast<const char*>(mPtr), len);
                mPtr += len;
                return s;
            }

            RefId getRefId()
            {
                std::string s = getStr16();
                if (s.empty())
                    return RefId();
                return RefId::stringRefId(s);
            }
        };
    }

    CtrInfoBlob& CtrInfoBlob::instance()
    {
        static CtrInfoBlob blob;
        return blob;
    }

    uint32_t CtrInfoBlob::bake(const InfoList& infos)
    {
        const uint32_t section = static_cast<uint32_t>(mBlob.size());

        // u32 section byte length (patched below) — the LRU costs entries by
        // it — then u32 info count.
        putT(mBlob, static_cast<uint32_t>(0));
        putT(mBlob, static_cast<uint32_t>(infos.size()));
        for (const DialInfo& info : infos)
        {
            putT(mBlob, info.mData.mType);
            putT(mBlob, info.mData.mDisposition);
            putT(mBlob, info.mData.mRank);
            putT(mBlob, info.mData.mGender);
            putT(mBlob, info.mData.mPCrank);
            putT(mBlob, static_cast<uint8_t>(info.mQuestStatus));
            putT(mBlob, static_cast<uint8_t>(info.mFactionLess ? 1 : 0));

            putRefId(mBlob, info.mId);
            putRefId(mBlob, info.mActor);
            putRefId(mBlob, info.mRace);
            putRefId(mBlob, info.mClass);
            putRefId(mBlob, info.mFaction);
            putRefId(mBlob, info.mPcFaction);
            putRefId(mBlob, info.mCell);
            putStr16(mBlob, info.mSound);

            putT(mBlob, info.mResponseOfs);
            putT(mBlob, info.mResponseLen);
            putT(mBlob, info.mResultOfs);
            putT(mBlob, info.mResultLen);
            // Inline fallbacks (empty whenever the SD pool took the string).
            putStr32(mBlob, info.mResponse);
            putStr32(mBlob, info.mResultScript);

            putT(mBlob, static_cast<uint16_t>(info.mSelects.size()));
            for (const DialogueCondition& cond : info.mSelects)
            {
                putT(mBlob, static_cast<int8_t>(cond.mFunction));
                putT(mBlob, static_cast<char>(cond.mComparison));
                putT(mBlob, cond.mIndex);
                uint8_t kind = static_cast<uint8_t>(cond.mValue.index());
                putT(mBlob, kind);
                if (kind == 0)
                    putT(mBlob, std::get<int32_t>(cond.mValue));
                else
                    putT(mBlob, std::get<float>(cond.mValue));
                putStr16(mBlob, cond.mVariable);
            }
        }

        const uint32_t sectionBytes = static_cast<uint32_t>(mBlob.size()) - section;
        std::memcpy(mBlob.data() + section, &sectionBytes, sizeof(sectionBytes));

        return section;
    }

    uint32_t CtrInfoBlob::sectionBytes(uint32_t section) const
    {
        uint32_t bytes;
        std::memcpy(&bytes, mBlob.data() + section, sizeof(bytes));
        return bytes;
    }

    std::shared_ptr<const CtrInfoBlob::InfoList> CtrInfoBlob::materialize(uint32_t section) const
    {
        auto list = std::make_shared<InfoList>();

        Cursor c{ mBlob.data() + section };
        c.get<uint32_t>(); // section byte length
        const uint32_t count = c.get<uint32_t>();
        for (uint32_t i = 0; i < count; ++i)
        {
            DialInfo& info = list->emplace_back();
            info.mData.mType = c.get<int32_t>();
            info.mData.mDisposition = c.get<int32_t>();
            info.mData.mRank = c.get<signed char>();
            info.mData.mGender = c.get<signed char>();
            info.mData.mPCrank = c.get<signed char>();
            info.mQuestStatus = static_cast<DialInfo::QuestStatus>(c.get<uint8_t>());
            info.mFactionLess = c.get<uint8_t>() != 0;

            info.mId = c.getRefId();
            info.mActor = c.getRefId();
            info.mRace = c.getRefId();
            info.mClass = c.getRefId();
            info.mFaction = c.getRefId();
            info.mPcFaction = c.getRefId();
            info.mCell = c.getRefId();
            info.mSound = c.getStr16();

            info.mResponseOfs = c.get<uint32_t>();
            info.mResponseLen = c.get<uint32_t>();
            info.mResultOfs = c.get<uint32_t>();
            info.mResultLen = c.get<uint32_t>();
            info.mResponse = c.getStr32();
            info.mResultScript = c.getStr32();

            const uint16_t selects = c.get<uint16_t>();
            info.mSelects.reserve(selects);
            for (uint16_t s = 0; s < selects; ++s)
            {
                DialogueCondition& cond = info.mSelects.emplace_back();
                cond.mFunction = static_cast<DialogueCondition::Function>(c.get<int8_t>());
                cond.mComparison = static_cast<DialogueCondition::Comparison>(c.get<char>());
                cond.mIndex = c.get<uint8_t>();
                const uint8_t kind = c.get<uint8_t>();
                if (kind == 0)
                    cond.mValue = c.get<int32_t>();
                else
                    cond.mValue = c.get<float>();
                cond.mVariable = c.getStr16();
            }
        }

        return list;
    }

    std::shared_ptr<const CtrInfoBlob::InfoList> CtrInfoBlob::fetch(uint32_t section) const
    {
        if (section == sNoSection || section >= mBlob.size())
            return nullptr;

        std::lock_guard<std::mutex> lock(mMutex);

        auto it = mCache.find(section);
        if (it != mCache.end())
        {
            it->second.mStamp = ++mStamp;
            return it->second.mList;
        }

        auto list = materialize(section);

        // Evict least-recently-used unpinned entries once the byte budget is
        // crossed. Section byte size approximates the in-RAM cost ratio-wise;
        // entries still referenced by live handles are never dropped.
        size_t bytes = 0;
        for (const auto& [ofs, entry] : mCache)
            bytes += sectionBytes(ofs);
        while (mCache.size() > sCacheMinEntries && bytes > sCacheBudgetBytes)
        {
            auto lru = mCache.end();
            for (auto e = mCache.begin(); e != mCache.end(); ++e)
            {
                if (e->second.mList.use_count() > 1)
                    continue;
                if (lru == mCache.end() || e->second.mStamp < lru->second.mStamp)
                    lru = e;
            }
            if (lru == mCache.end())
                break;
            bytes -= sectionBytes(lru->first);
            mCache.erase(lru);
        }

        mCache.emplace(section, CacheEntry{ list, ++mStamp });
        return list;
    }

    void CtrInfoBlob::seal()
    {
        mBlob.shrink_to_fit();
        Log(Debug::Info) << "CtrInfoBlob: baked " << mBlob.size() / 1024 << " KB of dialogue infos";
    }
}
