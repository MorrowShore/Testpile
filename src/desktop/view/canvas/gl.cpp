// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/view/canvas/gl.h"
#include "desktop/view/canvas/controller.h"
#include "libclient/canvas/tilecache.h"
#include <QOpenGLFunctions>

Q_LOGGING_CATEGORY(lcDpGlCanvas, "net.drawpile.view.canvas.gl", QtDebugMsg)

namespace view {

struct GlCanvas::Private {
	static constexpr int BUFFER_COUNT = 2;
	static constexpr int VERTEX_COUNT = 8;

	void dispose(QOpenGLFunctions *f)
	{
		Q_ASSERT(initialized);
		initialized = false;

		qCDebug(lcDpGlCanvas, "Delete buffers");
		f->glDeleteBuffers(BUFFER_COUNT, buffers);
		for(int i = 0; i < BUFFER_COUNT; ++i) {
			buffers[i] = 0;
		}

		qCDebug(lcDpGlCanvas, "Delete texture");
		f->glDeleteTextures(1, &texture);
		texture = 0;

		qCDebug(lcDpGlCanvas, "Delete program");
		f->glDeleteProgram(program);
		program = 0;
		viewLocation = 0;
		samplerLocation = 0;
	}

	static GLuint
	parseShader(QOpenGLFunctions *f, GLenum type, const char *code)
	{
		GLuint shader = f->glCreateShader(type);
		f->glShaderSource(shader, 1, &code, nullptr);
		f->glCompileShader(shader);
		return shader;
	}

	static void dumpShaderInfoLog(QOpenGLFunctions *f, GLuint shader)
	{
		GLint logLength;
		f->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
		if(logLength > 0) {
			QByteArray buffer(logLength, '\0');
			GLsizei bufferLength;
			f->glGetShaderInfoLog(
				shader, logLength, &bufferLength, buffer.data());
			qCWarning(
				lcDpGlCanvas, "Shader info log: %.*s", int(bufferLength),
				buffer.constData());
		}
	}

	static GLint getShaderCompileStatus(QOpenGLFunctions *f, GLuint shader)
	{
		GLint compileStatus;
		f->glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
		return compileStatus;
	}

	static GLuint
	compileShader(QOpenGLFunctions *f, GLenum type, const char *code)
	{
		GLuint shader = parseShader(f, type, code);
		dumpShaderInfoLog(f, shader);
		if(!getShaderCompileStatus(f, shader)) {
			const char *title = type == GL_VERTEX_SHADER	 ? "vertex"
								: type == GL_FRAGMENT_SHADER ? "fragment"
															 : "unknown";
			qCCritical(lcDpGlCanvas, "Can't compile %s shader", title);
		}
		return shader;
	}

	static void dumpProgramInfoLog(QOpenGLFunctions *f, GLuint program)
	{
		GLint logLength;
		f->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
		if(logLength > 0) {
			QByteArray buffer(logLength, '\0');
			GLsizei bufferLength;
			f->glGetProgramInfoLog(
				program, logLength, &bufferLength, buffer.data());
			qCWarning(
				lcDpGlCanvas, "Program info log: %.*s", int(bufferLength),
				buffer.constData());
		}
	}

	static GLint getProgramLinkStatus(QOpenGLFunctions *f, GLuint program)
	{
		GLint linkStatus;
		f->glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		return linkStatus;
	}

