#include "ApplicationSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace {

QString darktableModelPath(const QString &relative)
{
  return QDir::homePath() + QStringLiteral("/.local/share/darktable/models/") + relative;
}

ApplicationSettingsData defaults()
{
  ApplicationSettingsData data;
  data.samModels.push_back({
    QStringLiteral("sam21-large-default"),
    QStringLiteral("SAM 2.1 Large"),
    darktableModelPath(QStringLiteral("mask-object-sam21-large")),
  });
  data.activeSamModelId = data.samModels.front().id;
  data.denoiseModels.push_back({
    QStringLiteral("raw-nind-default"),
    QStringLiteral("RawNIND Bayer"),
    darktableModelPath(QStringLiteral("rawdenoise-nind/model_bayer.onnx")),
  });
  return data;
}

} // namespace

ApplicationSettingsData ApplicationSettingsStore::load() const
{
  QSettings settings;
  ApplicationSettingsData data = defaults();
  data.onnxProvider = settings.value(QStringLiteral("ai/provider"), QStringLiteral("auto")).toString();
  data.onnxDeviceId = std::max(0, settings.value(QStringLiteral("ai/deviceId"), 0).toInt());
  const bool hasSavedSamModels = settings.contains(QStringLiteral("samModels/size"));
  if(hasSavedSamModels)
  {
    data.samModels.clear();
    const int samCount = settings.beginReadArray(QStringLiteral("samModels"));
    for(int i = 0; i < samCount; ++i)
    {
      settings.setArrayIndex(i);
      const SamModelDefinition model{
        settings.value(QStringLiteral("id")).toString(),
        settings.value(QStringLiteral("name")).toString(),
        settings.value(QStringLiteral("folder")).toString(),
        settings.value(QStringLiteral("adapter")).toString(),
        settings.value(QStringLiteral("sourceId")).toString(),
        settings.value(QStringLiteral("version")).toString(),
      };
      if(!model.id.isEmpty() && !model.name.isEmpty()) data.samModels.push_back(model);
    }
    settings.endArray();
    data.activeSamModelId = settings.value(QStringLiteral("samModels/activeId")).toString();
  }
  else if(settings.contains(QStringLiteral("sam21/encoder"))
          || settings.contains(QStringLiteral("sam21/decoder")))
  {
    const QString encoder = settings.value(QStringLiteral("sam21/encoder")).toString();
    const QString decoder = settings.value(QStringLiteral("sam21/decoder")).toString();
    const QString encoderFolder = QFileInfo{encoder}.absolutePath();
    const QString decoderFolder = QFileInfo{decoder}.absolutePath();
    if(!encoder.isEmpty() && !decoder.isEmpty() && encoderFolder == decoderFolder)
    {
      data.samModels = {{QStringLiteral("sam21-migrated"), QStringLiteral("SAM 2.1"), encoderFolder}};
      data.activeSamModelId = data.samModels.front().id;
    }
  }
  if(!data.samModels.isEmpty() && samModelById(data, data.activeSamModelId).id.isEmpty())
  {
    data.activeSamModelId = data.samModels.front().id;
  }
  if(data.samModels.isEmpty()) data.activeSamModelId.clear();

  const bool hasSavedModels = settings.contains(QStringLiteral("denoiseModels/size"));
  const int count = settings.beginReadArray(QStringLiteral("denoiseModels"));
  if(hasSavedModels) data.denoiseModels.clear();
  for(int i = 0; i < count; ++i)
  {
    settings.setArrayIndex(i);
    const DenoiseModelDefinition model{
      settings.value(QStringLiteral("id")).toString(),
      settings.value(QStringLiteral("name")).toString(),
      settings.value(QStringLiteral("path")).toString(),
      settings.value(QStringLiteral("adapter")).toString(),
      settings.value(QStringLiteral("sourceId")).toString(),
      settings.value(QStringLiteral("version")).toString(),
    };
    if(!model.id.isEmpty() && !model.name.isEmpty()) data.denoiseModels.push_back(model);
  }
  settings.endArray();

  const bool hasSavedSharpenModels = settings.contains(QStringLiteral("sharpenModels/size"));
  const int sharpenCount = settings.beginReadArray(QStringLiteral("sharpenModels"));
  if(hasSavedSharpenModels) data.sharpenModels.clear();
  for(int i = 0; i < sharpenCount; ++i)
  {
    settings.setArrayIndex(i);
    const SharpenModelDefinition model{
      settings.value(QStringLiteral("id")).toString(),
      settings.value(QStringLiteral("name")).toString(),
      settings.value(QStringLiteral("path")).toString(),
      settings.value(QStringLiteral("adapter")).toString(),
      settings.value(QStringLiteral("sourceId")).toString(),
      settings.value(QStringLiteral("version")).toString(),
    };
    if(!model.id.isEmpty() && !model.name.isEmpty()) data.sharpenModels.push_back(model);
  }
  settings.endArray();
  return data;
}

