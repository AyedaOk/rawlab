#include "MainWindow.h"
#include "PreviewWidget.h"
#include "BatchQueueStore.h"
#include "rawlab/dng/DngWriter.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{
  if(!condition) throw std::runtime_error{message};
}

struct MainWindowTestAccess
{
  static void checkRestart(MainWindow &window)
  {
    require(window.batchQueue_ && window.batchQueue_->remaining() == 1,
            "New window did not recover unfinished queue");
    require(window.resumeBatchAction_->isEnabled(), "Recovered queue cannot resume from the library");
    require(!window.source_, "Recovery should not require an open image");
  }
  static void run(MainWindow &window, const QString &modelPath)
  {
    rawlab::raw::RawMetadata metadata;
    metadata.cfa.isBayer = true;
    metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
    metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
    metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
    metadata.activeArea = {0, 0, 64, 64};
    metadata.whiteLevel = 4095;
    window.source_ = std::make_shared<rawlab::raw::RawImage>(
      64, 64, std::vector<float>(4096, 1024), metadata);
    window.originalPreview_ = rawlab::image::renderRawPreview(*window.source_, 64);
    window.composed_ = window.source_;
    window.appSettings_.denoiseModels = {{"one", "One", modelPath}, {"two", "Two", modelPath + "2"}};
    window.appSettings_.sharpenModels = {{"wrong", "Wrong", modelPath + "missing"},
                                        {"saved", "Saved", modelPath}};
    window.recipeSharpenModelId_ = "saved";
    require(window.sharpenModelPath() == modelPath, "Saved sharpening model changed when list order changed");
    window.recipeSharpenModelId_ = "missing";
    require(window.sharpenModelPath().isEmpty(), "Missing sharpening model silently fell back");
    window.recipeSharpenModelId_.clear();
    window.globalModelId_ = "one";
    window.refreshModelCombo();
    window.denoisedCache_[modelPath] = window.source_;
    window.compositionValid_ = true;
    MainWindow::MaskLayer mask;
    mask.id = 1;
    mask.modelId = "two";
    window.layers_.push_back(mask);
    auto *button = window.previewWidget_->findChild<QPushButton *>();
    require(button != nullptr, "Update button missing");
    window.refreshModelRunButton();
    require(!button->isHidden(), "Update must be shown for an uncached unselected mask");
    window.layers_[0].enabled = false;
    window.refreshModelRunButton();
    require(button->isHidden(), "Disabled masks must not request an update");
    window.layers_.clear();

    // Even when the chosen preset only changes blend and its model is cached,
    // selecting it must not compose or permit export before Update is clicked.
    PictureRecipe recipe;
    recipe.previewWidth = recipe.previewHeight = 32;
    recipe.globalModelId = "one";
    recipe.globalBlend = 0.4f;
    window.recipeStore_.savePreset("Deferred", recipe);
    window.refreshPresetCombo();
    window.presetCombo_->setCurrentIndex(window.presetCombo_->findData("Deferred"));
    require(window.presetAwaitingUpdate_, "Preset did not wait for Update");
    require(!window.compositionValid_, "Preset left a stale export valid");
    window.startComposition();
    require(!window.composeWatcher_.isRunning(), "Preset composed before Update");
    require(!window.exportAction_->isEnabled(), "Stale export is enabled");
    require(!button->isHidden(), "Cached preset still requires an Update button");
    button->click();
    QElapsedTimer timer;
    timer.start();
    while(!window.compositionValid_ && timer.elapsed() < 5000)
    {
      QApplication::processEvents();
      QThread::msleep(5);
    }
    require(window.compositionValid_, "Update did not compose the cached recipe");
    require(!window.presetAwaitingUpdate_, "Update did not release the preset");
    require(button->isHidden(), "Update remained visible after composition completed");
    window.highQualityAction_->setChecked(true);
    timer.restart();
    while(!window.compositionValid_ && timer.elapsed() < 5000)
    { QApplication::processEvents(); QThread::msleep(5); }
    require(window.compositionValid_ && !window.originalPreviewCache_[1].isNull(), "HQ original not cached");
    const auto cachedBefore = window.originalPreviewCache_[1].cacheKey();
    window.scheduleComposition();
    timer.restart();
    while(!window.compositionValid_ && timer.elapsed() < 5000)
    { QApplication::processEvents(); QThread::msleep(5); }
    require(window.compositionValid_ && window.originalPreviewCache_[1].cacheKey() == cachedBefore,
            "Unchanged HQ original was rendered again");
    window.undoAction_->trigger();
    require(window.globalBlend_ == 1.0f, "Undo preset did not restore original blend");
    require(window.presetAwaitingUpdate_, "Undo should wait for Update");
    window.redoAction_->trigger();
    require(window.globalBlend_ == 0.4f, "Redo preset did not restore preset blend");
    const auto count = window.undoEdits_.size();
    window.blendSlider_->setSliderDown(true);
    window.blendSlider_->setValue(50);
    window.blendSlider_->setValue(60);
    window.blendSlider_->setSliderDown(false);
    require(window.undoEdits_.size() == count + 1, "Slider drag should be one undo step");
    window.undoAction_->trigger();
    require(window.globalBlend_ == 0.4f, "Undo drag did not restore start value");
    window.blendSlider_->setValue(70);
    require(window.redoEdits_.empty(), "New edit must discard redo history");

    window.addMaskLayer();
    const int firstId = window.layers_.front().id;
    window.undoAction_->trigger();
    require(window.layers_.empty(), "Undo add did not remove layer");
    window.redoAction_->trigger();
    require(window.layers_.size() == 1 && window.layers_[0].id == firstId,
            "Redo add lost layer identity");
    window.layerList_->item(1)->setText("Subject");
    window.undoAction_->trigger();
    require(window.layers_[0].name != "Subject", "Undo rename failed");
    window.redoAction_->trigger();
    require(window.layers_[0].name == "Subject", "Redo rename failed");

    window.beginMaskEdit();
    const auto strokeCount = window.undoEdits_.size();
    window.previewWidget_->brushStrokeStarted({8, 8}, true);
    window.previewWidget_->brushStrokeContinued({8, 8}, {12, 12});
    require(!window.undoAction_->isEnabled(), "Undo allowed during unfinished stroke");
    window.previewWidget_->brushStrokeFinished();
    require(window.undoEdits_.size() == strokeCount + 1, "Stroke created multiple history entries");
    const auto painted = window.layers_[0].mask->values();
    window.undoAction_->trigger();
    require(window.layers_[0].categories[0].brushStrokes.empty(), "Undo stroke failed");
    window.redoAction_->trigger();
    require(window.layers_[0].mask->values() == painted, "Redo stroke changed mask pixels");
    window.beginMaskEdit();
    window.previewWidget_->brushStrokeStarted({8, 8}, false);
    window.previewWidget_->brushStrokeFinished();
    const auto erased = window.layers_[0].mask->values();
    require(erased != painted, "Erase fixture did not change mask");
    window.undoAction_->trigger();
    require(window.layers_[0].mask->values() == painted, "Undo erase failed");
    window.redoAction_->trigger();
    require(window.layers_[0].mask->values() == erased, "Redo erase failed");
    window.undoAction_->trigger();
    window.previewWidget_->shapeRequested({2, 2}, {20, 20}, false, true);
    const auto ellipse = window.layers_[0].mask->values();
    window.undoAction_->trigger();
    require(window.layers_[0].mask->values() == painted, "Undo ellipse lost painted mask");
    window.redoAction_->trigger();
    require(window.layers_[0].mask->values() == ellipse, "Redo ellipse changed pixels");
    window.clearPointsAction_->trigger();
    window.undoAction_->trigger();
    require(window.layers_[0].mask->values() == ellipse, "Undo clear did not restore mask");
    window.deleteSelectedLayer();
    require(window.layers_.empty(), "Delete layer failed");
    window.undoAction_->trigger();
    require(window.layers_.size() == 1 && window.layers_[0].mask->values() == ellipse,
            "Undo delete did not recover the complete mask");

    window.addMaskLayer();
    const int secondId = window.layers_[0].id;
    require(window.layerList_->model()->moveRow({}, 1, {}, 3), "Could not reorder layer fixture");
    require(window.layers_[0].id == firstId, "Layer order did not synchronize");
    window.undoAction_->trigger();
    require(window.layers_[0].id == secondId, "Undo reorder failed");
    window.redoAction_->trigger();
    require(window.layers_[0].id == firstId, "Redo reorder failed");

    // Exercise the real queue worker with synthetic DNGs, without ONNX models.
    QTemporaryDir batchDirectory;
    require(batchDirectory.isValid(), "Cannot create batch fixture directory");
    const QString good = batchDirectory.filePath("good.dng");
    const QString missing = batchDirectory.filePath("missing.dng");
    metadata.make = "RawLab";
    metadata.model = "Queue test";
    metadata.colorMatrix1 = {0.7f, -0.1f, 0.2f, -0.2f, 1.1f, 0.1f, 0.05f, -0.3f, 0.9f};
    metadata.asShotNeutral = {0.5f, 1.0f, 0.7f};
    const rawlab::raw::RawImage batchImage{64, 64, std::vector<float>(4096, 1024), metadata};
    rawlab::dng::TiffDngWriter{}.writeCfaDng(good.toStdString(), batchImage);
    PictureRecipe batchRecipe;
    batchRecipe.previewWidth = batchRecipe.previewHeight = 32;
    batchRecipe.globalEnabled = false;
    window.batchQueue_ = BatchQueue::create({good, missing, good}, batchDirectory.path(), batchRecipe, {});
    require(window.batchQueue_->jobs.size() == 2, "Queue failed to deduplicate input");
    const QString firstOutput = window.batchQueue_->jobs[0].output;
    const QString secondOutput = window.batchQueue_->jobs[1].output;

    QTimer dismissSummary;
    QObject::connect(&dismissSummary, &QTimer::timeout, [] {
      for(QWidget *widget : QApplication::topLevelWidgets())
        if(auto *box = qobject_cast<QMessageBox *>(widget)) box->reject();
    });
    dismissSummary.start(10);
    const auto waitForBatch = [&window] {
      QElapsedTimer timer;
      timer.start();
      while(window.batchCancelled_ && timer.elapsed() < 15000)
      {
        QApplication::processEvents();
        QThread::msleep(5);
      }
      require(!window.batchCancelled_, "Batch did not finish");
    };
    window.startBatchQueue();
    window.batchCancelled_->store(true);
    waitForBatch();
    require(window.batchQueue_->remaining() == 2 && window.batchQueue_->completed() == 0,
            "Cancelled queue lost unfinished jobs");
    auto recovered = BatchQueueStore::load();
    require(recovered && recovered->remaining() == 2 && !recovered->recipe.globalEnabled,
            "Saved cancelled queue did not recover its recipe");
    window.batchQueue_ = std::move(recovered);
    window.startBatchQueue();
    waitForBatch();
    require(window.batchQueue_->completed() == 1 && window.batchQueue_->remaining() == 1,
            "Queue did not retain failed job");
    require(window.batchQueue_->jobs[1].state == BatchQueue::State::Failed,
            "Missing file should be marked failed");
    recovered = BatchQueueStore::load();
    require(recovered && recovered->completed() == 1 && recovered->remaining() == 1,
            "Restart lost completed and failed states");
    window.batchQueue_ = std::move(recovered);
    require(window.resumeBatchAction_->isEnabled(), "Retry menu action missing");
    QFile saved{firstOutput};
    require(saved.open(QIODevice::ReadOnly), "First queue output missing");
    const QByteArray beforeRetry = saved.readAll();
    saved.close();
    rawlab::dng::TiffDngWriter{}.writeCfaDng(missing.toStdString(), batchImage);
    window.globalEnabled_ = true;
    window.globalModelId_ = "unrelated-missing-model";
    window.startBatchQueue();
    waitForBatch();
    require(window.batchQueue_->completed() == 2 && window.batchQueue_->remaining() == 0,
            "Retry did not reuse the original recipe");
    require(window.batchQueue_->jobs[1].output == secondOutput && QFileInfo::exists(secondOutput),
            "Retry changed the reserved output path");
    require(saved.open(QIODevice::ReadOnly) && saved.readAll() == beforeRetry,
            "Retry rewrote completed output");
    const auto nextQueue = BatchQueue::create({good}, batchDirectory.path(), batchRecipe, {});
    require(nextQueue.jobs[0].output != firstOutput, "New queue would overwrite an existing DNG");
    // Emulate a crash between output publication and the Completed checkpoint.
    window.batchQueue_->jobs[0].state = BatchQueue::State::Running;
    BatchQueueStore::save(*window.batchQueue_);
    recovered = BatchQueueStore::load();
    require(recovered && recovered->jobs[0].state == BatchQueue::State::Completed,
            "Published output was not recognized after interrupted checkpoint");
    window.batchQueue_->jobs[0].publishedHash = "different hash";
    BatchQueueStore::save(*window.batchQueue_);
    recovered = BatchQueueStore::load();
    require(recovered && recovered->jobs[0].state == BatchQueue::State::Pending,
            "Unrelated output was incorrectly accepted as completed");
    auto snapshotQueue = *recovered;
    snapshotQueue.snapshotId.clear();
    snapshotQueue.settings.onnxProvider = "cpu";
    snapshotQueue.settings.onnxDeviceId = 2;
    snapshotQueue.settings.denoiseModels = {{"saved-model", "Saved model", modelPath}};
    PictureRecipeLayer savedMask;
    savedMask.id = 7;
    savedMask.enabled = false;
    savedMask.baseMask = rawlab::pipeline::filledMask(32, 32, 0.75f);
    snapshotQueue.recipe.layers.push_back(savedMask);
    BatchQueueStore::save(snapshotQueue);
    recovered = BatchQueueStore::load();
    require(recovered && recovered->settings.onnxDeviceId == 2
            && recovered->settings.denoiseModels.front().id == "saved-model"
            && recovered->recipe.layers.size() == 1 && recovered->recipe.layers[0].baseMask
            && std::abs(recovered->recipe.layers[0].baseMask->sampleNearest(0, 0) - 0.75f) < 0.005f,
            "Recovery lost model settings or mask data");
    QFile checkpoint{BatchQueueStore::directory() + "/current.json"};
    require(checkpoint.open(QIODevice::ReadOnly), "Cannot read recovery checkpoint");
    const auto checkpointBytes = checkpoint.readAll();
    checkpoint.close();
    auto temporary = BatchQueue::create({good, missing}, batchDirectory.path(), batchRecipe, {});
    temporary.temporary = true;
    temporary.receiptPath = batchDirectory.filePath("temporary.results");
    const QString receiptPath = temporary.receiptPath;
    window.batchQueue_ = std::move(temporary);
    window.startBatchQueue();
    waitForBatch();
    require(window.batchQueue_->completed() == 2, "Temporary batch did not process all images");
    QFile receipt{receiptPath};
    require(receipt.open(QIODevice::ReadOnly) && receipt.readAll().startsWith("rawlab-results-v1\ncomplete\n"),
            "Temporary batch did not produce import receipt");
    require(checkpoint.open(QIODevice::ReadOnly) && checkpoint.readAll() == checkpointBytes,
            "Temporary batch modified standalone recovery");
  }
};

int main(int argc, char **argv)
{
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;
  qputenv("XDG_DATA_HOME", directory.path().toUtf8());
  qputenv("XDG_CACHE_HOME", directory.path().toUtf8());
  QApplication application{argc, argv};
  application.setOrganizationName("RawLabTest");
  application.setApplicationName("EditorWorkflow");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
  try
  {
    QFile model{directory.filePath("cached.onnx")};
    require(model.open(QIODevice::WriteOnly), "Cannot create model path fixture");
    model.close();
    {
      MainWindow window;
      MainWindowTestAccess::run(window, model.fileName());
    }
    MainWindow restarted;
    MainWindowTestAccess::checkRestart(restarted);
  }
  catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
