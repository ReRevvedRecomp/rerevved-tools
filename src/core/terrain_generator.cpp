#include "terrain_generator.h"

#include "heights_dds.h"
#include "terrain_dds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace rerevved::tools
{
namespace
{

constexpr int kTiles   = static_cast<int>(kMapWidth);
constexpr int kSide    = static_cast<int>(kHeightsDdsSide);
constexpr int kPerTile = kSide / kTiles;

// Mask and relief sizes in heights samples, fitted to the shipped numbered maps.
constexpr float kHillDisc      = 10.0F;
constexpr float kMountainEdge  = 0.46F;
constexpr float kBlueDepth     = 6.2F;
constexpr float kMountainRise  = 33000.0F;
constexpr float kMountainScale = 12.0F;
constexpr float kMountainCrag  = 5000.0F;
constexpr float kHeightKnee    = 54000.0F;
constexpr float kHeightCeiling = 65000.0F;

enum class Kind
{
    Sea,
    Flat,
    Hill,
    Mountain,
    Ice,
};

// Low nibble 4 is hill, 6 mountain and 7 the polar code. Unnamed codes are
// treated as flat land.
Kind kindOf(std::byte value)
{
    switch (std::to_integer<unsigned>(value) & 0x0FU)
    {
        case 0:
            return Kind::Sea;
        case 4:
            return Kind::Hill;
        case 6:
            return Kind::Mountain;
        case 7:
            return Kind::Ice;
        default:
            return Kind::Flat;
    }
}

struct Field
{
    std::vector<float> values = std::vector<float>(std::size_t(kSide) * kSide);

    float& at(int x, int y)
    {
        return values[std::size_t(y) * kSide + x];
    }

    float get(int x, int y) const
    {
        return values[std::size_t(std::clamp(y, 0, kSide - 1)) * kSide + std::clamp(x, 0, kSide - 1)];
    }
};

std::uint32_t mix(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

float hashUnit(std::uint32_t seed, int x, int y, std::uint32_t salt)
{
    const auto h = mix(seed ^ mix(static_cast<std::uint32_t>(x) * 0x9E3779B1U ^ mix(static_cast<std::uint32_t>(y) * 0x85EBCA77U ^ salt)));
    return static_cast<float>(h) / 2147483647.5F - 1.0F;
}

float valueNoise(std::uint32_t seed, float x, float y, float spacing, std::uint32_t salt)
{
    const float fx = x / spacing;
    const float fy = y / spacing;
    const int   x0 = static_cast<int>(std::floor(fx));
    const int   y0 = static_cast<int>(std::floor(fy));
    const auto  s  = [](float t)
    {
        return t * t * (3.0F - 2.0F * t);
    };
    const float tx = s(fx - static_cast<float>(x0));
    const float ty = s(fy - static_cast<float>(y0));
    const float a  = std::lerp(hashUnit(seed, x0, y0, salt), hashUnit(seed, x0 + 1, y0, salt), tx);
    const float b  = std::lerp(hashUnit(seed, x0, y0 + 1, salt), hashUnit(seed, x0 + 1, y0 + 1, salt), tx);
    return std::lerp(a, b, ty);
}

// Two separable box passes approximate a Gaussian of about this radius.
Field blur(const Field& input, int radius)
{
    Field       current = input;
    Field       next;
    const float width = static_cast<float>(2 * radius + 1);
    for (int pass = 0; pass < 4; ++pass)
    {
        const bool rows = pass % 2 == 0;
        for (int line = 0; line < kSide; ++line)
        {
            const auto read = [&](int k)
            {
                return rows ? current.get(k, line) : current.get(line, k);
            };
            float sum = 0.0F;
            for (int k = -radius; k <= radius; ++k)
            {
                sum += read(k);
            }
            for (int k = 0; k < kSide; ++k)
            {
                (rows ? next.at(k, line) : next.at(line, k)) = sum / width;
                sum += read(k + radius + 1) - read(k - radius);
            }
        }
        std::swap(current, next);
    }
    return current;
}

template <typename Value>
Field tileField(const std::array<Kind, kMapCoreSize>& kinds, Value value)
{
    Field field;
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            const int i    = y / kPerTile;
            const int j    = x / kPerTile;
            field.at(x, y) = value(i, j, kinds[std::size_t(i) * kTiles + j]);
        }
    }
    return field;
}

Rgba8Image upscale(const std::array<const Field*, 3>& channels, std::uint32_t side)
{
    Rgba8Image image;
    image.width           = side;
    image.height          = side;
    image.hasAlphaChannel = false;
    image.rgba8.resize(std::size_t(side) * side * 4);
    const float        scale = static_cast<float>(kSide) / static_cast<float>(side);
    std::vector<int>   lower(side);
    std::vector<int>   upper(side);
    std::vector<float> weight(side);
    for (std::uint32_t k = 0; k < side; ++k)
    {
        const float s = (static_cast<float>(k) + 0.5F) * scale - 0.5F;
        const int   f = static_cast<int>(std::floor(s));
        lower[k]      = std::clamp(f, 0, kSide - 1);
        upper[k]      = std::clamp(f + 1, 0, kSide - 1);
        weight[k]     = s - static_cast<float>(f);
    }
    for (std::uint32_t y = 0; y < side; ++y)
    {
        auto* out = image.rgba8.data() + std::size_t(y) * side * 4;
        for (std::size_t c = 0; c < 3; ++c)
        {
            const float* top    = channels[c]->values.data() + std::size_t(lower[y]) * kSide;
            const float* bottom = channels[c]->values.data() + std::size_t(upper[y]) * kSide;
            const float  ty     = weight[y];
            for (std::uint32_t x = 0; x < side; ++x)
            {
                const float a     = top[lower[x]] + (top[upper[x]] - top[lower[x]]) * weight[x];
                const float b     = bottom[lower[x]] + (bottom[upper[x]] - bottom[lower[x]]) * weight[x];
                const float value = a + (b - a) * ty;
                out[x * 4 + c]    = static_cast<std::byte>(static_cast<int>(std::clamp(value, 0.0F, 255.0F) + 0.5F));
            }
        }
        for (std::uint32_t x = 0; x < side; ++x)
        {
            out[x * 4 + 3] = std::byte{ 255 };
        }
    }
    return image;
}

float sample(const Field& field, float x, float y)
{
    const int   x0 = static_cast<int>(std::floor(x));
    const int   y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a  = std::lerp(field.get(x0, y0), field.get(x0 + 1, y0), tx);
    const float b  = std::lerp(field.get(x0, y0 + 1), field.get(x0 + 1, y0 + 1), tx);
    return std::lerp(a, b, ty);
}

Field insideDistance(const Field& mask, float threshold)
{
    constexpr float kFar      = 1.0e6F;
    constexpr float kDiagonal = 1.4142F;
    Field           distance;
    for (std::size_t k = 0; k < distance.values.size(); ++k)
    {
        distance.values[k] = mask.values[k] > threshold ? kFar : 0.0F;
    }
    const auto relax = [&](int x, int y, int dx, int dy, float step)
    {
        const int   nx        = x + dx;
        const int   ny        = y + dy;
        const float neighbour = nx < 0 || ny < 0 || nx >= kSide || ny >= kSide ? 0.0F : distance.at(nx, ny);
        distance.at(x, y)     = std::min(distance.at(x, y), neighbour + step);
    };
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            relax(x, y, -1, 0, 1.0F);
            relax(x, y, 0, -1, 1.0F);
            relax(x, y, -1, -1, kDiagonal);
            relax(x, y, 1, -1, kDiagonal);
        }
    }
    for (int y = kSide - 1; y >= 0; --y)
    {
        for (int x = kSide - 1; x >= 0; --x)
        {
            relax(x, y, 1, 0, 1.0F);
            relax(x, y, 0, 1, 1.0F);
            relax(x, y, 1, 1, kDiagonal);
            relax(x, y, -1, 1, kDiagonal);
        }
    }
    return distance;
}

