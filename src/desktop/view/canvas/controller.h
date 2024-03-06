// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_VIEW_CANVAS_CONTROLLER_H
#define DESKTOP_VIEW_CANVAS_CONTROLLER_H
#include <QColor>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QVector>
#include <functional>

class QMouseEvent;

namespace canvas {
class CanvasModel;
class TileCache;
}

namespace view {

class CanvasController : public QObject {
	Q_OBJECT
public:
	CanvasController(QWidget *parent);

	void setCanvasWidget(QWidget *canvasWidget);
	void setCanvasModel(canvas::CanvasModel *canvasModel);

	const QColor &clearColor() const { return m_clearColor; }
	const QSize &canvasSize() const { return m_canvasSize; }
	const QTransform transform() const { return m_transform; }

	bool shouldRenderSmooth() const;

	qreal zoom() const { return m_zoom; }
	qreal actualZoom() const { return actualZoomFor(m_zoom); }
	qreal actualZoomFor(qreal zoom) const { return zoom / devicePixelRatioF(); }
	qreal rotation() const;

	void scrollTo(const QPointF &pos);
	void scrollBy(int x, int y);
	void scrollStepLeft();
	void scrollStepRight();
	void scrollStepUp();
	void scrollStepDown();
	void setZoom(qreal zoom);
	void setZoomAt(qreal zoom, const QPointF &pos);
	void resetZoom();
	void zoomToFit();
	void zoomToFitHeight();
	void zoomToFitWidth();
	void zoomIn();
	void zoomOut();
	void zoomSteps(int steps);
	void zoomStepsAt(int steps, const QPointF &point);
	void setRotation(qreal degrees);
	void resetRotation();
	void rotateStepClockwise();
	void rotateStepCounterClockwise();
	void setFlip(bool flip);
	void setMirror(bool mirror);

	void eachDirtyTileReset(
		const std::function<void(const QRect &rect, const void *pixels)> &fn);

	void handleMouseMove(QMouseEvent *event);
	void handleMousePress(QMouseEvent *event);
	void handleMouseRelease(QMouseEvent *event);

signals:
	void clearColorChanged();
	void renderSmoothChanged();
	void canvasSizeChanged();
	void transformChanged(qreal zoom, qreal angle);
	void tileChanged(int tileX, int tileY);

private:
	static constexpr qreal SCROLL_STEP = 64.0;
	static constexpr qreal ROTATION_STEP = 5.0;

	void setClearColor(const QColor clearColor);
	void setRenderSmooth(bool renderSmooth);
	void updateCanvasSize(const QSize &newSize, const QPoint &offset);
	void markTileDirty(int tileX, int tileY);

	QPointF mapPointToCanvas(const QPoint &point) const;
	QPointF mapPointToCanvasF(const QPointF &point) const;

	void updateCanvasTransform(const std::function<void()> &block);
	void resetCanvasTransform();

	QTransform calculateCanvasTransform() const
	{
		return calculateCanvasTransformFrom(
			m_pos, m_zoom, m_rotation, m_mirror, m_flip);
	}

	QTransform calculateCanvasTransformFrom(
		const QPointF &pos, qreal zoom, qreal rotate, bool mirror,
		bool flip) const;

	static void mirrorFlip(QTransform &matrix, bool mirror, bool flip);

	bool isRotationInverted() const { return m_mirror ^ m_flip; }

	void setZoomToFit(Qt::Orientations orientations);

	void emitTransformChanged();

	QRectF rectF() const;
	QRect rect() const;
	qreal devicePixelRatioF() const;

	QWidget *m_canvasWidget = nullptr;
	canvas::CanvasModel *m_canvasModel = nullptr;
	QColor m_clearColor;
	bool m_renderSmooth = false;

	QPointF m_pos;
	qreal m_zoom = 1.0;
	qreal m_rotation = 0.0;
	bool m_flip = false;
	bool m_mirror = false;
	QRectF m_posBounds;
	QTransform m_transform;
	QTransform m_invertedTransform;

	QSize m_canvasSize;
	QSize m_tileSize;
	QVector<bool> m_dirtyTiles;

	bool m_dragging = false;
	QPoint m_lastDragPos;
};

}

#endif
