#ifndef OPENMW_ESM_DIAL_H
#define OPENMW_ESM_DIAL_H

#include <list>
#include <map>
#include <memory>
#include <string>

#include "components/esm/defs.hpp"
#include "components/esm/refid.hpp"
#include "components/esm3/infoorder.hpp"

#include "loadinfo.hpp"

namespace ESM
{

    class ESMReader;
    class ESMWriter;

    /*
     * Dialogue topic and journal entries. The actual data is contained in
     * the INFO records following the DIAL.
     */

    struct Dialogue
    {
        using InfoContainer = std::list<DialInfo>;

        constexpr static RecNameInts sRecordId = REC_DIAL;
        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Dialogue"; }

        enum Type : std::int8_t
        {
            Topic = 0,
            Voice = 1,
            Greeting = 2,
            Persuasion = 3,
            Journal = 4,
            Unknown = -1 // Used for deleted dialogues
        };

        RefId mId;
        std::string mStringId;
        Type mType;
        InfoContainer mInfo;
        InfoOrder<DialInfo> mInfoOrder;

#ifdef __3DS__
        // Section handle into the CtrInfoBlob once the diet pass baked this
        // topic's infos and cleared mInfo (see ESMStore::setUp).
        uint32_t mCtrInfoSection = 0xFFFFFFFFu;
#endif

        // Runtime accessor for this topic's INFO list. On desktop it is a
        // zero-cost view over mInfo; on the 3DS it rematerializes the baked
        // records through an LRU. Hold the returned handle for as long as
        // iterators or DialInfo pointers derived from it are used.
        class InfosHandle
        {
        public:
            InfoContainer::const_iterator begin() const { return mList->begin(); }
            InfoContainer::const_iterator end() const { return mList->end(); }
            std::size_t size() const { return mList->size(); }
            bool empty() const { return mList->empty(); }
            const InfoContainer& get() const { return *mList; }

        private:
            friend struct Dialogue;
#ifdef __3DS__
            std::shared_ptr<const InfoContainer> mHold;
#endif
            const InfoContainer* mList = nullptr;
        };

        InfosHandle readInfos() const;

        // Parameters: Info ID, (Info iterator, Deleted flag)
        typedef std::map<ESM::RefId, std::pair<InfoContainer::iterator, bool>> LookupMap;

        void load(ESMReader& esm, bool& isDeleted);
        ///< Loads all sub-records of Dialogue record
        void loadId(ESMReader& esm);
        ///< Loads NAME sub-record of Dialogue record
        void loadData(ESMReader& esm, bool& isDeleted);
        ///< Loads all sub-records of Dialogue record, except NAME sub-record

        void save(ESMWriter& esm, bool isDeleted = false) const;

        /// Remove all INFOs that are deleted
        void setUp();

        /// Read the next info record
        void readInfo(ESMReader& esm);

        void blank();
        ///< Set record to default state (does not touch the ID and does not change the type).
    };
}
#endif
