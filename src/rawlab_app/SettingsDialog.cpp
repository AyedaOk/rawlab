#include "SettingsDialog.h"
#include "ModelDownloader.h"
#include "rawlab/ai/OnnxSession.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

#include <exception>

SettingsDialog::SettingsDialog(const ApplicationSettingsData &settings, QWidget *parent)
  : QDialog{parent}
{
  sharpenModels_ = settings.sharpenModels;
  setWindowTitle(tr("RawLab Settings"));
  resize(850, 650);
  auto *layout = new QVBoxLayout{this};

  auto *accelerationGroup = new QGroupBox{tr("AI Acceleration"), this};
  auto *accelerationLayout = new QHBoxLayout{accelerationGroup};
  providerCombo_ = new QComboBox{accelerationGroup};
  providerCombo_->setObjectName(QStringLiteral("onnxProviderCombo"));
  providerCombo_->addItem(tr("Auto (best available)"), QStringLiteral("auto"));
  try
  {
    for(const rawlab::ai::OnnxProviderInfo &provider : rawlab::ai::OnnxSession::availableProviders())
      providerCombo_->addItem(QString::fromStdString(provider.name), QString::fromStdString(provider.id));
  }
  catch(const std::exception &)
  {
    providerCombo_->addItem(tr("CPU"), QStringLiteral("cpu"));
  }
  int providerIndex = providerCombo_->findData(settings.onnxProvider);
  if(providerIndex < 0) providerIndex = 0;
  providerCombo_->setCurrentIndex(providerIndex);
  deviceIdSpin_ = new QSpinBox{accelerationGroup};
  deviceIdSpin_->setObjectName(QStringLiteral("onnxDeviceIdSpin"));
  deviceIdSpin_->setRange(0, 31);
  deviceIdSpin_->setValue(settings.onnxDeviceId);
  deviceIdSpin_->setPrefix(tr("Device "));
  accelerationLayout->addWidget(providerCombo_);
  accelerationLayout->addWidget(deviceIdSpin_);
  accelerationLayout->addStretch();
  layout->addWidget(accelerationGroup);
  const auto refreshProviderStatus = [this] {
    const QString id = providerCombo_->currentData().toString();
    deviceIdSpin_->setVisible(id == QStringLiteral("cuda") || id == QStringLiteral("tensorrt")
                              || id == QStringLiteral("rocm") || id == QStringLiteral("migraphx")
                              || id == QStringLiteral("directml"));
  };
  connect(providerCombo_, &QComboBox::currentIndexChanged, this,
          [refreshProviderStatus](const int) { refreshProviderStatus(); });
  connect(deviceIdSpin_, &QSpinBox::valueChanged, this,
          [refreshProviderStatus](const int) { refreshProviderStatus(); });
  refreshProviderStatus();

  auto *samTitle = new QLabel{tr("SAM 2.1"), this};
  samTitle->setStyleSheet(QStringLiteral("font-weight:600;"));
  layout->addWidget(samTitle);
  samTable_ = new QTableWidget{0, 3, this};
  samTable_->setObjectName(QStringLiteral("samModelTable"));
  samTable_->setHorizontalHeaderLabels({tr("Active"), tr("Name"), tr("Model Folder")});
  samTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  samTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  samTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  samTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  for(const SamModelDefinition &model : settings.samModels)
  {
    addSamModel(model, model.id == settings.activeSamModelId);
  }
  for(int row = 0; row < samTable_->rowCount(); ++row)
  {
    if(samTable_->item(row, 0)->checkState() == Qt::Checked)
    {
      samTable_->setCurrentCell(row, 1);
      break;
    }
  }
  layout->addWidget(samTable_, 1);

  auto *samButtons = new QHBoxLayout;
  auto *addSamButton = new QPushButton{tr("Add Model"), this};
  auto *browseSamButton = new QPushButton{tr("Browse Folder"), this};
  auto *removeSamButton = new QPushButton{tr("Remove"), this};
  samButtons->addWidget(addSamButton);
  samButtons->addWidget(browseSamButton);
  samButtons->addWidget(removeSamButton);
  samButtons->addStretch();
  layout->addLayout(samButtons);
  samTitle->hide();
  samTable_->hide();
  addSamButton->hide();
  browseSamButton->hide();
  removeSamButton->hide();

  connect(samTable_, &QTableWidget::cellChanged, this, [this](const int row, const int column) {
    if(column != 0 || samTable_->item(row, 0)->checkState() != Qt::Checked) return;
    const QSignalBlocker blocker{samTable_};
    for(int other = 0; other < samTable_->rowCount(); ++other)
    {
      if(other != row) samTable_->item(other, 0)->setCheckState(Qt::Unchecked);
    }
  });
  connect(addSamButton, &QPushButton::clicked, this, [this] {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Add SAM Model"), tr("Model name"),
                                                QLineEdit::Normal, QString{}, &accepted).trimmed();
    if(!accepted || name.isEmpty()) return;
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose SAM model folder"));
    if(folder.isEmpty()) return;
    addSamModel({QUuid::createUuid().toString(QUuid::WithoutBraces), name, folder},
                samTable_->rowCount() == 0);
  });
  connect(browseSamButton, &QPushButton::clicked, this, [this] {
    const int row = samTable_->currentRow();
    if(row < 0) return;
    const QString folder = QFileDialog::getExistingDirectory(
      this, tr("Choose SAM model folder"), samTable_->item(row, 2)->text());
    if(!folder.isEmpty()) samTable_->item(row, 2)->setText(QDir::cleanPath(folder));
  });
  connect(removeSamButton, &QPushButton::clicked, this, [this] {
    if(samTable_->currentRow() < 0) return;
    samTable_->removeRow(samTable_->currentRow());
    ensureActiveSamModel();
  });

  auto *modelTitle = new QLabel{tr("Denoise Models"), this};
  modelTitle->setStyleSheet(QStringLiteral("font-weight:600;margin-top:12px;"));
  layout->addWidget(modelTitle);
  modelTable_ = new QTableWidget{0, 2, this};
  modelTable_->setObjectName(QStringLiteral("denoiseModelTable"));
  modelTable_->setHorizontalHeaderLabels({tr("Name"), tr("ONNX Path")});
  modelTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  modelTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  modelTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  for(const DenoiseModelDefinition &model : settings.denoiseModels) addModel(model);
  layout->addWidget(modelTable_, 1);

  auto *modelButtons = new QHBoxLayout;
  auto *addButton = new QPushButton{tr("Add Model"), this};
  auto *browseButton = new QPushButton{tr("Browse Path"), this};
  auto *removeButton = new QPushButton{tr("Remove"), this};
  modelButtons->addWidget(addButton);
  modelButtons->addWidget(browseButton);
  modelButtons->addWidget(removeButton);
  modelButtons->addStretch();
  layout->addLayout(modelButtons);
  modelTitle->hide();
  modelTable_->hide();
  addButton->hide();
  browseButton->hide();
  removeButton->hide();

  auto *managedGroup = new QGroupBox{tr("Model Manager"), this};
  auto *managedLayout = new QVBoxLayout{managedGroup};
  managedTable_ = new QTableWidget{0, 5, managedGroup};
  managedTable_->setObjectName(QStringLiteral("managedModelTable"));
  managedTable_->setHorizontalHeaderLabels({tr("Select"), tr("Name"), tr("Version"), tr("Task"), tr("Status")});
  managedTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  managedTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  managedTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  managedTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  managedTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  managedTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  managedTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  managedLayout->addWidget(managedTable_, 1);
  const auto addInstalledRow = [this](const QString &id, const QString &name,
                                      const QString &version, const QString &task,
                                      const QString &status, const QString &path,
                                      const QString &origin) {
    const int row = managedTable_->rowCount();
    managedTable_->insertRow(row);
    auto *check = new QTableWidgetItem;
    check->setFlags(check->flags() | Qt::ItemIsUserCheckable);
    check->setCheckState(Qt::Unchecked);
    managedTable_->setItem(row, 0, check);
    auto *nameItem = new QTableWidgetItem{name};
    nameItem->setData(Qt::UserRole, id);
    nameItem->setData(Qt::UserRole + 1, id.startsWith(QStringLiteral("hf:")) ? id.mid(3) : id);
    nameItem->setData(Qt::UserRole + 2, origin);
    nameItem->setData(Qt::UserRole + 3, path);
    managedTable_->setItem(row, 1, nameItem);
    managedTable_->setItem(row, 2, new QTableWidgetItem{version.isEmpty() ? tr("Local") : version});
    managedTable_->setItem(row, 3, new QTableWidgetItem{task});
    const QString actualStatus = path.isEmpty() || !QFileInfo::exists(path)
      ? (status == tr("Active") ? tr("Active (missing)") : tr("Available")) : status;
    managedTable_->setItem(row, 4, new QTableWidgetItem{actualStatus});
  };
  for(const SamModelDefinition &model : settings.samModels)
  {
    if(model.origin == QStringLiteral("imported") && !QFileInfo::exists(model.folder)) continue;
    addInstalledRow(model.id, model.name, model.version, tr("Mask"),
                    model.id == settings.activeSamModelId ? tr("Active") : tr("Installed"), model.folder, model.origin);
  }
  for(const DenoiseModelDefinition &model : settings.denoiseModels)
  {
    if(model.origin == QStringLiteral("imported") && !QFileInfo::exists(model.path)) continue;
    addInstalledRow(model.id, model.name, model.version, tr("Denoise"), tr("Installed"), model.path, model.origin);
  }
  for(const SharpenModelDefinition &model : settings.sharpenModels)
  {
    if(model.origin == QStringLiteral("imported") && !QFileInfo::exists(model.path)) continue;
    addInstalledRow(model.id, model.name, model.version, tr("Sharpen"), tr("Installed"), model.path, model.origin);
  }
  downloadStatus_ = new QLabel{managedGroup};
  downloadStatus_->setObjectName(QStringLiteral("modelDownloadStatus"));
  downloadStatus_->setWordWrap(true);
  downloadProgress_ = new QProgressBar{managedGroup};
  downloadProgress_->setObjectName(QStringLiteral("modelDownloadProgress"));
  downloadProgress_->setRange(0, 100);
  downloadProgress_->setValue(0);
  downloadProgress_->setFixedWidth(200);
  downloadProgress_->setVisible(false);
  auto *downloadButtons = new QHBoxLayout;
  downloadButton_ = new QPushButton{tr("Download Selected"), managedGroup};
  downloadButton_->setObjectName(QStringLiteral("downloadModelsButton"));
  cancelDownloadButton_ = new QPushButton{tr("Cancel"), managedGroup};
  cancelDownloadButton_->setObjectName(QStringLiteral("cancelModelDownloadButton"));
  cancelDownloadButton_->setEnabled(false);
  deleteButton_ = new QPushButton{tr("Delete Selected"), managedGroup};
  importButton_ = new QPushButton{tr("Import Model..."), managedGroup};
  downloadButtons->addWidget(downloadStatus_, 1);
  downloadButtons->addWidget(downloadProgress_);
  downloadButtons->addWidget(downloadButton_);
  downloadButtons->addWidget(deleteButton_);
  downloadButtons->addWidget(importButton_);
  downloadButtons->addWidget(cancelDownloadButton_);
  managedLayout->addLayout(downloadButtons);
  layout->addWidget(managedGroup, 1);

  downloader_ = new ModelDownloader{this};
  connect(downloadButton_, &QPushButton::clicked, this, [this] {
    QStringList ids;
    for(int row = 0; row < managedTable_->rowCount(); ++row)
      if(managedTable_->item(row, 0)->checkState() == Qt::Checked
         && managedTable_->item(row, 4)->text() == tr("Available"))
        ids << managedTable_->item(row, 1)->data(Qt::UserRole + 1).toString();
    if(ids.isEmpty())
    {
      downloadStatus_->setText(tr("Select one or more available models first."));
      return;
    }
    downloader_->start(ids);
  });
  connect(cancelDownloadButton_, &QPushButton::clicked, downloader_, &ModelDownloader::cancel);
  connect(deleteButton_, &QPushButton::clicked, this, [this] {
    const QString root = downloader_->modelRoot();
    for(int row = managedTable_->rowCount() - 1; row >= 0; --row)
    {
      if(managedTable_->item(row, 0)->checkState() != Qt::Checked) continue;
      if(managedTable_->item(row, 4)->text() != tr("Installed")
         && managedTable_->item(row, 4)->text() != tr("Active")) continue;
      const QString id = managedTable_->item(row, 1)->data(Qt::UserRole + 1).toString();
      const QString modelId = managedTable_->item(row, 1)->data(Qt::UserRole).toString();
      const QString origin = managedTable_->item(row, 1)->data(Qt::UserRole + 2).toString();
      const QString storedPath = managedTable_->item(row, 1)->data(Qt::UserRole + 3).toString();
      bool active = managedTable_->item(row, 4)->text() == tr("Active");
      for(int samRow = 0; samRow < samTable_->rowCount(); ++samRow)
        active = active || (samTable_->item(samRow, 1)->data(Qt::UserRole + 2).toString() == id
                            && samTable_->item(samRow, 0)->checkState() == Qt::Checked);
      if(active)
      {
        QMessageBox::warning(this, tr("Cannot Delete Active Model"),
                             tr("Select another active SAM model before deleting this model."));
        continue;
      }
      const QString version = managedTable_->item(row, 2)->text();
      if(origin == QStringLiteral("default"))
      {
        if(managedTable_->item(row, 3)->text() == tr("Mask")) QDir{storedPath}.removeRecursively();
        else QFile::remove(storedPath);
      }
      else QDir{QDir{root}.filePath(id + QLatin1Char('/') + version)}.removeRecursively();
      for(int samRow = 0; samRow < samTable_->rowCount(); ++samRow)
        if(samTable_->item(samRow, 1)->data(Qt::UserRole + 2).toString() == id
           || samTable_->item(samRow, 1)->data(Qt::UserRole).toString() == modelId)
          samTable_->item(samRow, 2)->setText(QString{});
      for(int modelRow = 0; modelRow < modelTable_->rowCount(); ++modelRow)
        if(modelTable_->item(modelRow, 0)->data(Qt::UserRole + 2).toString() == id
           || modelTable_->item(modelRow, 0)->data(Qt::UserRole).toString() == modelId)
          modelTable_->item(modelRow, 1)->setText(QString{});
      for(SharpenModelDefinition &model : sharpenModels_)
        if(model.sourceId == id || model.id == modelId) model.path.clear();
      if(origin == QStringLiteral("imported"))
      {
        for(int samRow = samTable_->rowCount() - 1; samRow >= 0; --samRow)
          if(samTable_->item(samRow, 1)->data(Qt::UserRole).toString() == modelId) samTable_->removeRow(samRow);
        for(int modelRow = modelTable_->rowCount() - 1; modelRow >= 0; --modelRow)
          if(modelTable_->item(modelRow, 0)->data(Qt::UserRole).toString() == modelId) modelTable_->removeRow(modelRow);
        sharpenModels_.erase(std::remove_if(sharpenModels_.begin(), sharpenModels_.end(),
          [&modelId](const SharpenModelDefinition &model) { return model.id == modelId; }), sharpenModels_.end());
        managedTable_->removeRow(row);
      }
      else
      {
        managedTable_->item(row, 4)->setText(tr("Available"));
        managedTable_->item(row, 0)->setCheckState(Qt::Unchecked);
      }
    }
  });
  connect(importButton_, &QPushButton::clicked, this, [this] {
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Import Model Folder"));
    if(folder.isEmpty()) return;
    QFile manifestFile{QDir{folder}.filePath(QStringLiteral("model.json"))};
    if(!manifestFile.open(QIODevice::ReadOnly))
    {
      QMessageBox::warning(this, tr("Import Model"), tr("The folder must contain model.json."));
      return;
    }
    QString error;
    const ModelManifest manifest = ModelCatalog::parseManifest(manifestFile.readAll(), &error);
    if(!error.isEmpty() || manifest.id.isEmpty() || manifest.version.isEmpty()
       || manifest.files.isEmpty())
    {
      QMessageBox::warning(this, tr("Import Model"), error.isEmpty() ? tr("Invalid model manifest.") : error);
      return;
    }
    const QString destination = QDir{downloader_->modelRoot()}.filePath(
      manifest.id + QLatin1Char('/') + manifest.version);
    if(QFileInfo::exists(destination))
    {
      QMessageBox::warning(this, tr("Import Model"), tr("This model version is already installed."));
      return;
    }
    if(!QDir{}.mkpath(destination) || !QFile::copy(manifestFile.fileName(),
                                                    QDir{destination}.filePath(QStringLiteral("model.json"))))
    {
      QMessageBox::warning(this, tr("Import Model"), tr("Could not copy the model manifest."));
      return;
    }
    for(const ModelFileEntry &file : manifest.files)
    {
      if(!QFile::copy(QDir{folder}.filePath(file.name), QDir{destination}.filePath(file.name)))
      {
        QDir{destination}.removeRecursively();
        QMessageBox::warning(this, tr("Import Model"), tr("Could not copy %1.").arg(file.name));
        return;
      }
    }
    const QString location = manifest.task == QStringLiteral("mask") ? destination
      : QDir{destination}.filePath(manifest.files.front().name);
    registerInstalledModel(manifest.id, manifest.name, manifest.version, manifest.task,
                           manifest.adapter, location, QStringLiteral("imported"));
    const int row = managedTable_->rowCount();
    managedTable_->insertRow(row);
    auto *check = new QTableWidgetItem;
    check->setFlags(check->flags() | Qt::ItemIsUserCheckable);
    check->setCheckState(Qt::Unchecked);
    managedTable_->setItem(row, 0, check);
    auto *name = new QTableWidgetItem{manifest.name};
    name->setData(Qt::UserRole, QStringLiteral("hf:") + manifest.id);
    name->setData(Qt::UserRole + 1, manifest.id);
    name->setData(Qt::UserRole + 2, QStringLiteral("imported"));
    name->setData(Qt::UserRole + 3, destination);
    managedTable_->setItem(row, 1, name);
    managedTable_->setItem(row, 2, new QTableWidgetItem{manifest.version});
    managedTable_->setItem(row, 3, new QTableWidgetItem{manifest.task});
    managedTable_->setItem(row, 4, new QTableWidgetItem{tr("Installed")});
  });
  connect(downloadButton_, &QPushButton::clicked, this, [this] {
    downloadButton_->setEnabled(false);
    cancelDownloadButton_->setEnabled(true);
    downloadProgress_->setVisible(true);
  });
  connect(downloader_, &ModelDownloader::catalogModelFound, this,
           [this](const QString &id, const QString &name, const QString &version,
                  const QString &task, const QString &, const bool installed) {
    int row = -1;
    for(int existing = 0; existing < managedTable_->rowCount(); ++existing)
    {
      const QTableWidgetItem *item = managedTable_->item(existing, 1);
       if(item && (item->data(Qt::UserRole).toString() == id
                  || item->data(Qt::UserRole + 1).toString() == id))
      {
        row = existing;
        break;
      }
    }
    if(row < 0)
    {
      row = managedTable_->rowCount();
      managedTable_->insertRow(row);
    }
    auto *nameItem = new QTableWidgetItem{name};
    nameItem->setData(Qt::UserRole, id);
    nameItem->setData(Qt::UserRole + 1, id);
     auto *check = managedTable_->item(row, 0);
     if(!check) { check = new QTableWidgetItem; check->setFlags(check->flags() | Qt::ItemIsUserCheckable); check->setCheckState(Qt::Unchecked); managedTable_->setItem(row, 0, check); }
     managedTable_->setItem(row, 1, nameItem);
     managedTable_->setItem(row, 2, new QTableWidgetItem{version});
     managedTable_->setItem(row, 3, new QTableWidgetItem{task});
     managedTable_->setItem(row, 4, new QTableWidgetItem{installed ? tr("Installed") : tr("Available")});
  });
  connect(downloader_, &ModelDownloader::modelInstalled, this,
           [this](const QString &id, const QString &name, const QString &version,
                  const QString &task, const QString &adapter, const QString &location) {
     registerInstalledModel(id, name, version, task, adapter, location);
    for(int row = 0; row < managedTable_->rowCount(); ++row)
    {
        if(managedTable_->item(row, 1)->data(Qt::UserRole).toString() == id
          || managedTable_->item(row, 1)->data(Qt::UserRole + 1).toString() == id)
         managedTable_->item(row, 4)->setText(tr("Installed"));
    }
  });
  connect(downloader_, &ModelDownloader::statusChanged, downloadStatus_, &QLabel::setText);
  connect(downloader_, &ModelDownloader::progressChanged, downloadProgress_, &QProgressBar::setValue);
  connect(downloader_, &ModelDownloader::finished, this, [this] {
    downloadButton_->setEnabled(true);
    cancelDownloadButton_->setEnabled(false);
    downloadProgress_->setVisible(false);
  });
  connect(downloader_, &ModelDownloader::failed, this, [this](const QString &) {
    downloadButton_->setEnabled(true);
    cancelDownloadButton_->setEnabled(false);
    downloadProgress_->setVisible(false);
  });

  connect(addButton, &QPushButton::clicked, this, [this] {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Add Denoise Model"), tr("Model name"),
                                                QLineEdit::Normal, QString{}, &accepted).trimmed();
    if(!accepted || name.isEmpty()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose denoise model"), QString{},
                                                       tr("ONNX models (*.onnx)"));
    if(path.isEmpty()) return;
    addModel({QUuid::createUuid().toString(QUuid::WithoutBraces), name, path});
  });
  connect(browseButton, &QPushButton::clicked, this, [this] {
    const int row = modelTable_->currentRow();
    if(row < 0) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose denoise model"),
                                                       modelTable_->item(row, 1)->text(),
                                                       tr("ONNX models (*.onnx)"));
    if(!path.isEmpty()) modelTable_->item(row, 1)->setText(path);
  });
  connect(removeButton, &QPushButton::clicked, this, [this] {
    if(modelTable_->currentRow() >= 0) modelTable_->removeRow(modelTable_->currentRow());
  });

  auto *buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    if(validate()) accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::addModel(const DenoiseModelDefinition &model)
{
  const int row = modelTable_->rowCount();
  modelTable_->insertRow(row);
  auto *name = new QTableWidgetItem{model.name};
  name->setData(Qt::UserRole, model.id);
  name->setData(Qt::UserRole + 1, model.adapter);
  name->setData(Qt::UserRole + 2, model.sourceId);
  name->setData(Qt::UserRole + 3, model.version);
  name->setData(Qt::UserRole + 4, model.origin);
  modelTable_->setItem(row, 0, name);
  modelTable_->setItem(row, 1, new QTableWidgetItem{model.path});
}

