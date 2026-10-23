#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace rawlab::dng {

// JPEG SOF3, predictor 1, no point transform: preserve every uint16 sample.
inline std::vector<std::uint8_t> losslessJpeg(std::span<const std::uint16_t> samples,
                                             int width, int height, int components)
{
  if(width <= 0 || width > 65535 || height <= 0 || height > 65535
     || components < 1 || components > 3
     || samples.size() != static_cast<std::size_t>(width) * height * components)
    throw std::invalid_argument{"Invalid lossless JPEG dimensions"};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(samples.size());
  const auto byte = [&](unsigned value) { bytes.push_back(static_cast<std::uint8_t>(value)); };
  const auto word = [&](unsigned value) { byte(value >> 8); byte(value); };
  word(0xffd8);
  word(0xffc3);
  word(8 + 3 * components);
  byte(16);
  word(height);
  word(width);
  byte(components);
  for(int c = 0; c < components; ++c) { byte(c); byte(0x11); byte(0); }

  // Categories 0..13 use four bits; 14..16 use five. No all-ones code.
  word(0xffc4);
  word(2 + 1 + 16 + 17);
  byte(0);
  for(int length = 1; length <= 16; ++length) byte(length == 4 ? 14 : (length == 5 ? 3 : 0));
  for(int category = 0; category <= 16; ++category) byte(category);
  word(0xffda);
  word(6 + 2 * components);
  byte(components);
  for(int c = 0; c < components; ++c) { byte(c); byte(0); }
  byte(1);
  byte(0);
  byte(0);

  unsigned buffer = 0;
  int pending = 0;
  const auto bits = [&](unsigned value, int count) {
    buffer = (buffer << count) | (value & ((1u << count) - 1));
    pending += count;
    while(pending >= 8)
    {
      pending -= 8;
      const unsigned next = (buffer >> pending) & 255;
      byte(next);
      if(next == 255) byte(0);
    }
  };
  const std::size_t rowSize = static_cast<std::size_t>(width) * components;
  for(int y = 0; y < height; ++y)
    for(int x = 0; x < width; ++x)
      for(int c = 0; c < components; ++c)
      {
        const std::size_t i = y * rowSize + x * components + c;
        const int predictor = x ? samples[i - components] : (y ? samples[i - rowSize] : 32768);
        int difference = static_cast<int>(samples[i]) - predictor;
        // Lossless JPEG uses modulo-16-bit differences at precision 16.
        if(difference > 32767) difference -= 65536;
        if(difference < -32768) difference += 65536;
        const unsigned magnitude = static_cast<unsigned>(difference < 0 ? -difference : difference);
        const int category = static_cast<int>(std::bit_width(magnitude));
        bits(category < 14 ? category : category + 14, category < 14 ? 4 : 5);
        // Category 16 denotes -32768 without amplitude bits (DNG 1.2+).
        if(category && category != 16) bits(difference < 0 ? difference - 1 : difference, category);
      }
  if(pending) bits(255, 8 - pending);
  word(0xffd9);
  return bytes;
}

} // namespace rawlab::dng
