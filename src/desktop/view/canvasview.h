// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_VIEW_CANVASVIEW_H
#define DESKTOP_VIEW_CANVASVIEW_H
#include <QAbstractScrollArea>

namespace view {

class CanvasController;
class CanvasInterface;

class CanvasView final : public QAbstractScrollArea {
	Q_OBJECT
public:
	CanvasView(QWidget *parent = nullptr);

	CanvasController *controller() { return m_controller; }

protected:
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	void setBackgroundColor(const QColor &backgroundColor);

	CanvasController *m_controller;
	CanvasInterface *m_canvas;
};

}

#endif
