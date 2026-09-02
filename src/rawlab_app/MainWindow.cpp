#include "MainWindow.h"

#include "LayerItemDelegate.h"
#include "LibraryPage.h"
#include "ModelDownloader.h"
#include "PictureRecipe.h"
#include "PreviewWidget.h"
#include "SettingsDialog.h"

#include "rawlab/ai/OnnxSession.h"
#include "rawlab/ai/RawForgeAdapter.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/PngImage.h"
#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/pipeline/LayerComposition.h"
#include "rawlab/pipeline/RawProcessingPipeline.h"
#include "rawlab/raw/BayerProcessing.h"
#include "rawlab/raw/LibRawDecoder.h"

#include <QAction>
#include <QActionGroup>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QLocale>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

constexpr int globalLayerId = 0;
constexpr int editorPreviewMaxDimension = 4096;
constexpr int embeddedPreviewMaxDimension = 2048;

QString thumbnailCachePath(const QString &path)
{
  const QFileInfo info{path};
  const QByteArray identity = path.toUtf8() + QByteArray::number(info.size())
                              + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/thumbnails");
  QDir{}.mkpath(directory);
  return directory + QLatin1Char('/')
         + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex())
         + QStringLiteral(".png");
}

QString metadataCacheKey(const QString &path)
{
  const QFileInfo info{path};
  return PictureLibraryStore::normalizedPath(path) + QLatin1Char('|')
         + QString::number(info.size()) + QLatin1Char('|')
         + QString::number(info.lastModified().toMSecsSinceEpoch());
}

QString activeSamPath(const ApplicationSettingsData &settings, const QString &fileName)
{
  const SamModelDefinition model = ApplicationSettingsStore::samModelById(
    settings, settings.activeSamModelId);
  return model.folder.isEmpty() ? QString{} : QDir{model.folder}.filePath(fileName);
}

QString sharpenCacheKey(const QString &denoisePath, const QString &sharpenPath)
{
  return denoisePath + QChar{0x1f} + sharpenPath;
}

QString normalSharpenPath()
{
  return QStringLiteral("<normal-sharpen>");
}

rawlab::ai::OnnxSessionOptions onnxOptions(const ApplicationSettingsData &settings)
{
  return {settings.onnxProvider.toStdString(), settings.onnxDeviceId,
          settings.onnxRuntimeLibrary.toStdString()};
}

QString compactNumber(const double value, const int decimals = 1)
{
  QString text = QLocale{}.toString(value, 'f', decimals);
  const QString decimal = QLocale{}.decimalPoint();
  if(decimals > 0 && text.contains(decimal))
  {
    while(text.endsWith(QLatin1Char('0'))) text.chop(1);
    if(text.endsWith(decimal)) text.chop(1);
  }
  return text;
}

QImage qImageCopy(const rawlab::image::RgbImage &image)
{
  if(image.width <= 0 || image.height <= 0 || image.pixels.empty()) return {};
  return QImage{image.pixels.data(), image.width, image.height, image.width * 3,
                QImage::Format_RGB888}.copy();
}

rawlab::dng::JpegPreview encodeJpegPreview(const QImage &source,
                                           const int maximumDimension,
                                           const int quality)
{
  if(source.isNull()) throw std::runtime_error{"Cannot export an empty DNG preview"};
  const QImage image = source.scaled(maximumDimension, maximumDimension,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QByteArray bytes;
  QBuffer buffer{&bytes};
  if(!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG", quality))
  {
    throw std::runtime_error{"Could not encode the DNG JPEG preview"};
  }
  rawlab::dng::JpegPreview result;
  result.width = image.width();
  result.height = image.height();
  result.data.assign(reinterpret_cast<const std::uint8_t *>(bytes.constData()),
                     reinterpret_cast<const std::uint8_t *>(bytes.constData() + bytes.size()));
  return result;
}

rawlab::dng::DngPreviews encodeDngPreviews(const QImage &image)
{
  return {encodeJpegPreview(image, 256, 85),
          encodeJpegPreview(image, embeddedPreviewMaxDimension, 92)};
}

QString shutterText(const double seconds)
{
  if(seconds >= 1.0) return compactNumber(seconds) + QStringLiteral(" s");
  const double reciprocal = 1.0 / seconds;
  const long long denominator = std::max(1LL, std::llround(reciprocal));
  if(std::abs(reciprocal - static_cast<double>(denominator)) > 0.08)
  {
    return compactNumber(seconds, 2) + QStringLiteral(" s");
  }
  return QStringLiteral("1/%1 s").arg(denominator);
}

QIcon themedIcon(QWidget *widget, const QString &name, const QStyle::StandardPixmap fallback)
{
  const QIcon themed = QIcon::fromTheme(name);
  return themed.isNull() ? widget->style()->standardIcon(fallback) : themed;
}

void publishNoReplace(const std::filesystem::path &temporary,
                      const std::filesystem::path &destination)
{
#if defined(__linux__) && defined(SYS_renameat2)
  if(::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
               destination.c_str(), RENAME_NOREPLACE) == 0) return;
  if(errno != ENOSYS && errno != EINVAL)
  {
    throw std::runtime_error{"could not publish DNG without overwriting: "
                             + std::error_code{errno, std::generic_category()}.message()};
  }
#endif
  std::error_code publishError;
  std::filesystem::create_hard_link(temporary, destination, publishError);
  if(publishError)
  {
    throw std::runtime_error{"could not publish DNG without overwriting: "
                             + publishError.message()};
  }
  std::filesystem::remove(temporary);
}

} // namespace

MainWindow::MainWindow(const QString &externalInputPath,
                       const QString &externalOutputPath,
                       const QString &recipeSourcePath,
                       QWidget *parent)
  : QMainWindow{parent}, externalInputPath_{externalInputPath},
    externalOutputPath_{externalOutputPath}, recipeSourcePath_{recipeSourcePath},
    catalogDownloader_{this}
{
  externalAutoExport_ = !recipeSourcePath_.isEmpty();
  appSettings_ = settingsStore_.load();
  if(!appSettings_.denoiseModels.isEmpty()) globalModelId_ = appSettings_.denoiseModels.front().id;
  buildUi();
  connectUi();
  refreshLayerControls();
  refreshLibrary();
  showLibrary();
  catalogDownloader_.refreshCatalog();
  if(!externalInputPath_.isEmpty()) openRawPath(externalInputPath_);
}