void SettingsDialog::addSamModel(const SamModelDefinition &model, const bool active)
{
  const int row = samTable_->rowCount();
  samTable_->insertRow(row);
  auto *activeItem = new QTableWidgetItem;
  activeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
  activeItem->setCheckState(active ? Qt::Checked : Qt::Unchecked);
  auto *name = new QTableWidgetItem{model.name};
  name->setData(Qt::UserRole, model.id);
  name->setData(Qt::UserRole + 1, model.adapter);
  name->setData(Qt::UserRole + 2, model.sourceId);
  name->setData(Qt::UserRole + 3, model.version);
  name->setData(Qt::UserRole + 4, model.origin);
  samTable_->setItem(row, 0, activeItem);
  samTable_->setItem(row, 1, name);
  samTable_->setItem(row, 2, new QTableWidgetItem{model.folder});
  samTable_->setCurrentCell(row, 1);
}

void SettingsDialog::rebuildModelTables(const ApplicationSettingsData &settings)
{
  samTable_->setRowCount(0);
  modelTable_->setRowCount(0);
  for(const SamModelDefinition &model : settings.samModels)
    addSamModel(model, model.id == settings.activeSamModelId);
  for(const DenoiseModelDefinition &model : settings.denoiseModels) addModel(model);
}

void SettingsDialog::registerInstalledModel(const QString &id, const QString &name,
                                             const QString &version, const QString &task,
                                             const QString &adapter,
                                             const QString &location, const QString &origin)
{
  ApplicationSettingsData data = settings();
  data.sharpenModels = sharpenModels_;
  ApplicationSettingsStore::registerInstalledModel(data, id, name, task, adapter, location, version, origin);
  sharpenModels_ = data.sharpenModels;
  rebuildModelTables(data);
}

