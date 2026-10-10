#ifndef OPENMW_COMPONENTS_CTR_UNICODE_CTRICU_H
#define OPENMW_COMPONENTS_CTR_UNICODE_CTRICU_H


#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

typedef char16_t UChar;
typedef int8_t UBool;

enum UErrorCode
{
    U_ZERO_ERROR = 0,
    U_ILLEGAL_ARGUMENT_ERROR = 1,
    U_INVALID_FORMAT_ERROR = 3,
    U_INVALID_STATE_ERROR = 27,
    U_PATTERN_SYNTAX_ERROR = 65799,
    U_ARGUMENT_TYPE_MISMATCH = 65804,
};

inline bool U_SUCCESS(UErrorCode code)
{
    return code <= U_ZERO_ERROR;
}
inline bool U_FAILURE(UErrorCode code)
{
    return code > U_ZERO_ERROR;
}

const char* u_errorName(UErrorCode code);

enum
{
    U_PARSE_CONTEXT_LEN = 16
};

struct UParseError
{
    int32_t line = 0;
    int32_t offset = 0;
    UChar preContext[U_PARSE_CONTEXT_LEN] = {};
    UChar postContext[U_PARSE_CONTEXT_LEN] = {};
};

namespace icu
{
    class StringPiece
    {
    public:
        StringPiece() = default;
        StringPiece(const char* str)
            : mView(str != nullptr ? std::string_view(str) : std::string_view())
        {
        }
        StringPiece(const std::string& str)
            : mView(str)
        {
        }
        StringPiece(const char* data, int32_t length)
            : mView(data, static_cast<std::size_t>(length))
        {
        }
        const char* data() const { return mView.data(); }
        int32_t length() const { return static_cast<int32_t>(mView.size()); }
        int32_t size() const { return length(); }
        std::string_view view() const { return mView; }

    private:
        std::string_view mView;
    };

    class UnicodeString
    {
    public:
        UnicodeString() = default;
        UnicodeString(const char* utf8)
            : mUtf8(utf8 != nullptr ? utf8 : "")
        {
        }
        UnicodeString(const UChar* utf16);
        explicit UnicodeString(std::string utf8)
            : mUtf8(std::move(utf8))
        {
        }

        static UnicodeString fromUTF8(StringPiece utf8) { return UnicodeString(std::string(utf8.view())); }

        std::string& toUTF8String(std::string& result) const
        {
            result += mUtf8;
            return result;
        }

        const std::string& utf8() const { return mUtf8; }
        int32_t length() const { return static_cast<int32_t>(mUtf8.size()); }
        bool isEmpty() const { return mUtf8.empty(); }

        UnicodeString& append(const UnicodeString& other)
        {
            mUtf8 += other.mUtf8;
            return *this;
        }
        UnicodeString& operator+=(const UnicodeString& other) { return append(other); }
        UnicodeString& remove()
        {
            mUtf8.clear();
            return *this;
        }

        bool operator==(const UnicodeString& other) const { return mUtf8 == other.mUtf8; }
        bool operator!=(const UnicodeString& other) const { return mUtf8 != other.mUtf8; }
        bool operator<(const UnicodeString& other) const { return mUtf8 < other.mUtf8; }

    private:
        std::string mUtf8;
    };

    class Locale
    {
    public:
        Locale()
            : Locale("en")
        {
        }
        Locale(const char* name);
        Locale(const char* language, const char* country, const char* variant = nullptr);

        const char* getName() const { return mName.c_str(); }
        const char* getLanguage() const { return mLanguage.c_str(); }
        const char* getCountry() const { return mCountry.c_str(); }
        const char* getVariant() const { return mVariant.c_str(); }
        const char* getBaseName() const { return mName.c_str(); }

        UnicodeString& getDisplayName(const Locale& displayLocale, UnicodeString& result) const;
        UnicodeString& getDisplayName(UnicodeString& result) const { return getDisplayName(*this, result); }

        static const Locale& getEnglish();
        static const Locale& getDefault();

        bool operator==(const Locale& other) const { return mName == other.mName; }
        bool operator!=(const Locale& other) const { return mName != other.mName; }

    private:
        std::string mName;
        std::string mLanguage;
        std::string mCountry;
        std::string mVariant;
    };

    class ErrorCode
    {
    public:
        ErrorCode() = default;
        virtual ~ErrorCode() = default;
        operator UErrorCode&() { return mCode; }
        operator UErrorCode*() { return &mCode; }
        UErrorCode get() const { return mCode; }
        UErrorCode reset()
        {
            UErrorCode code = mCode;
            mCode = U_ZERO_ERROR;
            return code;
        }
        void set(UErrorCode code) { mCode = code; }
        UBool isSuccess() const { return U_SUCCESS(mCode); }
        UBool isFailure() const { return U_FAILURE(mCode); }
        const char* errorName() const { return u_errorName(mCode); }

    private:
        UErrorCode mCode = U_ZERO_ERROR;
    };

    class Formattable
    {
    public:
        enum Type
        {
            kDate,
            kDouble,
            kLong,
            kString,
            kArray,
            kInt64,
            kObject
        };

        Formattable()
            : mType(kLong)
        {
        }
        Formattable(double value)
            : mType(kDouble)
            , mDouble(value)
        {
        }
        Formattable(int32_t value)
            : mType(kLong)
            , mInt(value)
        {
        }
        Formattable(int64_t value)
            : mType(kInt64)
            , mInt(value)
        {
        }
        Formattable(const char* utf8)
            : mType(kString)
            , mString(utf8)
        {
        }
        Formattable(const UnicodeString& value)
            : mType(kString)
            , mString(value)
        {
        }

        Type getType() const { return mType; }
        bool isNumeric() const { return mType == kDouble || mType == kLong || mType == kInt64; }

        int32_t getLong() const { return static_cast<int32_t>(mType == kDouble ? mDouble : mInt); }
        int32_t getLong(UErrorCode& status) const;
        int64_t getInt64() const { return mType == kDouble ? static_cast<int64_t>(mDouble) : mInt; }
        int64_t getInt64(UErrorCode& status) const;
        double getDouble() const { return mType == kDouble ? mDouble : static_cast<double>(mInt); }
        double getDouble(UErrorCode& status) const;
        UnicodeString& getString(UnicodeString& result) const
        {
            result = mString;
            return result;
        }
        const UnicodeString& getString(UErrorCode& status) const;

    private:
        Type mType;
        int64_t mInt = 0;
        double mDouble = 0;
        UnicodeString mString;
    };

    class MessageFormat
    {
    public:
        MessageFormat(const UnicodeString& pattern, const Locale& locale, UParseError& parseError, UErrorCode& status);
        MessageFormat(const UnicodeString& pattern, const Locale& locale, UErrorCode& status);
        MessageFormat(const MessageFormat& other);
        MessageFormat& operator=(const MessageFormat& other);
        MessageFormat(MessageFormat&& other) noexcept;
        MessageFormat& operator=(MessageFormat&& other) noexcept;
        ~MessageFormat();

        UnicodeString& toPattern(UnicodeString& result) const;

        UnicodeString& format(const UnicodeString* argumentNames, const Formattable* arguments, int32_t count,
            UnicodeString& appendTo, UErrorCode& status) const;

        const Locale& getLocale() const { return mLocale; }

        struct Node;

    private:
        UnicodeString mPattern;
        Locale mLocale;
        std::shared_ptr<const Node> mRoot; // immutable once parsed, so copies share it
    };

    class Calendar
    {
    };
}

#endif
