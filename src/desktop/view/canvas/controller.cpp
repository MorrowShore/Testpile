// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include "dpengine/tile.h"
}
#include "desktop/main.h"
#include "desktop/utils/qtguicompat.h"
#include "desktop/view/canvas/controller.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/paintengine.h"
#include <QSignalBlocker>
#include <cmath>

using libclient::settings::zoomMax;
using libclient::settings::zoomMin;

namespace view {

CanvasController::CanvasController(QWidget *parent)
	: QObject(parent)
{
	desktop::settings::Settings &settings = dpApp().settings();
	settings.bindCanvasViewBackgroundColor(
		this, &CanvasController::setClearColor);
	settings.bindRenderSmooth(this, &CanvasController::setRenderSmooth);
	resetCanvasTransform();
}

void CanvasController::setCanvasWidget(QWidget *canvasWidget)
{
	if(m_canvasWidget != canvasWidget) {
		m_canvasWidget = canvasWidget;
	}
}

void CanvasController::setCanvasModel(canvas::CanvasModel *canvasModel)
{
	m_canvasModel = canvasModel;
	updateCanvasSize(QSize(), QPoint());
	if(canvasModel) {
		canvas::PaintEngine *pe = canvasModel->paintEngine();
		connect(
			pe, &canvas::PaintEngine::resized, this,
			&CanvasController::updateCanvasSize, Qt::QueuedConnection);
		connect(
			pe, &canvas::PaintEngine::tileChanged, this,
			&CanvasController::markTileDirty, Qt::QueuedConnection);
	}
}

bool CanvasController::shouldRenderSmooth() const
{
	return m_renderSmooth && m_zoom <= 1.99 &&
		   !(std::fabs(m_zoom - 1.0) < 0.01 &&
			 std::fmod(std::fabs(rotation()), 90.0) < 0.01);
}

qreal CanvasController::rotation() const
{
	qreal r = isRotationInverted() ? 360.0 - m_rotation : m_rotation;
	return r > 180.0 ? r - 360.0 : r;
}

void CanvasController::scrollTo(const QPointF &point)
{
	updateCanvasTransform([&] {
		QTransform matrix = calculateCanvasTransformFrom(
			QPointF(), m_zoom, m_rotation, m_mirror, m_flip);
		m_pos = matrix.map(point);
	});
}

void CanvasController::scrollBy(int x, int y)
{
	updateCanvasTransform([&] {
		m_pos.setX(m_pos.x() + x);
		m_pos.setY(m_pos.y() + y);
	});
}

void CanvasController::scrollStepLeft()
{
	scrollBy(SCROLL_STEP * -(1.0 / m_zoom), 0.0);
}

void CanvasController::scrollStepRight()
{
	scrollBy(SCROLL_STEP * (1.0 / m_zoom), 0.0);
}

void CanvasController::scrollStepUp()
{
	scrollBy(0.0, SCROLL_STEP * -(1.0 / m_zoom));
}

void CanvasController::scrollStepDown()
{
	scrollBy(0.0, SCROLL_STEP * (1.0 / m_zoom));
}

void CanvasController::setZoom(qreal zoom)
{
	setZoomAt(zoom, mapPointToCanvasF(rectF().center()));
}

void CanvasController::setZoomAt(qreal zoom, const QPointF &point)
{
	// FIXME: position doesn't work.
	qreal newZoom = qBound(zoomMin, zoom, zoomMax);
	if(newZoom != m_zoom) {
		QTransform matrix;
		mirrorFlip(matrix, m_mirror, m_flip);
		matrix.rotate(m_rotation);

		updateCanvasTransform([&] {
			m_pos +=
				matrix.map(point * (actualZoomFor(newZoom) - actualZoom()));
			m_zoom = newZoom;
		});
	}
}

void CanvasController::resetZoom()
{
	setZoom(1.0);
}

void CanvasController::zoomToFit()
{
	setZoomToFit(Qt::Horizontal | Qt::Vertical);
}

void CanvasController::zoomIn()
{
	zoomSteps(1);
}

void CanvasController::zoomOut()
{
	zoomSteps(-1);
}

void CanvasController::zoomToFitWidth()
{
	setZoomToFit(Qt::Horizontal);
}

void CanvasController::zoomToFitHeight()
{
	setZoomToFit(Qt::Vertical);
}

void CanvasController::zoomSteps(int steps)
{
	zoomStepsAt(steps, mapPointToCanvasF(rectF().center()));
}

void CanvasController::zoomStepsAt(int steps, const QPointF &point)
{
	constexpr qreal eps = 1e-5;
	const QVector<qreal> &zoomLevels = libclient::settings::zoomLevels();
	// This doesn't actually take the number of steps into account, it just
	// zooms by a single step. But that works really well, so I'll leave it be.
	if(steps > 0) {
		int i = 0;
		while(i < zoomLevels.size() - 1 && m_zoom > zoomLevels[i] - eps) {
			i++;
		}
		qreal level = zoomLevels[i];
		qreal zoom = m_zoom > level - eps ? zoomMax : qMax(m_zoom, level);
		setZoomAt(zoom, point);
	} else if(steps < 0) {
		int i = zoomLevels.size() - 1;
		while(i > 0 && m_zoom < zoomLevels[i] + eps) {
			i--;
		}
		qreal level = zoomLevels[i];
		qreal zoom = m_zoom < level + eps ? zoomMin : qMin(m_zoom, level);
		setZoomAt(zoom, point);
	}
}

void CanvasController::setRotation(qreal degrees)
{
	degrees = std::fmod(degrees, 360.0);
	if(degrees < 0.0) {
		degrees += 360.0;
	}

	bool inverted = isRotationInverted();
	if(inverted) {
		degrees = 360.0 - degrees;
	}

	if(degrees != m_rotation) {
		QTransform prev, cur;
		prev.rotate(m_rotation);
		cur.rotate(degrees);

		updateCanvasTransform([&] {
			if(inverted) {
				m_pos = cur.inverted().map(prev.map(m_pos));
			} else {
				m_pos = prev.inverted().map(cur.map(m_pos));
			}
			m_rotation = degrees;
		});
	}
}

void CanvasController::resetRotation()
{
	setRotation(0.0);
}

void CanvasController::rotateStepClockwise()
{
	setRotation(m_rotation + ROTATION_STEP);
}

void CanvasController::rotateStepCounterClockwise()
{
	setRotation(m_rotation - ROTATION_STEP);
}

void CanvasController::setFlip(bool flip)
{
	if(flip != m_flip) {
		QTransform prev, cur;
		mirrorFlip(prev, m_mirror, m_flip);
		mirrorFlip(cur, m_mirror, flip);

		updateCanvasTransform([&] {
			m_pos = cur.inverted().map(prev.map(m_pos));
			m_flip = flip;
		});
	}
}

void CanvasController::setMirror(bool mirror)
{
	if(mirror != m_mirror) {
		QTransform prev, cur;
		mirrorFlip(prev, m_mirror, m_flip);
		mirrorFlip(cur, mirror, m_flip);

		updateCanvasTransform([&] {
			m_pos = cur.inverted().map(prev.map(m_pos));
			m_mirror = mirror;
		});
	}
}

void CanvasController::eachDirtyTileReset(
	const std::function<void(const QRect &rect, const void *pixels)> &fn)
{
	if(m_canvasModel) {
		m_canvasModel->paintEngine()->withTileCache(
			[&](const canvas::TileCache &tileCache) {
				Q_ASSERT(m_canvasSize.width() == tileCache.width());
				Q_ASSERT(m_canvasSize.height() == tileCache.height());
				Q_ASSERT(m_tileSize.width() == tileCache.xtiles());
				Q_ASSERT(m_tileSize.height() == tileCache.ytiles());
				int xtiles = m_tileSize.width();
				int ytiles = m_tileSize.height();
				for(int tileY = 0; tileY < ytiles; ++tileY) {
					for(int tileX = 0; tileX < xtiles; ++tileX) {
						bool &dirtyTile = m_dirtyTiles[tileY * xtiles + tileX];
						if(dirtyTile) {
							dirtyTile = false;
							fn(tileCache.rectAt(tileX, tileY),
							   tileCache.pixelsAt(tileX, tileY));
						}
					}
				}
			});
	}
}

void CanvasController::handleMouseMove(QMouseEvent *event)
{
	if(m_canvasWidget && m_dragging) {
		event->accept();
		QPoint pos = m_canvasWidget->mapFromGlobal(compat::globalPos(*event));
		int deltaX = m_lastDragPos.x() - pos.x();
		int deltaY = m_lastDragPos.y() - pos.y();
		scrollBy(deltaX, deltaY);
		m_lastDragPos = pos;
	}
}

void CanvasController::handleMousePress(QMouseEvent *event)
{
	if(m_canvasWidget) {
		event->accept();
		m_dragging = true;
		m_lastDragPos =
			m_canvasWidget->mapFromGlobal(compat::globalPos(*event));
	}
}

void CanvasController::handleMouseRelease(QMouseEvent *event)
{
	if(m_dragging) {
		event->accept();
		m_dragging = false;
	}
}

void CanvasController::setClearColor(const QColor clearColor)
{
	if(clearColor != m_clearColor) {
		m_clearColor = clearColor;
		emit clearColorChanged();
	}
}

void CanvasController::setRenderSmooth(bool renderSmooth)
{
	if(renderSmooth != m_renderSmooth) {
		m_renderSmooth = renderSmooth;
		emit renderSmoothChanged();
	}
}

void CanvasController::updateCanvasSize(
	const QSize &newSize, const QPoint &offset)
{
	QSize oldSize = m_canvasSize;
	m_canvasSize = newSize;
	if(oldSize.isEmpty()) {
		zoomToFit();
	} else {
		scrollBy(offset.x() * m_zoom, offset.y() * m_zoom);
	}

	DP_TileCounts tc = DP_tile_counts_round(newSize.width(), newSize.height());
	m_tileSize.setWidth(tc.x);
	m_tileSize.setHeight(tc.y);
	m_dirtyTiles.fill(true, tc.x * tc.y);

	// FIXME
	if(m_canvasModel) {
		m_canvasModel->paintEngine()->setCanvasViewArea(
			QRect(QPoint(0.0, 0.0), m_canvasSize));
	}

	emit canvasSizeChanged();
}

void CanvasController::markTileDirty(int tileX, int tileY)
{
	m_dirtyTiles[tileY * m_tileSize.width() + tileX] = true;
	emit tileChanged(tileX, tileY);
}

QPointF CanvasController::mapPointToCanvas(const QPoint &point) const
{
	return mapPointToCanvasF(QPointF(point));
}

QPointF CanvasController::mapPointToCanvasF(const QPointF &point) const
{
	return m_invertedTransform.map(point);
}

void CanvasController::updateCanvasTransform(const std::function<void()> &block)
{
	block();
	resetCanvasTransform();
}

void CanvasController::resetCanvasTransform()
{
	m_transform = calculateCanvasTransform();
	m_invertedTransform = m_transform.inverted();
	emitTransformChanged();
}

QTransform CanvasController::calculateCanvasTransformFrom(
	const QPointF &pos, qreal zoom, qreal rotate, bool mirror, bool flip) const
{
	QTransform matrix;
	matrix.translate(-pos.x(), -pos.y());
	qreal scale = actualZoomFor(zoom);
	matrix.scale(scale, scale);
	mirrorFlip(matrix, mirror, flip);
	matrix.rotate(rotate);
	return matrix;
}

void CanvasController::mirrorFlip(QTransform &matrix, bool mirror, bool flip)
{
	matrix.scale(mirror ? -1.0 : 1.0, flip ? -1.0 : 1.0);
}

void CanvasController::setZoomToFit(Qt::Orientations orientations)
{
	if(m_canvasWidget) {
		qreal dpr = devicePixelRatioF();
		QRectF r = QRectF(QPointF(), QSizeF(m_canvasSize) / dpr);
		qreal xScale = qreal(m_canvasWidget->width()) / r.width();
		qreal yScale = qreal(m_canvasWidget->height()) / r.height();
		qreal scale;
		if(orientations.testFlag(Qt::Horizontal)) {
			if(orientations.testFlag(Qt::Vertical)) {
				scale = qMin(xScale, yScale);
			} else {
				scale = xScale;
			}
		} else {
			scale = yScale;
		}

		qreal rotation = m_rotation;
		bool mirror = m_mirror;
		bool flip = m_flip;

		m_pos = r.center();
		m_zoom = 1.0;
		m_rotation = 0.0;
		m_mirror = false;
		m_flip = false;

		{
			QSignalBlocker blocker(this);
			setZoomAt(scale, m_pos * dpr);
			setRotation(rotation);
			setMirror(mirror);
			setFlip(flip);
		}

		emitTransformChanged();
	}
}

void CanvasController::emitTransformChanged()
{
	emit transformChanged(zoom(), rotation());
}

QRectF CanvasController::rectF() const
{
	return QRectF(rect());
}

QRect CanvasController::rect() const
{
	return m_canvasWidget ? m_canvasWidget->rect() : QRect();
}

qreal CanvasController::devicePixelRatioF() const
{
	return m_canvasWidget ? m_canvasWidget->devicePixelRatioF()
						  : dpApp().devicePixelRatio();
}

}
