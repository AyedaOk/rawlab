#include "PreviewWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QCursor>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

PreviewWidget::PreviewWidget(QWidget *parent)
  : QWidget{parent}
{
  setMinimumSize(480, 320);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  processingTimer_.setInterval(40);
  connect(&processingTimer_, &QTimer::timeout, this, [this] {
    processingAngle_ = (processingAngle_ + 12) % 360;
    update();
  });
}

void PreviewWidget::setImage(QImage image, const bool resetView)
{
  const bool firstImage = image_.isNull();
  image_ = std::move(image);
  if(mask_.size() != image_.size()) mask_ = {};
  if(!beforeImage_.isNull() && beforeImage_.size() != image_.size()) beforeImage_ = {};
  if(firstImage || resetView) fitImage();
  else update();
}

void PreviewWidget::setBeforeImage(QImage image)
{
  beforeImage_ = std::move(image);
  if(!image_.isNull() && beforeImage_.size() != image_.size()) beforeImage_ = {};
  update();
}

void PreviewWidget::setComparisonEnabled(const bool enabled)
{
  comparisonEnabled_ = enabled && !beforeImage_.isNull() && beforeImage_.size() == image_.size();
  dividerDragging_ = false;
  updateCursor(mapFromGlobal(QCursor::pos()));
  update();
}

void PreviewWidget::setComparisonReversed(const bool reversed)
{
  if(comparisonReversed_ == reversed) return;
  comparisonReversed_ = reversed;
  update();
}

void PreviewWidget::setMask(QImage mask)
{
  mask_ = std::move(mask);
  update();
}

void PreviewWidget::setPrompts(QVector<PromptMarker> prompts)
{
  prompts_ = std::move(prompts);
  update();
}