void ApplicationSettingsStore::save(const ApplicationSettingsData &data) const
{
  QSettings settings;
  settings.setValue(QStringLiteral("ai/provider"), data.onnxProvider);
  settings.setValue(QStringLiteral("ai/deviceId"), std::max(0, data.onnxDeviceId));
  settings.remove(QStringLiteral("sam21"));
  settings.beginWriteArray(QStringLiteral("samModels"), data.samModels.size());
  for(int i = 0; i < data.samModels.size(); ++i)
  {
    settings.setArrayIndex(i);
    const SamModelDefinition &model = data.samModels[i];
    settings.setValue(QStringLiteral("id"), model.id);
    settings.setValue(QStringLiteral("name"), model.name);
    settings.setValue(QStringLiteral("folder"), model.folder);
    settings.setValue(QStringLiteral("adapter"), model.adapter);
    settings.setValue(QStringLiteral("sourceId"), model.sourceId);
    settings.setValue(QStringLiteral("version"), model.version);
  }
  settings.endArray();
  settings.setValue(QStringLiteral("samModels/activeId"), data.activeSamModelId);
  settings.beginWriteArray(QStringLiteral("denoiseModels"), data.denoiseModels.size());
  for(int i = 0; i < data.denoiseModels.size(); ++i)
  {
    settings.setArrayIndex(i);
    const DenoiseModelDefinition &model = data.denoiseModels[i];
    settings.setValue(QStringLiteral("id"), model.id);
    settings.setValue(QStringLiteral("name"), model.name);
    settings.setValue(QStringLiteral("path"), model.path);
    settings.setValue(QStringLiteral("adapter"), model.adapter);
    settings.setValue(QStringLiteral("sourceId"), model.sourceId);
    settings.setValue(QStringLiteral("version"), model.version);
  }
  settings.endArray();
  settings.beginWriteArray(QStringLiteral("sharpenModels"), data.sharpenModels.size());
  for(int i = 0; i < data.sharpenModels.size(); ++i)
  {
    settings.setArrayIndex(i);
    const SharpenModelDefinition &model = data.sharpenModels[i];
    settings.setValue(QStringLiteral("id"), model.id);
    settings.setValue(QStringLiteral("name"), model.name);
    settings.setValue(QStringLiteral("path"), model.path);
    settings.setValue(QStringLiteral("adapter"), model.adapter);
    settings.setValue(QStringLiteral("sourceId"), model.sourceId);
    settings.setValue(QStringLiteral("version"), model.version);
  }
  settings.endArray();
}

SamModelDefinition ApplicationSettingsStore::samModelById(const ApplicationSettingsData &data,
                                                            const QString &id)
{
  for(const SamModelDefinition &model : data.samModels)
  {
    if(model.id == id) return model;
  }
  return {};
}

DenoiseModelDefinition ApplicationSettingsStore::modelById(const ApplicationSettingsData &data,
                                                             const QString &id)
{
  for(const DenoiseModelDefinition &model : data.denoiseModels)
  {
    if(model.id == id) return model;
  }
  return {};
}

void ApplicationSettingsStore::registerInstalledModel(ApplicationSettingsData &data,
                                                       const QString &sourceId,
                                                       const QString &name,
                                                       const QString &task,
                                                       const QString &adapter,
                                                       const QString &location,
                                                       const QString &version)
{
  const QString managedId = QStringLiteral("hf:%1").arg(sourceId);
  if(task == QStringLiteral("mask"))
  {
    for(SamModelDefinition &model : data.samModels)
    {
      if(model.sourceId == sourceId)
      {
        model.name = name;
        model.folder = location;
        model.adapter = adapter;
        model.version = version;
        return;
      }
    }
    data.samModels.push_back({managedId, name, location, adapter, sourceId, version});
    if(data.activeSamModelId.isEmpty()) data.activeSamModelId = managedId;
    return;
  }
  if(task == QStringLiteral("denoise"))
  {
    for(DenoiseModelDefinition &model : data.denoiseModels)
    {
      if(model.sourceId == sourceId)
      {
        model.name = name;
        model.path = location;
        model.adapter = adapter;
        model.version = version;
        return;
      }
    }
    data.denoiseModels.push_back({managedId, name, location, adapter, sourceId, version});
    return;
  }
  if(task == QStringLiteral("sharpen"))
  {
    for(SharpenModelDefinition &model : data.sharpenModels)
    {
      if(model.sourceId == sourceId)
      {
        model.name = name;
        model.path = location;
        model.adapter = adapter;
        model.version = version;
        return;
      }
    }
    data.sharpenModels.push_back({managedId, name, location, adapter, sourceId, version});
  }
}
