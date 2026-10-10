#include "ctricu.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

const char* u_errorName(UErrorCode code)
{
    switch (code)
    {
        case U_ZERO_ERROR:
            return "U_ZERO_ERROR";
        case U_ILLEGAL_ARGUMENT_ERROR:
            return "U_ILLEGAL_ARGUMENT_ERROR";
        case U_INVALID_FORMAT_ERROR:
            return "U_INVALID_FORMAT_ERROR";
        case U_INVALID_STATE_ERROR:
            return "U_INVALID_STATE_ERROR";
        case U_PATTERN_SYNTAX_ERROR:
            return "U_PATTERN_SYNTAX_ERROR";
        case U_ARGUMENT_TYPE_MISMATCH:
            return "U_ARGUMENT_TYPE_MISMATCH";
    }
    return "U_UNKNOWN_ERROR";
}

namespace icu
{
    namespace
    {
        void appendUtf8(std::string& out, char32_t cp)
        {
            if (cp < 0x80)
                out += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        std::string lower(std::string_view s)
        {
            std::string r(s);
            for (char& c : r)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return r;
        }

        std::string upper(std::string_view s)
        {
            std::string r(s);
            for (char& c : r)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return r;
        }
    }

    UnicodeString::UnicodeString(const UChar* utf16)
    {
        if (utf16 == nullptr)
            return;
        for (const UChar* p = utf16; *p != 0; ++p)
        {
            char32_t cp = *p;
            if (cp >= 0xD800 && cp < 0xDC00 && p[1] >= 0xDC00 && p[1] < 0xE000)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (p[1] - 0xDC00);
                ++p;
            }
            appendUtf8(mUtf8, cp);
        }
    }