void SettingsDialog::ensureActiveSamModel()
{
  for(int row = 0; row < samTable_->rowCount(); ++row)
  {
    if(samTable_->item(row, 0)->checkState() == Qt::Checked) return;
  }
  if(samTable_->rowCount() > 0) samTable_->item(0, 0)->setCheckState(Qt::Checked);
}

bool SettingsDialog::validate()
{
  ensureActiveSamModel();
  for(int row = 0; row < samTable_->rowCount(); ++row)
  {
    if(samTable_->item(row, 1)->text().trimmed().isEmpty()
       || samTable_->item(row, 2)->text().trimmed().isEmpty())
    {
      QMessageBox::warning(this, tr("Invalid Settings"), tr("Every SAM model needs a name and folder."));
      return false;
    }
  }
  for(int row = 0; row < modelTable_->rowCount(); ++row)
  {
    const QTableWidgetItem *name = modelTable_->item(row, 0);
    const QTableWidgetItem *path = modelTable_->item(row, 1);
    const QString sourceId = name->data(Qt::UserRole + 2).toString();
    if(modelTable_->item(row, 0)->text().trimmed().isEmpty()
       || (path->text().trimmed().isEmpty() && sourceId.isEmpty()))
    {
      QMessageBox::warning(this, tr("Invalid Settings"), tr("Every denoise model needs a name and path."));
      return false;
    }
  }
  return true;
}

