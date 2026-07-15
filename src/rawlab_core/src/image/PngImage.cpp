#include "rawlab/image/PngImage.h"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace rawlab::image {
namespace {

std::runtime_error pngError(const char *operation, const png_image &image)
{
  return std::runtime_error{std::string{operation} + " failed: " + image.message};
}

}

RgbImage readRgbPng(const std::filesystem::path &path)
{
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if(!png_image_begin_read_from_file(&image, path.string().c_str()))
  {
    throw pngError("PNG read", image);
  }

  image.format = PNG_FORMAT_RGB;
  RgbImage result;
  result.width = static_cast<int>(image.width);
  result.height = static_cast<int>(image.height);
  result.pixels.resize(PNG_IMAGE_SIZE(image));
  if(!png_image_finish_read(&image, nullptr, result.pixels.data(), 0, nullptr))
  {
    const std::runtime_error error = pngError("PNG decode", image);
    png_image_free(&image);
    throw error;
  }
  png_image_free(&image);
  return result;
}

pipeline::Mask readMaskPng(const std::filesystem::path &path)
{
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if(!png_image_begin_read_from_file(&image, path.string().c_str()))
  {
    throw pngError("Mask PNG read", image);
  }

  image.format = PNG_FORMAT_GRAY;
  std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
  if(!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
  {
    const std::runtime_error error = pngError("Mask PNG decode", image);
    png_image_free(&image);
    throw error;
  }

  std::vector<float> values(pixels.size());
  for(std::size_t i = 0; i < pixels.size(); ++i)
  {
    values[i] = static_cast<float>(pixels[i]) / 255.0f;
  }
  const int width = static_cast<int>(image.width);
  const int height = static_cast<int>(image.height);
  png_image_free(&image);
  return pipeline::Mask{width, height, std::move(values)};
}

void writeMaskPng(const std::filesystem::path &path, const pipeline::Mask &mask)
{
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = static_cast<png_uint_32>(mask.width());
  image.height = static_cast<png_uint_32>(mask.height());
  image.format = PNG_FORMAT_GRAY;

  std::vector<std::uint8_t> pixels(mask.values().size());
  for(std::size_t i = 0; i < pixels.size(); ++i)
  {
    pixels[i] = static_cast<std::uint8_t>(std::lround(
      std::clamp(mask.values()[i], 0.0f, 1.0f) * 255.0f));
  }

  if(!png_image_write_to_file(&image, path.string().c_str(), 0, pixels.data(), 0, nullptr))
  {
    throw pngError("Mask PNG write", image);
  }
}

}
