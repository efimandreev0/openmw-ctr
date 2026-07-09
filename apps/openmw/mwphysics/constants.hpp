#ifndef OPENMW_MWPHYSICS_CONSTANTS_H
#define OPENMW_MWPHYSICS_CONSTANTS_H

namespace MWPhysics
{
    static constexpr float sStepSizeDown = 62.0f;

    static constexpr float sMinStep = 10.0f; // hack to skip over tiny unwalkable slopes
    static constexpr float sMinStep2 = 20.0f; // hack to skip over shorter but longer/wider/further unwalkable slopes
    // whether to do the above stairstepping logic hacks to work around bad morrowind assets - disabling causes problems
    // but improves performance
    static constexpr bool sDoExtraStairHacks = true;

    static constexpr float sGroundOffset = 1.0f;

    // Arbitrary number. To prevent infinite loops. They shouldn't happen but it's good to be prepared.
    // Reduced on Vita for better performance - may cause slight clipping in complex collision scenarios
#if defined(__vita__) || defined(__3DS__)
    static constexpr int sMaxIterations = 5;
#else
    static constexpr int sMaxIterations = 8;
#endif
    // Allows for more precise movement solving without getting stuck or snagging too easily.
    static constexpr float sCollisionMargin = 0.2f;
    // Allow for a small amount of penetration to prevent numerical precision issues from causing the "unstuck"ing code
    // to run unnecessarily Currently set to 0 because having the "unstuck"ing code run whenever possible prevents some
    // glitchy snagging issues
    // Increased on Vita to reduce unstuck calculations for better performance
#if defined(__vita__) || defined(__3DS__)
    static constexpr float sAllowedPenetration = 0.5f;
#else
    static constexpr float sAllowedPenetration = 0.0f;
#endif
}

#endif
