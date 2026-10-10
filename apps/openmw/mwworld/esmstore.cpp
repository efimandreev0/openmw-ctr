#include "esmstore.hpp"

#include <algorithm>
#include <deque>
#include <fstream>
#include <tuple>

#include <components/debug/debuglog.hpp>

#include <components/esm/records.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/esm4/common.hpp>
#include <components/esm4/reader.hpp>
#include <components/esm4/readerutils.hpp>
#include <components/esmloader/load.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#ifndef OPENMW_NO_LUA
#include <components/lua/configuration.hpp>
#endif
#include <components/misc/algorithm.hpp>

#include "../mwmechanics/spelllist.hpp"

#ifdef __3DS__
#include <components/esm3/ctrinfoblob.hpp>
#include <components/esm3/ctrstrpool.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <map>
#include <vector>

#include "../vita/VitaInit.h"
#endif

namespace
{
    struct Ref
    {
        ESM::RefNum mRefNum;
        std::size_t mRefID;
        std::size_t mCellIndex;

        Ref(ESM::RefNum refNum, std::size_t refID, std::size_t cellIndex)
            : mRefNum(refNum)
            , mRefID(refID)
            , mCellIndex(cellIndex)
        {
        }
    };

#ifdef __3DS__
    // 3DS: these containers hold one entry per cell ref in the whole load
    // order (~300k+). vector's doubling needs old+new blocks live at once —
    // a guaranteed OOM on the fragmented sbrk heap. deque grows in pages.
    template <class T>
    using RefsContainer = std::deque<T>;
#else
    template <class T>
    using RefsContainer = std::vector<T>;
#endif

    constexpr std::size_t deletedRefID = std::numeric_limits<std::size_t>::max();

    void readRefs(const ESM::Cell& cell, std::size_t cellIndex, RefsContainer<Ref>& refs,
        RefsContainer<ESM::RefId>& refIDs,
        std::set<ESM::RefId>& keyIDs, ESM::ReadersCache& readers)
    {
        // TODO: we have many similar copies of this code.
        for (size_t i = 0; i < cell.mContextList.size(); i++)
        {
            const std::size_t index = static_cast<std::size_t>(cell.mContextList[i].index);
            const ESM::ReadersCache::BusyItem reader = readers.get(index);
            cell.restore(*reader, i);
            ESM::CellRef ref;
            bool deleted = false;
            while (cell.getNextRef(*reader, ref, deleted))
            {
                if (deleted)
                    refs.emplace_back(ref.mRefNum, deletedRefID, cellIndex);
                else if (std::find(cell.mMovedRefs.begin(), cell.mMovedRefs.end(), ref.mRefNum)
                    == cell.mMovedRefs.end())
                {
                    if (!ref.mKey.empty())
                        keyIDs.insert(std::move(ref.mKey));
                    refs.emplace_back(ref.mRefNum, refIDs.size(), cellIndex);
                    refIDs.push_back(std::move(ref.mRefID));
                }
            }
        }
        for (const auto& [value, deleted] : cell.mLeasedRefs)
        {
            if (deleted)
                refs.emplace_back(value.mRefNum, deletedRefID, cellIndex);
            else
            {
                if (!value.mKey.empty())
                    keyIDs.insert(std::move(value.mKey));
                refs.emplace_back(value.mRefNum, refIDs.size(), cellIndex);
                refIDs.push_back(value.mRefID);
            }
        }
    }

    const ESM::RefId& getDefaultClass(const MWWorld::Store<ESM::Class>& classes)
    {
        auto it = classes.begin();
        if (it != classes.end())
            return it->mId;
        throw std::runtime_error("List of NPC classes is empty!");
    }

    const ESM::RefId& getDefaultRace(const MWWorld::Store<ESM::Race>& races)
    {
        auto it = races.begin();
        if (it != races.end())
            return it->mId;
        throw std::runtime_error("List of NPC races is empty!");
    }

    std::vector<ESM::NPC> getNPCsToReplace(const MWWorld::Store<ESM::Faction>& factions,
        const MWWorld::Store<ESM::Class>& classes, const MWWorld::Store<ESM::Race>& races,
        const MWWorld::Store<ESM::Script>& scripts, const std::unordered_map<ESM::RefId, ESM::NPC>& npcs)
    {
        // Cache first class from store - we will use it if current class is not found
        const ESM::RefId& defaultCls = getDefaultClass(classes);
        // Same for races
        const ESM::RefId& defaultRace = getDefaultRace(races);

        // Validate NPCs for non-existing class and faction.
        // We will replace invalid entries by fixed ones
        std::vector<ESM::NPC> npcsToReplace;

        for (const auto& npcIter : npcs)
        {
            ESM::NPC npc = npcIter.second;
            bool changed = false;

            const ESM::RefId& npcFaction = npc.mFaction;
            if (!npcFaction.empty())
            {
                const ESM::Faction* fact = factions.search(npcFaction);
                if (!fact)
                {
                    Log(Debug::Verbose) << "NPC " << npc.mId << " (" << npc.mName << ") has nonexistent faction "
                                        << npc.mFaction << ", ignoring it.";
                    npc.mFaction = ESM::RefId();
                    npc.mNpdt.mRank = 0;
                    changed = true;
                }
            }

            const ESM::Class* cls = classes.search(npc.mClass);
            if (!cls)
            {
                Log(Debug::Verbose) << "NPC " << npc.mId << " (" << npc.mName << ") has nonexistent class "
                                    << npc.mClass << ", using " << defaultCls << " class as replacement.";
                npc.mClass = defaultCls;
                changed = true;
            }

            const ESM::Race* race = races.search(npc.mRace);
            if (!race)
            {
                Log(Debug::Verbose) << "NPC " << npc.mId << " (" << npc.mName << ") has nonexistent race " << npc.mRace
                                    << ", using " << defaultRace << " race as replacement.";
                npc.mRace = defaultRace;
                changed = true;
            }

            if (!npc.mScript.empty() && !scripts.search(npc.mScript))
            {
                Log(Debug::Verbose) << "NPC " << npc.mId << " (" << npc.mName << ") has nonexistent script "
                                    << npc.mScript << ", ignoring it.";
                npc.mScript = ESM::RefId();
                changed = true;
            }

            if (changed)
                npcsToReplace.push_back(std::move(npc));
        }

        return npcsToReplace;
    }

