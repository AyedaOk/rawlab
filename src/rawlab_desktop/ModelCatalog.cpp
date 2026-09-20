#include "ModelCatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace {

bool readString(const QJsonObject &object, const QString &key, QString &value)
{
  const QJsonValue field = object.value(key);
  if(!field.isString() || field.toString().trimmed().isEmpty()) return false;
  value = field.toString();
  return true;
}

bool readSize(const QJsonObject &object, const QString &key, qint64 &value)
{
  const QJsonValue field = object.value(key);
  if(!field.isDouble()) return false;
  const double number = field.toDouble(-1.0);
  if(number < 0.0 || number > static_cast<double>(std::numeric_limits<qint64>::max())
     || std::floor(number) != number) return false;
  value = static_cast<qint64>(number);
  return true;
}

bool readSha(const QJsonObject &object, const QString &key, QByteArray &value)
{
  QString text;
  if(!readString(object, key, text)) return false;
  static const QRegularExpression digestPattern{QStringLiteral("^[0-9a-fA-F]{64}$")};
  if(!digestPattern.match(text).hasMatch()) return false;
  value = text.toLatin1().toLower();
  return true;
}

bool safeComponent(const QString &value)
{
  static const QRegularExpression pattern{QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$")};
  return pattern.match(value).hasMatch();
}

QJsonObject parseRoot(const QByteArray &data, QString *error)
{
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
  if(parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    if(error) *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
    return {};
  }
  const QJsonObject root = document.object();
  if(root.value(QStringLiteral("schema")).toInt(-1) != 1)
  {
    if(error) *error = QStringLiteral("Unsupported model metadata schema");
    return {};
  }
  return root;
}

void setError(QString *error, const QString &message)
{
  if(error) *error = message;
}

} // namespace

QList<ModelCatalogEntry> ModelCatalog::parseCatalog(const QByteArray &data, QString *error)
{
  if(error) error->clear();
  const QJsonObject root = parseRoot(data, error);
  if(root.isEmpty()) return {};
  const QJsonValue modelsValue = root.value(QStringLiteral("models"));
  if(!modelsValue.isArray())
  {
    setError(error, QStringLiteral("Catalog has no models array"));
    return {};
  }

  QList<ModelCatalogEntry> result;
  for(const QJsonValue value : modelsValue.toArray())
  {
    if(!value.isObject())
    {
      setError(error, QStringLiteral("Catalog contains an invalid model entry"));
      return {};
    }
    const QJsonObject object = value.toObject();
    ModelCatalogEntry entry;
    if(!readString(object, QStringLiteral("id"), entry.id)
       || !readString(object, QStringLiteral("name"), entry.name)
       || !readString(object, QStringLiteral("version"), entry.version)
       || !readString(object, QStringLiteral("task"), entry.task)
       || !readString(object, QStringLiteral("adapter"), entry.adapter)
       || !readString(object, QStringLiteral("manifest"), entry.manifestPath)
       || !readSize(object, QStringLiteral("manifestSize"), entry.manifestSize)
       || !readSha(object, QStringLiteral("manifestSha256"), entry.manifestSha256)
       || !object.value(QStringLiteral("available")).isBool()
       || !safeComponent(entry.id) || !safeComponent(entry.version)
       || !isSafeRelativePath(entry.manifestPath))
    {
      setError(error, QStringLiteral("Catalog model entry is incomplete or unsafe"));
      return {};
    }
    entry.available = object.value(QStringLiteral("available")).toBool();
    result.push_back(entry);
  }
  return result;
}

ModelManifest ModelCatalog::parseManifest(const QByteArray &data, QString *error)
{
  if(error) error->clear();
  const QJsonObject root = parseRoot(data, error);
  if(root.isEmpty()) return {};
  ModelManifest result;
  if(!readString(root, QStringLiteral("id"), result.id)
     || !readString(root, QStringLiteral("name"), result.name)
     || !readString(root, QStringLiteral("version"), result.version)
     || !readString(root, QStringLiteral("task"), result.task)
     || !readString(root, QStringLiteral("adapter"), result.adapter)
     || !root.value(QStringLiteral("files")).isArray()
     || !safeComponent(result.id) || !safeComponent(result.version))
  {
    setError(error, QStringLiteral("Model manifest is incomplete"));
    return {};
  }
  for(const QJsonValue value : root.value(QStringLiteral("files")).toArray())
  {
    if(!value.isObject())
    {
      setError(error, QStringLiteral("Manifest contains an invalid file entry"));
      return {};
    }
    ModelFileEntry file;
    const QJsonObject object = value.toObject();
    if(!readString(object, QStringLiteral("name"), file.name)
       || !readSize(object, QStringLiteral("size"), file.size)
       || !readSha(object, QStringLiteral("sha256"), file.sha256)
       || !isSafeRelativePath(file.name) || file.name.contains(QLatin1Char('/')))
    {
      setError(error, QStringLiteral("Manifest file entry is incomplete or unsafe"));
      return {};
    }
    result.files.push_back(file);
  }
  if(result.files.isEmpty())
  {
    setError(error, QStringLiteral("Model manifest has no files"));
    return {};
  }
  return result;
}

bool ModelCatalog::verify(const QByteArray &data, const qint64 expectedSize,
                          const QByteArray &expectedSha256)
{
  return data.size() == expectedSize
         && QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()
              == expectedSha256.toLower();
}

bool ModelCatalog::isSafeRelativePath(const QString &path)
{
  if(path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char('\\'))) return false;
  const QString clean = QDir::cleanPath(path);
  return clean == path && clean != QStringLiteral("..")
         && !clean.startsWith(QStringLiteral("../")) && clean != QStringLiteral(".");
}
