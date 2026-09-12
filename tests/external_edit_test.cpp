#include "PictureRecipe.h"
#include "BatchQueueStore.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/raw/LibRawDecoder.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QLockFile>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
  QCoreApplication application{argc, argv};
  if(argc != 2) return 1;
  application.setOrganizationName("RawLab");
  application.setApplicationName("RawLab");
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;
  qputenv("XDG_DATA_HOME", directory.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
  qputenv("XDG_CACHE_HOME", directory.path().toUtf8());
  qputenv("QT_QPA_PLATFORM", "offscreen");
  rawlab::raw::RawMetadata metadata;
  metadata.make = "RawLab";
  metadata.model = "Workflow Test";
  metadata.colorMatrix1 = {0.7f, -0.1f, 0.2f, -0.2f, 1.1f, 0.1f, 0.05f, -0.3f, 0.9f};
  metadata.asShotNeutral = {0.5f, 1.0f, 0.7f};
  metadata.cfa.isBayer = true;
  metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
  metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
  metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
  metadata.whiteLevel = 4095;
  metadata.activeArea = {0, 0, 64, 64};
  metadata.exposureBiasEv = -1;
  const QString input = directory.filePath("input with spaces.dng");
  const QString first = directory.filePath("first.dng");
  const QString output = directory.filePath("output.dng");
  rawlab::dng::TiffDngWriter{}.writeCfaDng(input.toStdString(),
    {64, 64, std::vector<float>(4096, 1024), metadata});
  metadata.exposureBiasEv = 0.5;
  rawlab::dng::TiffDngWriter{}.writeCfaDng(first.toStdString(),
    {64, 64, std::vector<float>(4096, 1024), metadata});
  const auto decodedMetadata = rawlab::raw::LibRawDecoder{}.decodeRawImage(input.toStdString()).metadata();
  if(!decodedMetadata.colorMatrix1Valid || !decodedMetadata.asShotNeutralValid)
  {
    std::cerr << "Synthetic DNG color metadata was not preserved\n";
    return 1;
  }
  const auto run = [&](int expectedCode, const QString &destination) {
    QProcess process;
    process.start(QString::fromLocal8Bit(argv[1]), {"--external-edit", input,
                   "--output", destination, "--apply-settings-from", first});
    if(!process.waitForStarted() || !process.waitForFinished(15000))
    {
      process.kill();
      process.waitForFinished();
      std::cerr << "External edit hung (possibly waiting for a dialog)\n";
      return false;
    }
    if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != expectedCode)
    {
      std::cerr << "Unexpected external-edit exit: " << process.exitCode() << '\n'
                << process.readAllStandardError().constData();
      return false;
    }
    return true;
  };
  // A missing recipe must fail promptly, without an editor/dialog or output.
  if(!run(4, output) || QFileInfo::exists(output)) return 1;
  if(PictureRecipeStore{}.load(input, 32, 32)) return 1;
  PictureRecipe recipe;
  recipe.previewWidth = recipe.previewHeight = 32;
  recipe.globalEnabled = false;
  PictureRecipeStore store;
  store.save(first, recipe);
  if(!run(0, output) || !QFileInfo::exists(output)) return 1;
  const auto actual = rawlab::raw::LibRawDecoder{}.readMetadata(output.toStdString()).metadata;
  if(actual.exposureBiasEv != std::optional<double>{-1}) return 1;
  // Existing output must never be replaced.
  if(!run(2, output)) return 1;
  recipe.globalEnabled = true;
  recipe.globalModelId = "missing-model";
  store.save(first, recipe);
  const QString failedOutput = directory.filePath("failed.dng");
  if(!run(4, failedOutput) || QFileInfo::exists(failedOutput)) return 1;

  const QString request = directory.filePath("darktable batch.txt");
  const QString batchA = directory.filePath("batch A.dng");
  const QString batchB = directory.filePath("batch B.dng");
  QFile manifest{request};
  if(!manifest.open(QIODevice::WriteOnly)) return 1;
  manifest.write("rawlab-batch-v1\n" + input.toUtf8().toHex() + '\t' + batchA.toUtf8().toHex() + '\n'
                 + first.toUtf8().toHex() + '\t' + batchB.toUtf8().toHex() + '\n');
  manifest.close();
  BatchQueue queue;
  queue.jobs = ExternalBatch::read(request);
  queue.recipe = recipe;
  queue.recipe.globalEnabled = false;
  queue.receiptPath = request + ".results";
  const auto runBatch = [&](int expected) {
    QProcess process;
    process.start(QString::fromLocal8Bit(argv[1]), {"--external-batch", request});
    if(!process.waitForStarted() || !process.waitForFinished(15000))
    { process.kill(); process.waitForFinished(); return false; }
    if(process.exitStatus() != QProcess::NormalExit || process.exitCode() != expected)
    { std::cerr << process.readAllStandardError().constData(); return false; }
    return true;
  };
  QFile cancel{queue.receiptPath + ".cancel"};
  if(!cancel.open(QIODevice::WriteOnly)) return 1;
  cancel.close();
  // New request opens the first editor; cancellation there must not export.
  if(!runBatch(3) || QFileInfo::exists(batchA)) return 1;
  BatchQueueStore::save(queue);
  // An unrelated saved queue and another owner's lock must not block Lua.
  QLockFile lock{BatchQueueStore::directory() + "/queue.lock"};
  if(!lock.tryLock()) return 1;
  QFile checkpoint{BatchQueueStore::directory() + "/current.json"};
  if(!checkpoint.open(QIODevice::ReadOnly)) return 1;
  const auto originalCheckpoint = checkpoint.readAll();
  checkpoint.close();
  if(!runBatch(3) || QFileInfo::exists(batchA)) return 1;
  cancel.remove();
  if(!checkpoint.open(QIODevice::ReadOnly) || checkpoint.readAll() != originalCheckpoint) return 1;
}