MainWindow::~MainWindow()
{
  if(batchCancelled_) batchCancelled_->store(true);
  batchWatcher_.future().waitForFinished();
  saveCurrentRecipe();
  compositionTimer_.stop();
  loadWatcher_.future().waitForFinished();
  denoiseWatcher_.future().waitForFinished();
  sharpenWatcher_.future().waitForFinished();
  composeWatcher_.future().waitForFinished();
  samWatcher_.future().waitForFinished();
  exportWatcher_.future().waitForFinished();
  metadataWatcher_.future().waitForFinished();
  maskEffectWatcher_.future().waitForFinished();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if(!externalOutputPath_.isEmpty() && exportWatcher_.isRunning())
  {
    statusLabel_->setText(tr("Wait for the DNG export to finish"));
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
  setWindowTitle(tr("RawLab"));
  resize(1440, 900);
  setStyleSheet(QStringLiteral(
    "QMainWindow,QWidget{background:#1b1e24;color:#e8eaed;}"
    "QMenuBar,QMenu,QToolBar{background:#15181d;color:#e8eaed;}"
    "QMenuBar{padding:4px 8px;}"
    "QMenuBar::item,QMenu::item{padding:6px 10px;border-radius:5px;}"
    "QMenuBar::item:selected,QMenu::item:selected{background:#2d465a;}"
    "QToolBar{border:0;padding:7px 10px;spacing:5px;}"
    "QToolBar::separator{width:1px;background:#343a43;margin:4px 6px;}"
    "QGroupBox{background:#1b1e24;border:1px solid #353c46;border-radius:10px;margin-top:14px;padding:16px 10px 10px;}"
    "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:#c3cad4;font-weight:600;}"
    "QLineEdit,QComboBox,QListWidget{background:#15181d;border:1px solid #39414b;border-radius:7px;padding:6px 8px;selection-background-color:#31536c;}"
    "QComboBox::drop-down{border:0;width:24px;}"
    "QPushButton,QToolButton{background:#2d333c;border:1px solid #414a56;border-radius:7px;padding:7px 11px;}"
    "QPushButton:hover,QToolButton:hover{background:#39434f;border-color:#58748b;}"
    "QPushButton:pressed,QToolButton:pressed,QToolButton:checked{background:#2d536d;border-color:#71afd8;}"
    "QPushButton:disabled,QToolButton:disabled{color:#707782;background:#242930;border-color:#30353d;}"
    "QPushButton#exportButton{text-align:center;}"
    "QListWidget{padding:6px;}"
    "QListWidget::item{padding:8px;border-radius:6px;}"
    "QListWidget::item:hover{background:#272e37;}"
    "QListWidget::item:selected{background:#294b63;color:#ffffff;}"
    "QSplitter::handle{background:#303640;}"
    "QSplitter::handle:horizontal{width:2px;}"
    "QCheckBox::indicator{width:16px;height:16px;border:1px solid #59636f;border-radius:4px;background:#171a1f;}"
    "QCheckBox::indicator:checked{background:#4e91b9;border-color:#72b6dc;}"
    "QSlider::groove:horizontal{height:4px;border-radius:2px;background:#424a55;}"
    "QSlider::sub-page:horizontal{background:#4f8eaf;border-radius:2px;}"
     "QSlider::handle:horizontal{width:15px;margin:-6px 0;background:#83c3e5;border:2px solid #1b1e24;border-radius:8px;}"
     "QSlider::groove:horizontal:disabled{background:#2b3037;}"
     "QSlider::sub-page:horizontal:disabled{background:#343a42;}"
     "QSlider::handle:horizontal:disabled{background:#59616c;border-color:#1b1e24;}"
     "QCheckBox:disabled{color:#707782;}"
     "QCheckBox::indicator:disabled{background:#242930;border-color:#424a55;}"
    "QProgressBar{background:#15181d;border:1px solid #39414b;border-radius:6px;text-align:center;}"
    "QProgressBar::chunk{background:#4f8eaf;border-radius:5px;}"
    "QStatusBar{background:#15181d;color:#9da6b2;}"
    "QScrollBar:vertical{background:#171a1f;width:10px;margin:2px;}"
    "QScrollBar::handle:vertical{background:#454f5b;border-radius:5px;min-height:24px;}"
    "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"));

  auto *fileMenu = menuBar()->addMenu(tr("&File"));
  QAction *openAction = fileMenu->addAction(tr("&Open RAW..."));
  openAction->setIcon(themedIcon(this, QStringLiteral("document-open"), QStyle::SP_DialogOpenButton));
  openAction->setShortcut(QKeySequence::Open);
  exportAction_ = fileMenu->addAction(tr("&Export DNG..."));
  exportAction_->setIcon(themedIcon(this, QStringLiteral("document-save-as"), QStyle::SP_DialogSaveButton));
  exportAction_->setShortcut(QKeySequence::SaveAs);
  exportAction_->setEnabled(false);
  batchAction_ = new QAction{tr("Export Selected..."), this};
  batchAction_->setEnabled(false);
  fileMenu->addSeparator();
  QAction *importPicturesAction = fileMenu->addAction(tr("&Import Pictures..."));
  importPicturesAction->setIcon(themedIcon(this, QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder));
  QAction *importFolderAction = fileMenu->addAction(tr("Import &Folder..."));
  importFolderAction->setIcon(themedIcon(this, QStringLiteral("folder-open"), QStyle::SP_DirOpenIcon));
  fileMenu->addSeparator();
  QAction *quitAction = fileMenu->addAction(tr("&Quit"));
  quitAction->setShortcut(QKeySequence::Quit);

  auto *viewMenu = menuBar()->addMenu(tr("&View"));
  libraryAction_ = viewMenu->addAction(tr("&Library"));
  libraryAction_->setIcon(themedIcon(this, QStringLiteral("folder-pictures"), QStyle::SP_DirHomeIcon));
  editorAction_ = viewMenu->addAction(tr("&Edit"));
  editorAction_->setIcon(themedIcon(this, QStringLiteral("document-edit"), QStyle::SP_FileDialogDetailedView));
  filmstripAction_ = viewMenu->addAction(tr("Filmstrip"));
  filmstripAction_->setCheckable(true);
  filmstripAction_->setChecked(filmstripVisible_);
  libraryAction_->setCheckable(true);
  editorAction_->setCheckable(true);
  auto *pageGroup = new QActionGroup{this};
  pageGroup->setExclusive(true);
  pageGroup->addAction(libraryAction_);
  pageGroup->addAction(editorAction_);
  viewMenu->addSeparator();
  fitAction_ = viewMenu->addAction(tr("&Fit"));
  fitAction_->setShortcut(Qt::Key_F);
  zoomInAction_ = viewMenu->addAction(tr("Zoom &In"));
  zoomInAction_->setShortcuts(QKeySequence::ZoomIn);
  zoomOutAction_ = viewMenu->addAction(tr("Zoom &Out"));
  zoomOutAction_->setShortcuts(QKeySequence::ZoomOut);
  actualSizeAction_ = viewMenu->addAction(tr("Preview &100%"));
  actualSizeAction_->setShortcut(QKeySequence{Qt::CTRL | Qt::Key_1});

  auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));
  QAction *settingsAction = settingsMenu->addAction(tr("&Settings..."));
  settingsAction->setIcon(themedIcon(this, QStringLiteral("configure"), QStyle::SP_FileDialogDetailedView));

  previewToolbar_ = addToolBar(tr("Preview"));
  previewToolbar_->setObjectName(QStringLiteral("previewToolbar"));
  previewToolbar_->setMovable(false);
  previewToolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  fitAction_->setIcon(themedIcon(this, QStringLiteral("zoom-fit-best"), QStyle::SP_TitleBarNormalButton));
  fitAction_->setToolTip(tr("Fit preview (F)"));
  actualSizeAction_->setIcon(themedIcon(this, QStringLiteral("zoom-original"),
                                        QStyle::SP_FileDialogContentsView));
  actualSizeAction_->setToolTip(tr("Preview at 100% (Ctrl+1)"));
  zoomOutAction_->setIcon(themedIcon(this, QStringLiteral("zoom-out"), QStyle::SP_ArrowDown));
  zoomOutAction_->setToolTip(tr("Zoom out"));
  zoomInAction_->setIcon(themedIcon(this, QStringLiteral("zoom-in"), QStyle::SP_ArrowUp));
  zoomInAction_->setToolTip(tr("Zoom in"));
  previewToolbar_->addAction(fitAction_);
  previewToolbar_->addAction(actualSizeAction_);
  previewToolbar_->addAction(zoomOutAction_);
  zoomLabel_ = new QLabel{QStringLiteral("--%"), previewToolbar_};
  zoomLabel_->setObjectName(QStringLiteral("zoomLevelLabel"));
  zoomLabel_->setMinimumWidth(52);
  zoomLabel_->setAlignment(Qt::AlignCenter);
  zoomLabelAction_ = previewToolbar_->addWidget(zoomLabel_);
  previewToolbar_->addAction(zoomInAction_);
  highQualityAction_ = previewToolbar_->addAction(tr("High Quality"));
  highQualityAction_->setIcon(themedIcon(this, QStringLiteral("image-x-generic"),
                                         QStyle::SP_FileDialogContentsView));
  highQualityAction_->setToolTip(tr("Use high-quality preview rendering"));
  highQualityAction_->setCheckable(true);
   highQualityAction_->setChecked(false);
  beforeAfterAction_ = previewToolbar_->addAction(tr("Before / After"));
  beforeAfterAction_->setIcon(themedIcon(this, QStringLiteral("view-split-left-right"), QStyle::SP_FileDialogContentsView));
  beforeAfterAction_->setCheckable(true);
  beforeAfterAction_->setToolTip(tr("Drag the divider to compare original and processed previews"));
  swapComparisonAction_ = new QAction{tr("Swap Sides"), this};
  swapComparisonAction_->setIcon(themedIcon(this, QStringLiteral("object-flip-horizontal"), QStyle::SP_BrowserReload));
  swapComparisonAction_->setCheckable(true);
  swapComparisonAction_->setEnabled(false);
  swapComparisonAction_->setToolTip(tr("Swap before and after sides"));
  viewMenu->addSeparator();
  viewMenu->addAction(highQualityAction_);
  viewMenu->addAction(beforeAfterAction_);
  viewMenu->addAction(swapComparisonAction_);
  previewControlsSeparator_ = previewToolbar_->addSeparator();
  auto *previewSpacer = new QWidget{previewToolbar_};
  previewSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  previewToolbar_->addWidget(previewSpacer);
  previewToolbar_->addAction(libraryAction_);
  previewToolbar_->addAction(editorAction_);
  if(auto *button = qobject_cast<QToolButton *>(previewToolbar_->widgetForAction(libraryAction_)))
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  if(auto *button = qobject_cast<QToolButton *>(previewToolbar_->widgetForAction(editorAction_)))
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  editorPage_ = new QWidget{this};
  auto *editorLayout = new QVBoxLayout{editorPage_};
  editorLayout->setContentsMargins(0, 0, 0, 0);
  auto *splitter = new QSplitter{editorPage_};
  editorLayout->addWidget(splitter, 1);
  previewWidget_ = new PreviewWidget{splitter};
  splitter->addWidget(previewWidget_);

  auto *panel = new QWidget{splitter};
  panel->setMinimumWidth(330);
  panel->setMaximumWidth(430);
  auto *panelLayout = new QVBoxLayout{panel};
  panelLayout->setContentsMargins(12, 12, 12, 12);

  auto *layersTitle = new QLabel{tr("LAYERS"), panel};
  layersTitle->setStyleSheet(QStringLiteral("font-weight:600;color:#aeb6c2;letter-spacing:1px;"));
  panelLayout->addWidget(layersTitle);

  layerList_ = new QListWidget{panel};
  layerList_->setDragDropMode(QAbstractItemView::InternalMove);
  layerList_->setDefaultDropAction(Qt::MoveAction);
  layerList_->setSelectionMode(QAbstractItemView::SingleSelection);
  layerList_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  layerList_->setItemDelegate(new LayerItemDelegate{layerList_});
  auto *globalItem = new QListWidgetItem{tr("Global"), layerList_};
  globalItem->setData(Qt::UserRole, globalLayerId);
  globalItem->setData(ProcessingEnabledRole, true);
  globalItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
  globalItem->setToolTip(tr("Permanent full-image base layer"));
  layerList_->setCurrentItem(globalItem);
  panelLayout->addWidget(layerList_, 1);

  auto *layerButtons = new QHBoxLayout;
  auto *addMaskButton = new QPushButton{tr("Mask"), panel};
  renameLayerButton_ = new QPushButton{tr("Rename"), panel};
  deleteLayerButton_ = new QPushButton{tr("Delete"), panel};
  addMaskButton->setIcon(themedIcon(this, QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder));
  renameLayerButton_->setIcon(themedIcon(this, QStringLiteral("document-edit"), QStyle::SP_FileDialogDetailedView));
  deleteLayerButton_->setIcon(themedIcon(this, QStringLiteral("edit-delete"), QStyle::SP_TrashIcon));
  renameLayerButton_->setToolTip(tr("Rename selected layer"));
  deleteLayerButton_->setToolTip(tr("Delete selected layer"));
  layerButtons->addWidget(addMaskButton);
  layerButtons->addWidget(renameLayerButton_);
  layerButtons->addWidget(deleteLayerButton_);
  panelLayout->addLayout(layerButtons);

  auto *settingsGroup = new QGroupBox{tr("Denoise && Sharpen"), panel};
  auto *settingsLayout = new QFormLayout{settingsGroup};
  auto *modelRow = new QWidget{settingsGroup};
  auto *modelLayout = new QHBoxLayout{modelRow};
  modelLayout->setContentsMargins(0, 0, 0, 0);
  modelCombo_ = new QComboBox{modelRow};
  modelLayout->addWidget(modelCombo_, 1);
  settingsLayout->addRow(tr("Model"), modelRow);

  auto *blendRow = new QWidget{settingsGroup};
  auto *blendLayout = new QHBoxLayout{blendRow};
  blendLayout->setContentsMargins(0, 0, 0, 0);
  blendSlider_ = new QSlider{Qt::Horizontal, blendRow};
  blendSlider_->setRange(0, 100);
  blendSlider_->setValue(100);
  blendValueLabel_ = new QLabel{QStringLiteral("100%"), blendRow};
  blendValueLabel_->setMinimumWidth(42);
  blendLayout->addWidget(blendSlider_, 1);
  blendLayout->addWidget(blendValueLabel_);
  settingsLayout->addRow(tr("Blend"), blendRow);

  sharpenModeCombo_ = new QComboBox{settingsGroup};
  sharpenModeCombo_->addItems({tr("Off"), tr("Normal"), tr("AI")});
  sharpenModeCombo_->setCurrentIndex(static_cast<int>(SharpenMode::Off));
  sharpenModeCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  settingsLayout->addRow(tr("Sharpening"), sharpenModeCombo_);
  sharpenRow_ = new QWidget{settingsGroup};
  sharpenRow_->setObjectName(QStringLiteral("sharpenStrengthRow"));
  auto *sharpenLayout = new QHBoxLayout{sharpenRow_};
  sharpenLayout->setContentsMargins(0, 0, 0, 0);
  sharpenSlider_ = new QSlider{Qt::Horizontal, sharpenRow_};
  sharpenSlider_->setRange(0, 100);
  sharpenSlider_->setValue(100);
  sharpenValueLabel_ = new QLabel{QStringLiteral("100%"), sharpenRow_};
  sharpenValueLabel_->setMinimumWidth(42);
  sharpenLayout->addWidget(sharpenSlider_, 1);
  sharpenLayout->addWidget(sharpenValueLabel_);
  sharpenLabel_ = new QLabel{tr("Strength"), settingsGroup};
  sharpenLabel_->setObjectName(QStringLiteral("sharpenStrengthLabel"));
  settingsLayout->addRow(sharpenLabel_, sharpenRow_);

  clippedCheck_ = new QCheckBox{tr("Preserve clipped highlights"), settingsGroup};
  settingsLayout->addRow(clippedCheck_);
  panelLayout->addWidget(settingsGroup);

  auto *maskingGroup = new QGroupBox{tr("Masking"), panel};
  auto *maskingLayout = new QVBoxLayout{maskingGroup};
  const auto addRadiusControl = [maskingGroup, maskingLayout](const QString &name,
                                                               QWidget *&row,
                                                               QLabel *&rowLabel,
                                                               QSlider *&slider,
                                                               QLabel *&valueLabel) {
    row = new QWidget{maskingGroup};
    auto *layout = new QHBoxLayout{row};
    layout->setContentsMargins(0, 0, 0, 0);
    slider = new QSlider{Qt::Horizontal, row};
    slider->setRange(0, 300);
    valueLabel = new QLabel{QStringLiteral("0 px"), row};
    valueLabel->setMinimumWidth(52);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);
    rowLabel = new QLabel{name, maskingGroup};
    auto *rowLayout = new QHBoxLayout;
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(rowLabel);
    rowLayout->addWidget(row, 1);
    maskingLayout->addLayout(rowLayout);
  };
  addRadiusControl(tr("Feather"), featherRow_, featherLabel_, featherSlider_, featherValueLabel_);
  addRadiusControl(tr("Blur"), blurRow_, blurLabel_, blurSlider_, blurValueLabel_);
  invertCheck_ = new QCheckBox{tr("Invert mask"), maskingGroup};
  showMaskCheck_ = new QCheckBox{tr("Show mask"), maskingGroup};
  editMaskButton_ = new QPushButton{tr("Edit Mask"), maskingGroup};
  editMaskButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  editMaskButton_->setIcon(themedIcon(this, QStringLiteral("draw-brush"), QStyle::SP_DialogApplyButton));
  editMaskButton_->setToolTip(tr("Edit the selected mask"));
  maskingLayout->addWidget(invertCheck_);
  maskingLayout->addWidget(showMaskCheck_);
  maskingLayout->addWidget(editMaskButton_);
  panelLayout->addWidget(maskingGroup);

  auto *exportGroup = new QGroupBox{tr("Export"), panel};
  auto *exportLayout = new QVBoxLayout{exportGroup};
  exportButton_ = new QPushButton{exportAction_->text(), exportGroup};
  exportButton_->setObjectName(QStringLiteral("exportButton"));
  exportButton_->setIcon(exportAction_->icon());
  exportButton_->setIconSize(QSize{20, 20});
  exportButton_->setToolTip(tr("Export the current or selected pictures"));
  exportButton_->setEnabled(exportAction_->isEnabled());
  exportButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  exportLayout->addWidget(exportButton_);
  connect(exportButton_, &QPushButton::clicked, this, [this] { exportAction_->trigger(); });
  connect(exportAction_, &QAction::changed, this, [this] {
    exportButton_->setText(exportAction_->text());
    exportButton_->setIcon(exportAction_->icon());
    exportButton_->setEnabled(exportAction_->isEnabled());
  });
  panelLayout->addWidget(exportGroup);

  splitter->addWidget(panel);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 0);
  filmstrip_ = new QListWidget{editorPage_};
  filmstrip_->setViewMode(QListView::IconMode);
  filmstrip_->setFlow(QListView::LeftToRight);
  filmstrip_->setResizeMode(QListView::Adjust);
  filmstrip_->setMovement(QListView::Static);
  filmstrip_->setSelectionMode(QAbstractItemView::SingleSelection);
  filmstrip_->setIconSize({150, 100});
  filmstrip_->setGridSize({174, 126});
  filmstrip_->setMaximumHeight(150);
  editorLayout->addWidget(filmstrip_);
  filmstrip_->setVisible(filmstripVisible_);
  pages_ = new QStackedWidget{this};
  libraryPage_ = new LibraryPage{pages_};
  pages_->addWidget(libraryPage_);
  pages_->addWidget(editorPage_);
  setCentralWidget(pages_);

  addToolBarBreak(Qt::TopToolBarArea);
  maskToolbar_ = addToolBar(tr("Mask Editing"));
  maskToolbar_->setMovable(false);
  maskToolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  maskToolbar_->addWidget(new QLabel{tr("  Editing mask:  "), maskToolbar_});
  maskCategoryCombo_ = new QComboBox{maskToolbar_};
  maskCategoryCombo_->addItems({tr("AI"), tr("Gradient"), tr("Ellipse")});
  maskCategoryCombo_->setToolTip(tr("Choose which mask source to edit"));
  maskToolbar_->addWidget(maskCategoryCombo_);
  maskToolbar_->addSeparator();
  positiveAction_ = maskToolbar_->addAction(tr("Add Point"));
  positiveAction_->setIcon(themedIcon(this, QStringLiteral("list-add"), QStyle::SP_DialogYesButton));
  positiveAction_->setToolTip(tr("Add positive point"));
  positiveAction_->setCheckable(true);
  negativeAction_ = maskToolbar_->addAction(tr("Subtract Point"));
  negativeAction_->setIcon(themedIcon(this, QStringLiteral("list-remove"), QStyle::SP_DialogCancelButton));
  negativeAction_->setToolTip(tr("Subtract point"));
  negativeAction_->setCheckable(true);
  brushPaintAction_ = maskToolbar_->addAction(tr("Brush"));
  brushPaintAction_->setIcon(themedIcon(this, QStringLiteral("draw-brush"), QStyle::SP_DialogApplyButton));
  brushPaintAction_->setToolTip(tr("Paint mask"));
  brushPaintAction_->setCheckable(true);
  brushEraseAction_ = maskToolbar_->addAction(tr("Erase"));
  brushEraseAction_->setIcon(themedIcon(this, QStringLiteral("edit-clear"), QStyle::SP_DialogResetButton));
  brushEraseAction_->setToolTip(tr("Erase mask"));
  brushEraseAction_->setCheckable(true);
  gradientAddAction_ = maskToolbar_->addAction(tr("Gradient Add"));
  gradientAddAction_->setIcon(themedIcon(this, QStringLiteral("format-fill-color"), QStyle::SP_DialogApplyButton));
  gradientAddAction_->setToolTip(tr("Add gradient"));
  gradientAddAction_->setCheckable(true);
  gradientSubtractAction_ = maskToolbar_->addAction(tr("Gradient Subtract"));
  gradientSubtractAction_->setIcon(themedIcon(this, QStringLiteral("format-stroke-color"), QStyle::SP_DialogCancelButton));
  gradientSubtractAction_->setToolTip(tr("Subtract gradient"));
  gradientSubtractAction_->setCheckable(true);
  ellipseAddAction_ = maskToolbar_->addAction(tr("Ellipse Add"));
  ellipseAddAction_->setIcon(themedIcon(this, QStringLiteral("draw-ellipse"), QStyle::SP_DialogApplyButton));
  ellipseAddAction_->setToolTip(tr("Add ellipse"));
  ellipseAddAction_->setCheckable(true);
  ellipseSubtractAction_ = maskToolbar_->addAction(tr("Ellipse Subtract"));
  ellipseSubtractAction_->setIcon(themedIcon(this, QStringLiteral("draw-ellipse"), QStyle::SP_DialogCancelButton));
  ellipseSubtractAction_->setToolTip(tr("Subtract ellipse"));
  ellipseSubtractAction_->setCheckable(true);
  auto *promptGroup = new QActionGroup{maskToolbar_};
  promptGroup->setExclusive(true);
  promptGroup->addAction(positiveAction_);
  promptGroup->addAction(negativeAction_);
  promptGroup->addAction(brushPaintAction_);
  promptGroup->addAction(brushEraseAction_);
  promptGroup->addAction(gradientAddAction_);
  promptGroup->addAction(gradientSubtractAction_);
  promptGroup->addAction(ellipseAddAction_);
  promptGroup->addAction(ellipseSubtractAction_);
  positiveAction_->setChecked(true);
  maskToolbar_->addSeparator();
  maskToolbar_->addWidget(new QLabel{tr("Size"), maskToolbar_});
  brushSizeSlider_ = new QSlider{Qt::Horizontal, maskToolbar_};
  brushSizeSlider_->setRange(2, 250);
  brushSizeSlider_->setValue(30);
  brushSizeSlider_->setFixedWidth(110);
  brushSizeLabel_ = new QLabel{QStringLiteral("30"), maskToolbar_};
  brushSizeLabel_->setMinimumWidth(28);
  maskToolbar_->addWidget(brushSizeSlider_);
  maskToolbar_->addWidget(brushSizeLabel_);
  maskToolbar_->addWidget(new QLabel{tr("Hardness"), maskToolbar_});
  brushHardnessSlider_ = new QSlider{Qt::Horizontal, maskToolbar_};
  brushHardnessSlider_->setRange(0, 100);
  brushHardnessSlider_->setValue(80);
  brushHardnessSlider_->setFixedWidth(90);
  brushHardnessLabel_ = new QLabel{QStringLiteral("80%"), maskToolbar_};
  brushHardnessLabel_->setMinimumWidth(36);
  maskToolbar_->addWidget(brushHardnessSlider_);
  maskToolbar_->addWidget(brushHardnessLabel_);
  maskToolbar_->addSeparator();
  undoPointAction_ = maskToolbar_->addAction(tr("Undo Point"));
  undoPointAction_->setIcon(themedIcon(this, QStringLiteral("edit-undo"), QStyle::SP_ArrowBack));
  undoPointAction_->setToolTip(tr("Undo last mask point or stroke"));
  clearPointsAction_ = maskToolbar_->addAction(tr("Clear"));
  clearPointsAction_->setIcon(themedIcon(this, QStringLiteral("edit-clear"), QStyle::SP_DialogResetButton));
  clearPointsAction_->setToolTip(tr("Clear mask edits"));
  maskToolbar_->addSeparator();
  doneMaskAction_ = maskToolbar_->addAction(tr("Done"));
  doneMaskAction_->setIcon(themedIcon(this, QStringLiteral("dialog-ok"), QStyle::SP_DialogApplyButton));
  doneMaskAction_->setToolTip(tr("Finish mask editing"));
  maskToolbar_->hide();

  metadataDetailsLabel_ = new QLabel{tr("No picture selected"), statusBar()};
  metadataDetailsLabel_->setObjectName(QStringLiteral("imageMetadataDetailsLabel"));
  metadataDetailsLabel_->setStyleSheet(QStringLiteral("color:#b9c0ca;padding:0 8px;"));
  metadataDetailsLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  metadataDetailsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  statusLabel_ = new QLabel{tr("Ready"), statusBar()};
  progressBar_ = new QProgressBar{statusBar()};
  progressBar_->setMaximumWidth(220);
  progressBar_->hide();
  statusBar()->addWidget(metadataDetailsLabel_, 1);
  statusBar()->addPermanentWidget(statusLabel_);
  statusBar()->addPermanentWidget(progressBar_);

  connect(openAction, &QAction::triggered, this, &MainWindow::openRaw);
  connect(exportAction_, &QAction::triggered, this, [this] {
    if(batchPaths_.size() > 1) exportSelectedBatch();
    else exportDng();
  });
  connect(batchAction_, &QAction::triggered, this, &MainWindow::exportSelectedBatch);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);
  connect(fitAction_, &QAction::triggered, previewWidget_, &PreviewWidget::fitImage);
  connect(zoomInAction_, &QAction::triggered, previewWidget_, &PreviewWidget::zoomIn);
  connect(zoomOutAction_, &QAction::triggered, previewWidget_, &PreviewWidget::zoomOut);
  connect(actualSizeAction_, &QAction::triggered, previewWidget_, &PreviewWidget::actualSize);
  connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
  connect(importPicturesAction, &QAction::triggered, this, &MainWindow::importPictures);
  connect(importFolderAction, &QAction::triggered, this, &MainWindow::importFolder);
  connect(libraryAction_, &QAction::triggered, this, &MainWindow::showLibrary);
  connect(editorAction_, &QAction::triggered, this, &MainWindow::showEditor);
  connect(filmstripAction_, &QAction::toggled, this, [this](const bool visible) {
    filmstripVisible_ = visible;
    filmstrip_->setVisible(visible);
  });
  connect(filmstrip_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    const QString path = item->data(Qt::UserRole).toString();
    if(path != sourcePath_ && !openRawPath(path)) refreshFilmstrip();
  });
  connect(addMaskButton, &QPushButton::clicked, this, &MainWindow::addMaskLayer);
  refreshModelCombo();
}