    template <class RecordType>
    std::vector<RecordType> getSpellsToReplace(
        const MWWorld::Store<RecordType>& spells, const MWWorld::Store<ESM::MagicEffect>& magicEffects)
    {
        std::vector<RecordType> spellsToReplace;

        for (RecordType spell : spells)
        {
            if (spell.mEffects.mList.empty())
                continue;

            bool changed = false;
            auto iter = spell.mEffects.mList.begin();
            while (iter != spell.mEffects.mList.end())
            {
                const ESM::MagicEffect* mgef = magicEffects.search(iter->mData.mEffectID);
                if (!mgef)
                {
                    Log(Debug::Verbose) << RecordType::getRecordType() << " " << spell.mId
                                        << ": dropping invalid effect (" << iter->mData.mEffectID << ")";
                    iter = spell.mEffects.mList.erase(iter);
                    changed = true;
                    continue;
                }

                if (!(mgef->mData.mFlags & ESM::MagicEffect::TargetAttribute) && !iter->mData.mAttribute.empty())
                {
                    iter->mData.mAttribute = ESM::RefId();
                    Log(Debug::Verbose) << RecordType::getRecordType() << " " << spell.mId
                                        << ": dropping unexpected attribute argument of " << iter->mData.mEffectID
                                        << " effect";
                    changed = true;
                }

                if (!(mgef->mData.mFlags & ESM::MagicEffect::TargetSkill) && !iter->mData.mSkill.empty())
                {
                    iter->mData.mSkill = ESM::RefId();
                    Log(Debug::Verbose) << RecordType::getRecordType() << " " << spell.mId
                                        << ": dropping unexpected skill argument of " << iter->mData.mEffectID
                                        << " effect";
                    changed = true;
                }

                ++iter;
            }

            if (changed)
                spellsToReplace.emplace_back(spell);
        }

        return spellsToReplace;
    }

    // Custom enchanted items can reference scripts that no longer exist, this doesn't necessarily mean the base item no
    // longer exists however. So instead of removing the item altogether, we're only removing the script.
    template <class MapT>
    void removeMissingScripts(const MWWorld::Store<ESM::Script>& scripts, MapT& items)
    {
        for (auto& [id, item] : items)
        {
            if (!item.mScript.empty() && !scripts.search(item.mScript))
            {
                Log(Debug::Verbose) << MapT::mapped_type::getRecordType() << ' ' << id << " (" << item.mName
                                    << ") has nonexistent script " << item.mScript << ", ignoring it.";
                item.mScript = ESM::RefId();
            }
        }
    }
}

namespace MWWorld
{
    using IDMap = std::unordered_map<ESM::RefId, int>;

    struct ESMStoreImp
    {
        ESMStore::StoreTuple mStores;

        std::map<ESM::RecNameInts, DynamicStore*> mRecNameToStore;

        // Lookup of all IDs. Makes looking up references faster. Just
        // maps the id name to the record type.
        IDMap mIds;
        IDMap mStaticIds;

        template <typename T>
        static void assignStoreToIndex(ESMStore& stores, Store<T>& store)
        {
            const std::size_t storeIndex = ESMStore::getTypeIndex<T>();
            if (stores.mStores.size() <= storeIndex)
                stores.mStores.resize(storeIndex + 1);

            assert(&store == &std::get<Store<T>>(stores.mStoreImp->mStores));

            stores.mStores[storeIndex] = &store;
            if constexpr (std::is_convertible_v<Store<T>*, DynamicStore*>)
            {
                stores.mDynamicStores.push_back(&store);
                constexpr ESM::RecNameInts recName = T::sRecordId;
                if constexpr (recName != ESM::REC_INTERNAL_PLAYER)
                {
                    stores.mStoreImp->mRecNameToStore[recName] = &store;
                }
            }
        }

        template <typename T>
        static bool typedReadRecordESM4(ESM4::Reader& reader, Store<T>& store)
        {
            auto recordType = static_cast<ESM4::RecordTypes>(reader.hdr().record.typeId);

            ESM::RecNameInts esm4RecName = static_cast<ESM::RecNameInts>(ESM::esm4Recname(recordType));
            if constexpr (HasRecordId<T>)
            {
                if constexpr (ESM::isESM4Rec(T::sRecordId))
                {
                    if (T::sRecordId == esm4RecName)
                    {
                        reader.getRecordData();
                        T value;
                        value.load(reader);
                        store.insertStatic(value);
                        return true;
                    }
                }
            }
            return false;
        }

        static bool readRecord(ESM4::Reader& reader, ESMStore& store)
        {
            return std::apply(
                [&reader](auto&... x) { return (typedReadRecordESM4(reader, x) || ...); }, store.mStoreImp->mStores);
        }
    };