    Locale::Locale(const char* name)
    {
        std::string_view n = name != nullptr ? std::string_view(name) : std::string_view("en");
        std::vector<std::string_view> parts;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= n.size(); ++i)
        {
            if (i == n.size() || n[i] == '_' || n[i] == '-' || n[i] == '.' || n[i] == '@')
            {
                parts.push_back(n.substr(start, i - start));
                if (i < n.size() && (n[i] == '.' || n[i] == '@'))
                    break; // ignore codeset/keywords
                start = i + 1;
            }
        }
        mLanguage = parts.empty() ? std::string() : lower(parts[0]);
        if (parts.size() > 1)
            mCountry = upper(parts[1]);
        if (parts.size() > 2)
            mVariant = upper(parts[2]);
        mName = mLanguage;
        if (!mCountry.empty() || !mVariant.empty())
            mName += "_" + mCountry;
        if (!mVariant.empty())
            mName += "_" + mVariant;
    }

    Locale::Locale(const char* language, const char* country, const char* variant)
    {
        mLanguage = lower(language != nullptr ? language : "");
        mCountry = upper(country != nullptr ? country : "");
        mVariant = upper(variant != nullptr ? variant : "");
        mName = mLanguage;
        if (!mCountry.empty() || !mVariant.empty())
            mName += "_" + mCountry;
        if (!mVariant.empty())
            mName += "_" + mVariant;
    }

    UnicodeString& Locale::getDisplayName(const Locale& /*displayLocale*/, UnicodeString& result) const
    {
        static const std::pair<const char*, const char*> names[] = {
            { "en", "English" },
            { "de", "Deutsch" },
            { "fr", "Fran\xC3\xA7" "ais" },
            { "ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" },
            { "pl", "Polski" },
            { "sv", "Svenska" },
            { "es", "Espa\xC3\xB1ol" },
            { "it", "Italiano" },
            { "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" },
            { "uk", "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0" },
        };
        for (const auto& [code, name] : names)
        {
            if (mLanguage == code)
            {
                result = UnicodeString(name);
                return result;
            }
        }
        result = UnicodeString(mName);
        return result;
    }

    const Locale& Locale::getEnglish()
    {
        static const Locale english("en");
        return english;
    }

    const Locale& Locale::getDefault()
    {
        return getEnglish();
    }

    int32_t Formattable::getLong(UErrorCode& status) const
    {
        if (!isNumeric())
            status = U_INVALID_FORMAT_ERROR;
        return getLong();
    }

    int64_t Formattable::getInt64(UErrorCode& status) const
    {
        if (!isNumeric())
            status = U_INVALID_FORMAT_ERROR;
        return getInt64();
    }

    double Formattable::getDouble(UErrorCode& status) const
    {
        if (!isNumeric())
            status = U_INVALID_FORMAT_ERROR;
        return getDouble();
    }

    const UnicodeString& Formattable::getString(UErrorCode& status) const
    {
        if (mType != kString)
            status = U_INVALID_FORMAT_ERROR;
        return mString;
    }


    struct MessageFormat::Node
    {
        enum class Kind
        {
            Text,
            Pound,
            Simple,
            Number,
            Plural,
            Select
        };

        struct Case
        {
            std::string mSelector; // "=3", "one", "other", ...
            std::vector<Node> mMessage;
        };

        Kind mKind = Kind::Text;
        std::string mText; // literal text or argument name
        int mMinFraction = -1; // -1: default (up to 3, trailing zeros trimmed)
        int mMaxFraction = 3;
        bool mGrouping = true;
        bool mInteger = false;
        bool mPercent = false;
        double mOffset = 0;
        std::vector<Case> mCases;
        std::vector<Node> mChildren;
    };

    namespace
    {
        using Node = MessageFormat::Node;

        struct Parser
        {
            std::string_view mPattern;
            std::size_t mPos = 0;
            bool mError = false;

            bool atEnd() const { return mPos >= mPattern.size(); }
            char peek() const { return atEnd() ? '\0' : mPattern[mPos]; }

            void skipWhitespace()
            {
                while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
                    ++mPos;
            }

            std::string readToken()
            {
                skipWhitespace();
                std::size_t start = mPos;
                while (!atEnd())
                {
                    char c = peek();
                    if (c == ',' || c == '{' || c == '}' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
                        break;
                    ++mPos;
                }
                return std::string(mPattern.substr(start, mPos - start));
            }

            static void appendText(std::vector<Node>& out, std::string_view text)
            {
                if (text.empty())
                    return;
                if (!out.empty() && out.back().mKind == Node::Kind::Text)
                {
                    out.back().mText += text;
                    return;
                }
                Node node;
                node.mText = std::string(text);
                out.push_back(std::move(node));
            }

            void parseMessage(std::vector<Node>& out, bool inPlural, bool nested)
            {
                while (!atEnd())
                {
                    char c = peek();
                    if (c == '\'')
                    {
                        char next = mPos + 1 < mPattern.size() ? mPattern[mPos + 1] : '\0';
                        if (next == '\'')
                        {
                            appendText(out, "'");
                            mPos += 2;
                        }
                        else if (next == '{' || next == '}' || next == '|' || (inPlural && next == '#'))
                        {
                            ++mPos;
                            std::string literal;
                            while (!atEnd())
                            {
                                if (peek() == '\'')
                                {
                                    if (mPos + 1 < mPattern.size() && mPattern[mPos + 1] == '\'')
                                    {
                                        literal += '\'';
                                        mPos += 2;
                                        continue;
                                    }
                                    ++mPos;
                                    break;
                                }
                                literal += peek();
                                ++mPos;
                            }
                            appendText(out, literal);
                        }
                        else
                        {
                            appendText(out, "'");
                            ++mPos;
                        }
                    }
                    else if (c == '{')
                    {
                        ++mPos;
                        Node arg;
                        if (!parseArgument(arg))
                            return;
                        out.push_back(std::move(arg));
                    }
                    else if (c == '}')
                    {
                        if (nested)
                            return;
                        mError = true;
                        return;
                    }
                    else if (c == '#' && inPlural)
                    {
                        Node pound;
                        pound.mKind = Node::Kind::Pound;
                        out.push_back(std::move(pound));
                        ++mPos;
                    }
                    else
                    {
                        std::size_t start = mPos;
                        while (!atEnd() && peek() != '\'' && peek() != '{' && peek() != '}' && !(inPlural && peek() == '#'))
                            ++mPos;
                        appendText(out, mPattern.substr(start, mPos - start));
                    }
                }
                if (nested)
                    mError = true; // missing closing brace
            }

            static void parseNumberStyle(Node& node, std::string_view style)
            {
                while (!style.empty() && (style.front() == ' ' || style.front() == '\t'))
                    style.remove_prefix(1);
                while (!style.empty() && (style.back() == ' ' || style.back() == '\t'))
                    style.remove_suffix(1);
                if (style == "integer")
                {
                    node.mInteger = true;
                    return;
                }
                if (style == "percent")
                {
                    node.mPercent = true;
                    return;
                }
                if (style.substr(0, 2) != "::")
                    return;
                style.remove_prefix(2);
                std::size_t i = 0;
                while (i < style.size())
                {
                    while (i < style.size() && style[i] == ' ')
                        ++i;
                    std::size_t start = i;
                    while (i < style.size() && style[i] != ' ')
                        ++i;
                    std::string_view token = style.substr(start, i - start);
                    if (token.empty())
                        continue;
                    if (token == "group-off" || token == ",_")
                        node.mGrouping = false;
                    else if (token == "percent" || token == "%")
                        node.mPercent = true;
                    else if (token[0] == '.')
                    {
                        int required = 0, optional = 0;
                        for (char c : token.substr(1))
                        {
                            if (c == '0')
                                ++required;
                            else if (c == '#')
                                ++optional;
                        }
                        node.mMinFraction = required;
                        node.mMaxFraction = required + optional;
                    }
                }
            }

            bool parseArgument(Node& node)
            {
                node.mText = readToken();
                skipWhitespace();
                if (node.mText.empty())
                {
                    mError = true;
                    return false;
                }
                if (peek() == '}')
                {
                    ++mPos;
                    node.mKind = Node::Kind::Simple;
                    return true;
                }
                if (peek() != ',')
                {
                    mError = true;
                    return false;
                }
                ++mPos;
                const std::string type = readToken();
                skipWhitespace();
                if (type == "plural" || type == "selectordinal")
                    node.mKind = Node::Kind::Plural;
                else if (type == "select")
                    node.mKind = Node::Kind::Select;
                else
                    node.mKind = Node::Kind::Number; // number, and date/time/spellout fall back to it

                if (peek() == '}')
                {
                    ++mPos;
                    return node.mKind == Node::Kind::Number;
                }
                if (peek() != ',')
                {
                    mError = true;
                    return false;
                }
                ++mPos;

                if (node.mKind == Node::Kind::Number)
                {
                    std::size_t start = mPos;
                    while (!atEnd() && peek() != '}')
                        ++mPos;
                    if (atEnd())
                    {
                        mError = true;
                        return false;
                    }
                    parseNumberStyle(node, mPattern.substr(start, mPos - start));
                    ++mPos;
                    return true;
                }

                while (true)
                {
                    skipWhitespace();
                    if (atEnd())
                    {
                        mError = true;
                        return false;
                    }
                    if (peek() == '}')
                    {
                        ++mPos;
                        break;
                    }
                    std::string selector = readToken();
                    if (selector.empty())
                    {
                        mError = true;
                        return false;
                    }
                    if (node.mKind == Node::Kind::Plural && selector.rfind("offset:", 0) == 0)
                    {
                        node.mOffset = std::atof(selector.c_str() + 7);
                        continue;
                    }
                    skipWhitespace();
                    if (peek() != '{')
                    {
                        mError = true;
                        return false;
                    }
                    ++mPos;
                    Node::Case option;
                    option.mSelector = std::move(selector);
                    parseMessage(option.mMessage, node.mKind == Node::Kind::Plural, true);
                    if (mError || peek() != '}')
                    {
                        mError = true;
                        return false;
                    }
                    ++mPos;
                    node.mCases.push_back(std::move(option));
                }
                return true;
            }
        };

        std::string_view pluralCategory(std::string_view language, double n)
        {
            const bool isInteger = n == std::floor(n);
            const long long i = static_cast<long long>(std::fabs(n));
            const long long mod10 = i % 10;
            const long long mod100 = i % 100;
            if (language == "ru" || language == "uk" || language == "be")
            {
                if (!isInteger)
                    return "other";
                if (mod10 == 1 && mod100 != 11)
                    return "one";
                if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                    return "few";
                return "many";
            }
            if (language == "pl")
            {
                if (!isInteger)
                    return "other";
                if (i == 1)
                    return "one";
                if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
                    return "few";
                return "many";
            }
            if (language == "fr")
                return (i == 0 || i == 1) ? "one" : "other";
            if (language == "ja" || language == "zh" || language == "ko")
                return "other";
            return (isInteger && i == 1) ? "one" : "other";
        }

        void appendGrouped(std::string& out, std::string_view digits, std::string_view separator)
        {
            const std::size_t len = digits.size();
            for (std::size_t k = 0; k < len; ++k)
            {
                if (k != 0 && (len - k) % 3 == 0)
                    out += separator;
                out += digits[k];
            }
        }

        std::string formatNumber(const Node* style, std::string_view language, double value)
        {
            int minFraction = -1;
            int maxFraction = 3;
            bool grouping = true;
            if (style != nullptr)
            {
                minFraction = style->mMinFraction;
                maxFraction = style->mMaxFraction;
                grouping = style->mGrouping;
                if (style->mInteger)
                    maxFraction = 0;
                if (style->mPercent)
                {
                    value *= 100;
                    if (style->mMinFraction < 0)
                        maxFraction = 0;
                }
            }
            if (minFraction > maxFraction)
                maxFraction = minFraction;

            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", maxFraction, value);
            std::string text(buffer);
            std::string intPart = text;
            std::string fracPart;
            if (std::size_t dot = text.find('.'); dot != std::string::npos)
            {
                intPart = text.substr(0, dot);
                fracPart = text.substr(dot + 1);
            }
            const int keep = minFraction < 0 ? 0 : minFraction;
            while (static_cast<int>(fracPart.size()) > keep && !fracPart.empty() && fracPart.back() == '0')
                fracPart.pop_back();

            bool negative = !intPart.empty() && intPart[0] == '-';
            if (negative)
                intPart.erase(0, 1);
            if (negative && intPart.find_first_not_of('0') == std::string::npos && fracPart.empty())
                negative = false; // no "-0"

            const bool commaDecimal = language != "en" && language != "ja" && language != "zh" && language != "ko";
            std::string_view groupSeparator = ",";
            if (language == "de" || language == "it" || language == "es")
                groupSeparator = ".";
            else if (commaDecimal)
                groupSeparator = "\xC2\xA0"; // no-break space

            std::string out;
            if (negative)
                out += '-';
            if (grouping && intPart.size() > 3 && !(intPart.size() == 4 && (language == "pl" || language == "es")))
                appendGrouped(out, intPart, groupSeparator);
            else
                out += intPart;
            if (!fracPart.empty())
            {
                out += commaDecimal ? ',' : '.';
                out += fracPart;
            }
            if (style != nullptr && style->mPercent)
                out += commaDecimal ? "\xC2\xA0%" : "%";
            return out;
        }

        struct FormatContext
        {
            std::string_view mLanguage;
            const UnicodeString* mNames;
            const Formattable* mArgs;
            int32_t mCount;

            const Formattable* find(const std::string& name) const
            {
                if (mNames != nullptr)
                {
                    for (int32_t k = 0; k < mCount; ++k)
                        if (mNames[k].utf8() == name)
                            return &mArgs[k];
                }
                else if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos)
                {
                    const long index = std::strtol(name.c_str(), nullptr, 10);
                    if (index >= 0 && index < mCount)
                        return &mArgs[index];
                }
                return nullptr;
            }
        };

        void formatNodes(const std::vector<Node>& nodes, const FormatContext& context, const double* poundValue,
            const Node* poundStyle, std::string& out);

        void formatArgument(const Node& node, const FormatContext& context, std::string& out)
        {
            const Formattable* value = context.find(node.mText);
            if (value == nullptr)
            {
                out += '{';
                out += node.mText;
                out += '}';
                return;
            }
            switch (node.mKind)
            {
                case Node::Kind::Simple:
                    if (value->getType() == Formattable::kString)
                    {
                        UnicodeString text;
                        out += value->getString(text).utf8();
                    }
                    else
                        out += formatNumber(nullptr, context.mLanguage, value->getDouble());
                    return;
                case Node::Kind::Number:
                    if (value->getType() == Formattable::kString)
                    {
                        UnicodeString text;
                        out += value->getString(text).utf8();
                    }
                    else
                        out += formatNumber(&node, context.mLanguage, value->getDouble());
                    return;
                case Node::Kind::Plural:
                {
                    const double number = value->isNumeric() ? value->getDouble() : 0.0;
                    const Node::Case* chosen = nullptr;
                    for (const Node::Case& option : node.mCases)
                    {
                        if (option.mSelector.size() > 1 && option.mSelector[0] == '='
                            && std::atof(option.mSelector.c_str() + 1) == number)
                        {
                            chosen = &option;
                            break;
                        }
                    }
                    const double adjusted = number - node.mOffset;
                    if (chosen == nullptr)
                    {
                        const std::string_view category = pluralCategory(context.mLanguage, adjusted);
                        for (const Node::Case& option : node.mCases)
                            if (option.mSelector == category)
                                chosen = &option;
                    }
                    if (chosen == nullptr)
                        for (const Node::Case& option : node.mCases)
                            if (option.mSelector == "other")
                                chosen = &option;
                    if (chosen != nullptr)
                        formatNodes(chosen->mMessage, context, &adjusted, nullptr, out);
                    return;
                }
                case Node::Kind::Select:
                {
                    std::string key;
                    if (value->getType() == Formattable::kString)
                    {
                        UnicodeString text;
                        key = value->getString(text).utf8();
                    }
                    else
                        key = formatNumber(nullptr, "en", value->getDouble());
                    const Node::Case* chosen = nullptr;
                    for (const Node::Case& option : node.mCases)
                        if (option.mSelector == key)
                            chosen = &option;
                    if (chosen == nullptr)
                        for (const Node::Case& option : node.mCases)
                            if (option.mSelector == "other")
                                chosen = &option;
                    if (chosen != nullptr)
                        formatNodes(chosen->mMessage, context, nullptr, nullptr, out);
                    return;
                }
                default:
                    return;
            }
        }

        void formatNodes(const std::vector<Node>& nodes, const FormatContext& context, const double* poundValue,
            const Node* poundStyle, std::string& out)
        {
            for (const Node& node : nodes)
            {
                switch (node.mKind)
                {
                    case Node::Kind::Text:
                        out += node.mText;
                        break;
                    case Node::Kind::Pound:
                        if (poundValue != nullptr)
                            out += formatNumber(poundStyle, context.mLanguage, *poundValue);
                        else
                            out += '#';
                        break;
                    default:
                        formatArgument(node, context, out);
                        break;
                }
            }
        }
    }

    MessageFormat::MessageFormat(
        const UnicodeString& pattern, const Locale& locale, UParseError& parseError, UErrorCode& status)
        : mPattern(pattern)
        , mLocale(locale)
    {
        parseError = UParseError();
        if (U_FAILURE(status))
            return;
        auto root = std::make_shared<Node>();
        Parser parser{ pattern.utf8() };
        parser.parseMessage(root->mChildren, false, false);
        if (parser.mError)
        {
            status = U_PATTERN_SYNTAX_ERROR;
            parseError.offset = static_cast<int32_t>(parser.mPos);
            const std::string& text = pattern.utf8();
            const std::size_t from = parser.mPos > U_PARSE_CONTEXT_LEN - 1 ? parser.mPos - (U_PARSE_CONTEXT_LEN - 1) : 0;
            std::size_t k = 0;
            for (std::size_t p = from; p < parser.mPos && p < text.size() && k < U_PARSE_CONTEXT_LEN - 1; ++p, ++k)
                parseError.preContext[k] = static_cast<unsigned char>(text[p]);
            k = 0;
            for (std::size_t p = parser.mPos; p < text.size() && k < U_PARSE_CONTEXT_LEN - 1; ++p, ++k)
                parseError.postContext[k] = static_cast<unsigned char>(text[p]);
        }
        mRoot = std::move(root);
    }

    MessageFormat::MessageFormat(const UnicodeString& pattern, const Locale& locale, UErrorCode& status)
    {
        UParseError parseError;
        *this = MessageFormat(pattern, locale, parseError, status);
    }

    MessageFormat::MessageFormat(const MessageFormat& other) = default;
    MessageFormat& MessageFormat::operator=(const MessageFormat& other) = default;
    MessageFormat::MessageFormat(MessageFormat&& other) noexcept = default;
    MessageFormat& MessageFormat::operator=(MessageFormat&& other) noexcept = default;
    MessageFormat::~MessageFormat() = default;

    UnicodeString& MessageFormat::toPattern(UnicodeString& result) const
    {
        result = mPattern;
        return result;
    }

    UnicodeString& MessageFormat::format(const UnicodeString* argumentNames, const Formattable* arguments,
        int32_t count, UnicodeString& appendTo, UErrorCode& status) const
    {
        if (U_FAILURE(status))
            return appendTo;
        if (mRoot == nullptr)
        {
            status = U_INVALID_STATE_ERROR;
            return appendTo;
        }
        FormatContext context{ mLocale.getLanguage(), argumentNames, arguments, count };
        std::string out;
        formatNodes(mRoot->mChildren, context, nullptr, nullptr, out);
        appendTo.append(UnicodeString(std::move(out)));
        return appendTo;
    }
}
