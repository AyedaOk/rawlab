#include "LibraryPage.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

LibraryPage::LibraryPage(QWidget *parent)
  : QWidget{parent}
{
  auto *layout = new QVBoxLayout{this};
  layout->setContentsMargins(22, 18, 22, 18);
  auto *buttons = new QHBoxLayout;
  auto *importFiles = new QPushButton{tr("Import Pictures"), this};
  auto *importFolder = new QPushButton{tr("Import Folder"), this};
  auto *open = new QPushButton{tr("Open"), this};
  auto *remove = new QPushButton{tr("Remove From Library"), this};
  buttons->addWidget(importFiles);
  buttons->addWidget(importFolder);
  buttons->addStretch();
  buttons->addWidget(open);
  buttons->addWidget(remove);
  layout->addLayout(buttons);

  pictures_ = new QListWidget{this};
  pictures_->setViewMode(QListView::IconMode);
  pictures_->setResizeMode(QListView::Adjust);
  pictures_->setMovement(QListView::Static);
  pictures_->setIconSize({220, 145});
  pictures_->setGridSize({245, 195});
  pictures_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  pictures_->setSpacing(8);
  layout->addWidget(pictures_, 1);

  connect(importFiles, &QPushButton::clicked, this, &LibraryPage::importFilesRequested);
  connect(importFolder, &QPushButton::clicked, this, &LibraryPage::importFolderRequested);
  connect(open, &QPushButton::clicked, this, [this] {
    const QStringList paths = selectedPaths();
    if(!paths.isEmpty()) emit openRequested(paths);
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    QStringList paths;
    for(const QListWidgetItem *item : pictures_->selectedItems()) paths.push_back(item->data(Qt::UserRole).toString());
    if(!paths.isEmpty()) emit removeRequested(paths);
  });
  connect(pictures_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
    emit openRequested({item->data(Qt::UserRole).toString()});
  });
  connect(pictures_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *current, QListWidgetItem *) {
            emit currentPictureChanged(current ? current->data(Qt::UserRole).toString() : QString{});
          });
}

void LibraryPage::setFiles(const QStringList &paths)
{
  const QString currentPath = selectedPath();
  const QStringList selected = selectedPaths();
  const QSignalBlocker blocker{pictures_};
  pictures_->clear();
  for(const QString &path : paths)
  {
    const QFileInfo info{path};
    auto *item = new QListWidgetItem{info.fileName(), pictures_};
    item->setData(Qt::UserRole, path);
    item->setToolTip(info.exists() ? path : tr("Missing: %1").arg(path));
    if(!info.exists()) item->setForeground(QColor{185, 91, 91});
    if(selected.contains(path)) item->setSelected(true);
    if(path == currentPath) pictures_->setCurrentItem(item);
  }
  emit currentPictureChanged(selectedPath());
}

void LibraryPage::setThumbnail(const QString &path, const QImage &image)
{
  if(image.isNull()) return;
  for(int row = 0; row < pictures_->count(); ++row)
  {
    QListWidgetItem *item = pictures_->item(row);
    if(item->data(Qt::UserRole).toString() == path)
    {
      item->setIcon(QPixmap::fromImage(image));
      return;
    }
  }
}

QIcon LibraryPage::iconForPath(const QString &path) const
{
  for(int row = 0; row < pictures_->count(); ++row)
  {
    const QListWidgetItem *item = pictures_->item(row);
    if(item->data(Qt::UserRole).toString() == path) return item->icon();
  }
  return {};
}

QString LibraryPage::selectedPath() const
{
  const QListWidgetItem *item = pictures_->currentItem();
  return item ? item->data(Qt::UserRole).toString() : QString{};
}

QStringList LibraryPage::selectedPaths() const
{
  QStringList paths;
  for(int row = 0; row < pictures_->count(); ++row)
  {
    const QListWidgetItem *item = pictures_->item(row);
    if(item->isSelected()) paths.push_back(item->data(Qt::UserRole).toString());
  }
  return paths;
}
