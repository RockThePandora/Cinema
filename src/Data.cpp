#include "main.hpp"

#include "song-details/shared/Data/Song.hpp"
#include "song-details/shared/Data/MapDifficulty.hpp"
#include "song-details/shared/Data/RankedStatus.hpp"
#include <string>
#include <vector>
#include <cmath>

namespace SongDetailsCache {

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

            /// @brief check if this song is the same as the other, by checking the pointer. Since we do not allow copy construction we can just compare pointers (test pls)
            /// @param other the Song to check against
            /// @return equivalency
            //bool operator ==(Song& other) noexcept {
                //return this == &other;
            }

            /// @brief checks if this song is the same as none
            //inline operator bool() noexcept {
                //return this != &SongDifficulty::none;

            /// @brief Helper function to get a difficulty from this song
            /// @param outDiff a reference to the output pointer
            /// @param diff the difficulty to search for
            /// @param characteristic the characteristic to search for
            /// @return whether it was found
            //bool GetDifficulty(SongDifficulty*& outDiff, MapDifficulty diff, MapCharacteristic characteristic = MapCharacteristic::Standard) noexcept;
            /// @brief Helper function to get a difficulty from this song
            /// @param outDiff a reference to the output pointer
            /// @param diff the difficulty to search for
            /// @param characteristic the characteristic to search for as a string
            /// @return whether it was found
            //bool GetDifficulty(SongDifficulty*& outDiff, MapDifficulty diff, std::string_view characteristic) noexcept;
            /// @brief Helper function to get a difficulty from this song
            /// @param diff the difficulty to search for
            /// @param characteristic the characteristic to search for
            /// @return The found SongDifficulty, otherwise SongDifficulty::none
            //SongDifficulty& GetDifficulty(MapDifficulty diff, MapCharacteristic characteristic = MapCharacteristic::Standard) noexcept;
            /// @brief Helper function to get a difficulty from this song
            /// @param diff the difficulty to search for
            /// @param characteristic the characteristic to search for as a string
            /// @return The found SongDifficulty, otherwise SongDifficulty::none
            //SongDifficulty& GetDifficulty(MapDifficulty diff, std::string_view characteristic) noexcept;

            // allow iterating all difficulties of this song in a regular foreach loop
            using difficulty_const_iterator = std::vector<SongDifficulty>::const_iterator;
            difficulty_const_iterator begin() noexcept;
            difficulty_const_iterator end() noexcept;
            /// @brief none song, exists to provide an "invalid" song
            //static Song SongDifficulty::none;
            /// @brief default move ctor
            //Song(Song&&) = default;
            /// @brief delete copy constructor
            //Song(Song&) = delete;
            /// @brief this needs to be public for specific reasons, but it's not advised to make your own SongDetail::Songs!
            Song(std::size_t index, std::size_t diffOffset, uint8_t diffCount, Structs::SongProto* proto) noexcept;
        private:
            friend class SongDetailsContainer;
            friend class SongDetails;
    };
}
