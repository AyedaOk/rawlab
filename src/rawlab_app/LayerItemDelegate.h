#pragma once

#include <QStyledItemDelegate>

inline constexpr int ProcessingEnabledRole = Qt::UserRole + 100;

class LayerItemDelegate final : public QStyledItemDelegate
{
  Q_OBJECT

public:
  explicit LayerItemDelegate(QObject *parent = nullptr);

  void paint(QPainter *painter,
             const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  bool editorEvent(QEvent *event,
                   QAbstractItemModel *model,
                   const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

private:
  QRect eyeRect(const QStyleOptionViewItem &option) const;
};