void MainWindow::connectUi()
{
  compositionTimer_.setSingleShot(true);
  compositionTimer_.setInterval(120);
  connect(&compositionTimer_, &QTimer::timeout, this, &MainWindow::startComposition);
  connect(previewWidget_, &PreviewWidget::zoomChanged, this, [this](const double scale) {
    zoomLabel_->setText(scale > 0.0 ? QString::number(std::lround(scale * 100.0)) + QLatin1Char('%')
                                    : QStringLiteral("--%"));
  });
  connect(highQualityAction_, &QAction::toggled, this, [this] {
    if(source_) scheduleComposition();
  });
  connect(libraryPage_, &LibraryPage::importFilesRequested, this, &MainWindow::importPictures);
  connect(libraryPage_, &LibraryPage::importFolderRequested, this, &MainWindow::importFolder);
  connect(libraryPage_, &LibraryPage::currentPictureChanged,
          this, &MainWindow::requestLibraryMetadata);
  connect(libraryPage_, &LibraryPage::openRequested, this, [this](const QStringList &paths) {
    if(paths.isEmpty()) return;
    batchPaths_ = paths;
    filmstripAction_->setChecked(batchPaths_.size() > 1);
    refreshFilmstrip();
    openRawPath(paths.front());
  });
  connect(libraryPage_, &LibraryPage::removeRequested, this, [this](const QStringList &paths) {
    const QString currentPath = PictureLibraryStore::normalizedPath(sourcePath_);
    for(const QString &path : paths)
      if(PictureLibraryStore::normalizedPath(path) == currentPath) saveCurrentRecipe();
    for(const QString &path : paths)
    {
      const QString normalized = PictureLibraryStore::normalizedPath(path);
      deletedRecipePaths_.insert(normalized);
      recipeStore_.remove(path);
    }
    libraryStore_.removeFiles(paths);
    refreshLibrary();
  });
  connect(layerList_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *, QListWidgetItem *) {
            if(editingLayerId_ != 0 && selectedLayerId() != editingLayerId_) endMaskEdit();
            refreshLayerControls();
            refreshOverlay();
          });
  connect(layerList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    const int id = item->data(Qt::UserRole).toInt();
    const bool enabled = item->data(ProcessingEnabledRole).toBool();
    if(id == globalLayerId)
    {
      globalEnabled_ = enabled;
      if(enabled && globalSharpenMode_ != SharpenMode::Off) queueSharpen(globalModelId_);
    }
    else
    {
      auto found = std::find_if(layers_.begin(), layers_.end(), [id](const MaskLayer &layer) {
        return layer.id == id;
      });
      if(found != layers_.end())
      {
        found->enabled = enabled;
        const QString name = item->text().trimmed();
        if(name.isEmpty()) item->setText(found->name);
        else found->name = name;
      }
    }
    refreshOverlay();
    scheduleComposition();
  });
  connect(layerList_->model(), &QAbstractItemModel::rowsMoved, this,
          [this] { syncLayerOrder(); scheduleComposition(); });
  connect(deleteLayerButton_, &QPushButton::clicked, this, &MainWindow::deleteSelectedLayer);
  connect(renameLayerButton_, &QPushButton::clicked, this, [this] {
    if(selectedLayerId() != globalLayerId && layerList_->currentItem()) layerList_->editItem(layerList_->currentItem());
  });
  connect(editMaskButton_, &QPushButton::clicked, this, &MainWindow::beginMaskEdit);
  connect(modelCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
    if(index < 0) return;
    const QString id = modelCombo_->itemData(index).toString();
    if(selectedLayerId() == globalLayerId) globalModelId_ = id;
    else if(MaskLayer *layer = selectedMaskLayer()) layer->modelId = id;
    compositionValid_ = false;
    beforeAfterAction_->setChecked(false);
    refreshModelRunButton();
  });
  connect(previewWidget_, &PreviewWidget::updateRequested, this, &MainWindow::runSelectedModel);
  connect(beforeAfterAction_, &QAction::toggled,
          previewWidget_, &PreviewWidget::setComparisonEnabled);
  connect(beforeAfterAction_, &QAction::toggled, this, [this](const bool enabled) {
    swapComparisonAction_->setEnabled(enabled);
  });
  connect(swapComparisonAction_, &QAction::toggled,
          previewWidget_, &PreviewWidget::setComparisonReversed);
  connect(blendSlider_, &QSlider::valueChanged, this, [this](const int value) {
    blendValueLabel_->setText(QString::number(value) + QStringLiteral("%"));
    if(selectedLayerId() == globalLayerId) globalBlend_ = static_cast<float>(value) / 100.0f;
    else if(MaskLayer *layer = selectedMaskLayer()) layer->blend = static_cast<float>(value) / 100.0f;
    scheduleComposition();
  });
  connect(sharpenModeCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
    const SharpenMode mode = static_cast<SharpenMode>(std::clamp(index, 0, 2));
    const bool global = selectedLayerId() == globalLayerId;
    if(global) { globalSharpenMode_ = mode; globalSharpenEnabled_ = mode != SharpenMode::Off; }
    else if(MaskLayer *layer = selectedMaskLayer())
    {
      layer->sharpenMode = mode;
      layer->sharpenEnabled = mode != SharpenMode::Off;
    }
    refreshLayerControls();
    scheduleComposition();
  });
  connect(sharpenSlider_, &QSlider::valueChanged, this, [this](const int value) {
    sharpenValueLabel_->setText(QString::number(value) + QStringLiteral("%"));
    if(selectedLayerId() == globalLayerId) globalSharpenStrength_ = static_cast<float>(value) / 100.0f;
    else if(MaskLayer *layer = selectedMaskLayer())
      layer->sharpenStrength = static_cast<float>(value) / 100.0f;
    scheduleComposition();
  });
  connect(featherSlider_, &QSlider::valueChanged, this, [this](const int value) {
    featherValueLabel_->setText(tr("%1 px").arg(value));
    if(MaskLayer *layer = selectedMaskLayer())
    {
      layer->featherRadius = static_cast<float>(value);
      requestMaskEffects(*layer);
    }
  });
  connect(blurSlider_, &QSlider::valueChanged, this, [this](const int value) {
    blurValueLabel_->setText(tr("%1 px").arg(value));
    if(MaskLayer *layer = selectedMaskLayer())
    {
      layer->blurRadius = static_cast<float>(value);
      requestMaskEffects(*layer);
    }
  });
  connect(invertCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
    if(MaskLayer *layer = selectedMaskLayer()) layer->inverted = checked;
    refreshOverlay();
    scheduleComposition();
  });
  connect(showMaskCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
    if(MaskLayer *layer = selectedMaskLayer()) layer->showMask = checked;
    refreshOverlay();
  });
  connect(clippedCheck_, &QCheckBox::toggled, this, [this](const bool checked) {
    preserveClipped_ = checked;
    scheduleComposition();
  });
  connect(positiveAction_, &QAction::triggered, this, [this] {
    maskCategory_ = MaskCategory::Ai;
    maskCategoryCombo_->setCurrentIndex(0);
    refreshMaskCategoryTools();
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::Positive);
  });
  connect(negativeAction_, &QAction::triggered, this, [this] {
    maskCategory_ = MaskCategory::Ai;
    maskCategoryCombo_->setCurrentIndex(0);
    refreshMaskCategoryTools();
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::Negative);
  });
  connect(brushPaintAction_, &QAction::triggered, this, [this] {
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::BrushPaint);
  });
  connect(brushEraseAction_, &QAction::triggered, this, [this] {
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::BrushErase);
  });
  connect(gradientAddAction_, &QAction::triggered, this, [this] {
    maskCategory_ = MaskCategory::Gradient;
    maskCategoryCombo_->setCurrentIndex(1);
    refreshMaskCategoryTools();
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::GradientAdd);
  });
  connect(gradientSubtractAction_, &QAction::triggered, this, [this] {
    maskCategory_ = MaskCategory::Gradient;
    maskCategoryCombo_->setCurrentIndex(1);
    refreshMaskCategoryTools();
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::GradientSubtract);
  });
  connect(ellipseAddAction_, &QAction::triggered, this, [this] {
    maskCategory_ = MaskCategory::Ellipse;
    maskCategoryCombo_->setCurrentIndex(2);
    refreshMaskCategoryTools();
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::EllipseAdd);
  });
  connect(ellipseSubtractAction_, &QAction::triggered, this, [this] {
    maskCategory_ = MaskCategory::Ellipse;
    maskCategoryCombo_->setCurrentIndex(2);
    refreshMaskCategoryTools();
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::EllipseSubtract);
  });
  connect(maskCategoryCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
    maskCategory_ = index == 1 ? MaskCategory::Gradient
                               : index == 2 ? MaskCategory::Ellipse : MaskCategory::Ai;
    refreshMaskCategoryTools();
    if(maskCategory_ == MaskCategory::Ai)
    {
      positiveAction_->setChecked(true);
      previewWidget_->setPromptMode(PreviewWidget::PromptMode::Positive);
    }
    else if(maskCategory_ == MaskCategory::Gradient)
    {
      gradientAddAction_->setChecked(true);
      previewWidget_->setPromptMode(PreviewWidget::PromptMode::GradientAdd);
    }
    else
    {
      ellipseAddAction_->setChecked(true);
      previewWidget_->setPromptMode(PreviewWidget::PromptMode::EllipseAdd);
    }
  });
  connect(brushSizeSlider_, &QSlider::valueChanged, this, [this](const int value) {
    brushSizeLabel_->setText(QString::number(value));
    previewWidget_->setBrushRadius(static_cast<float>(value));
  });
  connect(brushHardnessSlider_, &QSlider::valueChanged, this, [this](const int value) {
    brushHardnessLabel_->setText(QString::number(value) + QStringLiteral("%"));
  });
  connect(previewWidget_, &PreviewWidget::shapeRequested, this,
          [this](const QPointF from, const QPointF to, const bool gradient, const bool foreground) {
    MaskLayer *layer = selectedMaskLayer();
    if(!layer || originalPreview_.image.width <= 0 || originalPreview_.image.height <= 0) return;
    const int width = originalPreview_.image.width;
    const int height = originalPreview_.image.height;
    const int categoryIndex = gradient ? 1 : 2;
    auto &category = layer->categories[static_cast<std::size_t>(categoryIndex)];
    if(!category.mask) category.mask = rawlab::pipeline::filledMask(width, height);
    const auto shape = gradient
      ? rawlab::pipeline::linearGradientMask(width, height,
          static_cast<float>(from.x()), static_cast<float>(from.y()),
          static_cast<float>(to.x()), static_cast<float>(to.y()))
      : rawlab::pipeline::ellipseMask(width, height,
          static_cast<float>(std::min(from.x(), to.x())),
          static_cast<float>(std::min(from.y(), to.y())),
          static_cast<float>(std::max(from.x(), to.x())),
          static_cast<float>(std::max(from.y(), to.y())));
    std::vector<float> values = category.mask->values();
    for(std::size_t i = 0; i < values.size(); ++i)
      values[i] = foreground ? std::max(values[i], shape.values()[i])
                             : std::max(0.0f, values[i] - shape.values()[i]);
    category.mask = rawlab::pipeline::Mask{width, height, std::move(values)};
    rebuildLayerMask(*layer);
    refreshOverlay();
    scheduleComposition();
  });
  connect(doneMaskAction_, &QAction::triggered, this, &MainWindow::endMaskEdit);
  connect(undoPointAction_, &QAction::triggered, this, [this] {
    MaskLayer *layer = selectedMaskLayer();
    if(!layer) return;
    const bool brushMode = brushPaintAction_->isChecked() || brushEraseAction_->isChecked();
    auto &category = layer->categories[static_cast<std::size_t>(maskCategoryIndex())];
    if(brushMode && !category.brushStrokes.empty())
    {
      layer->categories[static_cast<std::size_t>(maskCategoryIndex())].brushStrokes.pop_back();
      rebuildLayerMask(*layer);
      refreshOverlay();
      scheduleComposition();
    }
    else if(maskCategory_ == MaskCategory::Ai && !layer->points.empty())
    {
      layer->points.pop_back();
      samNeedsReset_ = true;
      ++samRevision_;
      if(layer->points.empty())
      {
        layer->categories[0].mask.reset();
        layer->samMask.reset();
        rebuildLayerMask(*layer);
        refreshOverlay();
        scheduleComposition();
      }
      else requestMask();
    }
  });
  connect(clearPointsAction_, &QAction::triggered, this, [this] {
    MaskLayer *layer = selectedMaskLayer();
    if(!layer) return;
    const int categoryIndex = maskCategoryIndex();
    layer->categories[static_cast<std::size_t>(categoryIndex)].mask.reset();
    layer->categories[static_cast<std::size_t>(categoryIndex)].brushStrokes.clear();
    if(categoryIndex == 0)
    {
      layer->points.clear();
      layer->samMask.reset();
      layer->brushStrokes.clear();
    }
    rebuildLayerMask(*layer);
    samNeedsReset_ = true;
    ++samRevision_;
    refreshOverlay();
    scheduleComposition();
  });
  connect(previewWidget_, &PreviewWidget::pointRequested, this,
          [this](const QPointF position, const bool foreground) {
            MaskLayer *layer = selectedMaskLayer();
            if(!layer || layer->id != editingLayerId_) return;
            layer->points.push_back({static_cast<float>(position.x()),
                                     static_cast<float>(position.y()), foreground});
            ++samRevision_;
            refreshOverlay();
            requestMask();
          });
  connect(previewWidget_, &PreviewWidget::brushStrokeStarted, this,
          [this](const QPointF point, const bool paint) {
            MaskLayer *layer = selectedMaskLayer();
            if(!layer || layer->id != editingLayerId_) return;
            ++layer->effectRevision;
            pendingMaskEffects_.remove(layer->id);
            MaskLayer::BrushStroke stroke;
            stroke.points.push_back(point);
             stroke.radius = static_cast<float>(brushSizeSlider_->value());
             stroke.hardness = static_cast<float>(brushHardnessSlider_->value()) / 100.0f;
             stroke.value = paint ? 1.0f : 0.0f;
             auto &category = layer->categories[static_cast<std::size_t>(maskCategoryIndex())];
             category.brushStrokes.push_back(stroke);
             if(!category.mask)
               category.mask = rawlab::pipeline::filledMask(originalPreview_.image.width,
                                                             originalPreview_.image.height);
             refreshOverlay();
          });
  connect(previewWidget_, &PreviewWidget::brushStrokeContinued, this,
              [this](const QPointF, const QPointF to) {
             MaskLayer *layer = selectedMaskLayer();
             if(!layer || layer->id != editingLayerId_) return;
             auto &category = layer->categories[static_cast<std::size_t>(maskCategoryIndex())];
             if(category.brushStrokes.empty() || !category.mask) return;
             MaskLayer::BrushStroke &stroke = category.brushStrokes.back();
             stroke.points.push_back(to);
           });
  connect(previewWidget_, &PreviewWidget::brushStrokeFinished, this, [this] {
    if(MaskLayer *layer = selectedMaskLayer()) rebuildLayerMask(*layer);
  });

  connect(&loadWatcher_, &QFutureWatcher<LoadResult>::finished, this, [this] {
    const LoadResult result = loadWatcher_.result();
    setCanvasBusy(QStringLiteral("load"), false);
    centralWidget()->setEnabled(true);
    setBusy(QString{}, false);
    if(result.generation != documentGeneration_) return;
    if(!result.error.isEmpty())
    {
      refreshMetadata();
      showError(tr("Open RAW"), result.error);
      return;
    }
    source_ = result.source;
    sourcePath_ = result.sourcePath;
    refreshFilmstrip();
    composed_ = source_;
    originalPreview_ = result.preview;
    denoisedCache_.clear();
    sharpenedCache_.clear();
    pendingRecipeSharpenRequests_.clear();
    modelProviders_.clear();
    segmenter_.reset();
    samNeedsReset_ = true;
    lastSegmentLayerId_ = 0;
    layers_.clear();
    nextLayerId_ = 1;
    editingLayerId_ = 0;
    globalModelId_ = appSettings_.denoiseModels.isEmpty()
      ? QString{} : appSettings_.denoiseModels.front().id;
    globalBlend_ = 1.0f;
    globalEnabled_ = true;
    globalSharpenEnabled_ = false;
    globalSharpenMode_ = SharpenMode::Off;
    globalSharpenStrength_ = 1.0f;
    preserveClipped_ = true;
    compositionValid_ = false;
    pendingSharpenPaths_.clear();
    while(layerList_->count() > 1) delete layerList_->takeItem(1);
    layerList_->item(0)->setData(ProcessingEnabledRole, true);
    const QImage displayImage = toQImage(result.preview.image);
    currentPreviewImage_ = displayImage;
    beforeAfterAction_->setChecked(false);
    previewWidget_->setImage(displayImage, true);
    previewWidget_->setBeforeImage(displayImage);
    previewWidget_->setMask({});
    previewWidget_->setPrompts({});
    if(recipeSourcePath_.isEmpty()) loadCurrentRecipe();
    else
    {
      const auto recipe = recipeStore_.load(recipeSourcePath_, originalPreview_.image.width,
                                            originalPreview_.image.height);
      if(recipe)
      {
        applyRecipe(*recipe);
        queueRecipeProcessing();
      }
      else
      {
        showError(tr("Apply settings"),
                  tr("No saved RawLab settings were found for %1.")
                    .arg(QFileInfo{recipeSourcePath_}.fileName()));
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(4); });
      }
    }
    pages_->setCurrentWidget(editorPage_);
    statusLabel_->setText(tr("Loaded %1 x %2 CFA").arg(source_->width()).arg(source_->height()));
    refreshMetadata();
    refreshLayerControls();
    const QImage thumbnail = displayImage.scaled({360, 240}, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);
    thumbnail.save(thumbnailCachePath(sourcePath_), "PNG");
    libraryPage_->setThumbnail(sourcePath_, thumbnail);
    refreshFilmstrip();
    refreshActionState();
  });

  connect(&denoiseWatcher_, &QFutureWatcher<DenoiseResult>::finished, this, [this] {
    const DenoiseResult result = denoiseWatcher_.result();
    setCanvasBusy(QStringLiteral("denoise"), false);
    runningModelPath_.clear();
    setBusy(QString{}, false);
    if(!result.error.isEmpty())
    {
      failedModelPath_ = result.modelPath;
      showError(tr("Denoise"), result.error);
    }
    else
    {
      failedModelPath_.clear();
      denoisedCache_[result.modelPath] = result.image;
      modelProviders_[result.modelPath] = result.executionProvider;
      statusLabel_->setText(tr("Denoise ready on %1").arg(result.executionProvider));
        if(pendingSharpenPaths_.remove(result.modelPath))
        {
         const SharpenMode mode = selectedLayerId() == globalLayerId ? globalSharpenMode_
           : selectedMaskLayer() ? selectedMaskLayer()->sharpenMode : SharpenMode::Off;
          runSharpenPath(result.modelPath, mode);
        }
        queuePendingRecipeSharpening();
        scheduleComposition();
    }
    if(!modelQueue_.isEmpty()) runDenoisePath(modelQueue_.takeFirst());
    refreshModelRunButton();
  });

  connect(&sharpenWatcher_, &QFutureWatcher<SharpenResult>::finished, this, [this] {
    const SharpenResult result = sharpenWatcher_.result();
    setCanvasBusy(QStringLiteral("sharpen"), false);
    runningSharpenKey_.clear();
    setBusy(QString{}, false);
    if(!result.error.isEmpty()) showError(tr("DeepSharpen"), result.error);
    else
    {
      sharpenedCache_[result.cacheKey] = result.image;
      statusLabel_->setText(tr("DeepSharpen cached"));
      queuePendingRecipeSharpening();
      scheduleComposition();
    }
     if(!sharpenQueue_.isEmpty())
     {
       const QString queued = sharpenQueue_.takeFirst();
       const int separator = queued.lastIndexOf(QChar{0x1e});
       runSharpenPath(separator < 0 ? queued : queued.left(separator),
                      separator < 0 ? SharpenMode::Ai
                                    : static_cast<SharpenMode>(queued.mid(separator + 1).toInt()));
     }
    refreshModelRunButton();
  });

  connect(&composeWatcher_, &QFutureWatcher<ComposeResult>::finished, this, [this] {
    const ComposeResult result = composeWatcher_.result();
    setCanvasBusy(QStringLiteral("compose"), false);
    if(!result.error.isEmpty()) showError(tr("Preview"), result.error);
    else if(result.generation == documentGeneration_ && result.revision == compositionRevision_)
    {
      composed_ = result.image;
      compositionValid_ = processingReady();
       currentPreviewImage_ = toQImage(result.preview.image);
       previewWidget_->setImage(currentPreviewImage_);
       refreshOverlay();
       refreshModelRunButton();
       statusLabel_->setText(tr("Preview updated"));
       refreshActionState();
       if(externalAutoExport_ && !externalExportStarted_)
       {
         externalExportStarted_ = true;
         QTimer::singleShot(0, this, &MainWindow::exportDng);
       }
    }
    if(result.generation == documentGeneration_
       && (compositionPending_ || result.revision != compositionRevision_))
    {
      compositionPending_ = false;
      compositionTimer_.start();
    }
  });

  connect(&samWatcher_, &QFutureWatcher<SamResult>::finished, this, [this] {
    const SamResult result = samWatcher_.result();
    setCanvasBusy(QStringLiteral("sam"), false);
    const bool sameModels = result.encoderPath == activeSamPath(appSettings_, QStringLiteral("encoder.onnx"))
                            && result.decoderPath == activeSamPath(appSettings_, QStringLiteral("decoder.onnx"));
    if(!result.error.isEmpty() && sameModels) showError(tr("SAM mask"), result.error);
    else if(sameModels)
    {
      segmenter_ = result.segmenter;
      if(result.revision == samRevision_)
      {
        auto found = std::find_if(layers_.begin(), layers_.end(), [&result](const MaskLayer &layer) {
          return layer.id == result.layerId;
        });
        if(found != layers_.end())
        {
          found->samMask = result.mask;
          found->categories[0].mask = result.mask;
          rebuildLayerMask(*found);
        }
        refreshOverlay();
        scheduleComposition();
        statusLabel_->setText(tr("Mask updated"));
      }
    }
    if(samPending_ || result.revision != samRevision_)
    {
      samPending_ = false;
      requestMask();
    }
  });

  connect(&exportWatcher_, &QFutureWatcher<ExportResult>::finished, this, [this] {
    const ExportResult result = exportWatcher_.result();
    setBusy(QString{}, false);
    if(!result.error.isEmpty()) showError(tr("Export DNG"), result.error);
    else
    {
      statusLabel_->setText(tr("DNG export complete"));
      if(!externalOutputPath_.isEmpty())
      {
        externalExportSucceeded_ = true;
        QCoreApplication::exit(0);
      }
    }
  });

  connect(&batchWatcher_, &QFutureWatcher<BatchResult>::finished, this, [this] {
    const BatchResult result = batchWatcher_.result();
    centralWidget()->setEnabled(true);
    if(batchProgress_)
    {
      batchProgress_->close();
      batchProgress_->deleteLater();
      batchProgress_ = nullptr;
    }
    batchCancelled_.reset();
    refreshActionState();
    QString summary = tr("Exported %1 picture(s).").arg(result.succeeded);
    if(result.cancelled) summary += tr("\nBatch cancelled.");
    if(!result.skipped.isEmpty()) summary += tr("\n\nSkipped:\n%1").arg(result.skipped.join(QLatin1Char('\n')));
    if(!result.errors.isEmpty()) summary += tr("\n\nFailed:\n%1").arg(result.errors.join(QLatin1Char('\n')));
    statusLabel_->setText(summary.section(QLatin1Char('\n'), 0, 0));
    if(result.errors.isEmpty()) QMessageBox::information(this, tr("Batch export"), summary);
    else QMessageBox::warning(this, tr("Batch export"), summary);
  });

  connect(&metadataWatcher_, &QFutureWatcher<MetadataResult>::finished, this, [this] {
    const MetadataResult result = metadataWatcher_.result();
    if(result.metadata) metadataCache_[metadataCacheKey(result.path)] = *result.metadata;
    if(result.revision == metadataRevision_ && pages_->currentWidget() == libraryPage_
       && libraryPage_->selectedPath() == result.path)
    {
      if(result.metadata)
      {
        displayMetadata(result.path, result.metadata->metadata,
                        result.metadata->rawWidth, result.metadata->rawHeight);
      }
      else
      {
        metadataDetailsLabel_->setText(tr("%1  |  Metadata unavailable: %2")
                                         .arg(result.path, result.error));
      }
    }
    if(!pendingMetadataPath_.isEmpty())
    {
      const QString next = std::exchange(pendingMetadataPath_, QString{});
      startLibraryMetadata(next, metadataRevision_);
    }
  });

  connect(&maskEffectWatcher_, &QFutureWatcher<MaskEffectResult>::finished, this, [this] {
    const MaskEffectResult result = maskEffectWatcher_.result();
    auto found = std::find_if(layers_.begin(), layers_.end(), [&result](const MaskLayer &layer) {
      return layer.id == result.layerId;
    });
    if(found != layers_.end() && found->effectRevision == result.revision)
    {
      if(result.mask)
      {
        found->effectiveMask = result.mask;
        refreshOverlay();
        scheduleComposition();
      }
      else statusLabel_->setText(tr("Could not update mask effects: %1").arg(result.error));
    }
    startNextMaskEffect();
    if(!maskEffectWatcher_.isRunning() && pendingMaskEffects_.isEmpty())
    {
      setCanvasBusy(QStringLiteral("mask-effects"), false);
    }
  });
}

