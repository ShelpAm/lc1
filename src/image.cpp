#include "lc1/image.hpp"

namespace lc1 {

void StbiFree::operator()(stbi_uc *data) const
{
    stbi_image_free(data);
}

std::span<stbi_uc const> Image::bytes() const
{
    return {data.get(), std::size_t{extent.x} * extent.y * 4};
}

Image Image::load_from_file(std::filesystem::path const &path)
{
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    stbi_uc *data =
        stbi_load(path.string().c_str(), &width, &height, &channels_in_file, STBI_rgb_alpha);
    if (data == nullptr) {
        fail("failed to load {}: {}", std::filesystem::absolute(path).string(),
             stbi_failure_reason());
    }
    return {
        .data = std::unique_ptr<stbi_uc, StbiFree>{data},
        .extent = {width, height},
    };
}

} // namespace lc1
