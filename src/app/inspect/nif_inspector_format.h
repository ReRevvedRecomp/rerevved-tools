#pragma once

#include "nif_document.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace rerevved::tools
{

struct NifMaterialPropertyText
{
    std::string                heading;
    std::array<std::string, 6> fields;
};

struct NifTexturingPropertyText
{
    std::string              heading;
    std::vector<std::string> fields;
};

struct NifSourceTextureText
{
    std::string              heading;
    std::vector<std::string> fields;
};

[[nodiscard]] std::string FormatNifInspectorFloat(float value);

[[nodiscard]] std::string FormatNifStringBytes(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<NifMaterialPropertyText>
FormatNifMaterialProperties(std::span<const NifMaterialPropertyInventory> materials);

[[nodiscard]] std::vector<NifTexturingPropertyText>
FormatNifTexturingProperties(std::span<const NifTexturingPropertyInventory> properties);

[[nodiscard]] std::vector<NifSourceTextureText>
FormatNifSourceTextures(const NifDocument& document);

} // namespace rerevved::tools