void MainWindow::openRaw()
{
  if(loadWatcher_.isRunning() || denoiseWatcher_.isRunning() || sharpenWatcher_.isRunning()
     || composeWatcher_.isRunning()
     || samWatcher_.isRunning() || maskEffectWatcher_.isRunning()
     || exportWatcher_.isRunning() || batchWatcher_.isRunning())
  {
    statusLabel_->setText(tr("Wait for the current operation to finish before opening another image"));
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
    this, tr("Open RAW image"), QString{}, tr("RAW images (*.ARW *.arw *.CR2 *.cr2 *.CR3 *.cr3 *.NEF *.nef *.RAF *.raf *.DNG *.dng);;All files (*)"));
  if(path.isEmpty()) return;
  batchPaths_.clear();
  filmstripAction_->setChecked(false);
  refreshFilmstrip();
  openRawPath(path);
}

bool MainWindow::openRawPath(const QString &path)
{
  if(path.isEmpty()) return false;
  if(loadWatcher_.isRunning() || denoiseWatcher_.isRunning() || sharpenWatcher_.isRunning()
     || composeWatcher_.isRunning()
     || samWatcher_.isRunning() || maskEffectWatcher_.isRunning()
     || exportWatcher_.isRunning() || batchWatcher_.isRunning())
  {
    statusLabel_->setText(tr("Wait for the current operation to finish before opening another image"));
    return false;
  }
  saveCurrentRecipe();
  deletedRecipePaths_.remove(PictureLibraryStore::normalizedPath(path));
  batchAction_->setEnabled(false);
  thumbnailQueue_.removeAll(path);
  compositionTimer_.stop();
  compositionPending_ = false;
  ++documentGeneration_;
  ++compositionRevision_;
  const int generation = documentGeneration_;
  endMaskEdit();
  pages_->setCurrentWidget(editorPage_);
  editorAction_->setChecked(true);
  metadataDetailsLabel_->setText(tr("Loading metadata for %1  |  %2")
                                   .arg(QFileInfo{path}.fileName(), path));
  centralWidget()->setEnabled(false);
  setBusy(tr("Decoding RAW and building preview..."), true);
  setCanvasBusy(QStringLiteral("load"), true);
  const auto quality = highQualityAction_->isChecked()
    ? rawlab::image::PreviewQuality::High : rawlab::image::PreviewQuality::Fast;
  loadWatcher_.setFuture(QtConcurrent::run([path, generation, quality] {
    LoadResult result;
    result.generation = generation;
    result.sourcePath = path;
    try
    {
      rawlab::raw::LibRawDecoder decoder;
      result.source = std::make_shared<rawlab::raw::RawImage>(
        decoder.decodeRawImage(std::filesystem::path{path.toStdString()}));
      result.preview = rawlab::image::renderRawPreview(
        *result.source, editorPreviewMaxDimension, quality);
    }
    catch(const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
  }));
  return true;
}

void MainWindow::exportDng()
{
  if(!composed_ || loadWatcher_.isRunning() || denoiseWatcher_.isRunning() || sharpenWatcher_.isRunning()
     || composeWatcher_.isRunning() || samWatcher_.isRunning() || exportWatcher_.isRunning()
     || batchWatcher_.isRunning() || !canvasBusyReasons_.isEmpty()) return;
  QString path = externalOutputPath_;
  if(path.isEmpty())
  {
    const QString suggested = sourcePath_.isEmpty()
      ? QStringLiteral("output.dng")
      : QFileInfo{sourcePath_}.absolutePath() + QLatin1Char('/')
          + QFileInfo{sourcePath_}.completeBaseName() + QStringLiteral("-rawlab.dng");
    path = QFileDialog::getSaveFileName(this, tr("Export Bayer CFA DNG"), suggested,
                                        tr("Digital Negative (*.dng)"));
  }
  if(path.isEmpty()) return;
  if(PictureLibraryStore::normalizedPath(path) == PictureLibraryStore::normalizedPath(sourcePath_))
  {
    showError(tr("Export DNG"), tr("The export path cannot replace the source image."));
    return;
  }
  if(!externalInputPath_.isEmpty()
     && PictureLibraryStore::normalizedPath(sourcePath_)
          != PictureLibraryStore::normalizedPath(externalInputPath_))
  {
    showError(tr("Export DNG"), tr("Return to the image opened by Darktable before exporting."));
    return;
  }
  if(!externalOutputPath_.isEmpty() && QFileInfo::exists(path))
  {
    showError(tr("Export DNG"), tr("The integration output already exists."));
    return;
  }
  const auto image = composed_;
  rawlab::dng::DngPreviews previews;
  try { previews = encodeDngPreviews(currentPreviewImage_); }
  catch(const std::exception &error)
  {
    showError(tr("Export DNG"), QString::fromUtf8(error.what()));
    return;
  }
  setBusy(tr("Exporting DNG..."), true);
  const bool noClobber = !externalOutputPath_.isEmpty();
  exportWatcher_.setFuture(QtConcurrent::run([path, image, previews = std::move(previews), noClobber] {
    ExportResult result;
    const std::filesystem::path destination{path.toStdString()};
    std::filesystem::path temporary = destination;
    temporary += ".rawlab-" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
    try
    {
      std::error_code error;
      std::filesystem::remove(temporary, error);
      rawlab::dng::TiffDngWriter{}.writeCfaDng(temporary, *image, &previews);
      if(noClobber) publishNoReplace(temporary, destination);
      else std::filesystem::rename(temporary, destination);
    }
    catch(const std::exception &e)
    {
      std::error_code error;
      std::filesystem::remove(temporary, error);
      result.error = QString::fromUtf8(e.what());
    }
    return result;
  }));
}

void MainWindow::exportSelectedBatch()
{
  if(batchPaths_.isEmpty() || !source_ || batchWatcher_.isRunning()) return;
  const QString outputDirectory = QFileDialog::getExistingDirectory(this, tr("Choose batch DNG folder"));
  if(outputDirectory.isEmpty()) return;

  saveCurrentRecipe();
  PictureRecipe recipe = currentRecipe();
  for(std::size_t index = 0; index < recipe.layers.size() && index < layers_.size(); ++index)
  {
    // A flattened soft mask preserves the exact tuned result across differing image dimensions.
    recipe.layers[index].baseMask = layers_[index].mask;
    recipe.layers[index].strokes.clear();
  }
  const QStringList paths = batchPaths_;
  const ApplicationSettingsData settings = appSettings_;
  batchCancelled_ = std::make_shared<std::atomic_bool>(false);
  const auto cancelled = batchCancelled_;

  batchProgress_ = new QProgressDialog{tr("Preparing batch export..."), tr("Cancel"),
                                       0, static_cast<int>(paths.size()), this};
  batchProgress_->setWindowTitle(tr("Batch export"));
  batchProgress_->setWindowModality(Qt::WindowModal);
  batchProgress_->setMinimumDuration(0);
  connect(batchProgress_, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
  centralWidget()->setEnabled(false);
  exportAction_->setEnabled(false);
  batchAction_->setEnabled(false);

  batchWatcher_.setFuture(QtConcurrent::run(
    [this, paths, outputDirectory, recipe, settings, cancelled] {
      BatchResult result;
      std::map<QString, std::unique_ptr<rawlab::ai::OnnxSession>> sessions;
      QSet<QString> destinations;
      const auto modelPathForId = [&settings](const QString &id) {
        return ApplicationSettingsStore::modelById(settings, id).path;
      };
      const auto modelForId = [&settings](const QString &id) {
        return ApplicationSettingsStore::modelById(settings, id);
      };

      for(int fileIndex = 0; fileIndex < paths.size(); ++fileIndex)
      {
        if(cancelled->load()) { result.cancelled = true; break; }
        const QString sourcePath = paths[fileIndex];
        const QFileInfo sourceInfo{sourcePath};
        const QString outputName = sourceInfo.completeBaseName() + QStringLiteral("-rawlab.dng");
        const QString outputPath = QDir{outputDirectory}.filePath(outputName);
        QMetaObject::invokeMethod(this, [this, fileIndex, paths, sourceInfo] {
          if(batchProgress_)
          {
            batchProgress_->setValue(fileIndex);
            batchProgress_->setLabelText(tr("Processing %1 (%2 of %3)")
              .arg(sourceInfo.fileName()).arg(fileIndex + 1).arg(paths.size()));
          }
        }, Qt::QueuedConnection);

        if(destinations.contains(outputName) || QFileInfo::exists(outputPath))
        {
          result.skipped.push_back(tr("%1: output already exists or name is duplicated").arg(sourceInfo.fileName()));
          continue;
        }
        destinations.insert(outputName);
        try
        {
          rawlab::raw::LibRawDecoder decoder;
          rawlab::raw::RawImage source = decoder.decodeRawImage(
            std::filesystem::path{sourcePath.toStdString()});
          const rawlab::image::RawPreview targetPreview = rawlab::image::renderRawPreview(
            source, editorPreviewMaxDimension);
          PictureRecipe targetRecipe = PictureRecipeStore::scaled(
            recipe, targetPreview.image.width, targetPreview.image.height);

          std::map<QString, std::shared_ptr<rawlab::raw::RawImage>> denoised;
          QSet<QString> requiredModels;
          if(targetRecipe.globalEnabled) requiredModels.insert(targetRecipe.globalModelId);
          for(const PictureRecipeLayer &layer : targetRecipe.layers)
          {
            if(layer.enabled) requiredModels.insert(layer.modelId);
          }
          for(const QString &modelId : requiredModels)
          {
            if(cancelled->load()) throw std::runtime_error{"batch cancelled"};
            const QString modelPath = modelPathForId(modelId);
            if(modelPath.isEmpty()) throw std::runtime_error{"recipe references a missing denoise model"};
            const DenoiseModelDefinition definition = modelForId(modelId);
            auto &session = sessions[modelPath];
            if(!session)
            {
              session = std::make_unique<rawlab::ai::OnnxSession>(
                std::filesystem::path{modelPath.toStdString()}, onnxOptions(settings));
            }
            const auto progress = [cancelled](int, int) {
              if(cancelled->load()) throw std::runtime_error{"batch cancelled"};
            };
            rawlab::raw::RawImage image;
            if(definition.adapter == QStringLiteral("rawforge-rgb-v1"))
            {
              image = rawlab::ai::RawForgeAdapter{}.run(source, *session,
                static_cast<float>(source.metadata().iso.value_or(100.0)), true, progress);
            }
            else
            {
              rawlab::pipeline::RawProcessingPipeline pipeline;
              image = pipeline.runRawNindDenoise(source, *session, progress);
            }
            denoised[modelPath] = std::make_shared<rawlab::raw::RawImage>(std::move(image));
          }

           std::map<QString, std::shared_ptr<rawlab::raw::RawImage>> sharpened;
           const QString aiSharpenPath = settings.sharpenModels.isEmpty()
             ? QString{} : settings.sharpenModels.front().path;
           {
             QSet<QString> normalPaths;
             QSet<QString> sharpenPaths;
             if(targetRecipe.globalEnabled && targetRecipe.globalSharpenMode == SharpenMode::Normal)
               normalPaths.insert(modelPathForId(targetRecipe.globalModelId));
             else if(targetRecipe.globalEnabled && targetRecipe.globalSharpenMode == SharpenMode::Ai)
               sharpenPaths.insert(modelPathForId(targetRecipe.globalModelId));
             for(const PictureRecipeLayer &layer : targetRecipe.layers)
             {
               if(!layer.enabled || layer.sharpenMode == SharpenMode::Off) continue;
               (layer.sharpenMode == SharpenMode::Normal ? normalPaths : sharpenPaths)
                 .insert(modelPathForId(layer.modelId));
             }
             rawlab::pipeline::RawProcessingPipeline pipeline;
             for(const QString &denoisePath : normalPaths)
             {
               const auto input = denoised.find(denoisePath);
               if(input != denoised.end()) sharpened[sharpenCacheKey(denoisePath, normalSharpenPath())] =
                 std::make_shared<rawlab::raw::RawImage>(pipeline.runNormalSharpen(*input->second, 1.0f));
             }
             if(!sharpenPaths.isEmpty() && !aiSharpenPath.isEmpty())
            {
              rawlab::ai::OnnxSession sharpenSession{
                std::filesystem::path{settings.sharpenModels.front().path.toStdString()},
                onnxOptions(settings)};
              for(const QString &denoisePath : sharpenPaths)
              {
                const auto input = denoised.find(denoisePath);
                if(input == denoised.end()) continue;
                sharpened[sharpenCacheKey(denoisePath, aiSharpenPath)] = std::make_shared<rawlab::raw::RawImage>(
                  rawlab::ai::RawForgeAdapter{}.run(*input->second, sharpenSession,
                    static_cast<float>(source.metadata().iso.value_or(100.0)), false,
                    [cancelled](int, int) {
                      if(cancelled->load()) throw std::runtime_error{"batch cancelled"};
                    }));
           }
            }
          }

          const QString globalPath = modelPathForId(targetRecipe.globalModelId);
          const auto global = denoised.find(globalPath);
          std::optional<rawlab::raw::RawImage> preservedGlobal;
          std::optional<rawlab::raw::RawImage> sharpenMixedGlobal;
          const rawlab::raw::RawImage *globalImage = global == denoised.end() ? nullptr : global->second.get();
           if(globalImage && targetRecipe.globalSharpenMode != SharpenMode::Off)
          {
             const auto sharpenedIt = sharpened.find(sharpenCacheKey(globalPath,
                targetRecipe.globalSharpenMode == SharpenMode::Normal ? normalSharpenPath() : aiSharpenPath));
            if(sharpenedIt != sharpened.end())
            {
              sharpenMixedGlobal = rawlab::raw::blendWithOriginal(
                *globalImage, *sharpenedIt->second, targetRecipe.globalSharpenStrength);
              globalImage = &*sharpenMixedGlobal;
            }
          }
          if(globalImage && targetRecipe.preserveClipped)
          {
            preservedGlobal = rawlab::raw::preserveClippedHighlights(source, *globalImage);
            globalImage = &*preservedGlobal;
          }
          std::vector<rawlab::pipeline::LocalLayerInput> localInputs;
          std::vector<rawlab::raw::RawImage> preservedLocals;
          std::vector<rawlab::raw::RawImage> sharpenMixedLocals;
          std::vector<rawlab::pipeline::Mask> effectiveMasks;
          preservedLocals.reserve(targetRecipe.layers.size());
          sharpenMixedLocals.reserve(targetRecipe.layers.size());
          effectiveMasks.reserve(targetRecipe.layers.size());
          for(auto it = targetRecipe.layers.rbegin(); it != targetRecipe.layers.rend(); ++it)
          {
            const auto denoisedIt = denoised.find(modelPathForId(it->modelId));
            if(denoisedIt == denoised.end()) continue;
            const rawlab::raw::RawImage *localImage = denoisedIt->second.get();
           if(it->sharpenMode != SharpenMode::Off)
           {
             const auto sharpenedIt = sharpened.find(sharpenCacheKey(modelPathForId(it->modelId),
                it->sharpenMode == SharpenMode::Normal ? normalSharpenPath() : aiSharpenPath));
              if(sharpenedIt != sharpened.end())
              {
                sharpenMixedLocals.push_back(rawlab::raw::blendWithOriginal(
                  *denoisedIt->second, *sharpenedIt->second, it->sharpenStrength));
                localImage = &sharpenMixedLocals.back();
              }
            }
            if(targetRecipe.preserveClipped)
            {
              preservedLocals.push_back(rawlab::raw::preserveClippedHighlights(source, *localImage));
              localImage = &preservedLocals.back();
            }
            const rawlab::pipeline::Mask *mask = nullptr;
            if(it->baseMask)
            {
              const float shortEdge = static_cast<float>(std::min(
                targetPreview.image.width, targetPreview.image.height));
              effectiveMasks.push_back(rawlab::pipeline::blurMask(
                rawlab::pipeline::featherMask(*it->baseMask, it->feather * shortEdge),
                it->blur * shortEdge));
              mask = &effectiveMasks.back();
            }
            localInputs.push_back({localImage, mask,
                                    it->blend, it->inverted, it->enabled});
          }
          rawlab::raw::RawImage composed = rawlab::pipeline::composeDenoiseLayers(
            source, {globalImage, targetRecipe.globalBlend, targetRecipe.globalEnabled},
            localInputs, targetPreview.geometry);
          const rawlab::image::RawPreview displayPreview = rawlab::image::renderRawPreview(
            composed, editorPreviewMaxDimension, rawlab::image::PreviewQuality::High);
          const rawlab::dng::DngPreviews previews = encodeDngPreviews(
            qImageCopy(displayPreview.image));

          if(cancelled->load()) throw std::runtime_error{"batch cancelled"};

          const QString temporaryPath = outputPath + QStringLiteral(".rawlab-")
                                        + QUuid::createUuid().toString(QUuid::Id128);
          const std::filesystem::path temporary{temporaryPath.toStdString()};
          try
          {
            rawlab::dng::TiffDngWriter{}.writeCfaDng(temporary, composed, &previews);
            if(cancelled->load()) throw std::runtime_error{"batch cancelled"};
            std::error_code publishError;
            std::filesystem::create_hard_link(temporary,
              std::filesystem::path{outputPath.toStdString()}, publishError);
            if(publishError) throw std::runtime_error{"could not publish DNG without overwriting: "
                                                       + publishError.message()};
            std::filesystem::remove(temporary);
          }
          catch(...)
          {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            throw;
          }
          ++result.succeeded;
          if(fileIndex > 0)
          {
            try { PictureRecipeStore{}.save(sourcePath, targetRecipe); }
            catch(const std::exception &error)
            {
              result.errors.push_back(sourceInfo.fileName()
                + tr(": DNG exported, but its recipe could not be saved: ")
                + QString::fromUtf8(error.what()));
            }
          }
        }
        catch(const std::exception &error)
        {
          if(cancelled->load()) { result.cancelled = true; break; }
          result.errors.push_back(sourceInfo.fileName() + QStringLiteral(": ")
                                  + QString::fromUtf8(error.what()));
        }
      }
      QMetaObject::invokeMethod(this, [this, paths] {
        if(batchProgress_) batchProgress_->setValue(static_cast<int>(paths.size()));
      }, Qt::QueuedConnection);
      return result;
    }));
}

void MainWindow::addMaskLayer()
{
  MaskLayer layer;
  layer.id = nextLayerId_++;
  layer.name = tr("Mask %1").arg(layer.id);
  layer.modelId = globalModelId_;
  if(source_)
  {
    layer.mask = rawlab::pipeline::filledMask(originalPreview_.image.width,
                                              originalPreview_.image.height);
    layer.effectiveMask = layer.mask;
  }
  layers_.insert(layers_.begin(), layer);

  const QSignalBlocker blocker{layerList_};
  auto *item = new QListWidgetItem{layer.name};
  item->setData(Qt::UserRole, layer.id);
  item->setData(ProcessingEnabledRole, true);
  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                 | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
  layerList_->insertItem(1, item);
  layerList_->setCurrentItem(item);
  refreshLayerControls();
}

void MainWindow::deleteSelectedLayer()
{
  const int id = selectedLayerId();
  if(id == globalLayerId) return;
  if(editingLayerId_ == id) endMaskEdit();
  pendingMaskEffects_.remove(id);
  layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [id](const MaskLayer &layer) {
    return layer.id == id;
  }), layers_.end());
  delete layerList_->takeItem(layerList_->currentRow());
  layerList_->setCurrentRow(layerList_->count() - 1);
  refreshLayerControls();
  scheduleComposition();
}

