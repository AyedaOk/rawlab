#pragma once

#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <QWidget>

class PreviewWidget final : public QWidget
{
  Q_OBJECT

public:
  enum class PromptMode { None, Positive, Negative, BrushPaint, BrushErase };

  struct PromptMarker
  {
    QPointF position;
    bool foreground = true;
  };

  explicit PreviewWidget(QWidget *parent = nullptr);

  void setImage(QImage image, bool resetView = false);
  void setBeforeImage(QImage image);
  void setComparisonEnabled(bool enabled);
  void setMask(QImage mask);
  void setPrompts(QVector<PromptMarker> prompts);
  void setPromptMode(PromptMode mode);
  void setBrushRadius(float radius);
  void setProcessing(bool processing);
  bool hasImage() const { return !image_.isNull(); }
  double imageScale() const;
  void fitImage();
  void zoomIn();
  void zoomOut();
  void actualSize();

signals:
  void pointRequested(QPointF imagePosition, bool foreground);
  void brushStrokeStarted(QPointF imagePosition, bool paint);
  void brushStrokeContinued(QPointF from, QPointF to);
  void brushStrokeFinished();
  void zoomChanged(double imageScale);

protected:
  void paintEvent(QPaintEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  QRectF imageRect() const;
  QPointF widgetToImage(const QPointF &position) const;
  void zoomAt(double factor, const QPointF &anchor);
  bool dividerHit(const QPointF &position) const;
  void updateDivider(const QPointF &position);
  void updateCursor(const QPointF &position);

  QImage image_;
  QImage beforeImage_;
  QImage mask_;
  QVector<PromptMarker> prompts_;
  PromptMode promptMode_ = PromptMode::None;
  double zoom_ = 1.0;
  QPointF pan_;
  QPointF dragStart_;
  QPointF panStart_;
  bool dragging_ = false;
  bool brushing_ = false;
  bool comparisonEnabled_ = false;
  bool dividerDragging_ = false;
  double dividerPosition_ = 0.5;
  bool actualSizeMode_ = false;
  QPointF lastBrushPoint_;
  QPointF hoverImagePoint_;
  float brushRadius_ = 30.0f;
  QTimer processingTimer_;
  bool processing_ = false;
  int processingAngle_ = 0;
};
