/**
 * SHA256 hashing utilities
 *
 * Provides SHA256 hashing for strings and files, used for cache invalidation.
 */

#pragma once

#include <string>
#include <string_view>
#include <filesystem>

namespace rex::util {

std::string sha256(std::string_view data);
std::string sha256_file(const std::filesystem::path& path);

}  // namespace rex::util
