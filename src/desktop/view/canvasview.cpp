// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/view/canvasview.h"
#include "desktop/main.h"
#include "desktop/view/canvas/controller.h"
#include "desktop/view/canvas/gl.h"
#include "desktop/view/canvas/interface.h"

namespace view {

CanvasView::CanvasView(QWidget *parent)
	: QAbstractScrollArea(parent)
	, m_controller(new CanvasController(this))
	, m_canvas(new GlCanvas(m_controller, this))
{
	desktop::settings::Settings &settings = dpApp().settings();
	settings.bindCanvasViewBackgroundColor(
		this, &CanvasView::setBackgroundColor);
	setViewport(m_canvas->asWidget());
	setMouseTracking(true);
	setTabletTracking(true);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
	QAbstractScrollArea::resizeEvent(event);
	m_canvas->handleResize(event);
}

void CanvasView::paintEvent(QPaintEvent *event)
{
	QAbstractScrollArea::paintEvent(event);
	m_canvas->handlePaint(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
	QAbstractScrollArea::mouseMoveEvent(event);
	m_controller->handleMouseMove(event);
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
	QAbstractScrollArea::mousePressEvent(event);
	m_controller->handleMousePress(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
	QAbstractScrollArea::mouseReleaseEvent(event);
	m_controller->handleMouseRelease(event);
}

void CanvasView::setBackgroundColor(const QColor &backgroundColor)
{
	setStyleSheet(
		QStringLiteral("QAbstractScrollArea { background-color: %1; }")
			.arg(backgroundColor.name(QColor::HexRgb)));
}

}
