#include "rawlab/dng/DngWriter.h"
#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/image/LensCorrection.h"
#include "rawlab/image/RawPreview.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QBuffer>
#include <QFile>
#include <QDataStream>
#include <tiffio.h>
#include <cmath>
#include <iostream>

bool hasWindowsExifPointer(const QString &path)
{
  QFile file{path};
  if(!file.open(QIODevice::ReadOnly) || !file.seek(4)) return false;
  QDataStream stream{&file};
  stream.setByteOrder(QDataStream::LittleEndian);
  quint32 offset = 0;
  stream >> offset;
  if(!file.seek(offset)) return false;
  quint16 count = 0;
  stream >> count;
  for(unsigned i = 0; i < count; ++i)
  {
    quint16 tag = 0, type = 0;
    quint32 length = 0, value = 0;
    stream >> tag >> type >> length >> value;
    if(stream.status() != QDataStream::Ok) return false;
    if(tag == TIFFTAG_EXIFIFD) return type == TIFF_LONG && length == 1 && value != 0;
  }
  return false;
}

int main(int argc, char **argv)
{
  QCoreApplication application{argc, argv};
  const bool exportOnly = argc == 3 && QString::fromLocal8Bit(argv[2]) == "--export-only";
  if(argc != 2 && !exportOnly) return 1;
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;
  rawlab::raw::RawMetadata metadata;
  metadata.make = "RawLab"; metadata.model = "Preview Test";
  metadata.cfa.isBayer = true;
  metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
  metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
  metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
  metadata.whiteLevel = 4095;
  metadata.activeArea = {0, 0, 64, 96}; metadata.flip = 6;
  metadata.iso = 640; metadata.exposureBiasEv = -1;
  const QString input = directory.filePath("test with spaces.dng");
  const QString secondInput = directory.filePath("second.dng");
  qputenv("XDG_DATA_HOME", directory.path().toUtf8());
  rawlab::dng::TiffDngWriter{}.writeCfaDng(input.toStdString(), {64, 96, std::vector<float>(64*96, 1024), metadata});
  rawlab::dng::TiffDngWriter{}.writeCfaDng(secondInput.toStdString(), {64, 96, std::vector<float>(64*96, 2048), metadata});
  QFile source{input};
  if(!source.open(QIODevice::ReadOnly)) return 1;
  const auto original = source.readAll(); source.close();
  for(const QString &quality : {QStringLiteral("fast"), QStringLiteral("high")})
  {
    QProcess process;
    process.start(QString::fromLocal8Bit(argv[1]), {input, "128", quality});
    if(!process.waitForFinished(15000) || process.exitCode()) { std::cerr << process.readAllStandardError().constData(); return 1; }
    const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    const auto info = json["metadata"].toObject();
    const auto optics = info["optics"].toObject();
    const QImage preview = QImage::fromData(QByteArray::fromBase64(json["png"].toString().toLatin1()), "PNG");
    if(json["version"].toInt() != 1 || preview.size() != QSize{96, 64}
       || info["iso"].toDouble() != 640 || info["exposureBiasEv"].toDouble() != -1
       || info["rawWidth"].toInt() != 64 || info["rawHeight"].toInt() != 96
       || !optics.contains("lensfunAvailable") || optics["message"].toString().isEmpty()) return 1;
  }
  QProcess invalid;
  invalid.start(QString::fromLocal8Bit(argv[1]), {input, "99999", "fast"});
  if(!invalid.waitForFinished(5000) || invalid.exitCode() == 0) return 1;
  if(!source.open(QIODevice::ReadOnly) || source.readAll() != original) return 1;
  QProcess server;
  server.start(QString::fromLocal8Bit(argv[1]), {"--serve"});
  if(!server.waitForStarted())return 1;
   const auto send=[&server](const QJsonObject &request) {
     server.write(QJsonDocument{request}.toJson(QJsonDocument::Compact)+'\n');
     server.waitForBytesWritten();
     for(;;)
     {
       while(!server.canReadLine())if(!server.waitForReadyRead(10000))return QJsonObject{};
       const auto response = QJsonDocument::fromJson(server.readLine()).object();
       if(!response.contains("progress")) return response;
     }
   };
  server.write("{not-json}\n");
  server.waitForBytesWritten();
  while(!server.canReadLine()) if(!server.waitForReadyRead(10000)) return 1;
  if(QJsonDocument::fromJson(server.readLine()).object()["error"] != "Invalid JSON request") return 1;
  const QJsonObject recipe{{"layers",QJsonArray{QJsonObject{{"id",0},{"enabled",false}}}},{"highlights",true}};
  QJsonObject request{{"operation","process"},{"path",input},{"recipe",recipe},{"size",128},{"quality","high"}};
  const auto processed=send(request);
   const QString resultId = processed["resultId"].toString();
    if(processed["png"].toString().isEmpty() || processed["before"]!=processed["png"]
       || resultId.isEmpty() || processed["sourceWidth"].toInt() != 96
       || processed["sourceHeight"].toInt() != 64)return 1;
    request["operation"] = "recompose";
    request["resultId"] = resultId;
    const auto recomposed = send(request);
    if(recomposed["png"].toString().isEmpty() || recomposed["resultId"].toString() != resultId)
      return 1;
    request["operation"] = "processed_preview";
   request["quality"] = "fast";
   request["resultId"] = resultId;
   if(send({{"operation", "preview"}, {"path", secondInput}, {"size", 128}, {"quality", "fast"}})["png"].toString().isEmpty()) return 1;
   if(send(request)["png"].toString().isEmpty()) return 1;
    const auto detail = send({{"operation", "detail"}, {"path", input}, {"target", "processed"},
      {"resultId", resultId}, {"recipe", recipe}, {"x", 11}, {"y", 7}, {"width", 24}, {"height", 18}});
   const QImage detailImage = QImage::fromData(QByteArray::fromBase64(detail["png"].toString().toLatin1()), "PNG");
   if(detailImage.size() != QSize{24, 18} || detail["sourceWidth"].toInt() != 96
      || detail["sourceHeight"].toInt() != 64 || detail["x"].toInt() != 11) return 1;
    const auto staleDetail = send({{"operation", "detail"}, {"path", input}, {"target", "processed"},
      {"resultId", "stale"}, {"recipe", recipe}, {"x", 0}, {"y", 0}, {"width", 8}, {"height", 8}});
   if(staleDetail["error"].toString().isEmpty()) return 1;
  const QString output=directory.filePath("export.dng");
  request["operation"]="export";request["output"]=output;
  if(send(request)["output"].toString()!=output || !QFile::exists(output))return 1;
  if(!hasWindowsExifPointer(output)) return 1;
  if(send(request)["error"].toString().isEmpty())return 1;
  const QString linearOutput=directory.filePath("linear.dng");
  request["operation"]="export_linear";request["output"]=linearOutput;
  if(send(request)["output"].toString()!=linearOutput || !QFile::exists(linearOutput))return 1;
  if(!hasWindowsExifPointer(linearOutput)) return 1;
  TIFF *linear = TIFFOpen(linearOutput.toStdString().c_str(), "r");
  if(!linear) return 1;
  std::uint32_t thumbnailWidth = 0, thumbnailHeight = 0;
  std::uint32_t thumbnailType = 0;
  std::uint16_t thumbnailCompression = 0, linearSubIfdCount = 0;
  toff_t *linearSubIfds = nullptr;
  TIFFGetField(linear, TIFFTAG_IMAGEWIDTH, &thumbnailWidth);
  TIFFGetField(linear, TIFFTAG_IMAGELENGTH, &thumbnailHeight);
  TIFFGetField(linear, TIFFTAG_SUBFILETYPE, &thumbnailType);
  TIFFGetField(linear, TIFFTAG_COMPRESSION, &thumbnailCompression);
  TIFFGetField(linear, TIFFTAG_SUBIFD, &linearSubIfdCount, &linearSubIfds);
  const bool linearThumbnailValid = thumbnailWidth > 0 && thumbnailWidth <= 320
    && thumbnailHeight > 0 && thumbnailHeight <= 320
    && thumbnailType == FILETYPE_REDUCEDIMAGE && thumbnailCompression == COMPRESSION_JPEG
    && linearSubIfdCount == 2 && linearSubIfds;
  const toff_t linearMainOffset = linearThumbnailValid ? linearSubIfds[0] : 0;
  const toff_t linearDisplayOffset = linearThumbnailValid ? linearSubIfds[1] : 0;
  if(!linearThumbnailValid || TIFFSetSubDirectory(linear, linearMainOffset) != 1)
  { TIFFClose(linear); return 1; }
  std::uint16_t samplesPerPixel = 0, bitsPerSample = 0, photometric = 0;
   std::uint32_t width = 0, height = 0;
   std::uint16_t orientation = 0;
   char *uniqueModel = nullptr;
   std::uint32_t matrixCount = 0, neutralCount = 0, blackCount = 0, whiteCount = 0;
   float *colorMatrix = nullptr, *neutral = nullptr, *blackLevel = nullptr;
   std::uint32_t *whiteLevel = nullptr;
   std::uint16_t illuminant = 0;
  TIFFGetField(linear, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(linear, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetField(linear, TIFFTAG_PHOTOMETRIC, &photometric);
   TIFFGetField(linear, TIFFTAG_IMAGEWIDTH, &width);
   TIFFGetField(linear, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField(linear, TIFFTAG_ORIENTATION, &orientation);
   TIFFGetField(linear, TIFFTAG_UNIQUECAMERAMODEL, &uniqueModel);
   TIFFGetField(linear, TIFFTAG_COLORMATRIX1, &matrixCount, &colorMatrix);
   TIFFGetField(linear, TIFFTAG_ASSHOTNEUTRAL, &neutralCount, &neutral);
   TIFFGetField(linear, TIFFTAG_CALIBRATIONILLUMINANT1, &illuminant);
   TIFFGetField(linear, TIFFTAG_BLACKLEVEL, &blackCount, &blackLevel);
   TIFFGetField(linear, TIFFTAG_WHITELEVEL, &whiteCount, &whiteLevel);
   const bool linearMetadataValid = uniqueModel && QString::fromLatin1(uniqueModel) == "RawLab Linear sRGB"
     && matrixCount == 9 && colorMatrix && std::abs(colorMatrix[0] - 3.2404542f) < 0.001f
     && neutralCount == 3 && neutral && neutral[0] == 1.0f && neutral[1] == 1.0f && neutral[2] == 1.0f
     && illuminant == 21 && blackCount == 3 && blackLevel && blackLevel[0] == 0.0f
     && whiteCount == 3 && whiteLevel && whiteLevel[0] == 65535;
   std::vector<std::uint16_t> linearRow(static_cast<std::size_t>(width) * 3U);
    const bool linearPixelsValid = TIFFReadScanline(linear, linearRow.data(), 0, 0) == 1
      && std::any_of(linearRow.begin(), linearRow.end(), [](std::uint16_t value) { return value > 0; });
    const bool hasLinearDisplayPreview = TIFFSetSubDirectory(linear, linearDisplayOffset) == 1;
    std::uint32_t displayType = 0;
    std::uint16_t displayCompression = 0;
    if(hasLinearDisplayPreview)
    {
      std::uint32_t previewWidth = 0, previewHeight = 0;
      TIFFGetField(linear, TIFFTAG_IMAGEWIDTH, &previewWidth);
      TIFFGetField(linear, TIFFTAG_IMAGELENGTH, &previewHeight);
      const bool rotated = orientation == ORIENTATION_RIGHTTOP || orientation == ORIENTATION_LEFTBOT;
      if(previewWidth != (rotated ? height : width) || previewHeight != (rotated ? width : height))
      { TIFFClose(linear); return 1; }
      TIFFGetField(linear, TIFFTAG_SUBFILETYPE, &displayType);
      TIFFGetField(linear, TIFFTAG_COMPRESSION, &displayCompression);
    }
    TIFFClose(linear);
    if(samplesPerPixel != 3 || bitsPerSample != 16 || photometric != 34892
       || width != 64 || height != 96 || orientation != ORIENTATION_RIGHTTOP
       || !linearMetadataValid || !linearPixelsValid || !hasLinearDisplayPreview
        || displayType != FILETYPE_REDUCEDIMAGE || displayCompression != COMPRESSION_JPEG) return 1;
  // Compare displayed highlights with actual exported samples, including white balance and orientation.
  auto brightMetadata = metadata;
  brightMetadata.asShotNeutral = {.25f, 1.0f, .5f};
  const QString brightInput = directory.filePath("highlights.dng");
  const QString brightOutput = directory.filePath("highlights-linear.dng");
  rawlab::dng::TiffDngWriter{}.writeCfaDng(brightInput.toStdString(),
    {64, 96, std::vector<float>(64 * 96, 2048), brightMetadata});
  const auto bright = rawlab::raw::LibRawDecoder{}.decodeRawImage(brightInput.toStdString());
  const auto brightRgb = rawlab::image::renderLinearRgb(bright);
  if(std::none_of(brightRgb.pixels.begin(), brightRgb.pixels.end(), [](float value) { return value > 1.25f; }))
  { std::cerr << "Highlight fixture needs values above display white\n"; return 1; }
  auto brightRecipe = recipe;
  brightRecipe["outputKind"] = "linear-rgb";
  QJsonObject brightRequest{{"operation", "export_linear"}, {"path", brightInput}, {"output", brightOutput},
    {"recipe", brightRecipe}, {"size", 128}, {"quality", "high"}};
  const auto brightExport = send(brightRequest);
  TIFF *brightTiff = TIFFOpen(brightOutput.toStdString().c_str(), "r");
  if(!brightTiff) return 1;
  std::uint16_t brightIfdCount = 0;
  toff_t *brightIfds = nullptr;
  if(TIFFGetField(brightTiff, TIFFTAG_SUBIFD, &brightIfdCount, &brightIfds) != 1
     || brightIfdCount != 2 || TIFFSetSubDirectory(brightTiff, brightIfds[0]) != 1)
  { TIFFClose(brightTiff); return 1; }
  std::vector<std::uint16_t> brightSamples(64 * 96 * 3);
  for(int y = 0; y < 96; ++y)
    if(TIFFReadScanline(brightTiff, brightSamples.data() + y * 64 * 3, y) != 1)
    { TIFFClose(brightTiff); return 1; }
  TIFFClose(brightTiff);
  const auto brightGeometry = rawlab::image::rawPreviewGeometry(bright, 128);
  const auto matchesExport = [&](const QJsonObject &response, const QString &field = "png") {
    const auto png = QImage::fromData(QByteArray::fromBase64(response[field].toString().toLatin1()), "PNG")
      .convertToFormat(QImage::Format_RGB888);
    if(png.isNull()) return false;
    for(int y = 0; y < png.height(); ++y) for(int x = 0; x < png.width(); ++x)
    {
      const auto raw = brightGeometry.previewToRaw(x + response["x"].toInt() + .5f, y + response["y"].toInt() + .5f);
      const int offset = (static_cast<int>(raw.second) * 64 + static_cast<int>(raw.first)) * 3;
      for(int c = 0; c < 3; ++c)
      {
        const float value = brightSamples[offset + c] / 65535.0f;
        const int expected = std::lround(255 * (value <= .0031308f ? 12.92f * value : 1.055f * std::pow(value, 1 / 2.4f) - .055f));
        if(std::abs(png.constScanLine(y)[x * 3 + c] - expected) > 1) return false;
      }
    }
    return true;
  };
  if(!matchesExport(brightExport)) { std::cerr << "Export preview highlights differ from Linear RGB DNG\n"; return 1; }
  for(const QString &operation : {QStringLiteral("preview"), QStringLiteral("process"),
      QStringLiteral("recompose"), QStringLiteral("processed_preview")})
  {
    brightRequest["operation"] = operation;
    const auto response = send(brightRequest);
    if(operation == "process") brightRequest["resultId"] = response["resultId"];
    if(!matchesExport(response) || (response.contains("before") && !matchesExport(response, "before")))
    { std::cerr << "Preview highlights differ from Linear RGB DNG: " << operation.toStdString() << '\n'; return 1; }
  }
  brightRequest["operation"] = "detail";
  brightRequest["x"] = 11; brightRequest["y"] = 7; brightRequest["width"] = 24; brightRequest["height"] = 18;
  for(const QString &target : {QStringLiteral("original"), QStringLiteral("processed")})
  {
    brightRequest["target"] = target;
    if(!matchesExport(send(brightRequest))) { std::cerr << "Detail highlights differ from Linear RGB DNG\n"; return 1; }
  }
  if(exportOnly)
  {
    server.closeWriteChannel();
    if(!server.waitForFinished(5000) || server.exitCode() != 0) return 1;
    std::cout << "Packaged CFA and Linear RGB DNG exports passed\n";
    return 0;
  }
  QJsonObject batch{{"operation", "batch_create"},
                    {"sources", QJsonArray{input}},
                    {"outputDirectory", directory.path()},
                    {"recipe", recipe}, {"width", 64}, {"height", 96}};
  const auto created = send(batch);
  if(created["remaining"].toInt() != 1 || created["jobs"].toArray().size() != 1) return 1;
  if(send({{"operation", "batch_cancel"}})["remaining"].toInt() != 1) return 1;
   if(send({{"operation", "batch_retry"}})["jobs"].toArray().at(0).toObject()["state"].toInt(-1) != 0) return 1;
   const auto batchRun = send({{"operation", "batch_run"}});
    if(batchRun["completed"].toInt() != 1 || !QFile::exists(directory.filePath("test with spaces-rawlab.dng"))) {
     std::cerr << "batch run failed: " << QJsonDocument{batchRun}.toJson().constData()
               << QJsonDocument{send({{"operation", "batch_status"}})}.toJson().constData();
     return 1;
    }
   const QString recoveryDirectory = directory.filePath("recovery");
   if(!QDir{}.mkdir(recoveryDirectory)) return 1;
   batch["sources"] = QJsonArray{input, secondInput};
   const QJsonObject secondRecipe{{"layers", QJsonArray{QJsonObject{{"id",0},{"enabled",false}}}},
     {"highlights", false}, {"outputKind", "linear-rgb"}};
   const QString recipesPath = directory.filePath("staged-recipes.json");
   batch["recipesPath"] = recipesPath;
   batch["outputDirectory"] = recoveryDirectory;
   if(send(batch)["error"].toString().isEmpty()) return 1;
   QFile stagedRecipes{recipesPath};
   if(!stagedRecipes.open(QIODevice::WriteOnly) || stagedRecipes.write("{invalid}") < 1) return 1;
   stagedRecipes.close();
   if(send(batch)["error"].toString().isEmpty()) return 1;
   if(!stagedRecipes.open(QIODevice::WriteOnly | QIODevice::Truncate)
      || stagedRecipes.write(QJsonDocument{QJsonArray{recipe}}.toJson()) < 1) return 1;
   stagedRecipes.close();
   if(send(batch)["error"].toString().isEmpty()) return 1;
   if(!stagedRecipes.open(QIODevice::WriteOnly | QIODevice::Truncate)
      || stagedRecipes.write(QJsonDocument{QJsonArray{recipe, secondRecipe}}.toJson()) < 1) return 1;
   stagedRecipes.close();
   const auto recoverableBatch = send(batch);
   if(!QFile::remove(recipesPath)) return 1;
   const QString cancelPath = directory.filePath("cancel-batch");
   server.write(QJsonDocument{QJsonObject{{"operation", "batch_run"}, {"cancelPath", cancelPath}}}.toJson(QJsonDocument::Compact)+'\n');
   server.waitForBytesWritten();
   QJsonObject cancelledRun;
   for(;;)
   {
     while(!server.canReadLine()) if(!server.waitForReadyRead(10000)) return 1;
     const auto response = QJsonDocument::fromJson(server.readLine()).object();
     if(response.contains("progress") && response["stage"].toString() == "Batch checkpoint"
        && response["completed"].toInt() == 1 && !QFileInfo::exists(cancelPath))
     {
       QFile cancelFile{cancelPath};
       if(!cancelFile.open(QIODevice::WriteOnly)) return 1;
     }
     if(!response.contains("progress")) { cancelledRun = response; break; }
   }
   const QString firstOutput = recoveryDirectory + "/test with spaces-rawlab.dng";
   QFile firstFile{firstOutput};
   if(recoverableBatch["snapshot"].toString().isEmpty() || cancelledRun["completed"].toInt() != 1
      || !firstFile.open(QIODevice::ReadOnly)) return 1;
   const QByteArray firstPublished = firstFile.readAll();
   server.kill();
   server.waitForFinished();
   server.start(QString::fromLocal8Bit(argv[1]), {"--serve"});
   if(!server.waitForStarted()) return 1;
   if(send({{"operation", "batch_retry"}})["remaining"].toInt() != 1) return 1;
   const auto resumed = send({{"operation", "batch_run"}});
   firstFile.close();
     if(resumed["completed"].toInt() != 2 || !firstFile.open(QIODevice::ReadOnly)
        || firstFile.readAll() != firstPublished || !QFileInfo::exists(recoveryDirectory + "/second-rawlab.dng")) return 1;
   TIFF *secondExport = TIFFOpen((recoveryDirectory + "/second-rawlab.dng").toStdString().c_str(), "r");
   if(!secondExport) return 1;
   std::uint16_t subIfdCount = 0;
   toff_t *subIfds = nullptr;
   std::uint16_t secondPhotometric = 0;
   const bool hasLinearMain = TIFFGetField(secondExport, TIFFTAG_SUBIFD, &subIfdCount, &subIfds) == 1
     && subIfdCount == 2 && TIFFSetSubDirectory(secondExport, subIfds[0]) == 1;
   if(hasLinearMain) TIFFGetField(secondExport, TIFFTAG_PHOTOMETRIC, &secondPhotometric);
   TIFFClose(secondExport);
   if(secondPhotometric != 34892) return 1;
   const QString externalOutput = directory.filePath("darktable output.dng");
   const QString externalRequest = directory.filePath("darktable-request");
   QFile externalRequestFile{externalRequest};
   if(!externalRequestFile.open(QIODevice::WriteOnly)
      || externalRequestFile.write("rawlab-batch-v1\n" + input.toUtf8().toHex() + '\t' + externalOutput.toUtf8().toHex() + '\n') < 1) return 1;
   externalRequestFile.close();
   const auto externalResult = send({{"operation", "external_batch_run"}, {"requestPath", externalRequest}, {"recipe", recipe}});
   QFile receipt{externalRequest + ".results"};
   if(externalResult["complete"] != true || !QFileInfo::exists(externalOutput)
      || !receipt.open(QIODevice::ReadOnly) || !receipt.readAll().startsWith("rawlab-results-v1\ncomplete\n")) return 1;
   QImage maskImage{1, 1, QImage::Format_Grayscale8};
  maskImage.fill(255);
  QByteArray maskBytes;
  QBuffer maskBuffer{&maskBytes};
  maskBuffer.open(QIODevice::WriteOnly);
  if(!maskImage.save(&maskBuffer, "PNG")) return 1;
  const QString maskPng = QString::fromLatin1(maskBytes.toBase64());
  const QJsonObject richLayer{{"id", 1}, {"name", "Subject"}, {"enabled", true}, {"modelId", "test"},
     {"marks", QJsonArray{
       QJsonObject{{"tool", "Brush"}, {"radius", 0.04}, {"hardness", 0.7},
         {"points", QJsonArray{QJsonArray{0.1, 0.2}, QJsonArray{0.3, 0.4}}}},
       QJsonObject{{"tool", "Gradient"}, {"radius", 0.04}, {"hardness", 1.0},
         {"points", QJsonArray{QJsonArray{0.1, 0.2}, QJsonArray{0.8, 0.9}}}}}},
    {"points", QJsonArray{QJsonArray{0.2, 0.3, true}}}, {"aiMask", maskPng},
    {"categoryMasks", QJsonArray{maskPng, QJsonValue{}, QJsonValue{}}},
    {"categoryStrokes", QJsonArray{QJsonArray{QJsonObject{{"radius", 0.02}, {"hardness", 0.8},
      {"value", 1.0}, {"points", QJsonArray{QJsonArray{0.4, 0.5}}}}}, QJsonArray{}, QJsonArray{}}}};
  const QJsonObject richRecipe{{"layers", QJsonArray{recipe["layers"].toArray().first(), richLayer}},
                               {"highlights", true}};
  QJsonObject recipeSave{{"operation", "recipe_save"}, {"path", input}, {"recipe", richRecipe}, {"width", 64}, {"height", 96}};
  const auto savedRecipe = send(recipeSave);
  if(savedRecipe["saved"] != true) { std::cerr << "recipe save failed: " << QJsonDocument{savedRecipe}.toJson().constData(); return 1; }
   QJsonObject recipeLoad{{"operation", "recipe_load"}, {"path", input}};
  const auto roundTrip = send(recipeLoad)["recipe"].toObject();
  const auto roundTripLayer = roundTrip["layers"].toArray().at(1).toObject();
   if(roundTripLayer["strokes"].toArray().size() < 2 || roundTripLayer["strokes"].toArray().at(1).toObject()["tool"] != "Gradient"
      || roundTripLayer["points"].toArray().isEmpty()
     || roundTripLayer["categoryMasks"].toArray().at(0).toString().isEmpty()
     || roundTripLayer["categoryStrokes"].toArray().at(0).toArray().isEmpty()) {
    std::cerr << "recipe round trip failed: " << QJsonDocument{roundTrip}.toJson().constData(); return 1;
  }
  server.closeWriteChannel();
  if(!server.waitForFinished(5000))return 1;
}
