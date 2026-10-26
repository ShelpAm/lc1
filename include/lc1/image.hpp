#pragma once

#include "lc1/error.hpp"

#include <glm/glm.hpp>
#include <span>
#include <stb_image.h>

#include <filesystem>
#include <memory>

namespace lc1 {

struct StbiFree {
    void operator()(stbi_uc *data) const;
};

struct Image {
    std::unique_ptr<stbi_uc, StbiFree> data;
    glm::uvec2 extent;

    // 4 bytes per pixel: load() asks stb for RGBA whatever the file holds.
    std::span<stbi_uc const> bytes() const;

    static Image load_from_file(std::filesystem::path const &path);
};

} // namespace lc1
