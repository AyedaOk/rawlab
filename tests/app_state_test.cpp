#include "ApplicationSettings.h"
#include "LibraryPage.h"
#include "PictureLibraryStore.h"
#include "PictureRecipe.h"
#include "PreviewWidget.h"
#include "SettingsDialog.h"

#include "rawlab/dng/DngWriter.h"
#include "rawlab/raw/LibRawDecoder.h"

#include <tiffio.h>

#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSettings>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTableWidget>

#include <cmath>
#include <filesystem>
#include <vector>

int main(int argc, char **argv)
{
  QApplication application{argc, argv};
  QCoreApplication::setOrganizationName(QStringLiteral("RawLabTest"));
  QCoreApplication::setApplicationName(QStringLiteral("StateTest"));
  QTemporaryDir settingsDirectory;
  if(!settingsDirectory.isValid()) return 1;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
  qputenv("XDG_DATA_HOME", settingsDirectory.path().toUtf8());

  ApplicationSettingsData data;
  data.onnxProvider = QStringLiteral("cpu");
  data.onnxDeviceId = 2;
  data.samModels = {
    {QStringLiteral("sam-small"), QStringLiteral("SAM 2.1 Small"), QStringLiteral("/models/small")},
    {QStringLiteral("sam-large"), QStringLiteral("SAM 2.1 Large"), QStringLiteral("/models/large")},
  };
  data.activeSamModelId = QStringLiteral("sam-large");
  data.denoiseModels = {
    {QStringLiteral("one"), QStringLiteral("Model One"), QStringLiteral("one.onnx")},
    {QStringLiteral("two"), QStringLiteral("Model Two"), QStringLiteral("two.onnx")},
  };
  ApplicationSettingsStore{}.save(data);
  const ApplicationSettingsData loaded = ApplicationSettingsStore{}.load();
  if(loaded.samModels.size() != 2 || loaded.activeSamModelId != QStringLiteral("sam-large")
     || loaded.onnxProvider != QStringLiteral("cpu") || loaded.onnxDeviceId != 2) return 1;
  if(ApplicationSettingsStore::samModelById(loaded, QStringLiteral("sam-large")).folder
     != QStringLiteral("/models/large")) return 1;
  SettingsDialog settingsDialog{loaded};
  QTableWidget *samTable = settingsDialog.findChild<QTableWidget *>(QStringLiteral("samModelTable"));
  if(!samTable || samTable->rowCount() != 2 || samTable->item(1, 0)->checkState() != Qt::Checked
     || samTable->currentRow() != 1) return 1;
  if(!settingsDialog.findChild<QTableWidget *>(QStringLiteral("managedModelTable"))
     || !settingsDialog.findChild<QPushButton *>(QStringLiteral("downloadModelsButton"))
     || !settingsDialog.findChild<QProgressBar *>(QStringLiteral("modelDownloadProgress"))
     || !settingsDialog.findChild<QComboBox *>(QStringLiteral("onnxProviderCombo"))) return 1;
  const ApplicationSettingsData tableSettings = settingsDialog.settings();
  if(tableSettings.samModels.size() != 2 || tableSettings.activeSamModelId != QStringLiteral("sam-large")) return 1;
  if(loaded.denoiseModels.size() != 2 || loaded.denoiseModels[1].id != QStringLiteral("two")) return 1;
  if(ApplicationSettingsStore::modelById(loaded, QStringLiteral("one")).path != QStringLiteral("one.onnx")) return 1;
  ApplicationSettingsData withoutModels = loaded;
  withoutModels.denoiseModels.clear();
  ApplicationSettingsStore{}.save(withoutModels);
  if(!ApplicationSettingsStore{}.load().denoiseModels.isEmpty()) return 1;

  ApplicationSettingsData withoutSam = loaded;
  withoutSam.samModels.clear();
  withoutSam.activeSamModelId.clear();
  ApplicationSettingsStore{}.save(withoutSam);
  if(!ApplicationSettingsStore{}.load().samModels.isEmpty()) return 1;

  QSettings{}.clear();
  QSettings{}.setValue(QStringLiteral("sam21/encoder"), QStringLiteral("/legacy/sam/encoder.onnx"));
  QSettings{}.setValue(QStringLiteral("sam21/decoder"), QStringLiteral("/legacy/sam/decoder.onnx"));
  const ApplicationSettingsData migrated = ApplicationSettingsStore{}.load();
  if(migrated.samModels.size() != 1 || migrated.samModels.front().folder != QStringLiteral("/legacy/sam")
     || migrated.activeSamModelId != migrated.samModels.front().id) return 1;

  const QString firstPath = settingsDirectory.filePath(QStringLiteral("first.ARW"));
  const QString secondPath = settingsDirectory.filePath(QStringLiteral("second.NEF"));
  QFile first{firstPath};
  QFile second{secondPath};
  if(!first.open(QIODevice::WriteOnly) || !second.open(QIODevice::WriteOnly)) return 1;
  first.close();
  second.close();

  PictureLibraryStore store;
  const QStringList imported = store.addFiles({firstPath, firstPath, secondPath});
  if(imported.size() != 2) return 1;
  store.removeFiles({firstPath});
  const QStringList remaining = store.files();
  if(remaining.size() != 1 || remaining.front() != PictureLibraryStore::normalizedPath(secondPath)) return 1;

  PictureRecipe recipe;
  recipe.previewWidth = 4;
  recipe.previewHeight = 2;
  recipe.globalModelId = QStringLiteral("one");
  PictureRecipeLayer layer;
  layer.id = 7;
  layer.name = QStringLiteral("Subject");
  layer.feather = 0.075f;
  layer.blur = 0.025f;
  layer.sharpenEnabled = true;
  layer.sharpenStrength = 0.35f;
  layer.prompts.push_back({0.25f, 0.75f, true});
  layer.strokes.push_back({{{0.1, 0.2}, {0.8, 0.9}}, 0.125f, 0.6f, 1.0f});
  layer.baseMask = rawlab::pipeline::Mask{4, 2, {0.0f, 0.25f, 0.75f, 1.0f,
                                                         0.0f, 0.25f, 0.75f, 1.0f}};
  recipe.layers.push_back(layer);
  PictureRecipeStore recipeStore;
  recipeStore.save(firstPath, recipe);
  const auto loadedRecipe = recipeStore.load(firstPath, 2, 4);
  if(!loadedRecipe || loadedRecipe->previewWidth != 2 || loadedRecipe->previewHeight != 4) return 1;
  if(!loadedRecipe->preserveClipped) return 1;
  if(loadedRecipe->layers.size() != 1 || !loadedRecipe->layers.front().baseMask) return 1;
  const PictureRecipeLayer &loadedLayer = loadedRecipe->layers.front();
  if(loadedLayer.id != 7 || loadedLayer.prompts.size() != 1 || loadedLayer.strokes.size() != 1) return 1;
  if(std::abs(loadedLayer.feather - 0.075f) > 0.0001f
     || std::abs(loadedLayer.blur - 0.025f) > 0.0001f
     || !loadedLayer.sharpenEnabled
     || std::abs(loadedLayer.sharpenStrength - 0.35f) > 0.0001f) return 1;
  if(std::abs(loadedLayer.prompts.front().x - 0.25f) > 0.0001f
     || std::abs(loadedLayer.prompts.front().y - 0.75f) > 0.0001f) return 1;
  if(loadedLayer.baseMask->width() != 2 || loadedLayer.baseMask->height() != 4) return 1;
  const float resizedValue = loadedLayer.baseMask->sampleNearest(1, 2);
  if(resizedValue <= 0.5f || resizedValue >= 1.0f) return 1;

  const QJsonObject legacyLayer{
    {QStringLiteral("id"), 3},
    {QStringLiteral("prompts"), QJsonArray{QJsonObject{
      {QStringLiteral("x"), 300}, {QStringLiteral("y"), 100},
      {QStringLiteral("foreground"), true}}}},
    {QStringLiteral("strokes"), QJsonArray{QJsonObject{
      {QStringLiteral("radius"), 40}, {QStringLiteral("hardness"), 0.5},
      {QStringLiteral("value"), 1.0},
      {QStringLiteral("points"), QJsonArray{QJsonArray{300, 100}}}}}},
  };
  const QJsonObject legacyRoot{
    {QStringLiteral("version"), 1},
    {QStringLiteral("layers"), QJsonArray{legacyLayer}},
  };
  QFile legacyFile{PictureRecipeStore::pathForSource(secondPath)};
  if(!legacyFile.open(QIODevice::WriteOnly)) return 1;
  legacyFile.write(QJsonDocument{legacyRoot}.toJson());
  legacyFile.close();
  const auto legacy = recipeStore.load(secondPath, 600, 400);
  if(!legacy || legacy->layers.size() != 1) return 1;
  if(!legacy->preserveClipped) return 1;
  if(std::abs(legacy->layers.front().prompts.front().x - 0.5f) > 0.0001f
     || std::abs(legacy->layers.front().prompts.front().y - 0.25f) > 0.0001f
     || std::abs(legacy->layers.front().strokes.front().radius - 0.1f) > 0.0001f) return 1;
  if(legacy->layers.front().sharpenEnabled) return 1;
  if(!legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
  legacyFile.write(QJsonDocument{QJsonObject{
    {QStringLiteral("version"), 3},
    {QStringLiteral("layers"), QJsonArray{QJsonObject{
      {QStringLiteral("id"), 4},
      {QStringLiteral("sharpenEnabled"), true},
      {QStringLiteral("sharpenStrength"), 0.2},
    }}},
  }}.toJson());
  legacyFile.close();
  const auto version3 = recipeStore.load(secondPath, 600, 400);
  if(!version3 || version3->layers.size() != 1 || version3->layers.front().sharpenEnabled) return 1;
  if(!legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
  legacyFile.write(QJsonDocument{QJsonObject{
    {QStringLiteral("version"), 4},
    {QStringLiteral("preserveClipped"), false},
  }}.toJson());
  legacyFile.close();
  const auto version4 = recipeStore.load(secondPath, 600, 400);
  if(!version4 || !version4->preserveClipped) return 1;
  if(!legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
  legacyFile.write(QJsonDocument{QJsonObject{
    {QStringLiteral("version"), 5},
    {QStringLiteral("preserveClipped"), false},
  }}.toJson());
  legacyFile.close();
  const auto version5 = recipeStore.load(secondPath, 600, 400);
  if(!version5 || version5->preserveClipped) return 1;
  if(!legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
  legacyFile.write(QJsonDocument{QJsonObject{{QStringLiteral("version"), 99}}}.toJson());
  legacyFile.close();
  if(recipeStore.load(secondPath, 600, 400)) return 1;

  LibraryPage libraryPage;
  libraryPage.setFiles({firstPath, secondPath, QStringLiteral("third.raw")});
  QListWidget *pictures = libraryPage.findChild<QListWidget *>();
  if(!pictures) return 1;
  pictures->item(2)->setSelected(true);
  pictures->item(0)->setSelected(true);
  pictures->setCurrentItem(pictures->item(2), QItemSelectionModel::NoUpdate);
  const QStringList selected = libraryPage.selectedPaths();
  if(selected.size() != 2 || selected[0] != firstPath
     || selected[1] != QStringLiteral("third.raw")) return 1;
  libraryPage.setFiles({firstPath, secondPath, QStringLiteral("third.raw")});
  if(libraryPage.selectedPath() != QStringLiteral("third.raw")
     || libraryPage.selectedPaths().size() != 2) return 1;

  PreviewWidget previewWidget;
  previewWidget.resize(800, 600);
  previewWidget.show();
  QApplication::processEvents();
  previewWidget.setImage(QImage{400, 400, QImage::Format_RGB888}, true);
  if(std::abs(previewWidget.imageScale() - 1.5) > 0.0001) return 1;
  previewWidget.actualSize();
  if(std::abs(previewWidget.imageScale() - 1.0) > 0.0001) return 1;
  previewWidget.resize(1000, 500);
  QApplication::processEvents();
  if(std::abs(previewWidget.imageScale() - 1.0) > 0.0001) return 1;
  previewWidget.zoomIn();
  if(std::abs(previewWidget.imageScale() - 1.25) > 0.0001) return 1;

  PreviewWidget comparisonWidget;
  comparisonWidget.resize(200, 100);
  QImage before{100, 100, QImage::Format_RGB888};
  QImage after{100, 100, QImage::Format_RGB888};
  before.fill(Qt::blue);
  after.fill(Qt::red);
  comparisonWidget.setImage(after, true);
  comparisonWidget.setBeforeImage(before);
  comparisonWidget.setComparisonEnabled(true);
  comparisonWidget.show();
  QApplication::processEvents();
  const QImage comparison = comparisonWidget.grab().toImage();
  if(comparison.pixelColor(75, 50).blue() < 200 || comparison.pixelColor(125, 50).red() < 200) return 1;

  QImage jpegImage{64, 64, QImage::Format_RGB888};
  jpegImage.fill(QColor{220, 160, 40});
  QByteArray jpegBytes;
  QBuffer jpegBuffer{&jpegBytes};
  if(!jpegBuffer.open(QIODevice::WriteOnly) || !jpegImage.save(&jpegBuffer, "JPEG", 90)) return 1;
  rawlab::dng::JpegPreview jpegPreview;
  jpegPreview.width = jpegImage.width();
  jpegPreview.height = jpegImage.height();
  jpegPreview.data.assign(reinterpret_cast<const std::uint8_t *>(jpegBytes.constData()),
                          reinterpret_cast<const std::uint8_t *>(jpegBytes.constData()
                                                                 + jpegBytes.size()));
  const rawlab::dng::DngPreviews dngPreviews{jpegPreview, jpegPreview};
  rawlab::raw::RawMetadata dngMetadata;
  dngMetadata.cfa.isBayer = true;
  dngMetadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
  dngMetadata.cfa.cells[0][1] = rawlab::raw::CfaColor::Green;
  dngMetadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
  dngMetadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
  dngMetadata.activeArea = {0, 0, 64, 64};
  dngMetadata.whiteLevel = 1023.0f;
  const rawlab::raw::RawImage dngImage{
    64, 64, std::vector<float>(64 * 64, 512.0f), dngMetadata,
  };
  const QString dngPath = settingsDirectory.filePath(QStringLiteral("preview-roundtrip.dng"));
  rawlab::dng::TiffDngWriter{}.writeCfaDng(
    std::filesystem::path{dngPath.toStdString()}, dngImage, &dngPreviews);
  TIFF *dngTiff = TIFFOpen(dngPath.toStdString().c_str(), "r");
  if(!dngTiff) return 1;
  std::uint16_t subIfdCount = 0;
  toff_t *subIfdOffsets = nullptr;
  const bool hasRawSubIfd = TIFFGetField(dngTiff, TIFFTAG_SUBIFD, &subIfdCount,
                                         &subIfdOffsets) == 1
                           && subIfdCount == 2 && subIfdOffsets;
  const toff_t rawSubIfdOffset = hasRawSubIfd ? subIfdOffsets[0] : 0;
  if(!hasRawSubIfd || TIFFSetSubDirectory(dngTiff, rawSubIfdOffset) != 1)
  {
    TIFFClose(dngTiff);
    return 1;
  }
  std::uint16_t *repeatDimensions = nullptr;
  std::uint32_t patternCount = 0;
  std::uint8_t *pattern = nullptr;
  const bool hasCfaTags = TIFFGetField(dngTiff, TIFFTAG_CFAREPEATPATTERNDIM,
                                       &repeatDimensions) == 1
                          && TIFFGetField(dngTiff, TIFFTAG_CFAPATTERN,
                                          &patternCount, &pattern) == 1;
  const bool validCfaTags = hasCfaTags && repeatDimensions && pattern
                            && repeatDimensions[0] == 2 && repeatDimensions[1] == 2
                            && patternCount == 4 && pattern[0] == 0 && pattern[1] == 1
                            && pattern[2] == 1 && pattern[3] == 2;
  TIFFClose(dngTiff);
  if(!validCfaTags) return 1;
  const rawlab::raw::RawImage decodedDng = rawlab::raw::LibRawDecoder{}.decodeRawImage(
    std::filesystem::path{dngPath.toStdString()});
  if(decodedDng.width() != 64 || decodedDng.height() != 64
     || !decodedDng.metadata().cfa.isBayer) return 1;
  if(QImage{dngPath}.isNull()) return 1;
  return 0;
}
