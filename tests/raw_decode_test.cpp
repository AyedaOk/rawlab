#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/RawPreview.h"
#include "rawlab/raw/LibRawDecoder.h"

#include <QTemporaryDir>
#include <libraw/libraw.h>
#include <tiffio.h>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, const char *message)
{
  if(!condition) throw std::runtime_error{message};
}

} // namespace

int main(int argc, char **argv)
{
  try
  {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create decode test directory");
    const auto input = directory.filePath("input.dng").toStdString();
    const auto output = directory.filePath("roundtrip.dng").toStdString();
    const rawlab::raw::LibRawDecoder decoder;
    const rawlab::dng::TiffDngWriter writer;
    if(argc == 2)
    {
      const auto source = decoder.decodeRawImage(argv[1]);
      const auto &metadata = source.metadata();
      require(metadata.model == "Z5_2", "Expected a Z5II fixture");
      require(metadata.colorMatrix1Valid && metadata.asShotNeutralValid, "Missing Z5II color calibration");
      require(std::abs(metadata.colorMatrix1[0] - 1.3133f) < 0.0001f, "Unexpected Z5II matrix");
      for(int row = 0; row < 3; ++row)
      {
        const auto &m = metadata.cameraToSrgb;
        require(std::abs(m[row * 3] + m[row * 3 + 1] + m[row * 3 + 2] - 1.0f) < 0.0001f,
                "Z5II preview calibration does not preserve white-balanced neutrals");
      }
      const auto preview = rawlab::image::renderLinearRawPreview(source, 256);
      for(float value : preview.pixels) require(std::isfinite(value), "Non-finite Z5II preview");
      writer.writeCfaDng(output, source);
      const auto roundtrip = decoder.decodeRawImage(output);
      require(roundtrip.samples() == source.samples(), "Z5II export changed CFA samples");
      require(roundtrip.metadata().blackLevel == metadata.blackLevel, "Z5II export changed black levels");
      require(roundtrip.metadata().whiteLevel == metadata.whiteLevel, "Z5II export changed white level");
      for(std::size_t i = 0; i < 9; ++i)
        require(std::abs(roundtrip.metadata().colorMatrix1[i] - metadata.colorMatrix1[i]) < 0.0001f,
                "Z5II DNG lost calibration");
      std::cout << "Z5II real-file preview and CFA round trip passed\n";
    }
    for(const bool bggr : {false, true})
      for(const std::array<std::uint16_t, 2> repeat : {
            std::array<std::uint16_t, 2>{1, 1}, {1, 2}, {2, 1}, {2, 2}})
        for(const std::array<float, 4> black : {
              std::array<float, 4>{0, 0, 0, 0}, {512, 512, 512, 512},
              {257, 257, 258, 258}, {1, 9, 9, 0}})
        {
          rawlab::raw::RawMetadata metadata;
          metadata.make = "RawLab";
          metadata.model = "Black Level Test";
          metadata.cfa.isBayer = true;
          metadata.cfa.cells[0][0] = bggr ? rawlab::raw::CfaColor::Blue : rawlab::raw::CfaColor::Red;
          metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
          metadata.cfa.cells[1][1] = bggr ? rawlab::raw::CfaColor::Red : rawlab::raw::CfaColor::Blue;
          metadata.activeArea = {2, 4, 60, 56};
          metadata.whiteLevel = 16124;
          metadata.asShotNeutral = {0.6597938f, 1.0f, 0.3932412f};
          for(int y = 0; y < 2; ++y)
            for(int x = 0; x < 2; ++x)
              metadata.blackLevel[y * 2 + x] = black[(y % repeat[0]) * repeat[1] + x % repeat[1]];
          std::vector<float> samples(64 * 64);
          for(int y = 0; y < 64; ++y)
            for(int x = 0; x < 64; ++x)
            {
              const auto color = metadata.cfa.cells[y & 1][x & 1];
              const int channel = color == rawlab::raw::CfaColor::Red ? 0
                : color == rawlab::raw::CfaColor::Green ? 1 : 2;
              const float level = metadata.blackLevel[rawlab::raw::siteIndex(x, y)];
              samples[y * 64 + x] = std::round(level + 0.04f * metadata.asShotNeutral[channel]
                                                       * (metadata.whiteLevel - level));
            }
          const rawlab::raw::RawImage source{64, 64, std::move(samples), metadata};
          writer.writeCfaDng(input, source);
          // Exercise LibRaw's spatial black table, including scalar and one-axis repeats.
          TIFF *tif = TIFFOpen(input.c_str(), "r+");
          require(tif != nullptr, "Cannot open synthetic DNG");
          const bool rewritten = TIFFSetField(tif, TIFFTAG_BLACKLEVELREPEATDIM, repeat.data()) == 1
            && TIFFSetField(tif, TIFFTAG_BLACKLEVEL, static_cast<std::uint32_t>(repeat[0] * repeat[1]),
                            black.data()) == 1
            && TIFFRewriteDirectory(tif) == 1;
          TIFFClose(tif);
          require(rewritten, "Cannot set synthetic DNG black repeat");

          const auto header = decoder.readMetadata(input).metadata;
          const auto decoded = decoder.decodeRawImage(input);
          require(header.blackLevel == metadata.blackLevel, "Metadata-only decode lost repeated black levels");
          require(decoded.metadata().blackLevel == metadata.blackLevel, "RAW decode lost repeated black levels");
          require(decoded.samples() == source.samples(), "RAW decode changed sensor samples");
          require(decoded.metadata().whiteLevel == metadata.whiteLevel, "RAW decode changed white level");
          for(const auto quality : {rawlab::image::PreviewQuality::Fast, rawlab::image::PreviewQuality::High})
          {
            const auto preview = rawlab::image::renderLinearRawPreview(decoded, 64, quality);
            for(const float value : preview.pixels)
              require(std::isfinite(value) && std::abs(value - 0.04f) < 0.001f,
                      "Black subtraction or white balance changed a neutral shadow");
          }
          const auto linear = rawlab::image::renderLinearRgb(decoded);
          for(const float value : linear.pixels)
            require(std::isfinite(value) && std::abs(value - 0.04f) < 0.001f,
                    "Linear RGB export changed a neutral shadow");

          writer.writeCfaDng(output, decoded);
          const auto roundtrip = decoder.decodeRawImage(output);
          require(roundtrip.metadata().blackLevel == metadata.blackLevel, "CFA export lost repeated black levels");
          require(roundtrip.samples() == source.samples(), "CFA export changed sensor samples");
        }
    TIFF *tif = TIFFOpen(input.c_str(), "r+");
    require(tif != nullptr, "Cannot open corrupt RAW fixture");
    // A Nikon-compressed strip with invalid data reproduces unpack success with data errors.
    std::array<std::uint8_t, 4096> corruptStrip{};
    const bool corrupted = TIFFUnsetField(tif, TIFFTAG_DNGVERSION) == 1
      && TIFFUnsetField(tif, TIFFTAG_DNGBACKWARDVERSION) == 1
      && TIFFSetField(tif, TIFFTAG_MAKE, "NIKON") == 1
      && TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 12) == 1
      && TIFFSetField(tif, TIFFTAG_COMPRESSION, 34713) == 1
      && TIFFWriteRawStrip(tif, 0, corruptStrip.data(), corruptStrip.size())
           == static_cast<tmsize_t>(corruptStrip.size())
      && TIFFRewriteDirectory(tif) == 1;
    TIFFClose(tif);
    require(corrupted, "Cannot write corrupt RAW fixture");
    LibRaw processor;
    require(processor.open_file(input.c_str()) == LIBRAW_SUCCESS, "Cannot open corrupt RAW fixture in LibRaw");
    require(processor.unpack() == LIBRAW_SUCCESS, "Fixture did not reproduce unpack success");
    require(processor.error_count() > 0, "Fixture did not reproduce RAW data errors");
    require(decoder.readMetadata(input).rawWidth == 64, "Corrupt pixels blocked metadata-only reads");
    for(const bool inspect : {false, true})
    {
      bool rejected = false;
      try
      {
        if(inspect) static_cast<void>(decoder.inspect(input));
        else static_cast<void>(decoder.decodeRawImage(input));
      }
      catch(const std::runtime_error &error)
      {
        rejected = std::string_view{error.what()}.find("unsupported compression") != std::string_view::npos;
      }
      require(rejected, "RAW data errors were accepted despite unpack success");
    }
    std::cout << "RAW decode: black levels, neutral shadows, CFA round trips and corrupt-data rejection passed\n";
  }
  catch(const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
