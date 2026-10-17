#include "ProcessingSession.h"
#include "ProcessingSessionFixture.h"
#include "rawlab/dng/DngWriter.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QProcess>
#include <QElapsedTimer>
#include <tiffio.h>
#include <iostream>

int main(int argc,char **argv)
{
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;
  qputenv("XDG_CONFIG_HOME",directory.path().toUtf8());
  QCoreApplication app{argc,argv};app.setOrganizationName("RawLab");app.setApplicationName("RawLab");
  rawlab::raw::RawMetadata metadata;
  metadata.make="RawLab";metadata.model="Processing test";metadata.cfa.isBayer=true;
  metadata.cfa.cells[0][0]=rawlab::raw::CfaColor::Red;
  metadata.cfa.cells[0][1]=metadata.cfa.cells[1][0]=rawlab::raw::CfaColor::Green;
  metadata.cfa.cells[1][1]=rawlab::raw::CfaColor::Blue;
  metadata.whiteLevel=4095;metadata.activeArea={0,0,64,64};metadata.iso=640;metadata.exposureBiasEv=-1;
  const auto path=directory.filePath("source.dng");
  std::vector<float> samples(4096);
  for(int y = 0; y < 64; ++y) for(int x = 0; x < 64; ++x)
    samples[static_cast<std::size_t>(y * 64 + x)] = static_cast<float>(500 + x * 40 + y * 4 + ((x / 4 + y / 4) % 2) * 120);
  samples[32 * 64 + 32] = samples[32 * 64 + 33] = 4095;
  rawlab::dng::TiffDngWriter{}.writeCfaDng(path.toStdString(),{64,64,std::move(samples),metadata});
  try
  {
    ProcessingSession session;
    QJsonObject global{{"id",0},{"enabled",false},{"blend",75},{"sharpen","Off"}};
    QJsonObject recipe{{"layers",QJsonArray{global}},{"highlights",true}};
    const auto source=session.source(path);
    const auto region = rawlab::image::renderRawPreviewRegion(*source, 7, 9, 20, 16);
    if(region.image.width != 20 || region.image.height != 16
       || region.geometry.previewWidth != 64 || region.geometry.previewHeight != 64) return 1;
    bool hasSourceDetail = false;
    for(int x = 1; x < region.image.width; ++x)
      if(region.image.pixels[static_cast<std::size_t>(x * 3)]
         != region.image.pixels[static_cast<std::size_t>((x - 1) * 3)]) hasSourceDetail = true;
    if(!hasSourceDetail) return 1;
    bool invalidRegion = false;
    try { (void)rawlab::image::renderRawPreviewRegion(*source, 60, 60, 20, 20); }
    catch(const std::invalid_argument &) { invalidRegion = true; }
    if(!invalidRegion) return 1;
    const auto sharp = rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(*source, 1.0f);
    const auto unsharpDetail = rawlab::image::renderLinearRawPreviewRegion(*source, 7, 9, 20, 16);
    const auto sharpDetail = rawlab::image::renderLinearRawPreviewRegion(sharp, 7, 9, 20, 16);
    if(sharpDetail.pixels == unsharpDetail.pixels) {
      std::cerr << "Normal sharpening is invisible in the detail preview\n"; return 1;
    }
    const QRect tile{7, 9, 20, 16};
    QElapsedTimer timer; timer.start();
    const auto cold = session.detail(path, recipe, tile);
    const auto coldNs = timer.nsecsElapsed(); timer.restart();
    const auto warm = session.detail(path, recipe, tile);
    std::cout << "Detail endpoint cold/warm ms: " << coldNs / 1e6 << '/' << timer.nsecsElapsed() / 1e6 << '\n';
    if(cold.pixels != unsharpDetail.pixels || cold.pixels != warm.pixels) return 1;
    if(session.process(path,recipe,{})!=source || source->metadata().exposureBiasEv!=std::optional<double>{-1}) return 1;
    const QString firstResult = session.rememberProcessed(path, source, QStringLiteral("first"));
    const QString secondResult = session.rememberProcessed(path, source, QStringLiteral("second"));
    const QString thirdResult = session.rememberProcessed(path, source, QStringLiteral("third"));
    if(session.processed(path, secondResult) != source || session.processed(path, thirdResult) != source) return 1;
    bool evicted = false;
    try { (void)session.processed(path, firstResult); }
    catch(const std::runtime_error &) { evicted = true; }
    if(!evicted) return 1;
    global["enabled"]=true;global["modelId"]="missing";recipe["layers"]=QJsonArray{global};
    bool rejected=false;try{session.process(path,recipe,{});}catch(const std::exception &){rejected=true;}
    if(!rejected)return 1;
    QJsonObject categoryLayer{{"categoryStrokes", QJsonArray{
      QJsonArray{QJsonObject{{"tool", "Brush"}, {"value", 1.0}, {"radius", .2},
        {"hardness", 1.0}, {"points", QJsonArray{QJsonArray{.5, .5}}}}}, QJsonArray{}, QJsonArray{}}}};
    const auto categoryMask = desktopMask(categoryLayer, 32, 32);
    if(categoryMask.sampleNearest(16, 16) < .9f || categoryMask.sampleNearest(0, 0) > .01f)
    { std::cerr << "category mask pixels " << categoryMask.sampleNearest(16, 16) << ' ' << categoryMask.sampleNearest(0, 0) << '\n'; return 1; }
    QJsonArray shapeOperations;
    shapeOperations.push_back(QJsonObject{{"tool", "Ellipse"},
      {"points", QJsonArray{QJsonArray{.1, .1}, QJsonArray{.9, .9}}}});
    shapeOperations.push_back(QJsonObject{{"tool", "Ellipse"}, {"erase", true},
      {"points", QJsonArray{QJsonArray{.3, .3}, QJsonArray{.7, .7}}}});
    QJsonObject erasedShape{{"marks", shapeOperations}};
    const auto shapeMask = desktopMask(erasedShape, 32, 32);
    if(shapeMask.sampleNearest(16, 16) > .01f || shapeMask.sampleNearest(5, 16) < .5f)
    { std::cerr << "shape mask pixels " << shapeMask.sampleNearest(16, 16) << ' ' << shapeMask.sampleNearest(5, 16) << '\n'; return 1; }
    // Optional arguments replace the deterministic CFA fixture with real inference.
    {
      const QString modelPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : directory.filePath("fixture.onnx");
      if(argc == 1) { QFile file{modelPath}; if(!file.open(QIODevice::WriteOnly)) return 1; file.write("fixture"); }
      ApplicationSettingsData settings;settings.onnxProvider="cpu";
      if(argc>2)settings.onnxRuntimeLibrary=QString::fromLocal8Bit(argv[2]);
      settings.denoiseModels.push_back({"test","Test",modelPath,"rawnind-bayer-v1"});
      ApplicationSettingsStore{}.save(settings);
      if(argc == 1) ProcessingSessionTest::seed(session, path, settings);
       global["modelId"]="test";global["sharpen"]="Normal";global["strength"]=30;global["blend"]=100;recipe["layers"]=QJsonArray{global};
       recipe["outputKind"]="linear-rgb";
        auto processed=session.process(path,recipe,{});
        if(processed->width()!=64 || processed->metadata().exposureBiasEv!=source->metadata().exposureBiasEv)return 1;
        recipe["outputKind"] = "cfa";
        if(session.process(path, recipe, {}, nullptr, false)->samples() != processed->samples())
          throw std::runtime_error{"Normal sharpening must apply to RAW DNG"};
        const auto sharpenedDng = directory.filePath("normal-raw.dng");
        rawlab::dng::TiffDngWriter{}.writeCfaDng(sharpenedDng.toStdString(), *processed);
        const auto sharpenedReloaded = rawlab::raw::LibRawDecoder{}.decodeRawImage(sharpenedDng.toStdString());
        for(std::size_t i = 0; i < processed->samples().size(); ++i)
          if(std::fabs(processed->samples()[i] - sharpenedReloaded.samples()[i]) > .51f)
            throw std::runtime_error{"RAW DNG lost Normal sharpening"};
        recipe["outputKind"] = "linear-rgb";
       global["strength"]=0;recipe["layers"]=QJsonArray{global};
       const auto unsharpened=session.process(path,recipe,{});
       global["strength"]=100;recipe["layers"]=QJsonArray{global};
       const auto fullySharpened=session.process(path,recipe,{});
       if(unsharpened->samples()==fullySharpened->samples())
       { std::cerr<<"Sharpening strength did not change CFA output\n";return 1; }
       timer.restart();
        const auto fullDetail = session.detail(path, recipe, tile);
       const auto coldSharpNs = timer.nsecsElapsed();
       global["strength"]=0;recipe["layers"]=QJsonArray{global};
       timer.restart();
        const auto zeroDetail = session.detail(path, recipe, tile);
       std::cout << "Sharpening detail cold/warm ms: " << coldSharpNs / 1e6 << '/' << timer.nsecsElapsed() / 1e6 << '\n';
       const auto expectedFull = rawlab::image::renderLinearRawPreviewRegion(*fullySharpened, 7, 9, 20, 16);
       const auto expectedZero = rawlab::image::renderLinearRawPreviewRegion(*unsharpened, 7, 9, 20, 16);
       if(fullDetail.pixels == zeroDetail.pixels) return 1;
       for(std::size_t i=0;i<fullDetail.pixels.size();++i)
         if(std::fabs(fullDetail.pixels[i]-expectedFull.pixels[i])>1e-5f
            || std::fabs(zeroDetail.pixels[i]-expectedZero.pixels[i])>1e-5f) {
           std::cerr << "Detail sharpening differs from full CFA composition\n"; return 1;
         }
       int inferenceProgress=0;
      global["blend"]=0;recipe["layers"]=QJsonArray{global};
      auto zero=session.process(path,recipe,[&](int,int){++inferenceProgress;});
      if(zero->samples()!=source->samples() || inferenceProgress)return 1;
      auto local=global;local["id"]=1;local["blend"]=100;local["marks"]=QJsonArray{QJsonObject{{"tool","Brush"},{"points",QJsonArray{QJsonArray{0.5,0.5}}}}};
      global["enabled"]=false;recipe["layers"]=QJsonArray{global,local};
       auto masked=session.process(path,recipe,{});
       if(masked->sample(0,0)!=source->sample(0,0))return 1;
       const auto maskedFull = rawlab::image::renderLinearRawPreviewRegion(*masked, 0, 0, 64, 64);
       const auto maskedRegion = session.detail(path, recipe, tile);
       for(int y = 0; y < tile.height(); ++y) for(int x = 0; x < tile.width(); ++x) for(int c = 0; c < 3; ++c)
         if(std::fabs(maskedRegion.pixels[(y * tile.width() + x) * 3 + c]
           - maskedFull.pixels[((y + tile.y()) * 64 + x + tile.x()) * 3 + c]) > 1e-5f) return 1;

       // Local strength replaces Global inside the mask, including at zero.
       // Persisted depth selections need neither a depth model nor more denoise inference.
       const QJsonObject depthMaps{{"test", QJsonObject{{"png", depthMapPng(rawlab::pipeline::horizontalGradientMask(64, 64))}, {"modelId", "removed"}}}};
       global["enabled"] = true; global["blend"] = 100; global["sharpen"] = "Off";
       recipe["highlights"] = false;
       recipe["layers"] = QJsonArray{global};
       const auto denoised = session.process(path, recipe, {}, nullptr, false);
       if(denoised->samples() == source->samples()) throw std::runtime_error{"Test needs visible denoising"};
       for(const bool highlights : {false, true})
       {
         recipe["highlights"] = highlights;
         const auto composed = session.process(path, recipe, {}, nullptr, false);
         const auto reference = rawlab::image::renderLinearRawPreview(*composed, 64, rawlab::image::PreviewQuality::High);
         const auto preview = session.preview(path, recipe, "highlights", *composed, 64, rawlab::image::PreviewQuality::High);
         for(std::size_t i = 0; i < reference.pixels.size(); ++i)
           if(std::fabs(preview.pixels[i] - reference.pixels[i]) > 1e-5f)
             throw std::runtime_error{"Overview highlights differ from CFA clipping"};
       }
       recipe["highlights"] = false;
       local = global; local["id"] = 1;
       local["marks"] = QJsonArray{};
       recipe["depthMaps"] = depthMaps;
       for(const QJsonArray &range : {QJsonArray{0, 0, 20, 40}, QJsonArray{60, 80, 100, 100}})
       for(const bool inverted : {false, true})
       for(const int blend : {0, 40, 100})
       {
         local["depth"] = QJsonObject{{"map", "test"}, {"range", range}};
         local["inverted"] = inverted;
         local["blend"] = blend;
         recipe["layers"] = QJsonArray{global, local};
         const auto depthProcessed = session.process(path, recipe, {}, nullptr, false);
         const auto mask = desktopMask(local, 64, 64, depthMaps);
         auto expectedSamples = denoised->samples();
         for(std::size_t i = 0; i < expectedSamples.size(); ++i)
         {
           const float restore = (inverted ? 1 - mask.values()[i] : mask.values()[i]) * (1 - blend / 100.0f);
           expectedSamples[i] += (source->samples()[i] - expectedSamples[i]) * restore;
           if(std::fabs(depthProcessed->samples()[i] - expectedSamples[i]) > .001f)
             throw std::runtime_error{"Local blend failed to override Global in CFA"};
         }
         const rawlab::raw::RawImage expected{64, 64, std::move(expectedSamples), source->metadata()};
         const auto depthFull = ProcessingSessionTest::exportReference(expected);
         const auto depthDetail = session.detail(path, recipe, tile);
         for(int y = 0; y < tile.height(); ++y) for(int x = 0; x < tile.width(); ++x) for(int c = 0; c < 3; ++c)
           if(std::fabs(depthDetail.pixels[(y * tile.width() + x) * 3 + c]
             - depthFull.pixels[((y + tile.y()) * 64 + x + tile.x()) * 3 + c]) > 1e-5f)
             throw std::runtime_error{"Depth detail differs from export CFA composition"};
         for(const auto quality : {rawlab::image::PreviewQuality::Fast, rawlab::image::PreviewQuality::High})
         {
           const auto before = rawlab::image::renderLinearRawPreview(*source, 64, quality);
           const auto after = rawlab::image::renderLinearRawPreview(*denoised, 64, quality);
           const auto previewMask = desktopMask(local, 64, 64, depthMaps);
           const auto preview = session.preview(path, recipe, "depth-blend", *depthProcessed, 64, quality);
           for(std::size_t i = 0; i < preview.pixels.size(); ++i)
           {
             const float amount = previewMask.sampleNearestMapped((i / 3) % before.width, (i / 3) / before.width, before.width, before.height);
             const float restore = (inverted ? 1 - amount : amount) * (1 - blend / 100.0f);
             if(std::fabs(preview.pixels[i] - (after.pixels[i] + (before.pixels[i] - after.pixels[i]) * restore)) > 1e-5f)
               throw std::runtime_error{"Local blend failed to override Global in overview"};
           }
         }
         const QString exported = directory.filePath("depth.dng");
         rawlab::dng::TiffDngWriter{}.writeCfaDng(exported.toStdString(), *depthProcessed);
         const auto decoded = rawlab::raw::LibRawDecoder{}.decodeRawImage(exported.toStdString());
         for(std::size_t i = 0; i < decoded.samples().size(); ++i)
           if(std::abs(decoded.samples()[i] - expected.samples()[i]) > .51f)
             throw std::runtime_error{"Depth selection changed in exported DNG"};

         const QString linearPath = directory.filePath("depth-linear.dng");
         rawlab::dng::TiffDngWriter{}.writeLinearRgbDng(linearPath.toStdString(), *depthProcessed);
         const auto rgb = rawlab::image::renderLinearRgb(expected);
         const std::unique_ptr<TIFF, decltype(&TIFFClose)> linear{TIFFOpen(linearPath.toStdString().c_str(), "r"), TIFFClose};
         if(!linear) throw std::runtime_error{"Cannot read Linear RGB DNG"};
         std::vector<std::uint16_t> row(rgb.width * 3);
         for(int y = 0; y < rgb.height; ++y)
         {
           if(TIFFReadScanline(linear.get(), row.data(), y) != 1) throw std::runtime_error{"Cannot read Linear RGB pixels"};
           for(int x = 0; x < rgb.width * 3; ++x)
             if(std::fabs(row[x] / 65535.0f - std::clamp(rgb.pixels[y * rgb.width * 3 + x], 0.0f, 1.0f)) > 2.0f / 65535)
               throw std::runtime_error{"Local blend changed in exported Linear RGB DNG"};
         }
       }

       // A zero-strength top layer restores the source over overlapping local layers.
       auto lower = local; lower["id"] = 2; lower["blend"] = 100;
       local["blend"] = 0;
       local["depth"] = lower["depth"] = QJsonObject{{"map", "test"}, {"range", QJsonArray{0, 0, 100, 100}}};
       local["inverted"] = lower["inverted"] = false;
       recipe["layers"] = QJsonArray{global, local, lower};
       if(session.process(path, recipe, {}, nullptr, false)->samples() != source->samples())
         throw std::runtime_error{"Top local layer did not override lower layers"};
       local["enabled"] = false;
       recipe["layers"] = QJsonArray{global, local, lower};
       if(session.process(path, recipe, {}, nullptr, false)->samples() != denoised->samples())
         throw std::runtime_error{"Disabled local layer changed composition"};

       // More than three CFA entries must survive, and switching photos must
       // retain model output without reusing it for the wrong source.
       for(int i = 0; i < 4; ++i) {
         const auto modelPath = directory.filePath(QStringLiteral("model-%1.onnx").arg(i));
          if(!QFile::link(settings.denoiseModels.front().path, modelPath)) return 1;
         settings.denoiseModels.push_back({QString::number(i), "Cache test", modelPath, "rawnind-bayer-v1"});
       }
        ApplicationSettingsStore{}.save(settings);
        if(argc == 1) ProcessingSessionTest::seed(session, path, settings);
       global["enabled"] = true; global["blend"] = 100; global["sharpen"] = "Off";
       for(int i = 0; i < 4; ++i) {
         global["modelId"] = QString::number(i); recipe["layers"] = QJsonArray{global};
         session.process(path, recipe, {});
       }
       const auto otherPath = directory.filePath("other.dng");
       rawlab::dng::TiffDngWriter{}.writeCfaDng(otherPath.toStdString(), {64, 64, std::vector<float>(4096, 1000), metadata});
       if(session.source(otherPath) == source || session.source(path) != source) return 1;
       for(int i = 0; i < 4; ++i) {
         global["modelId"] = QString::number(i); recipe["layers"] = QJsonArray{global};
         int runs = 0;
         session.process(path, recipe, [&](int, int) { ++runs; }, nullptr, false);
         if(runs) throw std::runtime_error{"Denoise ran again after a photo switch"};
       }
       bool needsInference = false;
       try { session.process(otherPath, recipe, {}, nullptr, false); }
       catch(const std::runtime_error &) { needsInference = true; }
       if(!needsInference) throw std::runtime_error{"Used another photo's denoise cache"};
       std::cout<<"Denoise/sharpen, cache reuse, zero blend and local mask checks passed\n";
    }
  }
  catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