// Hard, ragged mask edges like the shipped files: red over hills, green over mountains,
// blue speckle replacing green deep inside a range. Channels never overlap in a pixel.
Rgba8Image blendsImage(const Field& hill, const Field& mountain, const Field& inside, std::uint32_t seed)
{
    constexpr std::uint32_t side = kMountainHillBlendsSide;
    Rgba8Image              image;
    image.width           = side;
    image.height          = side;
    image.hasAlphaChannel = false;
    image.rgba8.resize(std::size_t(side) * side * 4);
    const float scale = static_cast<float>(kSide) / static_cast<float>(side);
    for (std::uint32_t y = 0; y < side; ++y)
    {
        for (std::uint32_t x = 0; x < side; ++x)
        {
            const float u          = (static_cast<float>(x) + 0.5F) * scale;
            const float v          = (static_cast<float>(y) + 0.5F) * scale;
            auto*       out        = image.rgba8.data() + (std::size_t(y) * side + x) * 4;
            const float hillAt     = sample(hill, u - 0.5F, v - 0.5F);
            const float mountainAt = sample(mountain, u - 0.5F, v - 0.5F);
            out[3]                 = std::byte{ 255 };
            if (hillAt + 0.375F <= 0.5F && mountainAt + 0.15F <= kMountainEdge)
            {
                continue;
            }
            const float edge  = valueNoise(seed, u, v, 2.0F, 7) + 0.5F * valueNoise(seed, u, v, 1.0F, 8);
            const bool  red   = hillAt + 0.25F * edge > 0.5F;
            bool        green = !red && mountainAt + 0.1F * edge > kMountainEdge;
            bool        blue  = false;
            if (green && sample(inside, u - 0.5F, v - 0.5F) + 3.0F * valueNoise(seed, u, v, 1.0F, 9) > kBlueDepth)
            {
                green = false;
                blue  = true;
            }
            out[0] = red ? std::byte{ 255 } : std::byte{ 0 };
            out[1] = green ? std::byte{ 255 } : std::byte{ 0 };
            out[2] = blue ? std::byte{ 255 } : std::byte{ 0 };
            out[3] = std::byte{ 255 };
        }
    }
    return image;
}

} // namespace

