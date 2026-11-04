#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace rerevved::tools
{

inline constexpr int kSmokeCaptureWidth  = 1600;
inline constexpr int kSmokeCaptureHeight = 900;

enum class SmokeCaptureError
{
    DirectoryUnavailable,
    InvalidFramebufferSize,
    ReadFailed,
    OpenFailed,
    WriteFailed
};

[[nodiscard]] std::string_view                       SmokeCaptureErrorMessage(SmokeCaptureError error);
[[nodiscard]] std::expected<void, SmokeCaptureError> CheckSmokeCaptureDirectory(const std::filesystem::path& directory);
[[nodiscard]] std::expected<void, SmokeCaptureError> CaptureSmokeFrame(const std::filesystem::path& directory, int session, unsigned int frame, int width, int height);
[[nodiscard]] std::expected<void, SmokeCaptureError> CaptureNamedSmokeFrame(const std::filesystem::path& directory, std::string_view filename, int width, int height);

// Writes a highlight image for each changed pair into diffDirectory, which must be empty.
// Returns whether every pair matched.
[[nodiscard]] std::expected<bool, std::string> CompareSmokeCaptures(const std::filesystem::path& baseline, const std::filesystem::path& candidate, const std::filesystem::path& diffDirectory);

} // namespace rerevved::tools
