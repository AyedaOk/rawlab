#pragma once

#include "rawlab/ai/Sam2Segmenter.h"
#include "rawlab/image/RawPreview.h"
#include "rawlab/pipeline/Mask.h"
#include "rawlab/raw/RawImage.h"
#include "rawlab/raw/LibRawDecoder.h"

#include "ApplicationSettings.h"
#include "PictureLibraryStore.h"
#include "PictureRecipe.h"

#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QPointF>
#include <QString>
#include <QSet>
#include <QTimer>

#include <map>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QSlider;
class QToolBar;
class QStackedWidget;
class LibraryPage;
class PreviewWidget;

class MainWindow final : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private:
  struct MaskLayer
  {
    struct BrushStroke
    {
      std::vector<QPointF> points;
      float radius = 30.0f;
      float hardness = 0.8f;
      float value = 1.0f;
    };

    int id = 0;
    QString name;
    QString modelId;
    float blend = 1.0f;
    bool sharpenEnabled = false;
    float sharpenStrength = 1.0f;
    bool enabled = true;
    bool inverted = false;
    bool showMask = true;
    float featherRadius = 0.0f;
    float blurRadius = 0.0f;
    int effectRevision = 0;
    std::optional<rawlab::pipeline::Mask> samMask;
    std::optional<rawlab::pipeline::Mask> mask;
    std::optional<rawlab::pipeline::Mask> effectiveMask;
    std::vector<rawlab::ai::SamPointPrompt> points;
    std::vector<BrushStroke> brushStrokes;
  };

  struct LoadResult
  {
    int generation = 0;
    QString sourcePath;
    std::shared_ptr<rawlab::raw::RawImage> source;
    rawlab::image::RawPreview preview;
    QString error;
  };

  struct DenoiseResult
  {
    QString modelPath;
    QString executionProvider;
    std::shared_ptr<rawlab::raw::RawImage> image;
    QString error;
  };

  struct ComposeResult
  {
    int generation = 0;
    int revision = 0;
    std::shared_ptr<rawlab::raw::RawImage> image;
    rawlab::image::RawPreview preview;
    QString error;
  };
  struct SharpenResult
  {
    QString cacheKey;
    std::shared_ptr<rawlab::raw::RawImage> image;
    QString error;
  };

  struct SamResult
  {
    int revision = 0;
    int layerId = 0;
    QString encoderPath;
    QString decoderPath;
    std::shared_ptr<rawlab::ai::Sam2Segmenter> segmenter;
    std::optional<rawlab::pipeline::Mask> mask;
    QString error;
  };

  struct ExportResult { QString error; };
  struct MaskEffectResult
  {
    int layerId = 0;
    int revision = 0;
    std::optional<rawlab::pipeline::Mask> mask;
    QString error;
  };
  struct MetadataResult
  {
    int revision = 0;
    QString path;
    std::optional<rawlab::raw::RawFileMetadata> metadata;
    QString error;
  };
  struct BatchResult
  {
    int succeeded = 0;
    QStringList skipped;
    QStringList errors;
    bool cancelled = false;
  };

  void buildUi();
  void connectUi();
  void openRaw();
  bool openRawPath(const QString &path);
  void exportDng();
  void exportSelectedBatch();
  void addMaskLayer();
  void deleteSelectedLayer();
  void beginMaskEdit();
  void endMaskEdit();
  void requestMask();
  void rebuildLayerMask(MaskLayer &layer);
  void requestMaskEffects(MaskLayer &layer);
  void startNextMaskEffect();
  void runDenoisePath(const QString &path);
  void queueModel(const QString &modelId);
  void runSelectedModel();
  void refreshModelRunButton();
  bool processingReady() const;
  void queueSharpen(const QString &modelId);
  void runSharpenPath(const QString &denoisePath);
  QString modelPath(const QString &modelId) const;
  QString sharpenModelPath() const;
  void refreshModelCombo();
  void showSettings();
  void showLibrary();
  void showEditor();
  void refreshMetadata();
  void displayMetadata(const QString &path, const rawlab::raw::RawMetadata &metadata,
                       int rawWidth, int rawHeight);
  void requestLibraryMetadata(const QString &path);
  void startLibraryMetadata(const QString &path, int revision);
  void refreshLibrary();
  void importPictures();
  void importFolder();
  void requestLibraryThumbnail(const QString &path);
  void startLibraryThumbnail(const QString &path);
  void saveCurrentRecipe();
  void loadCurrentRecipe();
  PictureRecipe currentRecipe();
  void applyRecipe(const PictureRecipe &recipe);
  void scheduleComposition();
  void startComposition();
  void refreshLayerControls();
  void refreshFilmstrip();
  void refreshOverlay();
  void syncLayerOrder();
  void setBusy(const QString &message, bool busy);
  void setCanvasBusy(const QString &reason, bool busy);
  void refreshActionState();
  void showError(const QString &title, const QString &message);
  MaskLayer *selectedMaskLayer();
  const MaskLayer *selectedMaskLayer() const;
  int selectedLayerId() const;
  QImage toQImage(const rawlab::image::RgbImage &image) const;
  QImage maskOverlay(const MaskLayer &layer) const;

  PreviewWidget *previewWidget_ = nullptr;
  QListWidget *layerList_ = nullptr;
  QListWidget *filmstrip_ = nullptr;
  QComboBox *modelCombo_ = nullptr;
  QPushButton *runModelButton_ = nullptr;
  QSlider *blendSlider_ = nullptr;
  QLabel *blendValueLabel_ = nullptr;
  QCheckBox *sharpenCheck_ = nullptr;
  QWidget *sharpenRow_ = nullptr;
  QLabel *sharpenLabel_ = nullptr;
  QSlider *sharpenSlider_ = nullptr;
  QLabel *sharpenValueLabel_ = nullptr;
  QCheckBox *invertCheck_ = nullptr;
  QCheckBox *showMaskCheck_ = nullptr;
  QCheckBox *clippedCheck_ = nullptr;
  QSlider *featherSlider_ = nullptr;
  QSlider *blurSlider_ = nullptr;
  QWidget *featherRow_ = nullptr;
  QWidget *blurRow_ = nullptr;
  QLabel *featherLabel_ = nullptr;
  QLabel *blurLabel_ = nullptr;
  QLabel *featherValueLabel_ = nullptr;
  QLabel *blurValueLabel_ = nullptr;
  QPushButton *editMaskButton_ = nullptr;
  QPushButton *deleteLayerButton_ = nullptr;
  QPushButton *renameLayerButton_ = nullptr;
  QToolBar *maskToolbar_ = nullptr;
  QAction *positiveAction_ = nullptr;
  QAction *negativeAction_ = nullptr;
  QAction *brushPaintAction_ = nullptr;
  QAction *brushEraseAction_ = nullptr;
  QAction *gradientAddAction_ = nullptr;
  QAction *gradientSubtractAction_ = nullptr;
  QAction *ellipseAddAction_ = nullptr;
  QAction *ellipseSubtractAction_ = nullptr;
  QAction *undoPointAction_ = nullptr;
  QAction *clearPointsAction_ = nullptr;
  QAction *doneMaskAction_ = nullptr;
  QSlider *brushSizeSlider_ = nullptr;
  QSlider *brushHardnessSlider_ = nullptr;
  QLabel *brushSizeLabel_ = nullptr;
  QLabel *brushHardnessLabel_ = nullptr;
  QAction *exportAction_ = nullptr;
  QAction *batchAction_ = nullptr;
  QAction *libraryAction_ = nullptr;
  QAction *editorAction_ = nullptr;
  QAction *filmstripAction_ = nullptr;
  QAction *fitAction_ = nullptr;
  QAction *zoomInAction_ = nullptr;
  QAction *zoomOutAction_ = nullptr;
  QAction *actualSizeAction_ = nullptr;
  QAction *highQualityAction_ = nullptr;
  QAction *beforeAfterAction_ = nullptr;
  QAction *previewControlsSeparator_ = nullptr;
  QToolBar *previewToolbar_ = nullptr;
  QLabel *zoomLabel_ = nullptr;
  QAction *zoomLabelAction_ = nullptr;
  QLabel *metadataDetailsLabel_ = nullptr;
  QProgressBar *progressBar_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  QWidget *editorPage_ = nullptr;
  LibraryPage *libraryPage_ = nullptr;
  QProgressDialog *batchProgress_ = nullptr;

  QString sourcePath_;
  std::shared_ptr<rawlab::raw::RawImage> source_;
  std::shared_ptr<rawlab::raw::RawImage> composed_;
  rawlab::image::RawPreview originalPreview_;
  QImage currentPreviewImage_;
  std::map<QString, std::shared_ptr<rawlab::raw::RawImage>> denoisedCache_;
  std::map<QString, std::shared_ptr<rawlab::raw::RawImage>> sharpenedCache_;
  std::map<QString, QString> modelProviders_;
  std::map<QString, rawlab::raw::RawFileMetadata> metadataCache_;
  std::vector<MaskLayer> layers_;
  QString globalModelId_;
  float globalBlend_ = 1.0f;
  bool globalEnabled_ = true;
  bool globalSharpenEnabled_ = false;
  float globalSharpenStrength_ = 1.0f;
  bool preserveClipped_ = true;
  int nextLayerId_ = 1;
  int editingLayerId_ = 0;
  int documentGeneration_ = 0;
  int compositionRevision_ = 0;
  int samRevision_ = 0;
  int metadataRevision_ = 0;
  bool compositionPending_ = false;
  bool compositionValid_ = false;
  bool samPending_ = false;
  bool samNeedsReset_ = true;
  int lastSegmentLayerId_ = 0;
  std::shared_ptr<rawlab::ai::Sam2Segmenter> segmenter_;
  ApplicationSettingsData appSettings_;
  ApplicationSettingsStore settingsStore_;
  PictureLibraryStore libraryStore_;
  PictureRecipeStore recipeStore_;
  QStringList modelQueue_;
  QStringList sharpenQueue_;
  QStringList batchPaths_;
  bool filmstripVisible_ = false;
  std::shared_ptr<std::atomic_bool> batchCancelled_;
  QString runningModelPath_;
  QString failedModelPath_;
  int denoiseProgressPercent_ = 0;
  QString runningSharpenKey_;
  QString pendingMetadataPath_;
  QSet<QString> thumbnailJobs_;
  QSet<QString> pendingSharpenPaths_;
  QSet<int> pendingMaskEffects_;
  QSet<QString> canvasBusyReasons_;
  QStringList thumbnailQueue_;
  int activeThumbnailJobs_ = 0;

  QTimer compositionTimer_;
  QFutureWatcher<LoadResult> loadWatcher_;
  QFutureWatcher<DenoiseResult> denoiseWatcher_;
  QFutureWatcher<SharpenResult> sharpenWatcher_;
  QFutureWatcher<ComposeResult> composeWatcher_;
  QFutureWatcher<SamResult> samWatcher_;
  QFutureWatcher<ExportResult> exportWatcher_;
  QFutureWatcher<BatchResult> batchWatcher_;
  QFutureWatcher<MetadataResult> metadataWatcher_;
  QFutureWatcher<MaskEffectResult> maskEffectWatcher_;
};
