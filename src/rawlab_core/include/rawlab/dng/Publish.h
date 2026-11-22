#pragma once
#include <filesystem>

namespace rawlab::dng {
// Move a finished file within its destination filesystem without replacing any entry.
void publish(const std::filesystem::path &temporary, const std::filesystem::path &destination);
}