	static GLuint initTexture(QOpenGLFunctions *f)
	{
		f->glActiveTexture(GL_TEXTURE0);
		GLuint texture = 0;
		f->glGenTextures(1, &texture);
		if(texture == 0) {
			qCWarning(lcDpGlCanvas, "Can't generate texture");
		} else {
			f->glBindTexture(GL_TEXTURE_2D, texture);
			f->glTexParameteri(
				GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			f->glTexParameteri(
				GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			f->glTexParameteri(
				GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			f->glTexParameteri(
				GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			f->glBindTexture(GL_TEXTURE_2D, 0);
		}
		return texture;
	}

	void refreshTexture(QOpenGLFunctions *f)
	{
		if(dirtyTexture) {
			dirtyTexture = false;
			const QSize &size = controller->canvasSize();
			if(size != textureSize) {
				qCDebug(
					lcDpGlCanvas, "Resize texture from %dx%d to %dx%d",
					textureSize.width(), textureSize.height(), size.width(),
					size.height());
				QByteArray pixels(size.width() * size.height() * 4, char(255));
				f->glTexImage2D(
					GL_TEXTURE_2D, 0, GL_RGBA, size.width(), size.height(), 0,
					GL_RGBA, GL_UNSIGNED_BYTE, pixels.constData());
				textureSize = size;
			}
		}
	}

	void refreshTextureFilter(QOpenGLFunctions *f)
	{
		if(dirtyTextureFilter) {
			dirtyTextureFilter = false;
			bool linear = controller->shouldRenderSmooth();
			if(linear && !textureFilterLinear) {
				qCDebug(lcDpGlCanvas, "Set texture filter to linear");
				textureFilterLinear = true;
				f->glTexParameteri(
					GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				f->glTexParameteri(
					GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			} else if(!linear && textureFilterLinear) {
				qCDebug(lcDpGlCanvas, "Set texture filter to nearest");
				textureFilterLinear = false;
				f->glTexParameteri(
					GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				f->glTexParameteri(
					GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			}
		}
	}

	void refreshTiles(QOpenGLFunctions *f)
	{
		if(dirtyTiles) {
			dirtyTiles = false;
			controller->eachDirtyTileReset(
				[&](const QRect &rect, const void *pixels) {
					f->glTexSubImage2D(
						GL_TEXTURE_2D, 0, rect.x(), rect.y(), rect.width(),
						rect.height(), GL_RGBA, GL_UNSIGNED_BYTE, pixels);
				});
		}
	}

	void refreshVertices()
	{
		if(dirtyVertices) {
			dirtyVertices = false;
			const QSize &size = controller->canvasSize();
			const QTransform transform = controller->transform();
			qCDebug(
				lcDpGlCanvas,
				"Refresh vertices to canvas size %dx%d "
				"transform (%f, %f, %f; %f, %f, %f; %f, %f %f)",
				size.width(), size.height(), transform.m11(), transform.m12(),
				transform.m13(), transform.m21(), transform.m22(),
				transform.m23(), transform.m31(), transform.m32(),
				transform.m33());

			qreal x1 = 0.0;
			qreal y1 = 0.0;
			qreal x2 = size.width();
			qreal y2 = size.height();

			QPointF bl = transform.map(QPointF(x1, y2));
			QPointF tl = transform.map(QPointF(x1, y1));
			QPointF br = transform.map(QPointF(x2, y2));
			QPointF tr = transform.map(QPointF(x2, y1));

			vertices[0] = GLfloat(bl.x());
			vertices[1] = GLfloat(bl.y());
			vertices[2] = GLfloat(tl.x());
			vertices[3] = GLfloat(tl.y());
			vertices[4] = GLfloat(br.x());
			vertices[5] = GLfloat(br.y());
			vertices[6] = GLfloat(tr.x());
			vertices[7] = GLfloat(tr.y());
		}
	}

	void refreshUvs()
	{
		if(dirtyUvs) {
			dirtyUvs = false;
			QSize size = controller->canvasSize();
			qCDebug(
				lcDpGlCanvas, "Refresh uvs to canvas size %dx%d", size.width(),
				size.height());

			GLfloat u = 1.0f;
			GLfloat v = 1.0f;
			uvs[1] = v;
			uvs[4] = u;
			uvs[5] = v;
			uvs[6] = u;
		}
	}

	CanvasController *controller;
	QSize viewSize;
	bool dirtyTexture = true;
	bool dirtyTextureFilter = true;
	bool dirtyTiles = true;
	bool dirtyUvs = true;
	bool dirtyVertices = true;
	bool initialized = false;
	GLuint program = 0;
	GLuint buffers[BUFFER_COUNT] = {0, 0};
	GLint viewLocation = 0;
	GLint samplerLocation = 0;
	GLuint texture = 0;
	QSize textureSize;
	bool textureFilterLinear = false;
	GLfloat vertices[VERTEX_COUNT];
	GLfloat uvs[VERTEX_COUNT];
};

GlCanvas::GlCanvas(CanvasController *controller, QWidget *parent)
	: QOpenGLWidget(parent)
	, d(new Private)
{
	d->controller = controller;
	controller->setCanvasWidget(this);
	connect(
		controller, &CanvasController::clearColorChanged, this,
		&GlCanvas::onControllerClearColorChanged);
	connect(
		controller, &CanvasController::renderSmoothChanged, this,
		&GlCanvas::onControllerRenderSmoothChanged);
	connect(
		controller, &CanvasController::canvasSizeChanged, this,
		&GlCanvas::onControllerCanvasSizeChanged);
	connect(
		controller, &CanvasController::transformChanged, this,
		&GlCanvas::onControllerTransformChanged);
	connect(
		controller, &CanvasController::tileChanged, this,
		&GlCanvas::onControllerTileChanged);
}

GlCanvas::~GlCanvas()
{
	if(d->initialized) {
		makeCurrent();
		d->dispose(QOpenGLContext::currentContext()->functions());
		doneCurrent();
	}
	delete d;
}

QWidget *GlCanvas::asWidget()
{
	return this;
}

void GlCanvas::handleResize(QResizeEvent *event)
{
	resizeEvent(event);
}

void GlCanvas::handlePaint(QPaintEvent *event)
{
	paintEvent(event);
}

void GlCanvas::initializeGL()
{
	static constexpr char vertexCode[] = R"""(
#version 100

uniform vec2 u_view;
attribute mediump vec2 v_pos;
attribute mediump vec2 v_uv;
varying vec2 f_uv;

void main()
{
	float x = 2.0 * v_pos.x / u_view.x;
	float y = -2.0 * v_pos.y / u_view.y;
	gl_Position = vec4(x, y, 0.0, 1.0);
	f_uv = v_uv;
}
	)""";

	static constexpr char fragmentCode[] = R"""(
#version 100
precision mediump float;

uniform sampler2D u_sampler;
varying vec2 f_uv;

void main()
{
	gl_FragColor = vec4(texture2D(u_sampler, f_uv).bgr, 1.0);
}
	)""";

	qCDebug(lcDpGlCanvas, "initializeGL");

	QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
	if(d->initialized) {
		qCWarning(lcDpGlCanvas, "Already initialized!");
	} else {
		d->initialized = true;

		qCDebug(lcDpGlCanvas, "Create shader program");
		d->program = f->glCreateProgram();

		qCDebug(lcDpGlCanvas, "Compile vertex shader: %s", vertexCode);
		GLuint vertexShader =
			Private::compileShader(f, GL_VERTEX_SHADER, vertexCode);

		qCDebug(lcDpGlCanvas, "Compile fragment shader: %s", fragmentCode);
		GLuint fragmentShader =
			Private::compileShader(f, GL_FRAGMENT_SHADER, fragmentCode);

		qCDebug(lcDpGlCanvas, "Attach vertex shader");
		f->glAttachShader(d->program, vertexShader);

		qCDebug(lcDpGlCanvas, "Attach fragment shader");
		f->glAttachShader(d->program, fragmentShader);

		qCDebug(lcDpGlCanvas, "Link shader program");
		f->glLinkProgram(d->program);

		Private::dumpProgramInfoLog(f, d->program);
		if(d->getProgramLinkStatus(f, d->program)) {
			qCDebug(lcDpGlCanvas, "Get view uniform location");
			d->viewLocation = f->glGetUniformLocation(d->program, "u_view");
			qCDebug(lcDpGlCanvas, "Get sampler uniform location");
			d->samplerLocation =
				f->glGetUniformLocation(d->program, "u_sampler");
		} else {
			qCCritical(lcDpGlCanvas, "Can't link shader program");
		}

		qCDebug(lcDpGlCanvas, "Detach vertex shader");
		f->glDetachShader(d->program, vertexShader);
		qCDebug(lcDpGlCanvas, "Detach fragment shader");
		f->glDetachShader(d->program, fragmentShader);
		qCDebug(lcDpGlCanvas, "Delete vertex shader");
		f->glDeleteShader(vertexShader);
		qCDebug(lcDpGlCanvas, "Delete fragment shader");
		f->glDeleteShader(fragmentShader);

		qCDebug(lcDpGlCanvas, "Generate buffers");
		f->glGenBuffers(Private::BUFFER_COUNT, d->buffers);

		qCDebug(lcDpGlCanvas, "Init texture");
		d->texture = Private::initTexture(f);
		d->textureSize = QSize(0, 0);
		d->textureFilterLinear = false;

		for(int i = 0; i < Private::VERTEX_COUNT; ++i) {
			d->vertices[i] = 0.0f;
			d->uvs[i] = 0.0f;
		}

		d->dirtyTexture = true;
		d->dirtyTextureFilter = true;
		d->dirtyTiles = true;
		d->dirtyVertices = true;
		d->dirtyUvs = true;
	}
}

void GlCanvas::paintGL()
{
	qCDebug(lcDpGlCanvas, "paintGL");
	QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
	f->glDisable(GL_BLEND);

	CanvasController *controller = d->controller;
	const QColor &clearColor = controller->clearColor();
	f->glClearColor(
		clearColor.redF(), clearColor.greenF(), clearColor.blueF(), 1.0);
	f->glClear(GL_COLOR_BUFFER_BIT);

	if(!controller->canvasSize().isEmpty() && !d->viewSize.isEmpty()) {
		f->glActiveTexture(GL_TEXTURE0);
		f->glBindTexture(GL_TEXTURE_2D, d->texture);
		d->refreshTexture(f);
		d->refreshTextureFilter(f);
		d->refreshTiles(f);

		f->glUseProgram(d->program);
		f->glUniform2f(
			d->viewLocation, GLfloat(d->viewSize.width()),
			GLfloat(d->viewSize.height()));
		f->glUniform1i(d->samplerLocation, 0);

		d->refreshVertices();
		f->glEnableVertexAttribArray(0);
		f->glBindBuffer(GL_ARRAY_BUFFER, d->buffers[0]);
		f->glBufferData(
			GL_ARRAY_BUFFER, sizeof(d->vertices), d->vertices, GL_STATIC_DRAW);
		f->glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, nullptr);

		d->refreshUvs();
		f->glEnableVertexAttribArray(1);
		f->glBindBuffer(GL_ARRAY_BUFFER, d->buffers[1]);
		f->glBufferData(
			GL_ARRAY_BUFFER, sizeof(d->uvs), d->uvs, GL_STATIC_DRAW);
		f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

		f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
}

void GlCanvas::resizeGL(int w, int h)
{
	qCDebug(lcDpGlCanvas, "resizeGL(%d, %d)", w, h);
	d->viewSize.setWidth(w);
	d->viewSize.setHeight(h);
}

void GlCanvas::onControllerClearColorChanged()
{
	update();
}

void GlCanvas::onControllerRenderSmoothChanged()
{
	d->dirtyTextureFilter = true;
	update();
}

void GlCanvas::onControllerCanvasSizeChanged()
{
	d->dirtyTexture = true;
	d->dirtyTiles = true;
	d->dirtyVertices = true;
	d->dirtyUvs = true;
	update();
}

void GlCanvas::onControllerTransformChanged()
{
	d->dirtyTextureFilter = true;
	d->dirtyVertices = true;
	update();
}

void GlCanvas::onControllerTileChanged(int tileX, int tileY)
{
	Q_UNUSED(tileX);
	Q_UNUSED(tileY);
	qCDebug(lcDpGlCanvas, "tile changed %d %d", tileX, tileY);
	d->dirtyTiles = true;
	update();
}

}
