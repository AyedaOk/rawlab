#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/image/RawPreview.h"
#include "ProcessingSession.h"
#include "DesktopControl.h"
#include "BatchModelSnapshot.h"
#include "BatchRecipeSnapshot.h"
#include "ExternalBatch.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/LensCorrection.h"
#include <QUuid>
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QLockFile>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#include <iostream>

// Private desktop bridge protocol v1. stdout is one JSON response; diagnostics
// go to stderr. No output paths are accepted and source files are never modified.
static std::uint8_t displayByte(float value)
{
  value = std::clamp(value, 0.0f, 1.0f);
  value = value <= 0.0031308f ? 12.92f * value
                              : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(255.0f * std::clamp(value, 0.0f, 1.0f)));
}

static QImage linearDisplay(rawlab::image::LinearRawPreview preview,
                            const rawlab::raw::RawMetadata &metadata,
                            const rawlab::image::OpticsOptions &corrections)
{
  const auto linear = corrections.chromaticAberration || corrections.lensCorrection
    ? rawlab::image::applyOpticsCorrections(
        {preview.width, preview.height, std::move(preview.pixels)}, metadata, corrections)
    : rawlab::image::LinearRgbImage{preview.width, preview.height, std::move(preview.pixels)};
  QImage display{linear.width, linear.height, QImage::Format_RGB888};
  for(int y = 0; y < linear.height; ++y)
    for(int x = 0; x < linear.width; ++x)
    {
      auto *pixel = display.scanLine(y) + x * 3;
      const std::size_t offset = (static_cast<std::size_t>(y) * linear.width + x) * 3U;
      for(int channel = 0; channel < 3; ++channel) pixel[channel] = displayByte(linear.pixels[offset + channel]);
    }
  return display;
}

static QImage correctedDisplay(const rawlab::raw::RawImage &image,
                               const rawlab::image::OpticsOptions &corrections,
                               const int maxDimension,
                               const rawlab::image::PreviewQuality quality)
{
  return linearDisplay(rawlab::image::renderLinearRawPreview(image, maxDimension, quality),
                       image.metadata(), corrections);
}