ApplicationSettingsData SettingsDialog::settings() const
{
  ApplicationSettingsData data;
  data.onnxProvider = providerCombo_->currentData().toString();
  data.onnxDeviceId = deviceIdSpin_->value();
  for(int row = 0; row < samTable_->rowCount(); ++row)
  {
    const QString id = samTable_->item(row, 1)->data(Qt::UserRole).toString();
    data.samModels.push_back({
      id,
      samTable_->item(row, 1)->text().trimmed(),
      samTable_->item(row, 2)->text().trimmed().isEmpty() ? QString{}
        : QDir::cleanPath(samTable_->item(row, 2)->text().trimmed()),
      samTable_->item(row, 1)->data(Qt::UserRole + 1).toString(),
       samTable_->item(row, 1)->data(Qt::UserRole + 2).toString(),
       samTable_->item(row, 1)->data(Qt::UserRole + 3).toString(),
       samTable_->item(row, 1)->data(Qt::UserRole + 4).toString(),
    });
    if(samTable_->item(row, 0)->checkState() == Qt::Checked) data.activeSamModelId = id;
  }
  for(int row = 0; row < modelTable_->rowCount(); ++row)
  {
    data.denoiseModels.push_back({
      modelTable_->item(row, 0)->data(Qt::UserRole).toString(),
      modelTable_->item(row, 0)->text().trimmed(),
      modelTable_->item(row, 1)->text().trimmed(),
      modelTable_->item(row, 0)->data(Qt::UserRole + 1).toString(),
      modelTable_->item(row, 0)->data(Qt::UserRole + 2).toString(),
      modelTable_->item(row, 0)->data(Qt::UserRole + 3).toString(),
      modelTable_->item(row, 0)->data(Qt::UserRole + 4).toString(),
    });
  }
  data.sharpenModels = sharpenModels_;
  return data;
}
