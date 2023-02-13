#pragma once

#include <vector>
namespace SongDetailsCache {
    /// @brief Enum describing common mods that maps tend to use
    enum class MapMods {
        None =              0x0,
        NoodleExtensions =  0x1,
        NE =                0x1, // alias
        MappingExtensions = 0x2,
        ME =                0x2, // alias
        Chroma =            0x4,
        Cinema =            0x8,
    static std::vector<std::string> toVectorOfStrings(const MapMods& mods) {
        std::vector<std::string> result{};
        if (hasFlags(mods, MapMods::Cinema)) result.emplace_back("Cinema");
        return result;
    }
};
