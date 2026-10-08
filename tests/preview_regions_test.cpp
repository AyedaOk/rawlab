#include "ProcessingSession.h"
#include "rawlab/dng/DngWriter.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
  QTemporaryDir directory;
  qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
  qputenv("RAWLAB_LENSFUN_DB", directory.path().toUtf8());
  QCoreApplication app{argc, argv};
  app.setOrganizationName("RawLab"); app.setApplicationName("RawLab");
  try
  {
    QFile profile{directory.filePath("test.xml")};
    if(!profile.open(QIODevice::WriteOnly)) return 1;
    profile.write(R"(<lensdatabase version="1">
      <mount><name>RawLab test</name></mount>
      <camera><maker>RawLab</maker><model>Region test</model><mount>RawLab test</mount><cropfactor>1.5</cropfactor></camera>
      <lens><maker>RawLab</maker><model>Region lens</model><mount>RawLab test</mount><cropfactor>1.5</cropfactor>
        <calibration>
          <distortion model="ptlens" focal="24" a="0.012" b="-0.04" c="0.01"/>
          <tca model="linear" focal="24" kr="1.002" kb="0.998"/>
          <vignetting model="pa" focal="24" aperture="4" distance="1000" k1="-0.2" k2="0.04" k3="0"/>
        </calibration>
      </lens></lensdatabase>)");
    profile.close();
    rawlab::raw::RawMetadata metadata;
    metadata.make = "RawLab"; metadata.model = "Region test";
    metadata.lensMake = "RawLab"; metadata.lensModel = "Region lens";
    metadata.focalLengthMm = 24; metadata.aperture = 4;
    metadata.cfa.isBayer = true;
    metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
    metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
    metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
    metadata.whiteLevel = 4095; metadata.blackLevel = {10, 20, 30, 40};
    metadata.activeArea = {3, 5, 769, 513};
    std::vector<float> samples(780 * 524);
    for(int y = 0; y < 524; ++y) for(int x = 0; x < 780; ++x)
      samples[y * 780 + x] = 700 + (x * 19 + y * 11) % 1800;
    QString context;
    const auto compare = [&](const auto &full, const auto &region, const QRect &rect) {
      float error = 0;
      for(int y = 0; y < rect.height(); ++y) for(int x = 0; x < rect.width(); ++x)
        for(int c = 0; c < 3; ++c)
          error = std::max(error, std::fabs(region.pixels[(y * rect.width() + x) * 3 + c]
            - full.pixels[((y + rect.y()) * full.width + x + rect.x()) * 3 + c]));
      if(error > 0.0003f) throw std::runtime_error{"Region differs from full-image reference: " + std::to_string(error)
        + " " + context.toStdString() + " at " + std::to_string(rect.x()) + "," + std::to_string(rect.y())};
    };
    ProcessingSession session;
    if(argc > 1)
    {
      ApplicationSettingsData settings;
      settings.onnxProvider = "cpu";
      if(argc > 2) settings.onnxRuntimeLibrary = QString::fromLocal8Bit(argv[2]);
      settings.denoiseModels.push_back({"test", "Test", QString::fromLocal8Bit(argv[1]), "rawnind-bayer-v1"});
      ApplicationSettingsStore{}.save(settings);
    }
    const QJsonObject recipe{{"layers", QJsonArray{QJsonObject{{"enabled", false}}, QJsonObject{{"enabled", false}}}}};
    for(int flip : {0, 3, 5, 6})
    {
      context = QStringLiteral("CFA flip %1").arg(flip);
      metadata.flip = flip;
      const QString path = directory.filePath(QString::number(flip) + ".dng");
      rawlab::dng::TiffDngWriter{}.writeCfaDng(path.toStdString(), {780, 524, samples, metadata});
      const auto source = session.source(path);
      source->metadata().lensMake = metadata.lensMake;
      source->metadata().lensModel = metadata.lensModel;
      const auto geometry = rawlab::image::rawPreviewGeometry(*source, 10000);
      const auto full = rawlab::image::renderLinearRawPreviewRegion(*source, 0, 0, geometry.previewWidth, geometry.previewHeight);
      for(const QRect &rect : {QRect{0, 0, 64, 48}, QRect{247, 251, 97, 83},
          QRect{full.width - 70, full.height - 50, 70, 50}})
      {
        const auto region = session.detail(path, recipe, rect);
        compare(full, region, rect);
        const auto warm = session.detail(path, recipe, rect);
        if(warm.pixels != region.pixels) return 1;
      }
#ifdef RAWLAB_HAS_LENSFUN
      // Use explicit metadata: LibRaw need not preserve the synthetic lens name.
      for(const auto optics : {rawlab::image::OpticsOptions{true, false}, {false, true}, {true, true}})
      {
        context = QStringLiteral("optics %1 %2 flip %3").arg(optics.chromaticAberration).arg(optics.lensCorrection).arg(flip);
        const auto corrected = rawlab::image::applyOpticsCorrections({full.width, full.height, full.pixels}, metadata, optics);
        for(const QRect &rect : {QRect{0, 0, 64, 48}, QRect{247, 251, 97, 83},
            QRect{full.width - 70, full.height - 50, 70, 50}})
        {
          const auto region = rawlab::image::renderOpticsRegion(full.width, full.height,
            rect.x(), rect.y(), rect.width(), rect.height(), metadata, optics,
            [&](int x, int y, int w, int h) {
              if(w * h >= full.width * full.height / 4) throw std::runtime_error{"Detail rendered the whole source"};
              auto pixels = rawlab::image::renderLinearRawPreviewRegion(*source, x, y, w, h);
              return rawlab::image::LinearRgbImage{w, h, std::move(pixels.pixels)};
            });
          compare(corrected, region, rect);
        }
      }
#endif
      if(argc > 1)
      {
        const QJsonObject global{{"id", 0}, {"enabled", true}, {"modelId", "test"},
          {"blend", 73}, {"sharpen", "Normal"}, {"strength", 37}};
        auto local = global;
        local["id"] = 1; local["strength"] = 91; local["blend"] = 83;
        local["feather"] = 8; local["blur"] = 4;
        local["marks"] = QJsonArray{QJsonObject{{"tool", "Ellipse"},
          {"points", QJsonArray{QJsonArray{.12, .14}, QJsonArray{.85, .86}}}}};
        const QJsonObject maskedRecipe{{"layers", QJsonArray{global, local}},
          {"outputKind", "linear-rgb"}, {"highlights", true}};
        const auto composed = session.process(path, maskedRecipe, {});
        auto expected = rawlab::image::renderLinearRawPreviewRegion(*composed, 0, 0, full.width, full.height);
        const QRect rect{247, 251, 97, 83};
        context = QStringLiteral("masked CFA flip %1").arg(flip);
        compare(expected, session.detail(path, maskedRecipe, rect), rect);
#ifdef RAWLAB_HAS_LENSFUN
        const auto corrected = rawlab::image::applyOpticsCorrections(
          {expected.width, expected.height, std::move(expected.pixels)}, source->metadata(), {true, true});
        context = QStringLiteral("corrected masks flip %1").arg(flip);
        const auto detail = session.detail(path, maskedRecipe, rect, {true, true});
        compare(corrected, detail, rect);
        if(session.detail(path, maskedRecipe, rect, {true, true}).pixels != detail.pixels) return 1;
#endif
      }
      bool cancelled = false;
      try { session.detail(path, recipe, {0, 0, 10, 10}, {}, [] { throw std::runtime_error{"cancel"}; }); }
      catch(const std::runtime_error &error) { cancelled = std::string{error.what()} == "cancel"; }
      if(!cancelled) return 1;
    }
    std::cout << "Viewport pixels, tile seams, orientation, optical corrections and cancellation passed\n";
  }
  catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
