// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_VIEW_CANVAS_INTERFACE_H
#define DESKTOP_VIEW_CANVAS_INTERFACE_H

class QPaintEvent;
class QResizeEvent;
class QWidget;

namespace view {

class CanvasController;

class CanvasInterface {
public:
	virtual ~CanvasInterface() = default;

	virtual QWidget *asWidget() = 0;

	virtual void handleResize(QResizeEvent *event) = 0;
	virtual void handlePaint(QPaintEvent *event) = 0;
};

}

#endif