    int ESMStore::find(const ESM::RefId& id) const
    {
        IDMap::const_iterator it = mStoreImp->mIds.find(id);
        if (it == mStoreImp->mIds.end())
        {
#ifdef __3DS__
            return findStatic(id);
#else
            return 0;
#endif
        }
        return it->second;
    }

    int ESMStore::findStatic(const ESM::RefId& id) const
    {
        IDMap::const_iterator it = mStoreImp->mStaticIds.find(id);
        if (it == mStoreImp->mStaticIds.end())
        {
            return 0;
        }
        return it->second;
    }

    ESMStore::ESMStore()
    {
        mStoreImp = std::make_unique<ESMStoreImp>();
        std::apply([this](auto&... x) { (ESMStoreImp::assignStoreToIndex(*this, x), ...); }, mStoreImp->mStores);
        mDynamicCount = 0;
        getWritable<ESM::Pathgrid>().setCells(getWritable<ESM::Cell>());
    }

    ESMStore::~ESMStore() = default;

    void ESMStore::clearDynamic()
    {
        for (const auto& store : mDynamicStores)
            store->clearDynamic();
#ifdef __3DS__
        mStoreImp->mIds.clear();
#else
        mStoreImp->mIds = mStoreImp->mStaticIds;
#endif

        movePlayerRecord();
    }

    static bool isCacheableRecord(int id)
    {
        switch (id)
        {
            case ESM::REC_ACTI:
            case ESM::REC_ALCH:
            case ESM::REC_APPA:
            case ESM::REC_ARMO:
            case ESM::REC_BOOK:
            case ESM::REC_CLOT:
            case ESM::REC_CONT:
            case ESM::REC_CREA:
            case ESM::REC_DOOR:
            case ESM::REC_INGR:
            case ESM::REC_LEVC:
            case ESM::REC_LEVI:
            case ESM::REC_LIGH:
            case ESM::REC_LOCK:
            case ESM::REC_MISC:
            case ESM::REC_NPC_:
            case ESM::REC_PROB:
            case ESM::REC_REPA:
            case ESM::REC_STAT:
            case ESM::REC_WEAP:
            case ESM::REC_BODY:
            case ESM::REC_ACTI4:
            case ESM::REC_ALCH4:
            case ESM::REC_AMMO4:
            case ESM::REC_ARMO4:
            case ESM::REC_BOOK4:
            case ESM::REC_CONT4:
            case ESM::REC_CREA4:
            case ESM::REC_DOOR4:
            case ESM::REC_FLOR4:
            case ESM::REC_FURN4:
            case ESM::REC_IMOD4:
            case ESM::REC_INGR4:
            case ESM::REC_LIGH4:
            case ESM::REC_LVLI4:
            case ESM::REC_LVLC4:
            case ESM::REC_LVLN4:
            case ESM::REC_MISC4:
            case ESM::REC_MSTT4:
            case ESM::REC_NPC_4:
            case ESM::REC_SCOL4:
            case ESM::REC_STAT4:
            case ESM::REC_TERM4:
            case ESM::REC_TREE4:
            case ESM::REC_WEAP4:
                return true;
                break;
        }
        return false;
    }

    void ESMStore::load(ESM::ESMReader& esm, Loading::Listener* listener, ESM::Dialogue*& dialogue)
    {
        if (listener != nullptr)
            listener->setProgressRange(::EsmLoader::fileProgress);

#ifdef __3DS__
        struct CtrLoadCensus
        {
            using Clock = std::chrono::steady_clock;
            std::map<uint32_t, Clock::duration> mTimes;
            uint32_t mCurKey = 0;
            Clock::time_point mLast = Clock::now();

            static uint32_t keyOf(ESM::RecNameInts rec) { return rec == ESM::REC_INFO ? ESM::REC_DIAL : rec; }

            void tick(ESM::RecNameInts rec)
            {
                const uint32_t key = keyOf(rec);
                if (key == mCurKey)
                    return;
                const Clock::time_point now = Clock::now();
                if (mCurKey != 0)
                    mTimes[mCurKey] += now - mLast;
                mLast = now;
                mCurKey = key;
            }

            void report()
            {
                tick(static_cast<ESM::RecNameInts>(0));
                std::vector<std::pair<Clock::duration, uint32_t>> top;
                top.reserve(mTimes.size());
                for (const auto& [k, v] : mTimes)
                    top.emplace_back(v, k);
                std::sort(top.rbegin(), top.rend());
                char buf[96];
                int n = 0;
                for (const auto& [time, k] : top)
                {
                    const unsigned ms = static_cast<unsigned>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(time).count());
                    if (++n > 14 || ms < 100)
                        break;
                    snprintf(buf, sizeof(buf), "[LoadCensus] %c%c%c%c: %u ms", static_cast<char>(k & 0xff),
                        static_cast<char>((k >> 8) & 0xff), static_cast<char>((k >> 16) & 0xff),
                        static_cast<char>((k >> 24) & 0xff), ms);
                    Vita::breadcrumb(buf);
                }
            }
        } census;
#endif

