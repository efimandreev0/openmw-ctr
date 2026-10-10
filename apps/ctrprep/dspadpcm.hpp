#ifndef OPENMW_APPS_CTRPREP_DSPADPCM_H
#define OPENMW_APPS_CTRPREP_DSPADPCM_H

#include <array>
#include <cstdint>
#include <vector>

namespace CtrPrep
{
    struct AdpcmChannel
    {
        std::array<std::int16_t, 16> mCoefficients{}; ///< 8 predictor pairs, 5.11 fixed point
        std::vector<std::uint8_t> mFrames; ///< 8-byte frames: predictor/scale byte, then 14 nibbles (high first)
        std::vector<std::int16_t> mHistory0;
        std::vector<std::int16_t> mHistory1;
    };

    AdpcmChannel encodeAdpcm(const std::int16_t* samples, std::size_t count);

    std::vector<std::int16_t> decodeAdpcm(const AdpcmChannel& channel, std::size_t count);
}

#endif
