#pragma once

#include <QWidget>
#include <QIcon>

class QListWidget;

class LibraryPage final : public QWidget
{
  Q_OBJECT

public:
  explicit LibraryPage(QWidget *parent = nullptr);

  void setFiles(const QStringList &paths);
  void setThumbnail(const QString &path, const QImage &image);
  QIcon iconForPath(const QString &path) const;
  QString selectedPath() const;
  QStringList selectedPaths() const;

signals:
  void importFilesRequested();
  void importFolderRequested();
  void openRequested(QStringList paths);
  void removeRequested(QStringList paths);
  void currentPictureChanged(QString path);

private:
  QListWidget *pictures_ = nullptr;
};
