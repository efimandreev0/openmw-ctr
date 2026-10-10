#include "dspadpcm.hpp"

#include <algorithm>
#include <cmath>
#include <limits>


namespace CtrPrep
{
    namespace
    {
        constexpr int sSamplesPerFrame = 14;
        constexpr int sNumPredictors = 8;

        inline std::int16_t clamp16(std::int32_t v)
        {
            return static_cast<std::int16_t>(std::clamp<std::int32_t>(v, -32768, 32767));
        }

        inline std::int16_t decodeSample(int nibble, int scale, int c1, int c2, int h0, int h1)
        {
            const std::int32_t value = ((nibble * (1 << scale)) * 2048) + 1024 + c1 * h0 + c2 * h1;
            return clamp16(value >> 11);
        }

        struct Predictor
        {
            double mA1 = 0;
            double mA2 = 0;
        };

        std::array<std::int16_t, 16> findCoefficients(const std::int16_t* samples, std::size_t count)
        {
            std::vector<Predictor> points;
            std::vector<double> weights;
            for (std::size_t start = 0; start + sSamplesPerFrame <= count; start += sSamplesPerFrame)
            {
                double r11 = 0, r22 = 0, r12 = 0, q1 = 0, q2 = 0, energy = 0;
                for (int n = 0; n < sSamplesPerFrame; ++n)
                {
                    const std::size_t i = start + n;
                    const double x = samples[i];
                    const double x1 = i >= 1 ? samples[i - 1] : 0.0;
                    const double x2 = i >= 2 ? samples[i - 2] : 0.0;
                    r11 += x1 * x1;
                    r22 += x2 * x2;
                    r12 += x1 * x2;
                    q1 += x * x1;
                    q2 += x * x2;
                    energy += x * x;
                }
                const double det = r11 * r22 - r12 * r12;
                if (energy < 1.0 || det <= 1e-6 * r11 * r22 + 1e-9)
                    continue;
                Predictor p;
                p.mA1 = std::clamp((q1 * r22 - q2 * r12) / det, -1.99, 1.99);
                p.mA2 = std::clamp((q2 * r11 - q1 * r12) / det, -0.99, 0.99);
                points.push_back(p);
                weights.push_back(std::sqrt(energy));
            }

            std::array<Predictor, sNumPredictors> centers = { { { 0.0, 0.0 }, { 0.9, 0.0 }, { 1.8, -0.82 },
                { 1.5, -0.6 }, { 1.95, -0.96 }, { 1.2, -0.3 }, { 0.5, 0.2 }, { 1.65, -0.9 } } };
            if (!points.empty())
            {
                std::vector<int> assignment(points.size(), 0);
                for (int iteration = 0; iteration < 24; ++iteration)
                {
                    bool changed = false;
                    for (std::size_t i = 0; i < points.size(); ++i)
                    {
                        int best = 0;
                        double bestDistance = std::numeric_limits<double>::max();
                        for (int k = 0; k < sNumPredictors; ++k)
                        {
                            const double d1 = points[i].mA1 - centers[k].mA1;
                            const double d2 = points[i].mA2 - centers[k].mA2;
                            const double distance = d1 * d1 + d2 * d2;
                            if (distance < bestDistance)
                            {
                                bestDistance = distance;
                                best = k;
                            }
                        }
                        if (best != assignment[i] || iteration == 0)
                            changed = true;
                        assignment[i] = best;
                    }
                    if (!changed)
                        break;
                    std::array<double, sNumPredictors> sum1{}, sum2{}, total{};
                    for (std::size_t i = 0; i < points.size(); ++i)
                    {
                        sum1[assignment[i]] += points[i].mA1 * weights[i];
                        sum2[assignment[i]] += points[i].mA2 * weights[i];
                        total[assignment[i]] += weights[i];
                    }
                    for (int k = 1; k < sNumPredictors; ++k)
                    {
                        if (total[k] > 0)
                        {
                            centers[k].mA1 = sum1[k] / total[k];
                            centers[k].mA2 = sum2[k] / total[k];
                        }
                    }
                }
            }

            std::array<std::int16_t, 16> coefficients{};
            for (int k = 0; k < sNumPredictors; ++k)
            {
                coefficients[k * 2] = static_cast<std::int16_t>(std::clamp(std::lround(centers[k].mA1 * 2048.0), -32768L, 32767L));
                coefficients[k * 2 + 1]
                    = static_cast<std::int16_t>(std::clamp(std::lround(centers[k].mA2 * 2048.0), -32768L, 32767L));
            }
            return coefficients;
        }

        struct FrameChoice
        {
            int mPredictor = 0;
            int mScale = 0;
            std::int64_t mError = std::numeric_limits<std::int64_t>::max();
            std::array<int, sSamplesPerFrame> mNibbles{};
            std::int16_t mHistory0 = 0;
            std::int16_t mHistory1 = 0;
        };

