// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOOLS_SELECTION_H
#define TOOLS_SELECTION_H
#include "libclient/canvas/selection.h"
#include "libclient/net/message.h"
#include "libclient/tools/tool.h"

class QImage;
class QPolygon;

namespace tools {

/**
 * @brief Base class for selection tool
 *
 * These are used for selecting regions for copying & pasting, as well
 * as filling regions with solid color.
 */
class SelectionTool : public Tool {
public:
	SelectionTool(ToolController &owner, Type type, QCursor cursor)
		: Tool(owner, type, cursor, true, false, false)
		, m_allowTransform{true}
	{
	}

	void
	begin(const canvas::Point &point, bool right, float zoom) override final;

	void motion(
		const canvas::Point &point, bool constrain, bool center) override final;

	void end() override final;

	void finishMultipart() override final;
	void cancelMultipart() override final;
	void undoMultipart() override final;
	bool isMultipart() const override final;

	void offsetActiveTool(int x, int y) override;

	//! Start a layer region move operation
	void startMove();

	//! Allow selection moving and resizing
	void setTransformEnabled(bool enable) { m_allowTransform = enable; }

	static QImage transformSelectionImage(
		const QImage &source, const QPolygon &target, int interpolation,
		QPoint *offset);
	static QPolygon destinationQuad(
		const QImage &source, const QPolygon &target,
		QRect *outBounds = nullptr);
	static QImage shapeMask(
		const QColor &color, const QPolygonF &selection, QRect *maskBounds,
		bool mono);

	// Figure out the appropriate interpolation for a transformation. If the
	// image is only translated or rotated by a 90° step, we want to use
	// nearest-neighbor interpolation to avoid blurring the image.
	static int getEffectiveInterpolation(
		const QRect &srcRect, const QPolygon &dstQuad,
		int requestedInterpolation);

protected:
	virtual void initSelection(canvas::Selection *selection) = 0;
	virtual void newSelectionMotion(
		const canvas::Point &point, bool constrain, bool center) = 0;

	QPointF m_start, m_p1, m_end;
	canvas::Selection::Handle m_handle;

private:
	static bool isRightAngleRotationOrReflection(const QTransform &t1);

	bool m_allowTransform;
	net::MessageList m_messages;
};

class RectangleSelection final : public SelectionTool {
public:
	RectangleSelection(ToolController &owner);

protected:
	void initSelection(canvas::Selection *selection) override;
	void newSelectionMotion(
		const canvas::Point &point, bool constrain, bool center) override;
};

class PolygonSelection final : public SelectionTool {
public:
	PolygonSelection(ToolController &owner);

protected:
	void initSelection(canvas::Selection *selection) override;
	void newSelectionMotion(
		const canvas::Point &point, bool constrain, bool center) override;
};

}

#endif
