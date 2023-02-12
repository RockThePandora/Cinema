#include "main.hpp"

#include "song-details/shared/Data/Song.hpp"
#include <string>
#include <vector>

using namespace SongDetailsCache;

            /// @return Hexadecimal representation of the Map Hash
            std::string hash() noexcept;

            /// @return Song name of this song
            std::string& songName() noexcept;
            /// @return Song author name of this song
            std::string& songAuthorName() noexcept;
            /// @return Level author name of this song
            std::string& levelAuthorName() noexcept;
            /// @return Uploader name of this song
            std::string& uploaderName() noexcept;

            /// @return Cover url on beatsaver
            std::string coverURL() noexcept;

            // allow iterating all difficulties of this song in a regular foreach loop
            using difficulty_const_iterator = std::vector<SongDetailsCache::SongDifficulty>::const_iterator;
            difficulty_const_iterator begin() noexcept;
            difficulty_const_iterator end() noexcept;
}
