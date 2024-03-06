// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DESKTOP_VIEW_CANVAS_GL_H
#define DESKTOP_VIEW_CANVAS_GL_H
#include "desktop/view/canvas/interface.h"
#include <QLoggingCategory>
#include <QOpenGLWidget>

namespace view {

class CanvasController;

class GlCanvas final : public QOpenGLWidget, public CanvasInterface {
	Q_OBJECT
public:
	GlCanvas(CanvasController *controller, QWidget *parent = nullptr);
	~GlCanvas() override;

	QWidget *asWidget() override;

	void handleResize(QResizeEvent *event) override;
	void handlePaint(QPaintEvent *event) override;

protected:
	void initializeGL() override;
	void paintGL() override;
	void resizeGL(int w, int h) override;

private:
	void onControllerClearColorChanged();
	void onControllerRenderSmoothChanged();
	void onControllerCanvasSizeChanged();
	void onControllerTransformChanged();
	void onControllerTileChanged(int tileX, int tileY);

	struct Private;
	Private *d;
};

}

#endif
