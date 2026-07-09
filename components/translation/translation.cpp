#include "translation.hpp"

#include <fstream>
#include <sstream>

#include <components/misc/pathhelpers.hpp>

namespace Translation
{
    Storage::Storage()
        : mEncoder(nullptr)
    {
    }

    void Storage::loadTranslationData(const Files::Collections& dataFileCollections, std::string_view esmFileName)
    {
        std::string_view esmNameNoExtension = Misc::stemFile(esmFileName);

        loadData(mCellNamesTranslations, esmNameNoExtension, "cel", dataFileCollections);
        loadData(mPhraseForms, esmNameNoExtension, "top", dataFileCollections);
        loadData(mTopicIDs, esmNameNoExtension, "mrk", dataFileCollections);
    }

    void Storage::loadData(ContainerType& container, std::string_view fileNameNoExtension, std::string_view extension,
        const Files::Collections& dataFileCollections)
    {
        std::string fileName(fileNameNoExtension);
        fileName += '.';
        fileName += extension;

        const Files::MultiDirCollection& collection = dataFileCollections.getCollection(extension);
        if (collection.doesExist(fileName))
        {
#if defined(__vita__) || defined(__3DS__)
            // Read the whole file in one go and parse from memory. A plain
            // std::ifstream over the console devoptab paths ("sdmc:/...")
            // is both glacially slow (tiny filebuf refills, each an FS
            // round-trip) and risky with the !eof/!fail loop below — a
            // misbehaving streambuf spins it forever. One fread sidesteps
            // all of it; these files are a few hundred KB at most.
            const std::string pathStr = collection.getPath(fileName).string();
            FILE* f = fopen(pathStr.c_str(), "rb");
            if (!f)
                throw std::runtime_error("failed to open translation file: " + fileName);
            fseek(f, 0, SEEK_END);
            const long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::string data;
            if (size > 0)
            {
                data.resize(static_cast<size_t>(size));
                const size_t read = fread(data.data(), 1, data.size(), f);
                data.resize(read);
            }
            fclose(f);
            std::istringstream stream(data);
            loadDataFromStream(container, stream);
#else
            std::ifstream stream(collection.getPath(fileName));

            if (!stream.is_open())
                throw std::runtime_error("failed to open translation file: " + fileName);

            loadDataFromStream(container, stream);
#endif
        }
    }

    void Storage::loadDataFromStream(ContainerType& container, std::istream& stream)
    {
        std::string line;
        while (!stream.eof() && !stream.fail())
        {
            std::getline(stream, line);
            if (!line.empty() && *line.rbegin() == '\r')
                line.resize(line.size() - 1);

            if (!line.empty())
            {
                const std::string_view utf8 = mEncoder->getUtf8(line);

                size_t tabPos = utf8.find('\t');
                if (tabPos != std::string::npos && tabPos > 0 && tabPos < utf8.size() - 1)
                {
                    const std::string_view key = utf8.substr(0, tabPos);
                    const std::string_view value = utf8.substr(tabPos + 1);

                    if (!key.empty() && !value.empty())
                        container.emplace(key, value);
                }
            }
        }
    }

    std::string_view Storage::translateCellName(std::string_view cellName) const
    {
        auto entry = mCellNamesTranslations.find(cellName);

        if (entry == mCellNamesTranslations.end())
            return cellName;

        return entry->second;
    }

    std::string_view Storage::topicID(std::string_view phrase) const
    {
        std::string_view result = topicStandardForm(phrase);

        // seeking for the topic ID
        auto topicIDIterator = mTopicIDs.find(result);

        if (topicIDIterator != mTopicIDs.end())
            result = topicIDIterator->second;

        return result;
    }

    std::string_view Storage::topicStandardForm(std::string_view phrase) const
    {
        auto phraseFormsIterator = mPhraseForms.find(phrase);

        if (phraseFormsIterator != mPhraseForms.end())
            return phraseFormsIterator->second;
        else
            return phrase;
    }

    void Storage::setEncoder(ToUTF8::Utf8Encoder* encoder)
    {
        mEncoder = encoder;
    }

    bool Storage::hasTranslation() const
    {
        return !mCellNamesTranslations.empty() || !mTopicIDs.empty() || !mPhraseForms.empty();
    }
}