        // Loop through all records
        while (esm.hasMoreRecs())
        {
            ESM::NAME n = esm.getRecName();
            esm.getRecHeader();
            if (esm.getRecordFlags() & ESM::FLAG_Ignored)
            {
                esm.skipRecord();
                continue;
            }

            // Look up the record type.
            ESM::RecNameInts recName = static_cast<ESM::RecNameInts>(n.toInt());
#ifdef __3DS__
            census.tick(recName);
#endif
            const auto& it = mStoreImp->mRecNameToStore.find(recName);

            if (it == mStoreImp->mRecNameToStore.end())
            {
                if (recName == ESM::REC_INFO)
                {
                    if (dialogue)
                    {
                        dialogue->readInfo(esm);
                    }
                    else
                    {
                        Log(Debug::Error) << "Error: info record without dialog";
                        esm.skipRecord();
                    }
                }
                else if (n.toInt() == ESM::REC_MGEF)
                {
                    getWritable<ESM::MagicEffect>().load(esm);
                }
                else if (n.toInt() == ESM::REC_SKIL)
                {
                    getWritable<ESM::Skill>().load(esm);
                }
                else if (n.toInt() == ESM::REC_FILT || n.toInt() == ESM::REC_DBGP)
                {
                    // ignore project file only records
                    esm.skipRecord();
                }
                else if (n.toInt() == ESM::REC_LUAL)
                {
#ifdef OPENMW_NO_LUA
                    esm.skipRecord();
#else
                    ESM::LuaScriptsCfg cfg;
                    cfg.load(esm);
                    cfg.adjustRefNums(esm);
                    mLuaContent.push_back(std::move(cfg));
#endif
                }
                else
                {
                    throw std::runtime_error("Unknown record: " + n.toString());
                }
            }
            else
            {
                RecordId id = it->second->load(esm);
                if (id.mIsDeleted)
                {
                    it->second->eraseStatic(id.mId);
                    continue;
                }

                if (n.toInt() == ESM::REC_DIAL)
                {
                    dialogue = const_cast<ESM::Dialogue*>(getWritable<ESM::Dialogue>().find(id.mId));
                }
                else
                {
                    dialogue = nullptr;
                }
            }
            if (listener != nullptr)
                listener->setProgress(::EsmLoader::fileProgress * esm.getFileOffset() / esm.getFileSize());
        }

#ifdef __3DS__
        census.report();
#endif
    }

    void ESMStore::loadESM4(ESM4::Reader& reader, Loading::Listener* listener)
    {
        if (listener != nullptr)
            listener->setProgressRange(::EsmLoader::fileProgress);
        auto visitorRec = [this, listener](ESM4::Reader& r) {
            bool result = ESMStoreImp::readRecord(r, *this);
            if (listener != nullptr)
                listener->setProgress(::EsmLoader::fileProgress * r.getFileOffset() / r.getFileSize());
            return result;
        };
        ESM4::ReaderUtils::readAll(reader, visitorRec, [](ESM4::Reader&) {});
    }

    void ESMStore::setIdType(const ESM::RefId& id, ESM::RecNameInts type)
    {
        mStoreImp->mIds[id] = type;
    }

    ESM::LuaScriptsCfg ESMStore::getLuaScriptsCfg() const
    {
        ESM::LuaScriptsCfg cfg;
#ifndef OPENMW_NO_LUA
        for (const LuaContent& c : mLuaContent)
        {
            if (std::holds_alternative<std::filesystem::path>(c))
            {
                // *.omwscripts are intentionally reloaded every time when `getLuaScriptsCfg` is called.
                // It is important for the `reloadlua` console command.
                try
                {
                    auto file = std::ifstream(std::get<std::filesystem::path>(c));
                    std::string fileContent(std::istreambuf_iterator<char>(file), {});
                    LuaUtil::parseOMWScripts(cfg, fileContent);
                }
                catch (std::exception& e)
                {
                    Log(Debug::Error) << e.what();
                }
            }
            else
            {
                const ESM::LuaScriptsCfg& addition = std::get<ESM::LuaScriptsCfg>(c);
                cfg.mScripts.insert(cfg.mScripts.end(), addition.mScripts.begin(), addition.mScripts.end());
            }
        }
#endif
        return cfg;
    }

    void ESMStore::setUp()
    {
        if (mIsSetUpDone)
            throw std::logic_error("ESMStore::setUp() is called twice");
        mIsSetUpDone = true;

        for (const auto& [_, store] : mStoreImp->mRecNameToStore)
            store->setUp();

        getWritable<ESM4::Land>().updateLandPositions(get<ESM4::Cell>());
        getWritable<ESM4::Reference>().preprocessReferences(get<ESM4::Cell>());
        getWritable<ESM4::ActorCharacter>().preprocessReferences(get<ESM4::Cell>());
        getWritable<ESM4::ActorCreature>().preprocessReferences(get<ESM4::Cell>());

        rebuildIdsIndex();
#ifdef __3DS__
        mStoreImp->mStaticIds = std::move(mStoreImp->mIds);
        mStoreImp->mIds.clear();
#else
        mStoreImp->mStaticIds = mStoreImp->mIds;
#endif

#ifdef __3DS__
        // Memory census: the store is the single biggest heap consumer on
        // the 3DS. Dump per-type record counts and the string tonnage so the
        // fattest stores are visible in boot.log.
        {
            char buf[160];
            for (const auto& [recName, store] : mStoreImp->mRecNameToStore)
            {
                const size_t n = store->getSize();
                if (n == 0)
                    continue;
                const uint32_t k = static_cast<uint32_t>(recName);
                snprintf(buf, sizeof(buf), "[Store] %c%c%c%c: %u", static_cast<char>(k & 0xff),
                    static_cast<char>((k >> 8) & 0xff), static_cast<char>((k >> 16) & 0xff),
                    static_cast<char>((k >> 24) & 0xff), static_cast<unsigned>(n));
                Vita::breadcrumb(buf);
            }
            size_t infos = 0, selects = 0, dlgInline = 0, dlgPooled = 0;
            for (const ESM::Dialogue& d : get<ESM::Dialogue>())
            {
                for (const ESM::DialInfo& i : d.mInfo)
                {
                    ++infos;
                    selects += i.mSelects.size();
                    dlgInline += i.mResponse.size() + i.mResultScript.size();
                    dlgPooled += i.mResponseLen + i.mResultLen;
                }
            }
            size_t bookInline = 0, bookPooled = 0;
            for (const ESM::Book& b : get<ESM::Book>())
            {
                bookInline += b.mText.size();
                bookPooled += b.mTextLen;
            }
            size_t scptB = 0;
            for (const ESM::Script& s : get<ESM::Script>())
                scptB += s.mScriptText.size();
            snprintf(buf, sizeof(buf),
                "[Store] INFO=%u sel=%u dlgRAM=%uKB dlgSD=%uKB bookRAM=%uKB bookSD=%uKB scptRAM=%uKB",
                static_cast<unsigned>(infos), static_cast<unsigned>(selects), static_cast<unsigned>(dlgInline / 1024),
                static_cast<unsigned>(dlgPooled / 1024), static_cast<unsigned>(bookInline / 1024),
                static_cast<unsigned>(bookPooled / 1024), static_cast<unsigned>(scptB / 1024));
            Vita::breadcrumb(buf);

            // INFO diet: serialize every topic's ordered infos into the flat
            // CtrInfoBlob and free the std::list originals (~32k DialInfo =
            // ~9MB as lists, ~3MB baked). Runtime readers go through
            // Dialogue::readInfos(), which rematerializes topics on demand.
            {
                ESM::CtrInfoBlob& blob = ESM::CtrInfoBlob::instance();
                getWritable<ESM::Dialogue>().ctrForEachDialogue([&blob](ESM::Dialogue& d) {
                    if (d.mCtrInfoSection == ESM::CtrInfoBlob::sNoSection)
                    {
                        d.mCtrInfoSection = blob.bake(d.mInfo);
                        d.mInfo.clear();
                    }
                });
                blob.seal();
                malloc_trim(0);
                snprintf(buf, sizeof(buf), "[Store] INFO blob=%uKB", static_cast<unsigned>(blob.blobBytes() / 1024));
                Vita::breadcrumb(buf);
            }
            Vita::logMemoryStatus("Post-store-setUp");
        }
#endif
    }

    void ESMStore::rebuildIdsIndex()
    {
        mStoreImp->mIds.clear();
        for (const auto& [recordType, store] : mStoreImp->mRecNameToStore)
        {
            if (isCacheableRecord(recordType))
            {
                std::vector<ESM::RefId> identifiers;
                store->listIdentifier(identifiers);
                for (auto& record : identifiers)
                {
#ifdef __3DS__
                    if (!mStoreImp->mStaticIds.empty() && findStatic(record) == recordType)
                        continue;
#endif
                    mStoreImp->mIds[record] = recordType;
                }
            }
        }
    }

    void ESMStore::validateRecords(ESM::ReadersCache& readers)
    {
        validate();
        countAllCellRefsAndMarkKeys(readers);
    }

    void ESMStore::countAllCellRefsAndMarkKeys(ESM::ReadersCache& readers)
    {
        // TODO: We currently need to read entire files here again.
        // We should consider consolidating or deferring this reading.
        if (!mRefInfo.empty())
            return;
#ifdef __3DS__
        // content set, and recomputing them re-reads every ESM cell block --
        // ~25s on top of the load. Cache them on SD keyed by the same
        // fingerprint the string pool uses.
        if (loadRefCountCache())
        {
            pruneRefCells();
            return;
        }
#endif
        RefsContainer<Ref> refs;
        std::set<ESM::RefId> keyIDs;
        RefsContainer<ESM::RefId> refIDs;
        const Store<ESM::Cell>& cells = get<ESM::Cell>();
        const std::size_t cellCount = cells.getSize();
        const std::size_t interiorCellCount = cells.getIntSize();
        const std::size_t exteriorCellCount = cells.getExtSize();
        for (std::size_t i = 0; i < cellCount; ++i)
            readRefs(*cells.at(i), i, refs, refIDs, keyIDs, readers);
#ifdef __3DS__
        // Plain in-place sort with an insertion-order tiebreak: same final
        // order as stable_sort, but no temp buffer (stable_sort allocates a
        // half-size merge buffer — megabytes we don't have).
        const auto lessByRefNum = [](const Ref& l, const Ref& r) {
            if (l.mRefNum != r.mRefNum)
                return l.mRefNum < r.mRefNum;
            return l.mRefID < r.mRefID;
        };
        std::sort(refs.begin(), refs.end(), lessByRefNum);
#else
        const auto lessByRefNum = [](const Ref& l, const Ref& r) { return l.mRefNum < r.mRefNum; };
        std::stable_sort(refs.begin(), refs.end(), lessByRefNum);
#endif
        const auto equalByRefNum = [](const Ref& l, const Ref& r) { return l.mRefNum == r.mRefNum; };
        const auto forEachEffectiveRef = [&](auto&& fn) {
            Misc::forEachUnique(refs.rbegin(), refs.rend(), equalByRefNum, [&](const Ref& value) {
                if (value.mRefID != deletedRefID)
                    fn(value, refIDs[value.mRefID]);
            });
        };

        std::vector<std::vector<RefInfo*>> refsPerCell(cellCount);
        forEachEffectiveRef([&](const Ref& value, const ESM::RefId& refId) {
            RefInfo& info = mRefInfo[refId];
            ++info.mCount;
            refsPerCell[value.mCellIndex].push_back(&info);
        });
        for (std::size_t i = 0; i < cellCount; ++i)
        {
            const std::size_t cellIndex = i < exteriorCellCount ? interiorCellCount + i : i - exteriorCellCount;
            const ESM::Cell* const cell = cells.at(cellIndex);
            for (RefInfo* info : refsPerCell[cellIndex])
                if (info->mCells.empty() || info->mCells.back() != cell)
                    info->mCells.push_back(cell);
        }
        auto& store = getWritable<ESM::Miscellaneous>().mStatic;
        for (const auto& id : keyIDs)
        {
            auto it = store.find(id);
            if (it != store.end())
                it->second.mData.mFlags |= ESM::Miscellaneous::Key;
        }
#ifdef __3DS__
        saveRefCountCache(keyIDs);
        pruneRefCells();
#endif
    }

