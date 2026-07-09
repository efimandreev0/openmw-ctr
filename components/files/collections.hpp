#ifndef COMPONENTS_FILES_COLLECTION_HPP
#define COMPONENTS_FILES_COLLECTION_HPP

#include <filesystem>

#include "multidircollection.hpp"

namespace Files
{
    class Collections
    {
    public:
        using CollectionsMap = std::map<std::string, MultiDirCollection, Misc::StringUtils::CiComp>;

        Collections();

        ///< Directories are listed with increasing priority.
        Collections(const Files::PathContainer& directories);

        ///< Return a file collection for the given extension.
        const MultiDirCollection& getCollection(std::string_view extension) const;

        std::filesystem::path getPath(std::string_view file) const;
        ///< Return full path (including filename) of \a file.
        ///
        /// If the file does not exist in any of the collection's
        /// directories, an exception is thrown. \a file must include the
        /// extension.

        bool doesExist(std::string_view file) const;
        ///< \return Does a file with the given name exist?

        const Files::PathContainer& getPaths() const;

        /// Restore/read the per-extension cache map. Used by scancache.
        void setCollections(CollectionsMap collections) const { mCollections = std::move(collections); }
        const CollectionsMap& getCollections() const { return mCollections; }

    private:
        Files::PathContainer mDirectories;

        mutable CollectionsMap mCollections;
    };
}

#endif
