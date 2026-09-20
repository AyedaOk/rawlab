#include "rawlab/dng/DngWriter.h"
#include "rawlab/raw/LibRawDecoder.h"

#include <QTemporaryDir>
#include <cmath>
#include <iostream>

int main(int argc, char **argv)
{
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;
  rawlab::raw::RawMetadata metadata;
  metadata.make = "RawLab";
  metadata.model = "Metadata Test";
  metadata.cfa.isBayer = true;
  metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
  metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
  metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
  metadata.activeArea = {0, 0, 64, 64};
  metadata.whiteLevel = 4095;
  metadata.calibrationIlluminant1 = 17;
  rawlab::raw::LibRawDecoder decoder;
  const auto path = directory.filePath("roundtrip.dng").toStdString();
  for(const auto bias : {std::optional<double>{}, std::optional<double>{-1.0},
                         std::optional<double>{0.0}, std::optional<double>{1.0 / 3.0}})
  {
    metadata.exposureBiasEv = bias;
    // Deliberately different from EXIF exposure compensation.
    metadata.baselineExposureEv = 0.5;
    rawlab::raw::RawImage image{64, 64, std::vector<float>(64 * 64, 1024), metadata};
    rawlab::dng::TiffDngWriter{}.writeCfaDng(path, image);
    const auto actual = decoder.readMetadata(path).metadata;
    if(actual.exposureBiasEv.has_value() != bias.has_value()
       || (bias && std::abs(*bias - *actual.exposureBiasEv) > 1e-6)
       || !actual.baselineExposureEv || std::abs(*actual.baselineExposureEv - 0.5) > 1e-6
       || !actual.colorMatrix1Valid || actual.calibrationIlluminant1 != 17)
    {
      std::cerr << "Independent EXIF exposure bias / DNG baseline round trip failed\n";
      return 1;
    }
  }
  // Optional user-owned RAW fixture: never bundle photographs without permission.
  if(argc > 1)
  {
    const auto source = decoder.decodeRawImage(argv[1]);
    if(!source.metadata().exposureBiasEv) return 1;
    rawlab::dng::TiffDngWriter{}.writeCfaDng(path, source);
    const auto result = decoder.readMetadata(path).metadata;
    if(!result.exposureBiasEv
       || std::abs(*result.exposureBiasEv - *source.metadata().exposureBiasEv) > 1e-6) return 1;
    std::cout << "RAW -> DNG exposure compensation: " << *result.exposureBiasEv << " EV\n";
  }
}
