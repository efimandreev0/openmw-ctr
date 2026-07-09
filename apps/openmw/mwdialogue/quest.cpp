#include "quest.hpp"

#include <algorithm>

#include <components/esm3/queststate.hpp>

#include "../mwbase/luamanager.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwbase/environment.hpp"

namespace MWDialogue
{
    Quest::Quest()
        : Topic()
        , mIndex(0)
        , mFinished(false)
    {
    }

    Quest::Quest(const ESM::RefId& topic)
        : Topic(topic)
        , mIndex(0)
        , mFinished(false)
    {
    }

    Quest::Quest(const ESM::QuestState& state)
        : Topic(state.mTopic)
        , mIndex(state.mState)
        , mFinished(state.mFinished != 0)
    {
    }

    std::string_view Quest::getName() const
    {
        const ESM::Dialogue* dialogue = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().find(mTopic);

        const auto infos = dialogue->readInfos();
        for (auto iter = infos.begin(); iter != infos.end(); ++iter)
            if (iter->mQuestStatus == ESM::DialInfo::QS_Name)
            {
#ifdef __3DS__
                // Response may live in the SD pool and the info list itself
                // is rematerialized; keep the string alive past the return
                // (UI consumes it immediately).
                static thread_local std::string holder;
                holder = iter->getResponse();
                return holder;
#else
                return iter->mResponse;
#endif
            }

        return {};
    }

    int Quest::getIndex() const
    {
        return mIndex;
    }

    void Quest::setIndex(int index)
    {
        // The index must be set even if no related journal entry was found
        MWBase::Environment::get().getLuaManager()->questUpdated(mTopic, index);
        mIndex = index;
    }

    bool Quest::isFinished() const
    {
        return mFinished;
    }

    void Quest::setFinished(bool finished)
    {
        mFinished = finished;
    }

    bool Quest::addEntry(const JournalEntry& entry)
    {
        const ESM::Dialogue* dialogue
            = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().find(entry.mTopic);

        const auto infos = dialogue->readInfos();
        auto info = std::find_if(infos.begin(), infos.end(), [&](const auto& i) { return i.mId == entry.mInfoId; });

        if (info == infos.end() || info->mData.mJournalIndex == -1)
            throw std::runtime_error("unknown journal entry for topic " + mTopic.toDebugString());

        if (info->mQuestStatus == ESM::DialInfo::QS_Finished || info->mQuestStatus == ESM::DialInfo::QS_Restart)
            mFinished = info->mQuestStatus == ESM::DialInfo::QS_Finished;

        if (info->mData.mJournalIndex > mIndex)
        {
            mIndex = info->mData.mJournalIndex;
            MWBase::Environment::get().getLuaManager()->questUpdated(mTopic, mIndex);
        }

        for (TEntryIter iter(mEntries.begin()); iter != mEntries.end(); ++iter)
            if (iter->mInfoId == entry.mInfoId)
                return info->mQuestStatus == ESM::DialInfo::QS_Restart;

        mEntries.push_back(entry); // we want slicing here
        return info->mQuestStatus == ESM::DialInfo::QS_Restart;
    }

    void Quest::write(ESM::QuestState& state) const
    {
        state.mTopic = getTopic();
        state.mState = mIndex;
        state.mFinished = mFinished;
    }
}
