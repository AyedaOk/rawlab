#pragma once

#include "ApplicationSettings.h"

#include <QDialog>

class QTableWidget;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class ModelDownloader;
class QComboBox;
class QSpinBox;

class SettingsDialog final : public QDialog
{
  Q_OBJECT

public:
  explicit SettingsDialog(const ApplicationSettingsData &settings, QWidget *parent = nullptr);
  ApplicationSettingsData settings() const;

private:
  void addModel(const DenoiseModelDefinition &model);
  void addSamModel(const SamModelDefinition &model, bool active);
  void ensureActiveSamModel();
  void rebuildModelTables(const ApplicationSettingsData &settings);
  void registerInstalledModel(const QString &id, const QString &name, const QString &version,
                              const QString &task, const QString &adapter, const QString &location,
                              const QString &origin = QStringLiteral("verified"));
  bool validate();

  QTableWidget *samTable_ = nullptr;
  QLineEdit *runtimeLibraryEdit_ = nullptr;
  QComboBox *providerCombo_ = nullptr;
  QSpinBox *deviceIdSpin_ = nullptr;
  QTableWidget *modelTable_ = nullptr;
  QTableWidget *managedTable_ = nullptr;
  QLabel *downloadStatus_ = nullptr;
  QProgressBar *downloadProgress_ = nullptr;
  QPushButton *downloadButton_ = nullptr;
  QPushButton *refreshCatalogButton_ = nullptr;
  QPushButton *deleteButton_ = nullptr;
  QPushButton *importButton_ = nullptr;
  QPushButton *cancelDownloadButton_ = nullptr;
  ModelDownloader *downloader_ = nullptr;
  QList<SharpenModelDefinition> sharpenModels_;
};
