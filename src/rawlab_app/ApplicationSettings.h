#pragma once

#include <QList>
#include <QString>

struct DenoiseModelDefinition
{
  QString id;
  QString name;
  QString path;
  QString adapter{};
  QString sourceId{};
  QString version{};
};

struct SamModelDefinition
{
  QString id;
  QString name;
  QString folder;
  QString adapter{};
  QString sourceId{};
  QString version{};
};

struct SharpenModelDefinition
{
  QString id;
  QString name;
  QString path;
  QString adapter{};
  QString sourceId{};
  QString version{};
};

struct ApplicationSettingsData
{
  QString onnxProvider = QStringLiteral("auto");
  int onnxDeviceId = 0;
  QList<SamModelDefinition> samModels;
  QString activeSamModelId;
  QList<DenoiseModelDefinition> denoiseModels;
  QList<SharpenModelDefinition> sharpenModels;
};

class ApplicationSettingsStore
{
public:
  ApplicationSettingsData load() const;
  void save(const ApplicationSettingsData &data) const;

  static DenoiseModelDefinition modelById(const ApplicationSettingsData &data,
                                           const QString &id);
  static SamModelDefinition samModelById(const ApplicationSettingsData &data,
                                         const QString &id);
  static void registerInstalledModel(ApplicationSettingsData &data, const QString &sourceId,
                                     const QString &name, const QString &task,
                                     const QString &adapter, const QString &location,
                                     const QString &version = QString{});
};