#ifdef __3DS__
    void ESMStore::pruneRefCells()
    {
        constexpr std::size_t maxCells = 4;
        std::size_t freed = 0;
        for (auto& [id, info] : mRefInfo)
        {
            if (info.mCells.size() <= maxCells)
                continue;
            const int type = find(id);
            if (type == ESM::REC_NPC_ || type == ESM::REC_CREA)
                continue;
            freed += info.mCells.capacity();
            std::vector<const ESM::Cell*>().swap(info.mCells);
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "[RefCache] dropped %u cell entries of widespread ids", static_cast<unsigned>(freed));
        Vita::breadcrumb(msg);
    }
#endif

#ifdef __3DS__
    namespace
    {
        constexpr char sRefCacheMagic[8] = { 'O', 'M', 'W', 'R', 'E', 'F', 'C', '2' };
        constexpr const char* sRefCachePath = "sdmc:/3ds/openmw/config/refcount.bin";

        bool readAll(FILE* f, void* p, size_t n)
        {
            return fread(p, 1, n, f) == n;
        }
        bool writeAll(FILE* f, const void* p, size_t n)
        {
            return fwrite(p, 1, n, f) == n;
        }
        bool readId(FILE* f, std::string& buf, ESM::RefId& out)
        {
            uint16_t len = 0;
            if (!readAll(f, &len, 2))
                return false;
            buf.resize(len);
            if (!readAll(f, buf.data(), len))
                return false;
            out = ESM::RefId::deserializeText(buf);
            return true;
        }
        bool writeId(FILE* f, const ESM::RefId& id)
        {
            const std::string s = id.serializeText();
            const uint16_t len = static_cast<uint16_t>(s.size());
            return writeAll(f, &len, 2) && writeAll(f, s.data(), len);
        }
    }

    bool ESMStore::loadRefCountCache()
    {
        const uint64_t fp = ESM::CtrStrPool::instance().fingerprint();
        if (fp == 0)
            return false;
        FILE* f = fopen(sRefCachePath, "rb");
        if (!f)
            return false;
        const Store<ESM::Cell>& cells = get<ESM::Cell>();
        const std::size_t cellCount = cells.getSize();
        char magic[8];
        uint64_t ffp = 0;
        uint32_t fileCellCount = 0;
        bool ok = readAll(f, magic, 8) && readAll(f, &ffp, 8) && memcmp(magic, sRefCacheMagic, 8) == 0 && ffp == fp
            && readAll(f, &fileCellCount, 4) && fileCellCount == cellCount;
        uint32_t n = 0;
        ok = ok && readAll(f, &n, 4);
        std::string buf;
        ESM::RefId id;
        for (uint32_t i = 0; ok && i < n; ++i)
        {
            int32_t count = 0;
            uint32_t numCells = 0;
            ok = readId(f, buf, id) && readAll(f, &count, 4) && readAll(f, &numCells, 4);
            if (!ok)
                break;
            RefInfo& info = mRefInfo[id];
            info.mCount = count;
            info.mCells.reserve(numCells);
            for (uint32_t c = 0; ok && c < numCells; ++c)
            {
                uint32_t cellIndex = 0;
                ok = readAll(f, &cellIndex, 4) && cellIndex < cellCount;
                if (ok)
                    info.mCells.push_back(cells.at(cellIndex));
            }
        }
        uint32_t k = 0;
        ok = ok && readAll(f, &k, 4);
        auto& miscStore = getWritable<ESM::Miscellaneous>().mStatic;
        for (uint32_t i = 0; ok && i < k; ++i)
        {
            ok = readId(f, buf, id);
            if (ok)
            {
                auto it = miscStore.find(id);
                if (it != miscStore.end())
                    it->second.mData.mFlags |= ESM::Miscellaneous::Key;
            }
        }
        fclose(f);
        if (!ok)
        {
            mRefInfo.clear();
            Vita::breadcrumb("[RefCache] stale/corrupt - recomputing");
            return false;
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "[RefCache] loaded %u ref infos, %u key ids", static_cast<unsigned>(n),
            static_cast<unsigned>(k));
        Vita::breadcrumb(msg);
        return true;
    }

    void ESMStore::saveRefCountCache(const std::set<ESM::RefId>& keyIDs) const
    {
        const uint64_t fp = ESM::CtrStrPool::instance().fingerprint();
        if (fp == 0)
            return;
        FILE* f = fopen(sRefCachePath, "wb");
        if (!f)
            return;
        const Store<ESM::Cell>& cells = get<ESM::Cell>();
        const uint32_t cellCount = static_cast<uint32_t>(cells.getSize());
        std::unordered_map<const ESM::Cell*, uint32_t> cellIndices;
        cellIndices.reserve(cellCount);
        for (uint32_t i = 0; i < cellCount; ++i)
            cellIndices.emplace(cells.at(i), i);
        bool ok = writeAll(f, sRefCacheMagic, 8) && writeAll(f, &fp, 8) && writeAll(f, &cellCount, 4);
        const uint32_t n = static_cast<uint32_t>(mRefInfo.size());
        ok = ok && writeAll(f, &n, 4);
        for (auto it = mRefInfo.begin(); ok && it != mRefInfo.end(); ++it)
        {
            const int32_t count = it->second.mCount;
            const uint32_t numCells = static_cast<uint32_t>(it->second.mCells.size());
            ok = writeId(f, it->first) && writeAll(f, &count, 4) && writeAll(f, &numCells, 4);
            for (const ESM::Cell* cell : it->second.mCells)
            {
                const auto found = cellIndices.find(cell);
                const uint32_t cellIndex = found == cellIndices.end() ? cellCount : found->second;
                ok = ok && writeAll(f, &cellIndex, 4);
            }
        }
        const uint32_t k = static_cast<uint32_t>(keyIDs.size());
        ok = ok && writeAll(f, &k, 4);
        for (auto it = keyIDs.begin(); ok && it != keyIDs.end(); ++it)
            ok = writeId(f, *it);
        fclose(f);
        if (!ok)
        {
            remove(sRefCachePath);
            Vita::breadcrumb("[RefCache] write failed - removed");
        }
        else
            Vita::breadcrumb("[RefCache] saved");
    }
