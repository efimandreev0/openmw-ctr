#ifndef OPENMW_MWRENDER_CTR_SKY_H
#define OPENMW_MWRENDER_CTR_SKY_H

#include <algorithm>
#include <string>

#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/fallback/fallback.hpp>

#include "../weatherresult.hpp"

namespace MWRender
{
    class SkyManager
    {
    public:
        void update(float duration, float gameTimeScale = 30.f)
        {
            float cloudDelta = duration * mCloudSpeed / 400.f;
            if (mTimescaleClouds)
                cloudDelta *= gameTimeScale / 60.f;
            mCloudAnimationTimer += cloudDelta;
            if (mCloudAnimationTimer >= 4.f)
                mCloudAnimationTimer -= 4.f;
        }

        void setEnabled(bool enabled) { mEnabled = enabled; }
        bool isEnabled() { return mEnabled; }

        int getMasserPhase() const { return mEnabled ? static_cast<int>(MoonState::phaseToInt(mMasser.mPhase)) : 0; }
        int getSecundaPhase() const { return mEnabled ? static_cast<int>(MoonState::phaseToInt(mSecunda.mPhase)) : 0; }

        void setMoonColour(bool red) {}

        void setWeather(const WeatherResult& weather)
        {
            mSkyColour = weather.mSkyColor;
            mFogColour = weather.mFogColor;
            mClouds = weather.mCloudTexture;
            mNextClouds = weather.mNextCloudTexture;
            mCloudBlendFactor = std::min(std::max(weather.mCloudBlendFactor, 0.f), 1.f);
            mCloudSpeed = weather.mCloudSpeed;
            mSunDiscColour = weather.mSunDiscColor;
        }

        const std::string& getClouds() const { return mClouds; }
        const std::string& getNextClouds() const { return mNextClouds; }
        float getCloudBlendFactor() const { return mCloudBlendFactor; }
        float getCloudAnimationTimer() const { return mCloudAnimationTimer; }
        const osg::Vec3f& getSunDirection() const { return mSunDirection; }
        const osg::Vec4f& getSunDiscColor() const { return mSunDiscColour; }

        void sunEnable() { mSunEnabled = true; }
        void sunDisable() { mSunEnabled = false; }
        bool isSunEnabled() const { return mSunEnabled; }

        bool hasRain() const { return false; }
        bool getRainRipplesEnabled() const { return false; }
        float getPrecipitationAlpha() const { return 0.f; }
        float getBaseWindSpeed() const { return 0.f; }

        void setStormParticleDirection(const osg::Vec3f& direction) {}
        void setSunDirection(const osg::Vec3f& direction)
        {
            mSunDirection = direction;
            mSunDirection.normalize();
        }
        void setMasserState(const MoonState& state) { mMasser = state; }
        void setSecundaState(const MoonState& state) { mSecunda = state; }
        void setGlareTimeOfDayFade(float val) {}
        void setWaterEnabled(bool enabled) {}
        void setWaterHeight(float height) {}
        void setSunglare(bool enabled) {}

        osg::Vec4f getSkyColor() const { return mSkyColour; }
        osg::Vec4f getFogColor() const { return mFogColour; }

    private:
        bool mEnabled = false;
        bool mSunEnabled = true;
        MoonState mMasser{};
        MoonState mSecunda{};
        osg::Vec4f mSkyColour{ 0.5f, 0.6f, 0.75f, 1.f };
        osg::Vec4f mFogColour{ 0.5f, 0.6f, 0.75f, 1.f };
        std::string mClouds;
        std::string mNextClouds;
        float mCloudBlendFactor = 0.f;
        float mCloudSpeed = 0.f;
        float mCloudAnimationTimer = 0.f;
        bool mTimescaleClouds = Fallback::Map::getBool("Weather_Timescale_Clouds");
        osg::Vec3f mSunDirection{ 0.f, 0.f, 1.f };
        osg::Vec4f mSunDiscColour{ 1.f, 1.f, 1.f, 1.f };
    };
}

#endif
