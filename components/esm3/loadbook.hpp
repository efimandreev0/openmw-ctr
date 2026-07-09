#ifndef OPENMW_ESM_BOOK_H
#define OPENMW_ESM_BOOK_H

#include "components/esm/defs.hpp"
#include "components/esm/refid.hpp"

#include <cstdint>
#include <string>

namespace ESM
{
    /*
     * Books, magic scrolls, notes and so on
     */

    class ESMReader;
    class ESMWriter;

    struct Book
    {
        constexpr static RecNameInts sRecordId = REC_BOOK;

        /// Return a string descriptor for this record type. Currently used for debugging / error logs only.
        static std::string_view getRecordType() { return "Book"; }

        struct BKDTstruct
        {
            float mWeight;
            int32_t mValue, mIsScroll, mSkillId, mEnchant;
        };

        BKDTstruct mData;
        std::string mName, mModel, mIcon, mText;

        // 3DS: book text deferred to the SD string pool (see CtrStrPool).
        uint32_t mTextOfs = 0, mTextLen = 0;

        std::string getText() const;
        bool hasText() const { return mTextLen != 0 || !mText.empty(); }
        uint32_t mRecordFlags;
        RefId mId;
        RefId mScript, mEnchant;

        void load(ESMReader& esm, bool& isDeleted);
        void save(ESMWriter& esm, bool isDeleted = false) const;

        void blank();
        ///< Set record to default state (does not touch the ID).
    };
}
#endif
