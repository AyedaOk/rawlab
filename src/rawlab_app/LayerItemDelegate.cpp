#include "LayerItemDelegate.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

LayerItemDelegate::LayerItemDelegate(QObject *parent)
  : QStyledItemDelegate{parent}
{
}

QRect LayerItemDelegate::eyeRect(const QStyleOptionViewItem &option) const
{
  return {option.rect.left() + 8, option.rect.center().y() - 8, 22, 16};
}

void LayerItemDelegate::paint(QPainter *painter,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
  painter->save();
  if(option.state.testFlag(QStyle::State_Selected))
  {
    painter->fillRect(option.rect, option.palette.highlight());
  }

  QStyleOptionViewItem textOption{option};
  initStyleOption(&textOption, index);
  textOption.features.setFlag(QStyleOptionViewItem::HasCheckIndicator, false);
  textOption.rect.adjust(34, 0, 0, 0);
  textOption.state.setFlag(QStyle::State_Selected, false);
  if(option.state.testFlag(QStyle::State_Selected))
  {
    textOption.palette.setColor(QPalette::Text, option.palette.highlightedText().color());
  }
  QStyledItemDelegate::paint(painter, textOption, index);

  const bool enabled = index.data(ProcessingEnabledRole).toBool();
  const QRect eye = eyeRect(option);
  const QColor color = enabled ? QColor{218, 224, 232} : QColor{101, 108, 119};
  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setPen(QPen{color, 1.6});
  painter->setBrush(Qt::NoBrush);
  painter->drawEllipse(eye.adjusted(1, 3, -1, -3));
  painter->setBrush(color);
  painter->drawEllipse(eye.center(), 3, 3);
  if(!enabled)
  {
    painter->setPen(QPen{QColor{168, 84, 91}, 2.0});
    painter->drawLine(eye.topLeft() + QPoint{2, 1}, eye.bottomRight() - QPoint{2, 1});
  }
  painter->restore();
}

bool LayerItemDelegate::editorEvent(QEvent *event,
                                    QAbstractItemModel *model,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index)
{
  bool toggle = false;
  if(event->type() == QEvent::MouseButtonRelease)
  {
    const auto *mouseEvent = static_cast<QMouseEvent *>(event);
    toggle = mouseEvent->button() == Qt::LeftButton && eyeRect(option).contains(mouseEvent->position().toPoint());
  }
  else if(event->type() == QEvent::KeyPress)
  {
    const auto *keyEvent = static_cast<QKeyEvent *>(event);
    toggle = keyEvent->key() == Qt::Key_Space;
  }
  if(!toggle) return QStyledItemDelegate::editorEvent(event, model, option, index);

  return model->setData(index, !index.data(ProcessingEnabledRole).toBool(), ProcessingEnabledRole);
}