void PreviewWidget::setPromptMode(const PromptMode mode)
{
  promptMode_ = mode;
  setCursor(mode == PromptMode::None ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void PreviewWidget::setBrushRadius(const float radius)
{
  brushRadius_ = std::max(radius, 1.0f);
  update();
}

void PreviewWidget::setProcessing(const bool processing)
{
  if(processing_ == processing) return;
  processing_ = processing;
  if(processing_) processingTimer_.start();
  else processingTimer_.stop();
  update();
}

void PreviewWidget::fitImage()
{
  actualSizeMode_ = false;
  zoom_ = 1.0;
  pan_ = {};
  update();
  emit zoomChanged(imageScale());
}

void PreviewWidget::zoomIn()
{
  zoomAt(zoom_ * 1.25, rect().center());
}

void PreviewWidget::zoomOut()
{
  zoomAt(zoom_ / 1.25, rect().center());
}

void PreviewWidget::actualSize()
{
  if(image_.isNull()) return;
  const double fit = std::min(static_cast<double>(width()) / image_.width(),
                              static_cast<double>(height()) / image_.height());
  zoomAt(1.0 / fit, rect().center());
  actualSizeMode_ = true;
}

void PreviewWidget::zoomAt(const double factor, const QPointF &anchor)
{
  if(image_.isNull()) return;
  const QPointF imagePoint = widgetToImage(anchor);
  actualSizeMode_ = false;
  zoom_ = std::clamp(factor, 0.01, 64.0);
  const QRectF after = imageRect();
  const QPointF rendered{after.left() + imagePoint.x() * after.width() / image_.width(),
                         after.top() + imagePoint.y() * after.height() / image_.height()};
  pan_ += anchor - rendered;
  update();
  emit zoomChanged(imageScale());
}

double PreviewWidget::imageScale() const
{
  if(image_.isNull() || width() <= 0 || height() <= 0) return 0.0;
  const double fit = std::min(static_cast<double>(width()) / image_.width(),
                              static_cast<double>(height()) / image_.height());
  return fit * zoom_;
}

QRectF PreviewWidget::imageRect() const
{
  if(image_.isNull()) return {};
  const double fit = std::min(static_cast<double>(width()) / image_.width(),
                              static_cast<double>(height()) / image_.height());
  const QSizeF size{image_.width() * fit * zoom_, image_.height() * fit * zoom_};
  const QPointF topLeft{(width() - size.width()) * 0.5 + pan_.x(),
                        (height() - size.height()) * 0.5 + pan_.y()};
  return {topLeft, size};
}

QPointF PreviewWidget::widgetToImage(const QPointF &position) const
{
  const QRectF rect = imageRect();
  return {(position.x() - rect.left()) * image_.width() / rect.width(),
          (position.y() - rect.top()) * image_.height() / rect.height()};
}

bool PreviewWidget::dividerHit(const QPointF &position) const
{
  if(!comparisonEnabled_ || beforeImage_.isNull()) return false;
  const QRectF target = imageRect();
  const qreal dividerX = target.left() + dividerPosition_ * target.width();
  const QRectF visible = target.intersected(QRectF{rect()});
  return position.y() >= visible.top() && position.y() <= visible.bottom()
         && std::abs(position.x() - dividerX) <= 7.0;
}

void PreviewWidget::updateDivider(const QPointF &position)
{
  const QRectF target = imageRect();
  if(target.width() <= 0.0) return;
  dividerPosition_ = std::clamp((position.x() - target.left()) / target.width(), 0.0, 1.0);
  update();
}

void PreviewWidget::updateCursor(const QPointF &position)
{
  if(promptMode_ != PromptMode::None) setCursor(Qt::CrossCursor);
  else if(dividerHit(position)) setCursor(Qt::SplitHCursor);
  else setCursor(Qt::OpenHandCursor);
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
  QPainter painter{this};
  painter.fillRect(rect(), QColor{18, 20, 24});
  if(image_.isNull())
  {
    painter.setPen(QColor{145, 151, 162});
    painter.drawText(rect(), Qt::AlignCenter, tr("Open a RAW image to begin"));
  }
  else
  {
    const QRectF target = imageRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, image_);
    if(comparisonEnabled_ && !beforeImage_.isNull())
    {
      const qreal dividerX = target.left() + dividerPosition_ * target.width();
      painter.save();
      painter.setClipRect(comparisonReversed_
                            ? QRectF{dividerX, target.top(), target.right() - dividerX, target.height()}
                            : QRectF{target.left(), target.top(), dividerX - target.left(), target.height()});
      painter.drawImage(target, beforeImage_);
      painter.restore();
      const QRectF visible = target.intersected(QRectF{rect()});
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setPen(QPen{QColor{0, 0, 0, 180}, 3.0});
      painter.drawLine(QPointF{dividerX, visible.top()}, QPointF{dividerX, visible.bottom()});
      painter.setPen(QPen{Qt::white, 1.0});
      painter.drawLine(QPointF{dividerX, visible.top()}, QPointF{dividerX, visible.bottom()});
    }
    if(!mask_.isNull()) painter.drawImage(target, mask_);

    const double scaleX = target.width() / image_.width();
    const double scaleY = target.height() / image_.height();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if(shaping_)
    {
      const QPointF start{target.left() + shapeStart_.x() * scaleX,
                          target.top() + shapeStart_.y() * scaleY};
      const QPointF end{target.left() + hoverImagePoint_.x() * scaleX,
                        target.top() + hoverImagePoint_.y() * scaleY};
      painter.setPen(QPen{QColor{10, 12, 15, 220}, 5.0, Qt::DashLine});
      painter.setBrush(Qt::NoBrush);
      if(promptMode_ == PromptMode::GradientAdd || promptMode_ == PromptMode::GradientSubtract)
      {
        painter.drawLine(start, end);
      }
      else
      {
        const QRectF bounds{std::min(start.x(), end.x()), std::min(start.y(), end.y()),
                            std::abs(end.x() - start.x()), std::abs(end.y() - start.y())};
        painter.drawEllipse(bounds);
        painter.drawRect(bounds);
      }
      painter.setPen(QPen{Qt::white, 1.5});
      painter.setBrush(QColor{105, 167, 208, 220});
      painter.drawEllipse(start, 5.0, 5.0);
      painter.drawEllipse(end, 5.0, 5.0);
    }
    for(const PromptMarker &prompt : prompts_)
    {
      const QPointF center{target.left() + prompt.position.x() * scaleX,
                           target.top() + prompt.position.y() * scaleY};
      painter.setPen(QPen{QColor{10, 12, 15}, 4.0});
      painter.setBrush(prompt.foreground ? QColor{73, 220, 140} : QColor{245, 87, 87});
      painter.drawEllipse(center, 6.0, 6.0);
      painter.setPen(QPen{Qt::white, 1.5});
      painter.drawEllipse(center, 6.0, 6.0);
    }
    if((promptMode_ == PromptMode::BrushPaint || promptMode_ == PromptMode::BrushErase)
       && !hoverImagePoint_.isNull())
    {
      const QPointF center{target.left() + hoverImagePoint_.x() * scaleX,
                           target.top() + hoverImagePoint_.y() * scaleY};
      painter.setPen(QPen{Qt::white, 1.2});
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(center, brushRadius_ * scaleX, brushRadius_ * scaleY);
      painter.drawLine(QPointF{center.x() - 5.0, center.y()},
                       QPointF{center.x() + 5.0, center.y()});
      painter.drawLine(QPointF{center.x(), center.y() - 5.0},
                       QPointF{center.x(), center.y() + 5.0});
    }
  }

  if(processing_)
  {
    const QPointF center = rect().center();
    const QRectF spinnerRect{center.x() - 23.0, center.y() - 23.0, 46.0, 46.0};
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{QColor{8, 10, 13, 175}, 10.0, Qt::SolidLine, Qt::RoundCap});
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(spinnerRect);
    painter.setPen(QPen{QColor{105, 167, 208}, 6.0, Qt::SolidLine, Qt::RoundCap});
    painter.drawArc(spinnerRect, -processingAngle_ * 16, 105 * 16);
  }
}

