#pragma once

#include <libraw/libraw.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

inline std::vector<std::uint16_t> linearDngSamples(const std::string &path, int width, int height)
{
  // libtiff's baseline JPEG codec cannot decode DNG's lossless SOF3 JPEG.
  LibRaw decoder;
  if(decoder.open_file(path.c_str()) != LIBRAW_SUCCESS || decoder.unpack() != LIBRAW_SUCCESS
     || decoder.error_count()
     || (!decoder.imgdata.rawdata.color3_image && !decoder.imgdata.rawdata.color4_image)
     || decoder.imgdata.sizes.raw_width != width || decoder.imgdata.sizes.raw_height != height)
    throw std::runtime_error{"Cannot decode lossless Linear RGB DNG"};
  const auto &raw = decoder.imgdata.rawdata;
  std::vector<std::uint16_t> samples(static_cast<std::size_t>(width) * height * 3);
  for(std::size_t i = 0; i < samples.size() / 3; ++i)
    for(int c = 0; c < 3; ++c)
      samples[i * 3 + c] = raw.color3_image ? raw.color3_image[i][c] : raw.color4_image[i][c];
  return samples;
}