#endif

    int ESMStore::getRefCount(const ESM::RefId& id) const
    {
        auto it = mRefInfo.find(id);
        if (it == mRefInfo.end())
            return 0;
        return it->second.mCount;
    }

    std::span<const ESM::Cell* const> ESMStore::getRefCells(const ESM::RefId& id) const
    {
        const auto it = mRefInfo.find(id);
        if (it == mRefInfo.end())
            return {};
        return it->second.mCells;
    }

    void ESMStore::validate()
    {
        auto& npcs = getWritable<ESM::NPC>();
        std::vector<ESM::NPC> npcsToReplace = getNPCsToReplace(getWritable<ESM::Faction>(), getWritable<ESM::Class>(),
            getWritable<ESM::Race>(), getWritable<ESM::Script>(), npcs.mStatic);

        for (const ESM::NPC& npc : npcsToReplace)
        {
            npcs.eraseStatic(npc.mId);
            npcs.insertStatic(npc);
        }

        removeMissingScripts(getWritable<ESM::Script>(), getWritable<ESM::Creature>().mStatic);

        // Validate spell effects and enchantments for invalid arguments
        auto& spells = getWritable<ESM::Spell>();
        auto& enchantments = getWritable<ESM::Enchantment>();
        auto& magicEffects = getWritable<ESM::MagicEffect>();

        std::vector<ESM::Spell> spellsToReplace = getSpellsToReplace(spells, magicEffects);
        for (const ESM::Spell& spell : spellsToReplace)
        {
            spells.eraseStatic(spell.mId);
            spells.insertStatic(spell);
        }

        std::vector<ESM::Enchantment> enchantmentsToReplace = getSpellsToReplace(enchantments, magicEffects);
        for (const ESM::Enchantment& enchantment : enchantmentsToReplace)
        {
            enchantments.eraseStatic(enchantment.mId);
            enchantments.insertStatic(enchantment);
        }
    }

    void ESMStore::movePlayerRecord()
    {
        auto& npcs = getWritable<ESM::NPC>();
        auto player = npcs.find(ESM::RefId::stringRefId("Player"));
        npcs.insert(*player);
    }

    void ESMStore::validateDynamic()
    {
        auto& npcs = getWritable<ESM::NPC>();
        auto& scripts = getWritable<ESM::Script>();

        std::vector<ESM::NPC> npcsToReplace = getNPCsToReplace(getWritable<ESM::Faction>(), getWritable<ESM::Class>(),
            getWritable<ESM::Race>(), getWritable<ESM::Script>(), npcs.mDynamic);

        for (const ESM::NPC& npc : npcsToReplace)
            npcs.insert(npc);

        removeMissingScripts(scripts, getWritable<ESM::Armor>().mDynamic);
        removeMissingScripts(scripts, getWritable<ESM::Book>().mDynamic);
        removeMissingScripts(scripts, getWritable<ESM::Clothing>().mDynamic);
        removeMissingScripts(scripts, getWritable<ESM::Creature>().mDynamic);
        removeMissingScripts(scripts, getWritable<ESM::Weapon>().mDynamic);

        removeMissingObjects(getWritable<ESM::CreatureLevList>());
        removeMissingObjects(getWritable<ESM::ItemLevList>());
    }

    // Leveled lists can be modified by scripts. This removes items that no longer exist (presumably because the
    // plugin was removed) from modified lists
    template <class T>
    void ESMStore::removeMissingObjects(Store<T>& store)
    {
        for (auto& entry : store.mDynamic)
        {
            auto first = std::remove_if(entry.second.mList.begin(), entry.second.mList.end(), [&](const auto& item) {
                if (!find(item.mId))
                {
                    Log(Debug::Verbose) << "Leveled list " << entry.first << " has nonexistent object " << item.mId
                                        << ", ignoring it.";
                    return true;
                }
                return false;
            });
            entry.second.mList.erase(first, entry.second.mList.end());
        }
    }

    size_t ESMStore::countSavedGameRecords() const
    {
        return 1 // DYNA (dynamic name counter)
            + get<ESM::Potion>().getDynamicSize() + get<ESM::Armor>().getDynamicSize()
            + get<ESM::Book>().getDynamicSize() + get<ESM::Class>().getDynamicSize()
            + get<ESM::Clothing>().getDynamicSize() + get<ESM::Enchantment>().getDynamicSize()
            + get<ESM::NPC>().getDynamicSize() + get<ESM::Spell>().getDynamicSize()
            + get<ESM::Activator>().getDynamicSize() + get<ESM::Miscellaneous>().getDynamicSize()
            + get<ESM::Weapon>().getDynamicSize() + get<ESM::CreatureLevList>().getDynamicSize()
            + get<ESM::ItemLevList>().getDynamicSize() + get<ESM::Creature>().getDynamicSize()
            + get<ESM::Container>().getDynamicSize() + get<ESM::Light>().getDynamicSize()
            + get<ESM::Static>().getDynamicSize() + get<ESM::Door>().getDynamicSize()
            + get<ESM::Probe>().getDynamicSize() + get<ESM::Ingredient>().getDynamicSize();
    }

    void ESMStore::write(ESM::ESMWriter& writer, Loading::Listener& progress) const
    {
        writer.startRecord(ESM::REC_DYNA);
        writer.startSubRecord("COUN");
        writer.writeT(mDynamicCount);
        writer.endRecord("COUN");
        writer.endRecord(ESM::REC_DYNA);

        get<ESM::Potion>().write(writer, progress);
        get<ESM::Armor>().write(writer, progress);
        get<ESM::Book>().write(writer, progress);
        get<ESM::Class>().write(writer, progress);
        get<ESM::Clothing>().write(writer, progress);
        get<ESM::Enchantment>().write(writer, progress);
        get<ESM::NPC>().write(writer, progress);
        get<ESM::Miscellaneous>().write(writer, progress);
        get<ESM::Activator>().write(writer, progress);
        get<ESM::Spell>().write(writer, progress);
        get<ESM::Weapon>().write(writer, progress);
        get<ESM::CreatureLevList>().write(writer, progress);
        get<ESM::ItemLevList>().write(writer, progress);
        get<ESM::Creature>().write(writer, progress);
        get<ESM::Container>().write(writer, progress);
        get<ESM::Light>().write(writer, progress);
        get<ESM::Static>().write(writer, progress);
        get<ESM::Door>().write(writer, progress);
        get<ESM::Probe>().write(writer, progress);
        get<ESM::Ingredient>().write(writer, progress);
    }

    bool ESMStore::readRecord(ESM::ESMReader& reader, uint32_t typeId)
    {
        ESM::RecNameInts type = static_cast<ESM::RecNameInts>(typeId);
        switch (type)
        {
            case ESM::REC_ALCH:
            case ESM::REC_ARMO:
            case ESM::REC_BOOK:
            case ESM::REC_CLAS:
            case ESM::REC_CLOT:
            case ESM::REC_ENCH:
            case ESM::REC_SPEL:
            case ESM::REC_WEAP:
                mStoreImp->mRecNameToStore[type]->read(reader);
                return true;
            case ESM::REC_NPC_:
            case ESM::REC_CREA:
            case ESM::REC_CONT:
            case ESM::REC_MISC:
            case ESM::REC_ACTI:
            case ESM::REC_LEVI:
            case ESM::REC_LEVC:
            case ESM::REC_LIGH:
            case ESM::REC_STAT:
            case ESM::REC_DOOR:
            case ESM::REC_PROB:
            case ESM::REC_INGR:
                mStoreImp->mRecNameToStore[type]->read(reader, true);
                return true;

            case ESM::REC_DYNA:
                reader.getSubNameIs("COUN");
                if (reader.getFormatVersion() <= ESM::MaxActiveSpellTypeVersion)
                {
                    uint32_t dynamicCount32 = 0;
                    reader.getHT(dynamicCount32);
                    mDynamicCount = dynamicCount32;
                }
                else
                {
                    reader.getHT(mDynamicCount);
                }
                return true;

            default:

                return false;
        }
    }

    void ESMStore::checkPlayer()
    {
        const ESM::NPC* player = get<ESM::NPC>().find(ESM::RefId::stringRefId("Player"));

        if (!get<ESM::Race>().find(player->mRace) || !get<ESM::Class>().find(player->mClass))
            throw std::runtime_error("Invalid player record (race or class unavailable");
    }

    std::pair<std::shared_ptr<MWMechanics::SpellList>, bool> ESMStore::getSpellList(ESM::RefId id, bool autoCalc) const
    {
        auto result = mSpellListCache.find(id);
        std::shared_ptr<MWMechanics::SpellList> ptr;
        if (result != mSpellListCache.end())
            ptr = result->second.lock();
        if (!ptr)
        {
            int type = find(id);
            ptr = std::make_shared<MWMechanics::SpellList>(id, type, autoCalc);
            if (result != mSpellListCache.end())
                result->second = ptr;
            else
                mSpellListCache.insert({ id, ptr });
            return { ptr, false };
        }
        return { ptr, true };
    }

    template <>
    const ESM::Cell* ESMStore::insert<ESM::Cell>(const ESM::Cell& cell)
    {
        return getWritable<ESM::Cell>().insert(cell);
    }

    template <>
    const ESM::NPC* ESMStore::insert<ESM::NPC>(const ESM::NPC& npc)
    {

        auto& npcs = getWritable<ESM::NPC>();
        if (npc.mId == "Player")
        {
            return npcs.insert(npc);
        }
        const ESM::RefId id = ESM::RefId::generated(mDynamicCount++);
        if (npcs.search(id) != nullptr)
            throw std::runtime_error("Try to override existing record: " + id.toDebugString());
        ESM::NPC record = npc;

        record.mId = id;

        ESM::NPC* ptr = npcs.insert(record);
        mStoreImp->mIds[ptr->mId] = ESM::REC_NPC_;
        return ptr;
    }

} // end namespace
