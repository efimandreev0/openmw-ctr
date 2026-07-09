#ifndef OPENMW_COMPONENTS_VFS_FILEMAP_H
#define OPENMW_COMPONENTS_VFS_FILEMAP_H

#include <map>
#include <string>

namespace VFS
{
    class File;

    namespace Path
    {
        class Normalized;
        class NormalizedView;
    }

#ifdef __3DS__
    // 3DS: the index keys are views into name storage owned by the archives
    // (the BSA string tables, normalized in place at load, and the loose-file
    // archive's own index). Owning ~40k map keys costs ~3MB of a 124MB
    // budget; a view costs 8 bytes.
    using FileMap = std::map<Path::NormalizedView, File*, std::less<>>;
#else
    using FileMap = std::map<Path::Normalized, File*, std::less<>>;
#endif
}

#endif