static rawlab::dng::JpegPreview jpegPreview(const QImage &display, const int dimension)
{
  const QImage image = display.scaled(dimension, dimension, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
  QByteArray bytes;
  QBuffer buffer{&bytes};
  buffer.open(QIODevice::WriteOnly);
  if(!image.save(&buffer, "JPEG", 90)) throw std::runtime_error{"Cannot encode DNG preview"};
  rawlab::dng::JpegPreview preview;
  preview.width = image.width(); preview.height = image.height();
  preview.data.assign(bytes.begin(), bytes.end());
  return preview;
}

static rawlab::dng::DngPreviews dngPreviews(const rawlab::raw::RawImage &image,
                                             const rawlab::image::OpticsOptions &optics)
{
  const QImage display = (optics.chromaticAberration || optics.lensCorrection)
    ? correctedDisplay(image, optics, 2048, rawlab::image::PreviewQuality::Fast)
    : [&] {
        const auto preview = rawlab::image::renderRawPreview(
          image, 2048, rawlab::image::PreviewQuality::Fast);
        return QImage{preview.image.pixels.data(), preview.image.width, preview.image.height,
                      preview.image.width * 3, QImage::Format_RGB888}.copy();
      }();
  return {jpegPreview(display, 320), jpegPreview(display, 2048)};
}

static QJsonObject render(const rawlab::raw::RawImage &image, int size, const QString &quality,
                            const rawlab::image::OpticsOptions &corrections = {},
                            std::optional<rawlab::image::LinearRawPreview> linearPreview = std::nullopt,
                            const std::function<void()> &checkCancelled = {})
{
    if(size < 128 || size > 4096 || (quality != "fast" && quality != "high"))
      throw std::runtime_error{"Invalid preview size or quality"};
    const bool corrected = corrections.chromaticAberration || corrections.lensCorrection;
    const auto preview = corrected || linearPreview ? rawlab::image::RawPreview{} : rawlab::image::renderRawPreview(image, size,
      quality == "high" ? rawlab::image::PreviewQuality::High : rawlab::image::PreviewQuality::Fast);
    QImage display;
    int width = preview.image.width, height = preview.image.height;
    rawlab::image::PreviewGeometry geometry = preview.geometry;
    if(linearPreview)
    {
      geometry = linearPreview->geometry;
      display = linearDisplay(std::move(*linearPreview), image.metadata(), corrections);
      width = display.width(); height = display.height();
    }
    else if(corrected)
    {
      display = correctedDisplay(image, corrections, size,
        quality == "high" ? rawlab::image::PreviewQuality::High : rawlab::image::PreviewQuality::Fast);
      width = display.width(); height = display.height();
      geometry.rawArea = image.metadata().activeArea;
      if(geometry.rawArea.width <= 0 || geometry.rawArea.height <= 0)
        geometry.rawArea = {0, 0, image.width(), image.height()};
      geometry.previewWidth = width; geometry.previewHeight = height; geometry.flip = image.metadata().flip;
    }
    else display = QImage{preview.image.pixels.data(), width, height, width * 3, QImage::Format_RGB888};
    if(checkCancelled) checkCancelled();
    QByteArray encoded;
    QBuffer buffer{&encoded};
    if(!buffer.open(QIODevice::WriteOnly) || !display.save(&buffer, "PNG"))
      throw std::runtime_error{"Could not encode RAW preview"};
    const auto &m = image.metadata();
    const auto optics = rawlab::image::opticsStatus(m, image.width(), image.height());
    const auto number = [](std::optional<double> value) -> QJsonValue {
      return value && std::isfinite(*value) ? QJsonValue{*value} : QJsonValue{QJsonValue::Null};
    };
    const QJsonObject metadata{{"make", QString::fromStdString(m.make)}, {"model", QString::fromStdString(m.model)},
      {"lens", QString::fromStdString(m.lensModel)}, {"iso", number(m.iso)}, {"aperture", number(m.aperture)},
      {"exposureSeconds", number(m.exposureTimeSeconds)}, {"exposureBiasEv", number(m.exposureBiasEv)},
      {"focalLengthMm", number(m.focalLengthMm)}, {"rawWidth", image.width()}, {"rawHeight", image.height()},
      {"optics", QJsonObject{{"lensfunAvailable", optics.lensfunAvailable}, {"profileMatched", optics.profileMatched},
        {"chromaticAberrationAvailable", optics.chromaticAberrationAvailable},
        {"lensCorrectionAvailable", optics.lensCorrectionAvailable},
        {"message", QString::fromStdString(optics.message)}}}};
    const bool rotated = geometry.flip == 5 || geometry.flip == 6;
    const QJsonObject response{{"version", 1}, {"width", width}, {"height", height},
      {"sourceWidth", rotated ? geometry.rawArea.height : geometry.rawArea.width},
      {"sourceHeight", rotated ? geometry.rawArea.width : geometry.rawArea.height},
      {"quality", quality}, {"metadata", metadata}, {"png", QString::fromLatin1(encoded.toBase64())}};
    return response;
}

static rawlab::image::OpticsOptions opticsFromRecipe(const QJsonObject &recipe)
{
  return {recipe["chromaticAberration"].toBool(false), recipe["lensCorrection"].toBool(false)};
}

static QString processingRecipeKey(QJsonObject recipe)
{
  recipe.remove("chromaticAberration");
  recipe.remove("lensCorrection");
  return QString::fromUtf8(QJsonDocument{recipe}.toJson(QJsonDocument::Compact));
}

static void reply(const QJsonObject &response)
{
  std::cout << QJsonDocument{response}.toJson(QJsonDocument::Compact).constData() << std::endl;
}

#ifndef _WIN32
#include <sys/resource.h>
#endif

int main(int argc, char **argv)
{
#ifndef _WIN32
  // ONNX's symbolizer reserves descriptors above 1024; a 1024 soft limit causes diagnostic spam.
  rlimit descriptors{};
  if(getrlimit(RLIMIT_NOFILE, &descriptors) == 0 && descriptors.rlim_cur <= 1024 && descriptors.rlim_max > 1024)
  {
    descriptors.rlim_cur = std::min<rlim_t>(descriptors.rlim_max, 4096);
    setrlimit(RLIMIT_NOFILE, &descriptors);
  }
#endif
  QCoreApplication application{argc, argv};
  application.setOrganizationName("RawLab"); application.setApplicationName("RawLab");
  const auto args = application.arguments();
  if(args.size() == 2 && args[1] == "--serve")
  {
    ProcessingSession session;
    std::string line;
    while(std::getline(std::cin, line))
    {
      try
      {
        if(line.size() > 16 * 1024 * 1024) throw std::runtime_error{"Request too large"};
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parseError);
        if(parseError.error != QJsonParseError::NoError || !document.isObject()) throw std::runtime_error{"Invalid JSON request"};
        const auto request = document.object();
        const auto operation = request["operation"].toString();
        if(operation == "models") { reply({{"version", 1}, {"models", ProcessingSession::models()}}); continue; }
        if(operation.startsWith("settings")||operation.startsWith("model_")||operation=="catalog"||operation.startsWith("library_")||operation.startsWith("preset_")||operation.startsWith("recipe_")||(operation.startsWith("batch_") && operation != "batch_run"))
        {auto result=desktopControl(request,reply);result["version"]=1;reply(result);continue;}
         const auto checkCancelled = [&request] {
          if(QFileInfo::exists(request["cancelPath"].toString())) throw std::runtime_error{"Processing cancelled"};
         };
         if(operation == "external_batch_run")
         {
           const QString requestPath = request["requestPath"].toString();
           BatchQueue queue;
           queue.jobs = ExternalBatch::read(requestPath);
           queue.receiptPath = requestPath + ".results";
           const auto recipe = request["recipe"].toObject();
           ExternalBatch::writeReceipt(queue);
           for(auto &job : queue.jobs)
           {
             const bool cancelled = QFileInfo::exists(request["cancelPath"].toString())
               || QFileInfo::exists(queue.receiptPath + ".cancel");
             if(cancelled) { job.state = BatchQueue::State::Cancelled; break; }
             job.state = BatchQueue::State::Running;
             try
             {
               if(QFileInfo::exists(job.output) || QFileInfo::exists(job.output + ".xmp"))
                 throw std::runtime_error{"Darktable output is already reserved"};
               const auto image = session.process(job.source, recipe, [&checkCancelled, &queue](int done, int total) {
                 checkCancelled();
                 if(QFileInfo::exists(queue.receiptPath + ".cancel")) throw std::runtime_error{"Processing cancelled"};
                 reply({{"progress", done}, {"total", total}, {"stage", "Processing Darktable batch"}});
               });
               const QString temporary = job.output + ".rawlab-" + QUuid::createUuid().toString(QUuid::Id128);
               try
               {
                 if(recipe["outputKind"].toString("cfa") == "linear-rgb")
                  {
                    const auto optics = opticsFromRecipe(recipe);
                    const auto previews = dngPreviews(*image, optics);
                    rawlab::dng::TiffDngWriter{}.writeLinearRgbDng(
                      temporary.toStdString(), *image, optics, &previews);
                  }
                 else rawlab::dng::TiffDngWriter{}.writeCfaDng(temporary.toStdString(), *image);
                 std::error_code linkError;
                 std::filesystem::create_hard_link(temporary.toStdString(), job.output.toStdString(), linkError);
                 if(linkError) throw std::runtime_error{"Could not publish Darktable output"};
                 QFile::remove(temporary);
               }
               catch(...) { QFile::remove(temporary); throw; }
               job.state = BatchQueue::State::Completed;
             }
             catch(const std::exception &error)
             {
               job.state = (QFileInfo::exists(request["cancelPath"].toString())
                 || QFileInfo::exists(queue.receiptPath + ".cancel"))
                 ? BatchQueue::State::Cancelled : BatchQueue::State::Failed;
               job.message = QString::fromUtf8(error.what());
             }
             ExternalBatch::writeReceipt(queue);
             reply({{"progress", queue.completed()}, {"total", static_cast<int>(queue.jobs.size())},
               {"stage", "Darktable batch"}});
             if(job.state == BatchQueue::State::Cancelled) break;
           }
           ExternalBatch::writeReceipt(queue);
           reply({{"version", 1}, {"completed", queue.completed()},
             {"total", static_cast<int>(queue.jobs.size())},
             {"complete", queue.completed() == static_cast<int>(queue.jobs.size())}});
           continue;
         }
         const QString path = request["path"].toString();
         if(operation == "batch_run")
         {
           QLockFile batchLock{BatchQueueStore::directory() + "/queue.lock"};
           batchLock.setStaleLockTime(0);
           if(!batchLock.tryLock(0)) throw std::runtime_error{"Batch queue is open in another RawLab process"};
           auto queue = loadVerifiedBatchQueue();
          if(!queue) throw std::runtime_error{"No recoverable batch exists"};
           const auto legacyRecipe = fromQtRecipe(queue->recipe);
           int completed = queue->completed();
           const auto reportBatch = [&](const QString &stage, const int progress = -1, const int total = -1) {
             QJsonArray jobs;
             for(const auto &item : queue->jobs)
               jobs.push_back(QJsonObject{{"source", item.source}, {"output", item.output},
                 {"state", static_cast<int>(item.state)}, {"message", item.message},
                 {"completed", item.state == BatchQueue::State::Completed}});
             reply({{"progress", progress < 0 ? queue->completed() : progress},
               {"total", total < 0 ? static_cast<int>(queue->jobs.size()) : total},
               {"stage", stage}, {"snapshot", queue->snapshotId}, {"jobs", jobs},
               {"completed", queue->completed()}, {"remaining", queue->remaining()}});
           };
          for(auto &job : queue->jobs)
          {
            if(!job.retryable()) continue;
            if(QFileInfo::exists(request["cancelPath"].toString()))
            {
              for(auto &pending : queue->jobs)
                if(pending.retryable()) pending.state = BatchQueue::State::Cancelled;
              BatchQueueStore::save(*queue);
              break;
            }
             job.state = BatchQueue::State::Running;
             job.message.clear();
             BatchQueueStore::save(*queue);
             reportBatch("Processing batch");
            bool cancelled = false;
            try
            {
              const QString jobName = QFileInfo{job.source}.fileName();
              const auto recipe = loadBatchRecipeSnapshot(*queue, job.source, legacyRecipe);
               const auto processedImage = session.process(job.source, recipe, [&checkCancelled, &reportBatch, &jobName](int done, int total) {
                  checkCancelled(); reportBatch(QStringLiteral("Processing %1").arg(jobName), done, total);
                }, &queue->settings, true, [&checkCancelled, &reportBatch, &jobName](const QString &stage, int done, int total) {
                  checkCancelled(); reportBatch(QStringLiteral("%1: %2").arg(stage, jobName), done, total);
                });
              const QString temporary = job.output + ".rawlab-" + QUuid::createUuid().toString(QUuid::Id128);
              if(QFileInfo::exists(job.output)) throw std::runtime_error{"reserved output already exists; it will not be overwritten"};
              try
              {
                  if(recipe["outputKind"].toString("cfa") == "linear-rgb")
                  {
                    const auto optics = opticsFromRecipe(recipe);
                    const auto previews = dngPreviews(*processedImage, optics);
                    rawlab::dng::TiffDngWriter{}.writeLinearRgbDng(
                      temporary.toStdString(), *processedImage, optics, &previews);
                  }
                  else rawlab::dng::TiffDngWriter{}.writeCfaDng(temporary.toStdString(), *processedImage);
                 checkCancelled();
                 job.publishedHash = BatchQueueStore::hashFile(temporary);
                 BatchQueueStore::save(*queue);
                 std::error_code linkError;
                std::filesystem::create_hard_link(temporary.toStdString(), job.output.toStdString(), linkError);
                if(linkError) throw std::runtime_error{"Could not publish batch output"};
                QFile::remove(temporary);
              }
              catch(...) { QFile::remove(temporary); throw; }
              job.state = BatchQueue::State::Completed;
              ++completed;
            }
            catch(const std::exception &error)
            {
              job.state = QFileInfo::exists(request["cancelPath"].toString())
                ? BatchQueue::State::Cancelled : BatchQueue::State::Failed;
              cancelled = job.state == BatchQueue::State::Cancelled;
              job.message = QString::fromUtf8(error.what());
            }
             BatchQueueStore::save(*queue);
             reportBatch("Batch checkpoint");
            if(cancelled)
            {
              for(auto &pending : queue->jobs)
                if(pending.retryable()) pending.state = BatchQueue::State::Cancelled;
              BatchQueueStore::save(*queue);
              break;
            }
          }
          QJsonArray jobs;
          for(const auto &job : queue->jobs)
            jobs.push_back(QJsonObject{{"source", job.source}, {"output", job.output},
              {"state", static_cast<int>(job.state)}, {"message", job.message},
              {"completed", job.state == BatchQueue::State::Completed}});
          QJsonObject status{{"snapshot", queue->snapshotId}, {"jobs", jobs},
            {"completed", queue->completed()}, {"remaining", queue->remaining()}, {"version", 1}};
          reply(status);
          continue;
        }
        checkCancelled();
         const auto sourceKey = ProcessingSession::sourceFingerprint(path);
         const auto original = session.source(path);
           if(operation == "detail")
           {
             auto recipe = request["recipe"].toObject();
             if(request["target"].toString("original") == "processed")
             {
               const QString resultId = request["resultId"].toString();
               (void)session.processed(path, resultId);
             }
             else recipe["layers"] = QJsonArray{QJsonObject{{"enabled", false}}};
             const auto corrections = recipe["outputKind"].toString("cfa") == "linear-rgb"
              ? opticsFromRecipe(recipe) : rawlab::image::OpticsOptions{};
             const QRect region{request["x"].toInt(), request["y"].toInt(), request["width"].toInt(), request["height"].toInt()};
              auto linear = session.detail(path, recipe, region, corrections, checkCancelled,
                request["quality"].toString("high") == "high"
                  ? rawlab::image::PreviewQuality::High : rawlab::image::PreviewQuality::Fast);
             const auto geometry = linear.geometry;
             const auto display = linearDisplay(std::move(linear), original->metadata(), {});
             checkCancelled();
             QByteArray encoded;
             QBuffer buffer{&encoded};
             if(!buffer.open(QIODevice::WriteOnly) || !display.save(&buffer, "PNG"))
               throw std::runtime_error{"Could not encode detail preview"};
             checkCancelled();
             reply({{"version", 1}, {"x", region.x()}, {"y", region.y()},
               {"width", display.width()}, {"height", display.height()},
               {"sourceWidth", geometry.previewWidth}, {"sourceHeight", geometry.previewHeight},
               {"png", QString::fromLatin1(encoded.toBase64())}});
           continue;
         }
        if(operation == "depth")
        {
          auto result = session.depth(path, checkCancelled);
          checkCancelled();
          result["version"] = 1;
          reply(result); continue;
        }
        if(operation == "segment")
        {
          const auto mask = session.segment(path, request["points"].toArray());
          checkCancelled();
          QImage image{mask.width(),mask.height(),QImage::Format_Grayscale8};
          for(int y=0;y<mask.height();++y) for(int x=0;x<mask.width();++x) image.scanLine(y)[x]=static_cast<unsigned char>(std::lround(std::clamp(mask.sampleNearest(x,y),0.0f,1.0f)*255));
          QByteArray bytes; QBuffer buffer{&bytes}; buffer.open(QIODevice::WriteOnly);
          if(!image.save(&buffer,"PNG")) throw std::runtime_error{"Cannot encode SAM mask"};
          reply({{"version",1},{"mask",QString::fromLatin1(bytes.toBase64())}}); continue;
        }
         auto processed = original;
          const bool rememberResult = operation == "process";
          if(operation == "processed_preview")
          {
            (void)session.processed(path, request["resultId"].toString());
            processed = session.process(path, request["recipe"].toObject(),
              [&](int, int) { checkCancelled(); }, nullptr, false,
              [&](const QString &, int, int) { checkCancelled(); });
          }
         else if(operation == "recompose")
           processed = session.processed(path, request["resultId"].toString());
         else if(operation != "preview")
        {
           if(operation != "process" && operation != "recompose" && operation != "export" && operation != "export_linear" && operation != "processed_preview")
              throw std::runtime_error{"Unknown engine operation"};
            const auto progress = [&checkCancelled](int done, int total) {checkCancelled(); reply({{"progress", done}, {"total", total}});};
             processed = session.process(path, request["recipe"].toObject(), progress, nullptr, true,
              [&checkCancelled](const QString &stage, int done, int total) {
                checkCancelled(); reply({{"progress", done}, {"total", total}, {"stage", stage}});
              });
          }
          if(operation == "process" || operation == "recompose")
            reply({{"progress", 0}, {"total", 0}, {"stage", "Rendering preview"}});
           const auto recipe = request["recipe"].toObject();
           const auto corrections = recipe["outputKind"].toString("cfa") == "linear-rgb"
              ? opticsFromRecipe(recipe) : rawlab::image::OpticsOptions{};
           QString resultId = request["resultId"].toString();
           if(rememberResult) resultId = session.rememberProcessed(
             path, processed, processingRecipeKey(recipe), sourceKey);
           const int previewSize = request["size"].toInt(2048);
           const QString previewQuality = request["quality"].toString("high");
           const auto quality = previewQuality == "high"
             ? rawlab::image::PreviewQuality::High : rawlab::image::PreviewQuality::Fast;
           const bool useComposedPreview = operation == "recompose";
           auto linear = useComposedPreview
             ? std::optional<rawlab::image::LinearRawPreview>{session.preview(
                  path, recipe, resultId, *processed, previewSize, quality, checkCancelled)}
             : std::nullopt;
            checkCancelled();
            QJsonObject response = render(*processed, previewSize, previewQuality, corrections, std::move(linear), checkCancelled);
            if(operation == "process" || operation == "processed_preview")
            {
              auto before = session.preview(path,
                QJsonObject{{"layers", QJsonArray{QJsonObject{{"enabled", false}}}}},
                 QStringLiteral("original"), *original, previewSize, quality, checkCancelled);
              response["before"] = render(*original, previewSize, previewQuality,
                                          corrections, std::move(before), checkCancelled)["png"];
            }
         checkCancelled();
          if(rememberResult || operation == "recompose") response["resultId"] = resultId;
         if(operation == "export" || operation == "export_linear")
        {
          const QString destination = request["output"].toString();
          if(destination.isEmpty() || QFileInfo::exists(destination)) throw std::runtime_error{"Choose a new DNG path; existing files are not overwritten"};
          const auto temporary = destination + ".rawlab-" + QUuid::createUuid().toString(QUuid::Id128);
          try
          {
            const auto display = QImage::fromData(QByteArray::fromBase64(response["png"].toString().toLatin1()), "PNG");
             const rawlab::dng::DngPreviews previews{
               jpegPreview(display, 320), jpegPreview(display, 2048)};
             if(operation == "export_linear")
                rawlab::dng::TiffDngWriter{}.writeLinearRgbDng(temporary.toStdString(), *processed,
                  opticsFromRecipe(request["recipe"].toObject()), &previews);
             else
             {
               rawlab::dng::TiffDngWriter{}.writeCfaDng(temporary.toStdString(), *processed, &previews);
            }
            checkCancelled();
            std::filesystem::create_hard_link(temporary.toStdString(), destination.toStdString());
            std::filesystem::remove(temporary.toStdString());
          }
          catch(...) { std::error_code ec; std::filesystem::remove(temporary.toStdString(), ec); throw; }
          response["output"] = destination;
        }
        reply(response);
      }
      catch(const std::exception &error) { reply({{"error", QString::fromUtf8(error.what())}}); }
    }
    return 0;
  }
  try
  {
    if(args.size() != 4) throw std::runtime_error{"Usage: rawlab-preview INPUT MAX_DIMENSION fast|high"};
    bool valid = false; const int size = args[2].toInt(&valid);
    if(!valid) throw std::runtime_error{"Invalid preview size"};
    const auto image = rawlab::raw::LibRawDecoder{}.decodeRawImage(args[1].toStdString());
    reply(render(image, size, args[3]));
    return 0;
  }
  catch(const std::exception &error) { QTextStream{stderr} << "RAW preview: " << error.what() << '\n'; return 1; }
}