void PreviewWidget::wheelEvent(QWheelEvent *event)
{
  if(image_.isNull()) return;
  zoomAt(zoom_ * (event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15), event->position());
}

void PreviewWidget::mousePressEvent(QMouseEvent *event)
{
  if(image_.isNull()) return;
  if(event->button() == Qt::LeftButton && promptMode_ == PromptMode::None
     && dividerHit(event->position()))
  {
    dividerDragging_ = true;
    setCursor(Qt::SplitHCursor);
    event->accept();
    return;
  }
  if((promptMode_ == PromptMode::Positive || promptMode_ == PromptMode::Negative)
     && event->button() == Qt::LeftButton)
  {
    const QPointF point = widgetToImage(event->position());
    if(QRectF{QPointF{0.0, 0.0}, QSizeF{image_.size()}}.contains(point))
    {
      emit pointRequested(point, promptMode_ == PromptMode::Positive);
    }
    return;
  }
  if((promptMode_ == PromptMode::BrushPaint || promptMode_ == PromptMode::BrushErase)
     && event->button() == Qt::LeftButton)
  {
    const QPointF point = widgetToImage(event->position());
    if(QRectF{QPointF{0.0, 0.0}, QSizeF{image_.size()}}.contains(point))
    {
      brushing_ = true;
      lastBrushPoint_ = point;
      emit brushStrokeStarted(point, promptMode_ == PromptMode::BrushPaint);
    }
    return;
  }
  if((promptMode_ == PromptMode::GradientAdd || promptMode_ == PromptMode::GradientSubtract
      || promptMode_ == PromptMode::EllipseAdd || promptMode_ == PromptMode::EllipseSubtract)
     && event->button() == Qt::LeftButton)
  {
    const QPointF point = widgetToImage(event->position());
    if(QRectF{QPointF{0.0, 0.0}, QSizeF{image_.size()}}.contains(point))
    {
      shaping_ = true;
      shapeStart_ = point;
      hoverImagePoint_ = point;
      update();
    }
    return;
  }
  if(event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
  {
    dragging_ = true;
    dragStart_ = event->position();
    panStart_ = pan_;
    setCursor(Qt::ClosedHandCursor);
  }
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
  if(!image_.isNull())
  {
    hoverImagePoint_ = widgetToImage(event->position());
    update();
  }
  if(dividerDragging_)
  {
    updateDivider(event->position());
    event->accept();
    return;
  }
  if(brushing_)
  {
    QPointF point = widgetToImage(event->position());
    point.setX(std::clamp(point.x(), 0.0, static_cast<double>(image_.width())));
    point.setY(std::clamp(point.y(), 0.0, static_cast<double>(image_.height())));
    emit brushStrokeContinued(lastBrushPoint_, point);
    lastBrushPoint_ = point;
    return;
  }
  if(shaping_)
  {
    hoverImagePoint_ = widgetToImage(event->position());
    update();
    return;
  }
  if(dragging_)
  {
    pan_ = panStart_ + event->position() - dragStart_;
    update();
  }
  else updateCursor(event->position());
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
  if(dividerDragging_ && event->button() == Qt::LeftButton)
  {
    dividerDragging_ = false;
    updateDivider(event->position());
    updateCursor(event->position());
    event->accept();
    return;
  }
  if(brushing_ && event->button() == Qt::LeftButton)
  {
    brushing_ = false;
    emit brushStrokeFinished();
  }
  if(shaping_ && event->button() == Qt::LeftButton)
  {
    shaping_ = false;
    const QPointF end = widgetToImage(event->position());
    const bool gradient = promptMode_ == PromptMode::GradientAdd
      || promptMode_ == PromptMode::GradientSubtract;
    const bool foreground = promptMode_ == PromptMode::GradientAdd
      || promptMode_ == PromptMode::EllipseAdd;
    emit shapeRequested(shapeStart_, end, gradient, foreground);
  }
  dragging_ = false;
  updateCursor(event->position());
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  if(actualSizeMode_ && !image_.isNull())
  {
    const double fit = std::min(static_cast<double>(width()) / image_.width(),
                                static_cast<double>(height()) / image_.height());
    if(fit > 0.0) zoom_ = 1.0 / fit;
  }
  emit zoomChanged(imageScale());
}