GeneratedTerrain GenerateTerrainFromTiles(std::span<const std::byte, kMapCoreSize> core)
{
    std::array<Kind, kMapCoreSize> kinds{};
    std::uint32_t                  seed = 2166136261U;
    for (int i = 0; i < kTiles; ++i)
    {
        for (int j = 0; j < kTiles; ++j)
        {
            kinds[std::size_t(i) * kTiles + j] = kindOf(core[std::size_t(j) * kTiles + i]);
        }
    }
    for (const auto value : core)
    {
        seed = (seed ^ std::to_integer<std::uint32_t>(value)) * 16777619U;
    }

    // Sea depth follows the Chebyshev distance to the nearest land tile.
    std::array<int, kMapCoreSize> distance{};
    distance.fill(kTiles);
    for (int i = 0; i < kTiles; ++i)
    {
        for (int j = 0; j < kTiles; ++j)
        {
            for (int a = 0; a < kTiles; ++a)
            {
                for (int b = 0; b < kTiles; ++b)
                {
                    if (kinds[std::size_t(a) * kTiles + b] != Kind::Sea)
                    {
                        auto& d = distance[std::size_t(i) * kTiles + j];
                        d       = std::min(d, std::max(std::abs(a - i), std::abs(b - j)));
                    }
                }
            }
        }
    }

    // Base levels sit below the measured tile medians where a peak is added on top.
    const auto base      = tileField(kinds, [&](int i, int j, Kind kind)
                                     {
                                    switch (kind)
                                    {
                                        case Kind::Sea:
                                        {
                                            const int d = distance[std::size_t(i) * kTiles + j];
                                            return d <= 1 ? 20500.0F : d == 2 ? 11000.0F
                                                                              : 1200.0F;
                                        }
                                        case Kind::Hill:
                                            return 33000.0F;
                                        case Kind::Mountain:
                                            return 39500.0F;
                                        case Kind::Ice:
                                            return 30000.0F;
                                        case Kind::Flat:
                                            break;
                                    }
                                    return 29200.0F;
                                     });
    const auto roughness = blur(tileField(kinds, [](int, int, Kind kind)
                                          {
                                              switch (kind)
                                              {
                                                  case Kind::Sea:
                                                      return 250.0F;
                                                  case Kind::Hill:
                                                      return 2000.0F;
                                                  case Kind::Mountain:
                                                      return 3500.0F;
                                                  case Kind::Ice:
                                                      return 2500.0F;
                                                  case Kind::Flat:
                                                      break;
                                              }
                                              return 1400.0F;
                                          }),
                                kPerTile / 2);
    auto       height    = blur(base, 6);

    Field hillMask;
    for (int i = 0; i < kTiles; ++i)
    {
        for (int j = 0; j < kTiles; ++j)
        {
            if (kinds[std::size_t(i) * kTiles + j] != Kind::Hill)
            {
                continue;
            }
            const float amplitude = 5000.0F * (1.0F + 0.2F * hashUnit(seed, i, j, 1));
            const float radius    = 11.0F;
            const float cx        = static_cast<float>(j * kPerTile) + 7.5F + 3.0F * hashUnit(seed, i, j, 2);
            const float cy        = static_cast<float>(i * kPerTile) + 7.5F + 3.0F * hashUnit(seed, i, j, 3);
            for (int y = std::max(0, static_cast<int>(cy - radius)); y <= std::min(kSide - 1, static_cast<int>(cy + radius) + 1); ++y)
            {
                for (int x = std::max(0, static_cast<int>(cx - radius)); x <= std::min(kSide - 1, static_cast<int>(cx + radius) + 1); ++x)
                {
                    const float offset = std::hypot(static_cast<float>(x) - cx, static_cast<float>(y) - cy);
                    if (offset < radius)
                    {
                        height.at(x, y) += amplitude * 0.5F * (1.0F + std::cos(std::numbers::pi_v<float> * offset / radius));
                    }
                    hillMask.at(x, y) = std::max(hillMask.at(x, y), std::clamp((kHillDisc - offset) / 2.0F, 0.0F, 1.0F));
                }
            }
        }
    }

    // Relief rises with the distance inside a range, putting a ridge along its middle.
    auto mountain = blur(tileField(kinds, [](int, int, Kind kind)
                                   {
                                       return kind == Kind::Mountain ? 1.0F : 0.0F;
                                   }),
                         5);
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            mountain.at(x, y) += 0.25F * valueNoise(seed, static_cast<float>(x), static_cast<float>(y), 4.0F, 12);
        }
    }
    const auto inside = insideDistance(mountain, kMountainEdge);

    GeneratedTerrain terrain;
    terrain.heights.resize(kHeightsDdsSampleCount);
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            const float fx    = static_cast<float>(x);
            const float fy    = static_cast<float>(y);
            const float noise = valueNoise(seed, fx, fy, 8.0F, 4) + 0.5F * valueNoise(seed, fx, fy, 4.0F, 5);
            const float fine  = 1.0F - std::abs(valueNoise(seed, fx, fy, 4.0F, 11));
            const float depth = inside.get(x, y);
            const float rock  = kMountainRise * (1.0F - std::exp(-depth / kMountainScale)) + kMountainCrag * (fine * fine - 0.5F) * std::min(1.0F, depth / 3.0F);
            auto&       h     = height.at(x, y);
            h += roughness.get(x, y) * noise + rock;
            // Wide ranges would pass the sample limit. Compress the top smoothly
            // so their crags survive instead of clipping to a plateau.
            if (h > kHeightKnee)
            {
                h = kHeightKnee + (kHeightCeiling - kHeightKnee) * (1.0F - std::exp(-(h - kHeightKnee) / (kHeightCeiling - kHeightKnee)));
            }
            terrain.heights[std::size_t(y) * kSide + x] = static_cast<std::uint16_t>(std::lround(std::clamp(h, 0.0F, 65535.0F)));
        }
    }

    terrain.blends = blendsImage(hillMask, mountain, inside, seed);

    // Lightmap: per terrain brightness and tint from the DLC means, shaded by
    // the generated relief with light from the image's top left.
    const auto level = [&](std::size_t channel)
    {
        return blur(tileField(kinds, [=](int, int, Kind kind)
                              {
                                  constexpr std::array<std::array<float, 3>, 5> kLevels{ { { 175, 174, 188 }, { 207, 206, 217 }, { 148, 148, 165 }, { 92, 92, 120 }, { 133, 132, 151 } } };
                                  return kLevels[static_cast<std::size_t>(kind)][channel];
                              }),
                    6);
    };
    std::array<Field, 3> light{ level(0), level(1), level(2) };
    const float          lx = -0.55F;
    const float          ly = -0.55F;
    const float          lz = 0.63F;
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            const float gx    = (height.get(x + 1, y) - height.get(x - 1, y)) / 2.0F / 1200.0F;
            const float gy    = (height.get(x, y + 1) - height.get(x, y - 1)) / 2.0F / 1200.0F;
            const float shade = (-gx * lx - gy * ly + lz) / std::sqrt(gx * gx + gy * gy + 1.0F);
            const float scale = std::clamp(1.0F + 0.9F * (shade - lz), 0.35F, 1.3F);
            for (auto& channel : light)
            {
                channel.at(x, y) *= scale;
            }
        }
    }
    terrain.lightmap = upscale({ &light[0], &light[1], &light[2] }, kLightmapSide);
    return terrain;
}

} // namespace rerevved::tools