void MainWindow::beginMaskEdit()
{
  MaskLayer *layer = selectedMaskLayer();
  if(!source_ || !layer) return;
  editingLayerId_ = layer->id;
  layer->showMask = true;
  if(lastSegmentLayerId_ != layer->id) samNeedsReset_ = true;
  maskToolbar_->show();
  maskCategory_ = MaskCategory::Ai;
  maskCategoryCombo_->setCurrentIndex(0);
  refreshMaskCategoryTools();
  positiveAction_->setChecked(true);
  previewWidget_->setPromptMode(PreviewWidget::PromptMode::Positive);
  refreshOverlay();
  refreshLayerControls();
  statusLabel_->setText(tr("Mask editing: use SAM points, brush, gradients, or ellipses"));
}

void MainWindow::refreshMaskCategoryTools()
{
  const bool ai = maskCategory_ == MaskCategory::Ai;
  const bool gradient = maskCategory_ == MaskCategory::Gradient;
  const bool ellipse = maskCategory_ == MaskCategory::Ellipse;
  const QList<QAction *> toolActions{positiveAction_, negativeAction_, brushPaintAction_, brushEraseAction_,
                                     gradientAddAction_, gradientSubtractAction_, ellipseAddAction_, ellipseSubtractAction_};
  for(QAction *action : toolActions) maskToolbar_->removeAction(action);
  QAction *anchor = nullptr;
  for(QAction *action : maskToolbar_->actions())
    if(maskToolbar_->widgetForAction(action) == brushSizeSlider_) { anchor = action; break; }
  const QList<QAction *> ordered = ai
    ? QList<QAction *>{positiveAction_, negativeAction_, brushPaintAction_, brushEraseAction_}
    : gradient
      ? QList<QAction *>{gradientAddAction_, gradientSubtractAction_, brushPaintAction_, brushEraseAction_}
      : QList<QAction *>{ellipseAddAction_, ellipseSubtractAction_, brushPaintAction_, brushEraseAction_};
  for(QAction *action : ordered) maskToolbar_->insertAction(anchor, action);
  positiveAction_->setVisible(ai);
  negativeAction_->setVisible(ai);
  gradientAddAction_->setVisible(gradient);
  gradientSubtractAction_->setVisible(gradient);
  ellipseAddAction_->setVisible(ellipse);
  ellipseSubtractAction_->setVisible(ellipse);
  brushPaintAction_->setVisible(true);
  brushEraseAction_->setVisible(true);
}

int MainWindow::maskCategoryIndex() const
{
  return maskCategory_ == MaskCategory::Gradient ? 1
    : maskCategory_ == MaskCategory::Ellipse ? 2 : 0;
}

void MainWindow::endMaskEdit()
{
  editingLayerId_ = 0;
  if(maskToolbar_) maskToolbar_->hide();
  if(previewWidget_)
  {
    previewWidget_->setPromptMode(PreviewWidget::PromptMode::None);
    previewWidget_->setPrompts({});
  }
}

void MainWindow::rebuildLayerMask(MaskLayer &layer)
{
  if(originalPreview_.image.width <= 0 || originalPreview_.image.height <= 0)
  {
    layer.mask.reset();
    return;
  }

  rawlab::pipeline::Mask rebuilt = rawlab::pipeline::filledMask(
    originalPreview_.image.width, originalPreview_.image.height);
  // Recipes from before category editing stored their mask in samMask.
  if(layer.categories[0].mask == std::nullopt && layer.samMask)
  {
    layer.categories[0].mask = layer.samMask;
    layer.categories[0].brushStrokes = layer.brushStrokes;
  }
  for(auto &category : layer.categories)
  {
    if(!category.mask) continue;
    rawlab::pipeline::Mask contribution = *category.mask;
    for(const MaskLayer::BrushStroke &stroke : category.brushStrokes)
    {
      if(stroke.points.empty()) continue;
      if(stroke.points.size() == 1)
      {
        const QPointF point = stroke.points.front();
        contribution.paintSegment(static_cast<float>(point.x()), static_cast<float>(point.y()),
                                  static_cast<float>(point.x()), static_cast<float>(point.y()),
                                  stroke.radius, stroke.hardness, stroke.value);
        continue;
      }
      for(std::size_t i = 1; i < stroke.points.size(); ++i)
      {
        const QPointF from = stroke.points[i - 1];
        const QPointF to = stroke.points[i];
        contribution.paintSegment(static_cast<float>(from.x()), static_cast<float>(from.y()),
                                  static_cast<float>(to.x()), static_cast<float>(to.y()),
                                  stroke.radius, stroke.hardness, stroke.value);
      }
    }
    std::vector<float> values = rebuilt.values();
    for(std::size_t i = 0; i < values.size(); ++i)
      values[i] = std::max(values[i], contribution.values()[i]);
    rebuilt = rawlab::pipeline::Mask{rebuilt.width(), rebuilt.height(), std::move(values)};
  }
  layer.mask = std::move(rebuilt);
  requestMaskEffects(layer);
}

