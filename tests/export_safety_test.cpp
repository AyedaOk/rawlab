#include "BatchModelSnapshot.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/dng/Publish.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>

int main(int argc, char **argv)
{
  QCoreApplication app{argc, argv};
  QTemporaryDir directory;
  if(argc != 2 || !directory.isValid()) return 1;
  qputenv("XDG_DATA_HOME", directory.path().toUtf8());
  const auto read = [](const QString &path) {
    QFile file{path};
    if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"read failed"};
    return file.readAll();
  };
  const auto write = [](const QString &path, const QByteArray &bytes) {
    QFile file{path};
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
      throw std::runtime_error{"write failed"};
  };
  try
  {
    rawlab::raw::RawMetadata metadata;
    metadata.cfa.isBayer = true;
    metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
    metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
    metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
    metadata.whiteLevel = 4095;
    metadata.activeArea = {0, 0, 64, 64};
    const auto input = directory.filePath("input.dng");
    rawlab::dng::TiffDngWriter{}.writeCfaDng(input.toStdString(),
      {64, 64, std::vector<float>(64 * 64, 1024), metadata});
    const auto original = read(input);
    const auto run = [&](const QString &output) {
      QProcess process;
      process.start(QString::fromLocal8Bit(argv[1]), {"process", input, output});
      if(!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit)
        throw std::runtime_error{"CLI did not finish"};
      return process.exitCode();
    };
    const auto existing = directory.filePath("existing.dng");
    write(existing, "keep");
    if(run(input) == 0 || read(input) != original || run(existing) == 0 || read(existing) != "keep") return 1;
    const auto alias = directory.filePath("alias.dng");
    std::filesystem::create_hard_link(input.toStdString(), alias.toStdString());
    if(run(alias) == 0 || read(input) != original) return 1;
    const auto output = directory.filePath("new.dng");
    if(run(output) != 0 || read(output).isEmpty() || read(input) != original) return 1;
    if(!QDir{directory.path()}.entryList({"*.rawlab-*"}, QDir::Files).isEmpty()) return 1;
    const auto pending = directory.filePath("pending.dng");
    write(pending, "finished output");
    try { rawlab::dng::publish(pending.toStdString(), existing.toStdString()); return 1; }
    catch(const std::filesystem::filesystem_error &) {}
    if(read(existing) != "keep" || read(pending) != "finished output") return 1;
    const auto published = directory.filePath("published.dng");
    rawlab::dng::publish(pending.toStdString(), published.toStdString());
    if(QFileInfo::exists(pending) || read(published) != "finished output") return 1;

    const auto model = directory.filePath("later-model.onnx");
    write(model, "first model");
    BatchQueue queue;
    queue.recipe.previewWidth = queue.recipe.previewHeight = 64;
    queue.recipe.globalEnabled = false;
    queue.settings.denoiseModels.push_back({"later", "Later", model, "rawforge", {}, {}, {}});
    queue.settings.sharpenModels.push_back({"sharp", "Sharp", model, {}, {}, {}, {}});
    queue.jobs = {{input, directory.filePath("one.dng"), BatchQueue::State::Pending, {}, {}},
                  {input + ".second", directory.filePath("two.dng"), BatchQueue::State::Pending, {}, {}}};
    queue.requiresRecipes = queue.requiresModels = true;
    BatchQueueStore::save(queue);
    const QJsonObject later{{"layers", QJsonArray{QJsonObject{{"id", 0}, {"modelId", "later"}, {"sharpen", "AI"}}}},
                            {"sharpenModelId", "sharp"}};
    saveBatchRecipeSnapshots(queue, {fromQtRecipe(queue.recipe), later});
    saveBatchModelSnapshot(queue);
    verifyBatchModelSnapshot(queue);
    const auto selected = selectedBatchModels(queue);
    if(!selected.contains("denoise:later") || !selected.contains("sharpen:sharp")) return 1;
    for(const auto &path : {batchRecipeSnapshotPath(queue), batchModelSnapshotPath(queue)})
    {
      const auto saved = read(path);
      if(!QFile::remove(path)) return 1;
      try { (void)loadVerifiedBatchQueue(); return 1; }
      catch(const std::runtime_error &) {}
      write(path, saved);
    }
    if(!loadVerifiedBatchQueue()) return 1;
    const auto checkpoint = BatchQueueStore::directory() + "/current.json";
    auto legacy = QJsonDocument::fromJson(read(checkpoint)).object();
    legacy["version"] = 1;
    legacy.remove("requiresModels"); legacy.remove("requiresRecipes");
    write(checkpoint, QJsonDocument{legacy}.toJson());
    const auto legacyQueue = BatchQueueStore::load();
    if(!legacyQueue || legacyQueue->requiresModels || legacyQueue->requiresRecipes) return 1;
    BatchQueueStore::save(queue);
    write(model, "replacement model");
    try { verifyBatchModelSnapshot(queue); return 1; }
    catch(const std::runtime_error &) {}
  }
  catch(const std::exception &error) { std::cerr << error.what(); return 1; }
  return 0;
}
