#include "ProcessingSession.h"
#include "rawlab/dng/DngWriter.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
  QTemporaryDir directory;
  qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
  QCoreApplication app{argc, argv};
  app.setOrganizationName("RawLab"); app.setApplicationName("RawLab");
  const auto require = [](bool condition, const char *message) { if(!condition) throw std::runtime_error{message}; };
  try
  {
    ApplicationSettingsData configuration;
    configuration.depthModels.push_back({"small", "Small", "model.onnx", "depth-anything-v2-v1"});
    configuration.activeDepthModelId = "small";
    ApplicationSettingsStore{}.save(configuration);
    require(ApplicationSettingsStore{}.load().activeDepthModelId == "small", "Active depth model was not restored");
    configuration.activeDepthModelId.clear();
    ApplicationSettingsStore{}.save(configuration);
    require(ApplicationSettingsStore{}.load().activeDepthModelId.isEmpty(), "Depth model None was not retained");
    configuration.activeDepthModelId = "missing";
    ApplicationSettingsStore{}.save(configuration);
    require(ApplicationSettingsStore{}.load().activeDepthModelId == "small", "Missing depth model did not fall back");

    std::vector<float> values(18);
    for(int i = 0; i < 18; ++i) values[i] = (i % 9) / 8.0f;
    const rawlab::pipeline::Mask depth{9, 2, values};
    const auto png = depthMapPng(depth);
    const auto decoded = decodeDepthMap(png);
    for(std::size_t i = 0; i < values.size(); ++i)
      require(std::abs(decoded.values()[i] - values[i]) < 1.0f / 65535, "Depth PNG lost 16-bit precision");
    const QJsonObject maps{{"test", QJsonObject{{"png", png}, {"modelId", "small"}}}};
    QJsonObject layer{{"id", 1}, {"enabled", true}, {"depth", QJsonObject{{"map", "test"},
      {"range", QJsonArray{12.5, 37.5, 62.5, 87.5}}}}};
    const auto selected = desktopMask(layer, 9, 2, maps);
    const float expected[] = {0, 0, .5, 1, 1, 1, .5, 0, 0};
    for(int i = 0; i < 18; ++i) require(std::abs(selected.values()[i] - expected[i % 9]) < .0001f, "Depth fade changed");
    layer["feather"] = 100;
    require(desktopMask(layer, 9, 2, maps).values() == selected.values(), "Spatial feather replaced the depth fades");
    layer["blur"] = 100;
    require(desktopMask(layer, 9, 2, maps).values() == rawlab::pipeline::blurMask(selected, .2f).values(),
      "Depth blur was lost when disabling feather");
    layer.remove("blur");
    const auto full = rawlab::pipeline::depthRangeMask(depth, 19, 7, {0, 0, 1, 1});
    require(std::all_of(full.values().begin(), full.values().end(), [](float v) { return v == 1; }), "Hard endpoints lost near/far pixels");
    const auto narrow = rawlab::pipeline::depthRangeMask(depth, 9, 2, {.5f, .5f, .5f, .5f});
    require(narrow.sampleNearest(4, 0) == 1 && narrow.sampleNearest(3, 0) == 0, "Collapsed depth range is invalid");
    const auto resized = rawlab::pipeline::depthRangeMask({2, 1, {0, 1}}, 4, 1, {0, 0, .25f, .75f});
    require(resized.values() == std::vector<float>({1, 1, 0, 0}), "Depth values must be resized before thresholding");
    for(const auto &invalid : {QJsonObject{}, QJsonObject{{"test", QJsonObject{{"png", "invalid"}}}}})
    {
      bool rejected = false;
      try { (void)desktopMask(layer, 9, 2, invalid); } catch(const std::exception &) { rejected = true; }
      require(rejected, "Missing/corrupt depth data was treated as a valid mask");
    }
    bool rejected = false;
    try { (void)rawlab::pipeline::depthRangeMask(depth, 9, 2, {0, .7f, .3f, 1}); }
    catch(const std::invalid_argument &) { rejected = true; }
    require(rejected, "Crossed depth handles were accepted");

    const QJsonObject recipe{{"depthMaps", maps}, {"layers", QJsonArray{QJsonObject{{"id", 0}}, layer}}};
    const auto file = directory.filePath("recipe.json");
    PictureRecipeStore::saveAtPath(file, toQtRecipe(recipe, 9, 2));
    const auto stored = PictureRecipeStore::loadAtPath(file, 18, 4, true);
    require(stored.has_value(), "Depth recipe did not load");
    const auto restored = fromQtRecipe(*stored);
    require(restored["depthMaps"] == recipe["depthMaps"], "Depth recipe lost its map");
    const auto restoredLayer = restored["layers"].toArray()[1].toObject();
    require(restoredLayer["depth"] == layer["depth"], "Depth recipe lost its range");
    require(desktopMask(restoredLayer, 18, 4, restored["depthMaps"].toObject()).values()
      == desktopMask(layer, 18, 4, maps).values(), "Depth selection changed after restart/resize");

    rawlab::raw::RawMetadata metadata;
    metadata.make = "RawLab"; metadata.model = "Depth test";
    metadata.cfa.isBayer = true;
    metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
    metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
    metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
    metadata.whiteLevel = 4095; metadata.activeArea = {2, 4, 60, 88};
    for(const int flip : {0, 3, 5, 6})
    {
      metadata.flip = flip;
      const rawlab::raw::RawImage original{64, 96, std::vector<float>(64 * 96, 1000), metadata};
      const rawlab::raw::RawImage processed{64, 96, std::vector<float>(64 * 96, 1800), metadata};
      const auto geometry = rawlab::image::rawPreviewGeometry(original, 1024);
      const auto mask = desktopMask(layer, geometry.previewWidth, geometry.previewHeight, maps);
      const auto composed = rawlab::pipeline::blendWithPreviewMask(original, processed, mask, geometry, 1, false);
      const auto inverted = rawlab::pipeline::blendWithPreviewMask(original, processed, mask, geometry, 1, true);
      for(const int x : {2, 32, 59}) for(const int y : {4, 44, 87})
        require(std::abs(composed.sample(x, y) + inverted.sample(x, y) - 2800) < .001f,
          "Depth inversion differs in oriented CFA composition");
    }
    layer["marks"] = QJsonArray{QJsonObject{{"tool", "Erase"}, {"radius", .3}, {"hardness", 1},
      {"points", QJsonArray{QJsonArray{.5, .5}}}}};
    require(desktopMask(layer, 90, 40, maps).sampleNearest(45, 20) == 0, "Brush erasing did not refine depth");

    if(argc > 1)
    {
      ApplicationSettingsData settings;
      settings.onnxProvider = "cpu";
      if(argc > 2)
      {
        settings.onnxRuntimeLibrary = QString::fromLocal8Bit(argv[2]);
        const auto runtime = rawlab::ai::OnnxSession::probeRuntime(argv[2]);
        const auto providers = rawlab::ai::OnnxSession::availableProviders(argv[2]);
        require(!runtime.version.empty() && !runtime.providers.empty() && runtime.providers.front().id == "cpu",
          "Runtime probe failed to negotiate the supported API");
        require(providers.size() == runtime.providers.size() && providers.front().id == "cpu",
          "Runtime loading and probing disagree");
        std::cout << "ONNX Runtime " << runtime.version << '\n';
      }
      settings.depthModels.push_back({"small", "Depth test", QString::fromLocal8Bit(argv[1]), "depth-anything-v2-v1"});
      settings.activeDepthModelId = "small";
      ApplicationSettingsStore{}.save(settings);
      require(ApplicationSettingsStore{}.load().activeDepthModelId == "small", "Depth model setting was lost");
      const QString path = argc > 3 ? QString::fromLocal8Bit(argv[3]) : directory.filePath("source.dng");
      if(argc <= 3)
      {
        std::vector<float> samples(64 * 96);
        for(int y = 0; y < 96; ++y) for(int x = 0; x < 64; ++x) samples[y * 64 + x] = 700 + (x * 31 + y * 13) % 1800;
        rawlab::dng::TiffDngWriter{}.writeCfaDng(path.toStdString(), {64, 96, samples, metadata});
      }
      ProcessingSession session;
      QElapsedTimer timer; timer.start();
      const auto result = session.depth(path);
      const auto cold = timer.elapsed(); timer.restart();
      const auto cached = session.depth(path);
      const auto warm = timer.elapsed();
      require(result == cached, "Cached depth map changed");
      const auto output = decodeDepthMap(result["png"]);
      require(output.width() % 14 == 0 && output.height() % 14 == 0, "Unexpected actual depth output dimensions");
      require(std::all_of(output.values().begin(), output.values().end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }), "Invalid actual model depth");
      std::cout << "Actual Small depth " << output.width() << 'x' << output.height()
                << ", cold " << cold << " ms, cached " << warm << " ms\n";
      bool cancelled = false;
      try { session.depth(path, [] { throw std::runtime_error{"cancelled"}; }); }
      catch(const std::runtime_error &) { cancelled = true; }
      require(cancelled, "Depth ignored cancellation");
    }
    std::cout << "Depth selection, precision, recipe persistence and CFA composition passed\n";
  }
  catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