void MainWindow::requestMaskEffects(MaskLayer &layer)
{
  ++layer.effectRevision;
  if(!layer.mask)
  {
    layer.effectiveMask.reset();
    return;
  }
  if(layer.featherRadius <= 0.0f && layer.blurRadius <= 0.0f)
  {
    pendingMaskEffects_.remove(layer.id);
    layer.effectiveMask = layer.mask;
    refreshOverlay();
    scheduleComposition();
    return;
  }
  // Show edits immediately while the softened mask is recomputed off the UI thread.
  layer.effectiveMask = layer.mask;
  pendingMaskEffects_.insert(layer.id);
  setCanvasBusy(QStringLiteral("mask-effects"), true);
  startNextMaskEffect();
}

void MainWindow::startNextMaskEffect()
{
  if(maskEffectWatcher_.isRunning() || pendingMaskEffects_.isEmpty()) return;
  const int layerId = *pendingMaskEffects_.begin();
  pendingMaskEffects_.remove(layerId);
  const auto found = std::find_if(layers_.begin(), layers_.end(), [layerId](const MaskLayer &layer) {
    return layer.id == layerId;
  });
  if(found == layers_.end() || !found->mask)
  {
    startNextMaskEffect();
    return;
  }
  const int revision = found->effectRevision;
  const rawlab::pipeline::Mask source = *found->mask;
  const float feather = found->featherRadius;
  const float blur = found->blurRadius;
  maskEffectWatcher_.setFuture(QtConcurrent::run([layerId, revision, source, feather, blur] {
    MaskEffectResult result;
    result.layerId = layerId;
    result.revision = revision;
    try
    {
      rawlab::pipeline::Mask effective = rawlab::pipeline::featherMask(source, feather);
      result.mask = rawlab::pipeline::blurMask(effective, blur);
    }
    catch(const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
    return result;
  }));
}

void MainWindow::requestMask()
{
  MaskLayer *layer = selectedMaskLayer();
  if(!layer || layer->id != editingLayerId_ || layer->points.empty()) return;
  const QString encoder = activeSamPath(appSettings_, QStringLiteral("encoder.onnx"));
  const QString decoder = activeSamPath(appSettings_, QStringLiteral("decoder.onnx"));
  if(encoder.isEmpty() || decoder.isEmpty())
  {
    showError(tr("SAM mask"), tr("Select both SAM encoder and decoder models first."));
    return;
  }
  if(samWatcher_.isRunning()) { samPending_ = true; return; }

  const int revision = samRevision_;
  const int layerId = layer->id;
  const auto points = layer->points;
  const auto preview = originalPreview_.image;
  const auto existingSegmenter = segmenter_;
  const auto execution = onnxOptions(appSettings_);
  const bool resetRefinement = samNeedsReset_ || lastSegmentLayerId_ != layerId;
  samNeedsReset_ = false;
  lastSegmentLayerId_ = layerId;
  statusLabel_->setText(existingSegmenter ? tr("Updating SAM mask...") : tr("Encoding preview for SAM..."));
  setCanvasBusy(QStringLiteral("sam"), true);
  samWatcher_.setFuture(QtConcurrent::run(
    [revision, layerId, points, preview, encoder, decoder, existingSegmenter, execution,
     resetRefinement]() mutable {
      SamResult result;
      result.revision = revision;
      result.layerId = layerId;
      result.encoderPath = encoder;
      result.decoderPath = decoder;
      try
      {
        result.segmenter = existingSegmenter;
        if(!result.segmenter)
        {
          result.segmenter = std::make_shared<rawlab::ai::Sam2Segmenter>(
            std::filesystem::path{encoder.toStdString()}, std::filesystem::path{decoder.toStdString()},
            execution);
          result.segmenter->encode(preview);
        }
        if(resetRefinement) result.segmenter->resetRefinement();
        result.mask = result.segmenter->segment(points);
      }
      catch(const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
      return result;
    }));
}

QString MainWindow::modelPath(const QString &modelId) const
{
  return ApplicationSettingsStore::modelById(appSettings_, modelId).path;
}

bool MainWindow::processingReady() const
{
  if(!pendingMaskEffects_.isEmpty()) return false;
  const auto ready = [this](const QString &modelId) {
    const QString path = modelPath(modelId);
    return !path.isEmpty() && denoisedCache_.contains(path);
  };
  if(globalEnabled_ && !ready(globalModelId_)) return false;
  if(globalEnabled_ && globalSharpenMode_ != SharpenMode::Off)
  {
    const QString sharpenPath = globalSharpenMode_ == SharpenMode::Normal
      ? normalSharpenPath() : sharpenModelPath();
    const QString denoisePath = modelPath(globalModelId_);
    if(sharpenPath.isEmpty()
       || !sharpenedCache_.contains(sharpenCacheKey(denoisePath, sharpenPath))) return false;
  }
  for(const MaskLayer &layer : layers_)
  {
    if(!layer.enabled) continue;
    if(!ready(layer.modelId)) return false;
    if(layer.sharpenMode != SharpenMode::Off)
    {
      const QString sharpenPath = layer.sharpenMode == SharpenMode::Normal
        ? normalSharpenPath() : sharpenModelPath();
      const QString denoisePath = modelPath(layer.modelId);
      if(sharpenPath.isEmpty()
         || !sharpenedCache_.contains(sharpenCacheKey(denoisePath, sharpenPath))) return false;
    }
  }
  return true;
}

void MainWindow::queueModel(const QString &modelId)
{
  if(!source_) return;
  const QString path = modelPath(modelId);
  if(path.isEmpty())
  {
    statusLabel_->setText(tr("The selected layer references a missing denoise model"));
    return;
  }
  if(denoisedCache_.contains(path)) { scheduleComposition(); return; }
  if(path == runningModelPath_ || modelQueue_.contains(path)) return;
  if(denoiseWatcher_.isRunning()) { modelQueue_.push_back(path); return; }
  runDenoisePath(path);
}

void MainWindow::queueRecipeProcessing()
{
  if(!source_) return;
  pendingRecipeSharpenRequests_.clear();
  QSet<QString> modelIds;
  if(globalEnabled_)
  {
    modelIds.insert(globalModelId_);
    if(globalSharpenMode_ != SharpenMode::Off)
      pendingRecipeSharpenRequests_.insert(
        modelPath(globalModelId_) + QChar{0x1e}
        + QString::number(static_cast<int>(globalSharpenMode_)));
  }
  for(const MaskLayer &layer : layers_)
  {
    if(!layer.enabled) continue;
    modelIds.insert(layer.modelId);
    if(layer.sharpenMode != SharpenMode::Off)
      pendingRecipeSharpenRequests_.insert(
        modelPath(layer.modelId) + QChar{0x1e}
        + QString::number(static_cast<int>(layer.sharpenMode)));
  }
  for(const QString &modelId : modelIds) queueModel(modelId);
  queuePendingRecipeSharpening();
  scheduleComposition();
}

void MainWindow::queuePendingRecipeSharpening()
{
  const QStringList requests = pendingRecipeSharpenRequests_.values();
  for(const QString &request : requests)
  {
    const int separator = request.lastIndexOf(QChar{0x1e});
    if(separator < 0)
    {
      pendingRecipeSharpenRequests_.remove(request);
      continue;
    }
    const QString denoisePath = request.left(separator);
    const SharpenMode mode = static_cast<SharpenMode>(request.mid(separator + 1).toInt());
    const QString sharpenPath = mode == SharpenMode::Normal ? normalSharpenPath() : sharpenModelPath();
    if(denoisePath.isEmpty() || sharpenPath.isEmpty()) continue;
    if(sharpenedCache_.contains(sharpenCacheKey(denoisePath, sharpenPath)))
    {
      pendingRecipeSharpenRequests_.remove(request);
      continue;
    }
    queueSharpenPath(denoisePath, mode);
  }
}

void MainWindow::runSelectedModel()
{
  if(!source_ || modelCombo_->currentIndex() < 0) return;
  const QString id = modelCombo_->currentData().toString();
  if(id.isEmpty()) return;
  if(selectedLayerId() == globalLayerId) globalModelId_ = id;
  else if(MaskLayer *layer = selectedMaskLayer()) layer->modelId = id;
  compositionValid_ = false;
  beforeAfterAction_->setChecked(false);
  refreshActionState();
  failedModelPath_.clear();
  const QString path = modelPath(id);
  if(denoisedCache_.contains(path))
  {
    const MaskLayer *layer = selectedMaskLayer();
     const SharpenMode mode = selectedLayerId() == globalLayerId
       ? globalSharpenMode_ : layer ? layer->sharpenMode : SharpenMode::Off;
     const QString sharpenPath = mode == SharpenMode::Normal ? normalSharpenPath() : sharpenModelPath();
     const bool needsSharpen = mode != SharpenMode::Off && !sharpenPath.isEmpty()
       && !sharpenedCache_.contains(sharpenCacheKey(path, sharpenPath));
    if(needsSharpen) queueSharpen(id);
    else scheduleComposition();
  }
  else
  {
     if((selectedLayerId() == globalLayerId && globalSharpenMode_ != SharpenMode::Off)
        || (selectedMaskLayer() && selectedMaskLayer()->sharpenMode != SharpenMode::Off))
      pendingSharpenPaths_.insert(path);
    queueModel(id);
  }
  refreshModelRunButton();
}

void MainWindow::refreshModelRunButton()
{
  if(!modelCombo_ || !previewWidget_) return;
  const QString selectedId = modelCombo_->currentData().toString();
  const QString path = modelPath(selectedId);
  bool update = source_ && !path.isEmpty() && !denoisedCache_.contains(path);
  const MaskLayer *layer = selectedMaskLayer();
  const SharpenMode mode = selectedLayerId() == globalLayerId
    ? globalSharpenMode_ : layer ? layer->sharpenMode : SharpenMode::Off;
  if(!update && mode != SharpenMode::Off)
  {
    const QString sharpenPath = mode == SharpenMode::Normal ? normalSharpenPath() : sharpenModelPath();
    update = !sharpenPath.isEmpty()
      && !sharpenedCache_.contains(sharpenCacheKey(path, sharpenPath));
  }
  previewWidget_->setUpdateAvailable(update, tr("Preview needs updating"));
}

QString MainWindow::sharpenModelPath() const
{
  return appSettings_.sharpenModels.isEmpty() ? QString{} : appSettings_.sharpenModels.front().path;
}

void MainWindow::queueSharpen(const QString &modelId)
{
  if(!source_) return;
  const QString denoisePath = modelPath(modelId);
  const bool global = selectedLayerId() == globalLayerId;
  const SharpenMode mode = global ? globalSharpenMode_
    : selectedMaskLayer() ? selectedMaskLayer()->sharpenMode : SharpenMode::Off;
  const QString sharpenPath = mode == SharpenMode::Normal ? normalSharpenPath() : sharpenModelPath();
  if(denoisePath.isEmpty() || mode == SharpenMode::Off || sharpenPath.isEmpty()) return;
  queueSharpenPath(denoisePath, mode);
}

void MainWindow::queueSharpenPath(const QString &denoisePath, const SharpenMode mode)
{
  if(!source_ || denoisePath.isEmpty() || mode == SharpenMode::Off) return;
  const QString sharpenPath = mode == SharpenMode::Normal ? normalSharpenPath() : sharpenModelPath();
  if(sharpenPath.isEmpty()) return;
  const auto denoised = denoisedCache_.find(denoisePath);
  if(denoised == denoisedCache_.end()) return;
  const QString key = sharpenCacheKey(denoisePath, sharpenPath);
  if(sharpenedCache_.contains(key)) { scheduleComposition(); return; }
  const QString queued = denoisePath + QChar{0x1e} + QString::number(static_cast<int>(mode));
  if(runningSharpenKey_ == key || sharpenQueue_.contains(queued)) return;
  if(sharpenWatcher_.isRunning()) { sharpenQueue_.push_back(queued); return; }
  runSharpenPath(denoisePath, mode);
}

void MainWindow::runSharpenPath(const QString &denoisePath, const SharpenMode mode)
{
  const QString sharpenPath = mode == SharpenMode::Normal ? normalSharpenPath() : sharpenModelPath();
  const auto denoised = denoisedCache_.find(denoisePath);
  if(sharpenPath.isEmpty() || denoised == denoisedCache_.end() || sharpenWatcher_.isRunning()) return;
  const QString key = sharpenCacheKey(denoisePath, sharpenPath);
  if(sharpenedCache_.contains(key)) return;
  runningSharpenKey_ = key;
  const auto input = denoised->second;
  const auto execution = onnxOptions(appSettings_);
  const float iso = source_ && source_->metadata().iso
    ? static_cast<float>(*source_->metadata().iso) : 100.0f;
  setBusy(mode == SharpenMode::Normal ? tr("Running sharpening...") : tr("Running DeepSharpen..."), true);
  setCanvasBusy(QStringLiteral("sharpen"), true);
  progressBar_->setRange(0, 0);
  sharpenWatcher_.setFuture(QtConcurrent::run([this, input, sharpenPath, key, iso, execution, mode] {
    SharpenResult result;
    result.cacheKey = key;
    try
    {
      if(mode == SharpenMode::Normal)
      {
        result.image = std::make_shared<rawlab::raw::RawImage>(
          rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(*input, 1.0f));
      }
      else
      {
        rawlab::ai::OnnxSession session{std::filesystem::path{sharpenPath.toStdString()}, execution};
        result.image = std::make_shared<rawlab::raw::RawImage>(rawlab::ai::RawForgeAdapter{}.run(
        *input, session, iso, false, [this](const int completed, const int total) {
          QMetaObject::invokeMethod(this, [this, completed, total] {
            progressBar_->setRange(0, total);
            progressBar_->setValue(completed);
            statusLabel_->setText(tr("DeepSharpen tile %1 of %2").arg(completed).arg(total));
          }, Qt::QueuedConnection);
        }));
      }
    }
    catch(const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
  }));
}

void MainWindow::runDenoisePath(const QString &path)
{
  if(!source_ || path.isEmpty() || denoiseWatcher_.isRunning()) return;
  if(denoisedCache_.contains(path)) { scheduleComposition(); return; }
  runningModelPath_ = path;
  denoiseProgressPercent_ = 0;
  refreshModelRunButton();
  const auto source = source_;
  const auto execution = onnxOptions(appSettings_);
  QString adapter;
  for(const DenoiseModelDefinition &model : appSettings_.denoiseModels)
    if(model.path == path) { adapter = model.adapter; break; }
  setBusy(adapter == QStringLiteral("rawforge-rgb-v1")
            ? tr("Running RawForge denoise...") : tr("Running RawNIND denoise..."), true);
  setCanvasBusy(QStringLiteral("denoise"), true);
  progressBar_->setRange(0, 0);
  denoiseWatcher_.setFuture(QtConcurrent::run([this, source, path, adapter, execution] {
    DenoiseResult result;
    result.modelPath = path;
    try
    {
      rawlab::ai::OnnxSession session{std::filesystem::path{path.toStdString()}, execution};
      result.executionProvider = QString::fromStdString(session.modelInfo().executionProvider);
      const auto progress = [this](const int completed, const int total) {
          QMetaObject::invokeMethod(this, [this, completed, total] {
            progressBar_->setRange(0, total);
            progressBar_->setValue(completed);
            denoiseProgressPercent_ = total > 0 ? completed * 100 / total : 0;
            refreshModelRunButton();
            statusLabel_->setText(tr("Denoising tile %1 of %2").arg(completed).arg(total));
          }, Qt::QueuedConnection);
        };
      if(adapter == QStringLiteral("rawforge-rgb-v1"))
      {
        const float iso = source->metadata().iso ? static_cast<float>(*source->metadata().iso) : 100.0f;
        result.image = std::make_shared<rawlab::raw::RawImage>(
          rawlab::ai::RawForgeAdapter{}.run(*source, session, iso, true, progress));
      }
      else
      {
        rawlab::pipeline::RawProcessingPipeline pipeline;
        result.image = std::make_shared<rawlab::raw::RawImage>(
          pipeline.runRawNindDenoise(*source, session, progress));
      }
    }
    catch(const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
  }));
}

void MainWindow::refreshModelCombo()
{
  if(!modelCombo_) return;
  const QString selectedId = selectedLayerId() == globalLayerId
    ? globalModelId_ : selectedMaskLayer() ? selectedMaskLayer()->modelId : QString{};
  const QSignalBlocker blocker{modelCombo_};
  modelCombo_->clear();
  for(const DenoiseModelDefinition &model : appSettings_.denoiseModels)
  {
    if(model.path.isEmpty() || !QFileInfo::exists(model.path)) continue;
    modelCombo_->addItem(model.name, model.id);
    modelCombo_->setItemData(modelCombo_->count() - 1, model.path, Qt::ToolTipRole);
  }
  const int index = modelCombo_->findData(selectedId);
  modelCombo_->setCurrentIndex(index);
  refreshModelRunButton();
}

void MainWindow::showSettings()
{
  if(batchWatcher_.isRunning() || denoiseWatcher_.isRunning() || sharpenWatcher_.isRunning()
     || samWatcher_.isRunning() || composeWatcher_.isRunning())
  {
    statusLabel_->setText(tr("Wait for current processing to finish before changing settings"));
    return;
  }
  SettingsDialog dialog{appSettings_, this};
  if(dialog.exec() != QDialog::Accepted)
  {
    appSettings_ = settingsStore_.load();
    refreshLayerControls();
    refreshActionState();
    return;
  }
  const ApplicationSettingsData updated = dialog.settings();
  const bool runtimeChanged = updated.onnxRuntimeLibrary != appSettings_.onnxRuntimeLibrary;
  const bool samChanged = activeSamPath(updated, QStringLiteral("encoder.onnx"))
                            != activeSamPath(appSettings_, QStringLiteral("encoder.onnx"))
                          || activeSamPath(updated, QStringLiteral("decoder.onnx"))
                               != activeSamPath(appSettings_, QStringLiteral("decoder.onnx"))
                          || updated.onnxProvider != appSettings_.onnxProvider
                          || updated.onnxDeviceId != appSettings_.onnxDeviceId;
  appSettings_ = updated;
  settingsStore_.save(appSettings_);
  if(runtimeChanged)
    statusLabel_->setText(tr("ONNX Runtime changed; restart RawLab to apply it"));
  if(samChanged)
  {
    segmenter_.reset();
    samNeedsReset_ = true;
    lastSegmentLayerId_ = 0;
    ++samRevision_;
  }
  denoisedCache_.clear();
  sharpenedCache_.clear();
  modelProviders_.clear();
  modelQueue_.clear();
  sharpenQueue_.clear();
  refreshModelCombo();
  scheduleComposition();
}

void MainWindow::showLibrary()
{
  if(batchWatcher_.isRunning()) return;
  saveCurrentRecipe();
  endMaskEdit();
  pages_->setCurrentWidget(libraryPage_);
  refreshLibrary();
  libraryAction_->setChecked(true);
  refreshActionState();
}

void MainWindow::showEditor()
{
  if(!source_ || loadWatcher_.isRunning() || batchWatcher_.isRunning()) return;
  pages_->setCurrentWidget(editorPage_);
  editorAction_->setChecked(true);
  ++metadataRevision_;
  pendingMetadataPath_.clear();
  refreshMetadata();
  refreshActionState();
}

void MainWindow::refreshFilmstrip()
{
  if(!filmstrip_) return;
  const QSignalBlocker blocker{filmstrip_};
  filmstrip_->clear();
  for(const QString &path : batchPaths_)
  {
    const QFileInfo info{path};
    auto *item = new QListWidgetItem{libraryPage_->iconForPath(path), info.fileName(), filmstrip_};
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    if(path == sourcePath_) filmstrip_->setCurrentItem(item);
  }
}

void MainWindow::refreshMetadata()
{
  if(!metadataDetailsLabel_) return;
  if(!source_ || sourcePath_.isEmpty())
  {
    metadataDetailsLabel_->setText(tr("No picture selected"));
    return;
  }

  displayMetadata(sourcePath_, source_->metadata(), source_->width(), source_->height());
}

void MainWindow::displayMetadata(const QString &path,
                                 const rawlab::raw::RawMetadata &metadata,
                                 const int rawWidth, const int rawHeight)
{
  const QFileInfo file{path};
  QStringList details{file.absoluteFilePath()};
  const QString camera = QString::fromStdString(metadata.make + " " + metadata.model).trimmed();
  if(!camera.isEmpty()) details.push_back(tr("Camera: %1").arg(camera));
  if(metadata.iso) details.push_back(QStringLiteral("ISO %1").arg(compactNumber(*metadata.iso, 0)));
  if(metadata.aperture) details.push_back(QStringLiteral("f/%1").arg(compactNumber(*metadata.aperture)));
  if(metadata.exposureTimeSeconds) details.push_back(shutterText(*metadata.exposureTimeSeconds));
  if(metadata.focalLengthMm) details.push_back(QStringLiteral("%1 mm").arg(compactNumber(*metadata.focalLengthMm)));
  if(metadata.captureTimestamp)
  {
    const QDateTime captured = QDateTime::fromSecsSinceEpoch(*metadata.captureTimestamp).toLocalTime();
    if(captured.isValid()) details.push_back(QLocale{}.toString(captured, QLocale::ShortFormat));
  }
  details.push_back(tr("Raw dimensions: %1 x %2").arg(rawWidth).arg(rawHeight));
  details.push_back(tr("Active dimensions: %1 x %2")
                    .arg(metadata.activeArea.width).arg(metadata.activeArea.height));
  details.push_back(tr("File size: %1 MiB")
                    .arg(QLocale{}.toString(static_cast<double>(file.size()) / (1024.0 * 1024.0), 'f', 1)));
  metadataDetailsLabel_->setText(details.join(QStringLiteral("  |  ")));
}

void MainWindow::requestLibraryMetadata(const QString &path)
{
  if(!metadataDetailsLabel_ || pages_->currentWidget() != libraryPage_) return;
  ++metadataRevision_;
  if(path.isEmpty())
  {
    pendingMetadataPath_.clear();
    metadataDetailsLabel_->setText(tr("No picture selected"));
    return;
  }
  if(source_ && PictureLibraryStore::normalizedPath(path)
                == PictureLibraryStore::normalizedPath(sourcePath_))
  {
    pendingMetadataPath_.clear();
    displayMetadata(path, source_->metadata(), source_->width(), source_->height());
    return;
  }
  const auto cached = metadataCache_.find(metadataCacheKey(path));
  if(cached != metadataCache_.end())
  {
    pendingMetadataPath_.clear();
    displayMetadata(path, cached->second.metadata, cached->second.rawWidth, cached->second.rawHeight);
    return;
  }
  metadataDetailsLabel_->setText(tr("Loading metadata for %1  |  %2")
                                   .arg(QFileInfo{path}.fileName(), path));
  if(metadataWatcher_.isRunning()) pendingMetadataPath_ = path;
  else startLibraryMetadata(path, metadataRevision_);
}

void MainWindow::startLibraryMetadata(const QString &path, const int revision)
{
  metadataWatcher_.setFuture(QtConcurrent::run([path, revision] {
    MetadataResult result;
    result.revision = revision;
    result.path = path;
    try
    {
      result.metadata = rawlab::raw::LibRawDecoder{}.readMetadata(
        std::filesystem::path{path.toStdString()});
    }
    catch(const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
    return result;
  }));
}

void MainWindow::refreshLibrary()
{
  if(!libraryPage_) return;
  const QStringList files = libraryStore_.files();
  libraryPage_->setFiles(files);
  for(const QString &path : files) requestLibraryThumbnail(path);
}

void MainWindow::importPictures()
{
  if(batchWatcher_.isRunning()) return;
  const QStringList files = QFileDialog::getOpenFileNames(
    this, tr("Import RAW pictures"), QString{},
    tr("RAW images (*.ARW *.arw *.CR2 *.cr2 *.CR3 *.cr3 *.NEF *.nef *.RAF *.raf *.DNG *.dng);;All files (*)"));
  if(files.isEmpty()) return;
  libraryStore_.addFiles(files);
  showLibrary();
}

void MainWindow::importFolder()
{
  if(batchWatcher_.isRunning()) return;
  const QString folder = QFileDialog::getExistingDirectory(this, tr("Import RAW folder"));
  if(folder.isEmpty()) return;
  const QStringList filters{
    QStringLiteral("*.ARW"), QStringLiteral("*.arw"), QStringLiteral("*.CR2"), QStringLiteral("*.cr2"),
    QStringLiteral("*.CR3"), QStringLiteral("*.cr3"), QStringLiteral("*.NEF"), QStringLiteral("*.nef"),
    QStringLiteral("*.RAF"), QStringLiteral("*.raf"), QStringLiteral("*.DNG"), QStringLiteral("*.dng"),
  };
  QStringList files;
  QDirIterator iterator{folder, filters, QDir::Files, QDirIterator::Subdirectories};
  while(iterator.hasNext()) files.push_back(iterator.next());
  libraryStore_.addFiles(files);
  showLibrary();
}

void MainWindow::requestLibraryThumbnail(const QString &path)
{
  const QFileInfo info{path};
  if(!info.exists()) return;
  const QString cachePath = thumbnailCachePath(path);
  if(QFileInfo::exists(cachePath))
  {
    const QImage cached{cachePath};
    if(!cached.isNull())
    {
      libraryPage_->setThumbnail(path, cached);
      refreshFilmstrip();
      return;
    }
    QFile::remove(cachePath);
  }
  if(thumbnailJobs_.contains(path) || thumbnailQueue_.contains(path)) return;
  if(activeThumbnailJobs_ >= 2)
  {
    thumbnailQueue_.push_back(path);
    return;
  }
  startLibraryThumbnail(path);
}

void MainWindow::startLibraryThumbnail(const QString &path)
{
  const QFileInfo info{path};
  if(!info.exists()) return;
  const QString cachePath = thumbnailCachePath(path);
  thumbnailJobs_.insert(path);
  ++activeThumbnailJobs_;

  auto *watcher = new QFutureWatcher<QImage>{this};
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path, cachePath] {
    const QImage image = watcher->result();
    if(!image.isNull())
    {
      image.save(cachePath, "PNG");
      libraryPage_->setThumbnail(path, image);
      refreshFilmstrip();
    }
    thumbnailJobs_.remove(path);
    --activeThumbnailJobs_;
    watcher->deleteLater();
    while(!thumbnailQueue_.isEmpty())
    {
      const QString next = thumbnailQueue_.takeFirst();
      if(QFileInfo::exists(next)) { startLibraryThumbnail(next); break; }
    }
  });
  watcher->setFuture(QtConcurrent::run([path] {
    try
    {
      rawlab::raw::LibRawDecoder decoder;
      const rawlab::raw::RawImage raw = decoder.decodeRawImage(std::filesystem::path{path.toStdString()});
      const rawlab::image::RawPreview preview = rawlab::image::renderRawPreview(raw, 360);
      return QImage{preview.image.pixels.data(), preview.image.width, preview.image.height,
                    preview.image.width * 3, QImage::Format_RGB888}.copy();
    }
    catch(const std::exception &) { return QImage{}; }
  }));
}

PictureRecipe MainWindow::currentRecipe()
{
  syncLayerOrder();
  PictureRecipe recipe;
  recipe.previewWidth = originalPreview_.image.width;
  recipe.previewHeight = originalPreview_.image.height;
  recipe.globalModelId = globalModelId_;
  recipe.globalBlend = globalBlend_;
  recipe.globalEnabled = globalEnabled_;
  recipe.globalSharpenEnabled = globalSharpenEnabled_;
  recipe.globalSharpenStrength = globalSharpenStrength_;
  recipe.globalSharpenMode = globalSharpenMode_;
  recipe.preserveClipped = preserveClipped_;
  const float width = static_cast<float>(std::max(1, recipe.previewWidth));
  const float height = static_cast<float>(std::max(1, recipe.previewHeight));
  const float radiusScale = std::min(width, height);
  for(const MaskLayer &layer : layers_)
  {
    PictureRecipeLayer saved;
    saved.id = layer.id;
    saved.name = layer.name;
    saved.modelId = layer.modelId;
    saved.blend = layer.blend;
    saved.sharpenEnabled = layer.sharpenEnabled;
    saved.sharpenStrength = layer.sharpenStrength;
    saved.sharpenMode = layer.sharpenMode;
    saved.enabled = layer.enabled;
    saved.inverted = layer.inverted;
    saved.showMask = layer.showMask;
    saved.feather = layer.featherRadius / radiusScale;
    saved.blur = layer.blurRadius / radiusScale;
    // Store the flattened result so recipes remain visually compatible with older versions.
    saved.baseMask = layer.mask;
    for(const auto &point : layer.points)
    {
      saved.prompts.push_back({point.x / width, point.y / height, point.foreground});
    }
    for(const MaskLayer::BrushStroke &stroke : layer.brushStrokes)
    {
      PictureRecipeStroke savedStroke;
      savedStroke.radius = stroke.radius / radiusScale;
      savedStroke.hardness = stroke.hardness;
      savedStroke.value = stroke.value;
      for(const QPointF &point : stroke.points)
      {
        savedStroke.points.push_back({point.x() / width, point.y() / height});
      }
      saved.strokes.push_back(std::move(savedStroke));
    }
    for(int category = 0; category < 3; ++category)
    {
      const std::size_t index = static_cast<std::size_t>(category);
      saved.categoryMasks[index] = layer.categories[index].mask;
      for(const MaskLayer::BrushStroke &stroke : layer.categories[index].brushStrokes)
      {
        PictureRecipeStroke savedStroke;
        savedStroke.radius = stroke.radius / radiusScale;
        savedStroke.hardness = stroke.hardness;
        savedStroke.value = stroke.value;
        for(const QPointF &point : stroke.points)
          savedStroke.points.push_back({point.x() / width, point.y() / height});
        saved.categoryStrokes[index].push_back(std::move(savedStroke));
      }
    }
    recipe.layers.push_back(std::move(saved));
  }
  return recipe;
}

void MainWindow::saveCurrentRecipe()
{
  if(sourcePath_.isEmpty() || !source_) return;
  if(deletedRecipePaths_.contains(PictureLibraryStore::normalizedPath(sourcePath_))) return;
  try { recipeStore_.save(sourcePath_, currentRecipe()); }
  catch(const std::exception &) {}
}

void MainWindow::loadCurrentRecipe()
{
  const QSignalBlocker blocker{layerList_};
  const auto recipe = recipeStore_.load(sourcePath_, originalPreview_.image.width,
                                        originalPreview_.image.height);
  if(!recipe)
  {
    layerList_->item(0)->setData(ProcessingEnabledRole, globalEnabled_);
    refreshLayerControls();
    return;
  }
  applyRecipe(*recipe);
}

void MainWindow::applyRecipe(const PictureRecipe &recipe)
{
  globalModelId_ = recipe.globalModelId.isEmpty() ? globalModelId_ : recipe.globalModelId;
  globalBlend_ = recipe.globalBlend;
  globalEnabled_ = recipe.globalEnabled;
  globalSharpenEnabled_ = recipe.globalSharpenEnabled;
  globalSharpenStrength_ = recipe.globalSharpenStrength;
  globalSharpenMode_ = recipe.globalSharpenMode;
  preserveClipped_ = recipe.preserveClipped;
  layerList_->item(0)->setData(ProcessingEnabledRole, globalEnabled_);
  const float width = static_cast<float>(std::max(1, originalPreview_.image.width));
  const float height = static_cast<float>(std::max(1, originalPreview_.image.height));
  const float radiusScale = std::min(width, height);
  for(const PictureRecipeLayer &saved : recipe.layers)
  {
    MaskLayer layer;
    layer.id = saved.id > 0 ? saved.id : nextLayerId_++;
    nextLayerId_ = std::max(nextLayerId_, layer.id + 1);
    layer.name = saved.name.isEmpty() ? tr("Mask %1").arg(layer.id) : saved.name;
    layer.modelId = saved.modelId.isEmpty() ? globalModelId_ : saved.modelId;
    layer.blend = saved.blend;
    layer.sharpenEnabled = saved.sharpenEnabled;
    layer.sharpenStrength = saved.sharpenStrength;
    layer.sharpenMode = saved.sharpenMode;
    layer.enabled = saved.enabled;
    layer.inverted = saved.inverted;
    layer.showMask = saved.showMask;
    layer.featherRadius = saved.feather * radiusScale;
    layer.blurRadius = saved.blur * radiusScale;
    bool hasCategoryData = false;
    for(int category = 0; category < 3; ++category)
    {
      const std::size_t index = static_cast<std::size_t>(category);
      layer.categories[index].mask = saved.categoryMasks[index];
      hasCategoryData = hasCategoryData || layer.categories[index].mask.has_value()
                        || !saved.categoryStrokes[index].empty();
      for(const PictureRecipeStroke &savedStroke : saved.categoryStrokes[index])
      {
        MaskLayer::BrushStroke stroke;
        stroke.radius = savedStroke.radius * radiusScale;
        stroke.hardness = savedStroke.hardness;
        stroke.value = savedStroke.value;
        for(const QPointF &point : savedStroke.points)
          stroke.points.push_back({std::clamp(point.x(), 0.0, 1.0) * width,
                                   std::clamp(point.y(), 0.0, 1.0) * height});
        layer.categories[index].brushStrokes.push_back(std::move(stroke));
      }
    }
    if(!hasCategoryData) layer.samMask = saved.baseMask;
    for(const PictureRecipePrompt &prompt : saved.prompts)
    {
      layer.points.push_back({std::min(std::clamp(prompt.x, 0.0f, 1.0f) * width, width - 0.001f),
                              std::min(std::clamp(prompt.y, 0.0f, 1.0f) * height, height - 0.001f),
                              prompt.foreground});
    }
    for(const PictureRecipeStroke &savedStroke : saved.strokes)
    {
      MaskLayer::BrushStroke stroke;
      stroke.radius = savedStroke.radius * radiusScale;
      stroke.hardness = savedStroke.hardness;
      stroke.value = savedStroke.value;
      for(const QPointF &point : savedStroke.points)
      {
        stroke.points.push_back({std::clamp(point.x(), 0.0, 1.0) * width,
                                 std::clamp(point.y(), 0.0, 1.0) * height});
      }
      layer.brushStrokes.push_back(std::move(stroke));
    }
    layers_.push_back(std::move(layer));
    MaskLayer &addedLayer = layers_.back();
    rebuildLayerMask(addedLayer);
    auto *item = new QListWidgetItem{addedLayer.name};
    item->setData(Qt::UserRole, addedLayer.id);
    item->setData(ProcessingEnabledRole, addedLayer.enabled);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                   | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    layerList_->addItem(item);
  }
  layerList_->setCurrentRow(0);
  refreshLayerControls();
  scheduleComposition();
}

void MainWindow::scheduleComposition()
{
  if(!source_) return;
  ++compositionRevision_;
  exportAction_->setEnabled(false);
  if(composeWatcher_.isRunning()) { compositionPending_ = true; return; }
  compositionTimer_.start();
}

void MainWindow::startComposition()
{
  if(!source_) return;
  if(composeWatcher_.isRunning()) { compositionPending_ = true; return; }
  if(!processingReady())
  {
    refreshModelRunButton();
    return;
  }
  syncLayerOrder();
  const int generation = documentGeneration_;
  const int revision = compositionRevision_;
  const auto source = source_;
  const auto cache = denoisedCache_;
  const auto sharpenCache = sharpenedCache_;
  const auto layers = layers_;
  const auto models = appSettings_.denoiseModels;
  const QString sharpenPath = sharpenModelPath();
  const SharpenMode globalSharpenMode = globalSharpenMode_;
  const auto geometry = originalPreview_.geometry;
  const QString globalModelId = globalModelId_;
  const float globalBlend = globalBlend_;
  const bool globalEnabled = globalEnabled_;
  const float globalSharpenStrength = globalSharpenStrength_;
  const bool preserveClipped = preserveClipped_;
  const auto quality = highQualityAction_->isChecked()
    ? rawlab::image::PreviewQuality::High : rawlab::image::PreviewQuality::Fast;
  statusLabel_->setText(tr("Updating processed preview..."));
  setCanvasBusy(QStringLiteral("compose"), true);

  composeWatcher_.setFuture(QtConcurrent::run(
     [generation, revision, source, cache, sharpenCache, layers, models, sharpenPath, geometry,
      globalModelId, globalBlend, globalEnabled, globalSharpenMode,
      globalSharpenStrength,
      preserveClipped, quality] {
      ComposeResult result;
      result.generation = generation;
      result.revision = revision;
      try
      {
        const auto pathForId = [&models](const QString &id) {
          for(const DenoiseModelDefinition &model : models) if(model.id == id) return model.path;
          return QString{};
        };
        const auto global = cache.find(pathForId(globalModelId));
        std::optional<rawlab::raw::RawImage> preservedGlobal;
        std::optional<rawlab::raw::RawImage> sharpenMixedGlobal;
        const rawlab::raw::RawImage *globalImage = nullptr;
        if(global != cache.end()) globalImage = global->second.get();
         if(globalImage && globalSharpenMode != SharpenMode::Off)
         {
           const auto sharpened = sharpenCache.find(sharpenCacheKey(pathForId(globalModelId),
             globalSharpenMode == SharpenMode::Normal ? normalSharpenPath() : sharpenPath));
          if(sharpened != sharpenCache.end())
          {
            sharpenMixedGlobal = rawlab::raw::blendWithOriginal(
              *globalImage, *sharpened->second, globalSharpenStrength);
            globalImage = &*sharpenMixedGlobal;
          }
        }
        if(globalImage && preserveClipped)
        {
          preservedGlobal = rawlab::raw::preserveClippedHighlights(*source, *globalImage);
          globalImage = &*preservedGlobal;
        }

        std::vector<rawlab::pipeline::LocalLayerInput> localInputs;
        localInputs.reserve(layers.size());
        std::vector<rawlab::raw::RawImage> preservedLocals;
        preservedLocals.reserve(layers.size());
        std::vector<rawlab::raw::RawImage> sharpenMixedLocals;
        sharpenMixedLocals.reserve(layers.size());
        for(auto it = layers.rbegin(); it != layers.rend(); ++it)
        {
          const QString denoisePath = pathForId(it->modelId);
          const auto denoised = cache.find(denoisePath);
          if(denoised == cache.end()) continue;
          const rawlab::raw::RawImage *localImage = denoised->second.get();
           if(it->sharpenMode != SharpenMode::Off)
           {
             const auto sharpened = sharpenCache.find(sharpenCacheKey(denoisePath,
               it->sharpenMode == SharpenMode::Normal ? normalSharpenPath() : sharpenPath));
            if(sharpened != sharpenCache.end())
            {
              sharpenMixedLocals.push_back(rawlab::raw::blendWithOriginal(
                *denoised->second, *sharpened->second, it->sharpenStrength));
              localImage = &sharpenMixedLocals.back();
            }
          }
          if(preserveClipped)
          {
            preservedLocals.push_back(rawlab::raw::preserveClippedHighlights(*source, *localImage));
            localImage = &preservedLocals.back();
          }
          const auto &mask = it->effectiveMask ? it->effectiveMask : it->mask;
          localInputs.push_back({localImage, mask ? &*mask : nullptr,
                                 it->blend, it->inverted, it->enabled});
        }
        rawlab::raw::RawImage current = rawlab::pipeline::composeDenoiseLayers(
          *source, {globalImage, globalBlend, globalEnabled}, localInputs, geometry);
        result.image = std::make_shared<rawlab::raw::RawImage>(std::move(current));
        result.preview = rawlab::image::renderRawPreview(
          *result.image, editorPreviewMaxDimension, quality);
      }
      catch(const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
      return result;
    }));
}

void MainWindow::refreshLayerControls()
{
  const bool global = selectedLayerId() == globalLayerId;
  const MaskLayer *layer = selectedMaskLayer();
  const QSignalBlocker blendBlocker{blendSlider_};
  const QSignalBlocker invertBlocker{invertCheck_};
  const QSignalBlocker showMaskBlocker{showMaskCheck_};
  const QSignalBlocker clippedBlocker{clippedCheck_};
  const QSignalBlocker featherBlocker{featherSlider_};
  const QSignalBlocker blurBlocker{blurSlider_};
  const QSignalBlocker sharpenModeBlocker{sharpenModeCombo_};
  const QSignalBlocker sharpenSliderBlocker{sharpenSlider_};
  refreshModelCombo();
  const int blend = static_cast<int>((global ? globalBlend_ : layer ? layer->blend : 0.0f) * 100.0f + 0.5f);
  blendSlider_->setValue(blend);
  blendValueLabel_->setText(QString::number(blend) + QStringLiteral("%"));
  const int feather = layer ? static_cast<int>(std::lround(layer->featherRadius)) : 0;
  const int blur = layer ? static_cast<int>(std::lround(layer->blurRadius)) : 0;
  featherSlider_->setValue(feather);
  blurSlider_->setValue(blur);
  featherValueLabel_->setText(tr("%1 px").arg(feather));
  blurValueLabel_->setText(tr("%1 px").arg(blur));
  const int sharpen = global ? static_cast<int>(std::lround(globalSharpenStrength_ * 100.0f))
                             : layer ? static_cast<int>(std::lround(layer->sharpenStrength * 100.0f)) : 100;
  const SharpenMode mode = global ? globalSharpenMode_ : layer ? layer->sharpenMode : SharpenMode::Off;
  const bool sharpenEnabled = mode != SharpenMode::Off;
  sharpenModeCombo_->setCurrentIndex(static_cast<int>(mode));
  sharpenModeCombo_->setEnabled(global || layer != nullptr);
  sharpenSlider_->setValue(sharpen);
  sharpenSlider_->setEnabled((global || layer != nullptr) && sharpenEnabled);
  const bool showSharpenStrength = global || layer;
  sharpenRow_->setVisible(showSharpenStrength);
  sharpenLabel_->setVisible(showSharpenStrength);
  sharpenValueLabel_->setText(QString::number(sharpen) + QStringLiteral("%"));
  sharpenLabel_->setText(mode == SharpenMode::Ai ? tr("Blend") : tr("Strength"));
  featherSlider_->setEnabled(layer != nullptr);
  blurSlider_->setEnabled(layer != nullptr);
  featherRow_->setVisible(true);
  featherLabel_->setVisible(true);
  blurRow_->setVisible(true);
  blurLabel_->setVisible(true);
  invertCheck_->setChecked(layer && layer->inverted);
  invertCheck_->setEnabled(layer != nullptr);
  showMaskCheck_->setChecked(layer && layer->showMask);
  showMaskCheck_->setEnabled(layer != nullptr);
  showMaskCheck_->setVisible(true);
  clippedCheck_->setChecked(preserveClipped_);
  clippedCheck_->setVisible(global);
  refreshModelRunButton();
  editMaskButton_->setEnabled(source_ && layer);
  renameLayerButton_->setEnabled(layer != nullptr);
  deleteLayerButton_->setEnabled(layer != nullptr);
}

void MainWindow::refreshOverlay()
{
  const MaskLayer *layer = selectedMaskLayer();
  if(!layer || !layer->showMask || (!layer->effectiveMask && !layer->mask))
  {
    previewWidget_->setMask({});
  }
  else previewWidget_->setMask(maskOverlay(*layer));

  QVector<PreviewWidget::PromptMarker> prompts;
  if(layer && layer->id == editingLayerId_)
  {
    prompts.reserve(static_cast<qsizetype>(layer->points.size()));
    for(const auto &point : layer->points)
    {
      prompts.push_back({QPointF{point.x, point.y}, point.foreground});
    }
  }
  previewWidget_->setPrompts(std::move(prompts));
}

void MainWindow::syncLayerOrder()
{
  if(layerList_->count() == 0) return;
  int globalRow = -1;
  for(int row = 0; row < layerList_->count(); ++row)
  {
    if(layerList_->item(row)->data(Qt::UserRole).toInt() == globalLayerId) { globalRow = row; break; }
  }
  if(globalRow > 0)
  {
    const QSignalBlocker blocker{layerList_->model()};
    QListWidgetItem *global = layerList_->takeItem(globalRow);
    layerList_->insertItem(0, global);
  }

  std::vector<MaskLayer> ordered;
  ordered.reserve(layers_.size());
  for(int row = 1; row < layerList_->count(); ++row)
  {
    const int id = layerList_->item(row)->data(Qt::UserRole).toInt();
    if(id == globalLayerId) continue;
    const auto found = std::find_if(layers_.begin(), layers_.end(), [id](const MaskLayer &layer) {
      return layer.id == id;
    });
    if(found != layers_.end()) ordered.push_back(*found);
  }
  layers_ = std::move(ordered);
}

void MainWindow::setBusy(const QString &message, const bool busy)
{
  if(!message.isEmpty()) statusLabel_->setText(message);
  progressBar_->setVisible(busy);
  if(busy) progressBar_->setRange(0, 0);
  refreshActionState();
}

void MainWindow::setCanvasBusy(const QString &reason, const bool busy)
{
  if(busy) canvasBusyReasons_.insert(reason);
  else canvasBusyReasons_.remove(reason);
  previewWidget_->setProcessing(!canvasBusyReasons_.isEmpty());
  refreshActionState();
}

void MainWindow::refreshActionState()
{
  if(!exportAction_ || !batchAction_) return;
  const bool processing = loadWatcher_.isRunning() || denoiseWatcher_.isRunning()
                          || sharpenWatcher_.isRunning()
                          || composeWatcher_.isRunning() || samWatcher_.isRunning()
                          || maskEffectWatcher_.isRunning() || !pendingMaskEffects_.isEmpty()
                          || exportWatcher_.isRunning() || batchWatcher_.isRunning()
                          || compositionTimer_.isActive() || compositionPending_
                          || !canvasBusyReasons_.isEmpty();
  exportAction_->setText(!externalOutputPath_.isEmpty() ? tr("Export and Return to Darktable")
    : batchPaths_.size() > 1 ? tr("Export Selected...") : tr("Export DNG..."));
  exportAction_->setEnabled(!processing && (batchPaths_.size() > 1
    ? source_ != nullptr : composed_ != nullptr && compositionValid_));
  batchAction_->setEnabled(!processing && source_ != nullptr && batchPaths_.size() > 1);
  const bool navigating = loadWatcher_.isRunning() || batchWatcher_.isRunning();
  libraryAction_->setEnabled(!navigating);
  editorAction_->setEnabled(!navigating && source_ != nullptr);
  const bool editorControls = !navigating && pages_->currentWidget() == editorPage_
                              && previewWidget_->hasImage();
  fitAction_->setEnabled(editorControls);
  actualSizeAction_->setEnabled(editorControls);
  zoomInAction_->setEnabled(editorControls);
  zoomOutAction_->setEnabled(editorControls);
  highQualityAction_->setEnabled(editorControls);
  beforeAfterAction_->setEnabled(editorControls && compositionValid_ && processingReady());
  swapComparisonAction_->setEnabled(beforeAfterAction_->isEnabled() && beforeAfterAction_->isChecked());
  const bool inEditor = pages_->currentWidget() == editorPage_;
  filmstripAction_->setVisible(inEditor);
  for(QAction *action : {fitAction_, actualSizeAction_, zoomOutAction_, zoomInAction_,
                         highQualityAction_, beforeAfterAction_, swapComparisonAction_,
                         previewControlsSeparator_})
  {
    action->setVisible(inEditor);
  }
  zoomLabelAction_->setVisible(inEditor);
}

void MainWindow::showError(const QString &title, const QString &message)
{
  statusLabel_->setText(message);
  QMessageBox::critical(this, title, message);
}

MainWindow::MaskLayer *MainWindow::selectedMaskLayer()
{
  const int id = selectedLayerId();
  const auto found = std::find_if(layers_.begin(), layers_.end(), [id](const MaskLayer &layer) {
    return layer.id == id;
  });
  return found == layers_.end() ? nullptr : &*found;
}

const MainWindow::MaskLayer *MainWindow::selectedMaskLayer() const
{
  const int id = selectedLayerId();
  const auto found = std::find_if(layers_.begin(), layers_.end(), [id](const MaskLayer &layer) {
    return layer.id == id;
  });
  return found == layers_.end() ? nullptr : &*found;
}

int MainWindow::selectedLayerId() const
{
  const QListWidgetItem *item = layerList_ ? layerList_->currentItem() : nullptr;
  return item ? item->data(Qt::UserRole).toInt() : globalLayerId;
}

QImage MainWindow::toQImage(const rawlab::image::RgbImage &image) const
{
  return qImageCopy(image);
}

QImage MainWindow::maskOverlay(const MaskLayer &layer) const
{
  const auto &mask = layer.effectiveMask ? layer.effectiveMask : layer.mask;
  if(!mask) return {};
  QImage overlay{mask->width(), mask->height(), QImage::Format_ARGB32};
  for(int y = 0; y < mask->height(); ++y)
  {
    auto *line = reinterpret_cast<QRgb *>(overlay.scanLine(y));
    for(int x = 0; x < mask->width(); ++x)
    {
      float value = mask->sampleNearest(x, y);
      if(layer.inverted) value = 1.0f - value;
      const int alpha = static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 105.0f);
      line[x] = qRgba(230, 64, 92, alpha);
    }
  }
  return overlay;
}