        void tryFrame(const std::int16_t* in, int c1, int c2, int predictor, int scale, std::int16_t h0, std::int16_t h1,
            FrameChoice& best)
        {
            FrameChoice choice;
            choice.mPredictor = predictor;
            choice.mScale = scale;
            choice.mError = 0;
            const double step = static_cast<double>(1 << scale);
            for (int n = 0; n < sSamplesPerFrame; ++n)
            {
                const double predicted = (static_cast<double>(c1) * h0 + static_cast<double>(c2) * h1 + 1024.0) / 2048.0;
                int nibble = static_cast<int>(std::lround((in[n] - predicted) / step));
                nibble = std::clamp(nibble, -8, 7);
                std::int16_t decoded = decodeSample(nibble, scale, c1, c2, h0, h1);
                const int alternative = nibble + (decoded < in[n] ? 1 : -1);
                if (alternative >= -8 && alternative <= 7)
                {
                    const std::int16_t other = decodeSample(alternative, scale, c1, c2, h0, h1);
                    if (std::abs(other - in[n]) < std::abs(decoded - in[n]))
                    {
                        nibble = alternative;
                        decoded = other;
                    }
                }
                const std::int64_t error = static_cast<std::int64_t>(decoded) - in[n];
                choice.mError += error * error;
                if (choice.mError >= best.mError)
                    return;
                choice.mNibbles[n] = nibble;
                h1 = h0;
                h0 = decoded;
            }
            choice.mHistory0 = h0;
            choice.mHistory1 = h1;
            best = choice;
        }
    }

    AdpcmChannel encodeAdpcm(const std::int16_t* samples, std::size_t count)
    {
        AdpcmChannel channel;
        channel.mCoefficients = findCoefficients(samples, count);

        const std::size_t frames = (count + sSamplesPerFrame - 1) / sSamplesPerFrame;
        channel.mFrames.resize(frames * 8);
        channel.mHistory0.resize(frames);
        channel.mHistory1.resize(frames);

        std::int16_t h0 = 0, h1 = 0;
        std::array<std::int16_t, sSamplesPerFrame> in{};
        for (std::size_t f = 0; f < frames; ++f)
        {
            const std::size_t start = f * sSamplesPerFrame;
            for (int n = 0; n < sSamplesPerFrame; ++n)
                in[n] = start + n < count ? samples[start + n] : 0;
            channel.mHistory0[f] = h0;
            channel.mHistory1[f] = h1;

            FrameChoice best;
            for (int k = 0; k < sNumPredictors; ++k)
            {
                const int c1 = channel.mCoefficients[k * 2];
                const int c2 = channel.mCoefficients[k * 2 + 1];
                int maxResidual = 0;
                {
                    std::int32_t p0 = h0, p1 = h1;
                    for (int n = 0; n < sSamplesPerFrame; ++n)
                    {
                        const std::int32_t predicted = (c1 * p0 + c2 * p1 + 1024) >> 11;
                        maxResidual = std::max(maxResidual, std::abs(in[n] - predicted));
                        p1 = p0;
                        p0 = in[n];
                    }
                }
                int scale = 0;
                while (scale < 11 && (7 << scale) < maxResidual)
                    ++scale;
                for (int s = std::max(0, scale - 1); s <= std::min(12, scale + 1); ++s)
                    tryFrame(in.data(), c1, c2, k, s, h0, h1, best);
            }

            std::uint8_t* out = &channel.mFrames[f * 8];
            out[0] = static_cast<std::uint8_t>((best.mPredictor << 4) | best.mScale);
            for (int n = 0; n < sSamplesPerFrame; n += 2)
                out[1 + n / 2] = static_cast<std::uint8_t>(((best.mNibbles[n] & 0xF) << 4) | (best.mNibbles[n + 1] & 0xF));
            h0 = best.mHistory0;
            h1 = best.mHistory1;
        }
        return channel;
    }

    std::vector<std::int16_t> decodeAdpcm(const AdpcmChannel& channel, std::size_t count)
    {
        std::vector<std::int16_t> out;
        out.reserve(count);
        std::int16_t h0 = 0, h1 = 0;
        const std::size_t frames = channel.mFrames.size() / 8;
        for (std::size_t f = 0; f < frames && out.size() < count; ++f)
        {
            const std::uint8_t* frame = &channel.mFrames[f * 8];
            const int predictor = frame[0] >> 4;
            const int scale = frame[0] & 0xF;
            const int c1 = channel.mCoefficients[predictor * 2];
            const int c2 = channel.mCoefficients[predictor * 2 + 1];
            for (int n = 0; n < sSamplesPerFrame && out.size() < count; ++n)
            {
                const std::uint8_t byte = frame[1 + n / 2];
                int nibble = (n & 1) ? (byte & 0xF) : (byte >> 4);
                if (nibble >= 8)
                    nibble -= 16;
                const std::int16_t sample = decodeSample(nibble, scale, c1, c2, h0, h1);
                out.push_back(sample);
                h1 = h0;
                h0 = sample;
            }
        }
        return out;
    }
}
