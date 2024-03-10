// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpmsg/acl.h>
#include <dpmsg/message.h>
}
#include "libclient/canvas/acl.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/documentmetadata.h"
#include "libclient/canvas/layerlist.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/canvas/selection.h"
#include "libclient/canvas/timelinemodel.h"
#include "libclient/canvas/userlist.h"
#include "libclient/drawdance/viewmode.h"
#include "libclient/settings.h"
#include "libclient/utils/identicon.h"
#include "libshared/util/qtcompat.h"
#include <QDebug>
#include <QPainter>

namespace canvas {

CanvasModel::CanvasModel(
	libclient::settings::Settings &settings, uint8_t localUserId, int fps,
	int snapshotMaxCount, long long snapshotMinDelayMs,
	bool wantCanvasHistoryDump, QObject *parent)
	: QObject(parent)
	, m_selection(nullptr)
	, m_localUserId(1)
	, m_compatibilityMode(false)
{
	m_paintengine = new PaintEngine(
		fps, snapshotMaxCount, snapshotMinDelayMs, wantCanvasHistoryDump, this);

	m_aclstate = new AclState(this);
	m_layerlist = new LayerListModel(this);
	m_userlist = new UserListModel(this);
	m_timeline = new TimelineModel(this);
	m_metadata = new DocumentMetadata(m_paintengine, this);

	connect(
		m_aclstate, &AclState::userBitsChanged, m_userlist,
		&UserListModel::updateAclState);
	connect(
		m_paintengine, &PaintEngine::aclsChanged, m_aclstate,
		&AclState::aclsChanged);
	connect(
		m_paintengine, &PaintEngine::resetLockSet, m_aclstate,
		&AclState::resetLockSet);
	connect(
		m_paintengine, &PaintEngine::laserTrail, this,
		&CanvasModel::onLaserTrail);
	connect(
		m_paintengine, &PaintEngine::defaultLayer, m_layerlist,
		&LayerListModel::setDefaultLayer);
	connect(
		m_paintengine, &PaintEngine::recorderStateChanged, this,
		&CanvasModel::recorderStateChanged);

	m_aclstate->setLocalUserId(localUserId);

	m_layerlist->setAclState(m_aclstate);
	m_layerlist->setLayerGetter([this](int id) -> QImage {
		return m_paintengine->getLayerImage(id);
	});
	m_timeline->setAclState(m_aclstate);

	connect(
		m_layerlist, &LayerListModel::autoSelectRequest, this,
		&CanvasModel::layerAutoselectRequest);
	connect(
		m_paintengine, &PaintEngine::resized, this,
		&CanvasModel::onCanvasResize, Qt::QueuedConnection);
	connect(
		m_paintengine, &PaintEngine::layersChanged, m_layerlist,
		&LayerListModel::setLayers);
	connect(
		m_paintengine, &PaintEngine::timelineChanged, m_timeline,
		&TimelineModel::setTimeline);
	connect(
		m_paintengine, &PaintEngine::frameVisibilityChanged, m_layerlist,
		&LayerListModel::setLayersVisibleInFrame);

	settings.bindEngineFrameRate(m_paintengine, &PaintEngine::setFps);
	settings.bindEngineSnapshotCount(
		m_paintengine, &PaintEngine::setSnapshotMaxCount);
	settings.bindEngineSnapshotInterval(this, [this](int minDelaySec) {
		m_paintengine->setSnapshotMinDelayMs(minDelaySec * 1000LL);
	});
}

void CanvasModel::loadBlank(
	int undoDepthLimit, const QSize &size, const QColor &background)
{
	m_paintengine->enqueueLoadBlank(undoDepthLimit, size, background);
}

void CanvasModel::loadCanvasState(
	int undoDepthLimit, const drawdance::CanvasState &canvasState)
{
	m_paintengine->reset(m_localUserId, canvasState);
	net::Message undoDepthMessage =
		net::makeUndoDepthMessage(0, undoDepthLimit);
	m_paintengine->receiveMessages(false, 1, &undoDepthMessage);
}

void CanvasModel::loadPlayer(DP_Player *player)
{
	return m_paintengine->reset(
		m_localUserId, drawdance::CanvasState::null(), player);
}

QSize CanvasModel::size() const
{
	return m_paintengine->viewCanvasState().size();
}

void CanvasModel::previewAnnotation(int id, const QRect &shape)
{
	emit previewAnnotationRequested(id, shape);
}

void CanvasModel::connectedToServer(
	uint8_t myUserId, bool join, bool compatibilityMode)
{
	if(myUserId == 0) {
		// Zero is a reserved "null" user ID
		qWarning("connectedToServer: local user ID is zero!");
	}

	m_localUserId = myUserId;
	m_compatibilityMode = compatibilityMode;
	m_layerlist->setAutoselectAny(true);

	m_aclstate->setLocalUserId(myUserId);

	if(join) {
		m_paintengine->resetAcl(m_localUserId);
	}

	m_userlist->reset();
	emit compatibilityModeChanged(m_compatibilityMode);
}

void CanvasModel::disconnectedFromServer()
{
	m_compatibilityMode = false;
	m_paintengine->cleanup();
	m_userlist->allLogout();
	m_paintengine->resetAcl(m_localUserId);
	m_paintengine->cleanup();
	emit compatibilityModeChanged(m_compatibilityMode);
}

void CanvasModel::handleCommands(int count, const net::Message *msgs)
{
	handleMetaMessages(count, msgs);
	if(m_paintengine->receiveMessages(false, count, msgs) != 0) {
		emit canvasModified();
	}
}

void CanvasModel::handleLocalCommands(int count, const net::Message *msgs)
{
	if(m_paintengine->receiveMessages(true, count, msgs) != 0) {
		m_layerlist->setAutoselectAny(false);
	}
}

void CanvasModel::handleMetaMessages(int count, const net::Message *msgs)
{
	for(int i = 0; i < count; ++i) {
		const net::Message &msg = msgs[i];
		switch(msg.type()) {
		case DP_MSG_JOIN:
			handleJoin(msg);
			break;
		case DP_MSG_LEAVE:
			handleLeave(msg);
			break;
		case DP_MSG_CHAT:
			handleChat(msg);
			break;
		case DP_MSG_PRIVATE_CHAT:
			handlePrivateChat(msg);
			break;
		default:
			break;
		}
	}
}

void CanvasModel::handleJoin(const net::Message &msg)
{
	DP_MsgJoin *mj = DP_msg_join_cast(msg.get());
	uint8_t user_id = msg.contextId();

	size_t nameLength;
	const char *nameBytes = DP_msg_join_name(mj, &nameLength);
	QString name = QString::fromUtf8(nameBytes, compat::castSize(nameLength));

	size_t avatarSize;
	const unsigned char *avatarBytes = DP_msg_join_avatar(mj, &avatarSize);
	QImage avatar;
	if(avatarSize != 0) {
		QByteArray avatarData = QByteArray::fromRawData(
			reinterpret_cast<const char *>(avatarBytes),
			compat::castSize(avatarSize));
		if(!avatar.loadFromData(avatarData)) {
			qWarning(
				"Avatar loading failed for user '%s' (#%d)", qPrintable(name),
				user_id);
		}

		// Rescale avatar if its the wrong size
		if(avatar.width() > 32 || avatar.height() > 32) {
			avatar = avatar.scaled(
				32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}
	}
	if(avatar.isNull()) {
		avatar = make_identicon(name);
	}

	uint8_t flags = DP_msg_join_flags(mj);
	const User u{
		user_id,
		name,
		QPixmap::fromImage(avatar),
		user_id == localUserId(),
		false,
		false,
		bool(flags & DP_MSG_JOIN_FLAGS_MOD),
		bool(flags & DP_MSG_JOIN_FLAGS_BOT),
		bool(flags & DP_MSG_JOIN_FLAGS_AUTH),
		false,
		false,
		true};

	m_userlist->userLogin(u);
	emit userJoined(user_id, name);
}

void CanvasModel::handleLeave(const net::Message &msg)
{
	uint8_t user_id = msg.contextId();
	QString name = m_userlist->getUsername(user_id);
	m_userlist->userLogout(user_id);
	emit userLeft(user_id, name);
}

void CanvasModel::handleChat(const net::Message &msg)
{
	DP_MsgChat *mc = DP_msg_chat_cast(msg.get());

	size_t messageLength;
	const char *messageBytes = DP_msg_chat_message(mc, &messageLength);
	QString message =
		QString::fromUtf8(messageBytes, compat::castSize(messageLength));

	uint8_t oflags = DP_msg_chat_oflags(mc);
	if(oflags & DP_MSG_CHAT_OFLAGS_PIN) {
		// Special value to remove a pinned message
		if(message == "-") {
			message = QString();
		}

		if(m_pinnedMessage != message) {
			m_pinnedMessage = message;
			emit pinnedMessageChanged(message);
		}
	} else {
		uint8_t tflags = DP_msg_chat_tflags(mc);
		emit chatMessageReceived(msg.contextId(), 0, tflags, oflags, message);
	}
}

void CanvasModel::handlePrivateChat(const net::Message &msg)
{
	DP_MsgPrivateChat *mpc = DP_msg_private_chat_cast(msg.get());

	size_t messageLength;
	const char *messageBytes = DP_msg_private_chat_message(mpc, &messageLength);
	QString message =
		QString::fromUtf8(messageBytes, compat::castSize(messageLength));

	uint8_t target = DP_msg_private_chat_target(mpc);
	uint8_t oflags = DP_msg_private_chat_oflags(mpc);
	emit chatMessageReceived(msg.contextId(), target, 0, oflags, message);
}

void CanvasModel::onLaserTrail(uint8_t userId, int persistence, uint32_t color)
{
	emit laserTrail(
		userId, qMin(15, persistence) * 1000, QColor::fromRgb(color));
}

net::MessageList CanvasModel::generateSnapshot(
	bool includePinnedMessage, unsigned int aclIncludeFlags) const
{
	net::MessageList snapshot;
	m_paintengine->historyCanvasState().toResetImage(snapshot, 0);
	amendSnapshotMetadata(snapshot, includePinnedMessage, aclIncludeFlags);
	return snapshot;
}

void CanvasModel::amendSnapshotMetadata(
	net::MessageList &snapshot, bool includePinnedMessage,
	unsigned int aclIncludeFlags) const
{
	snapshot.prepend(
		net::makeUndoDepthMessage(0, m_paintengine->undoDepthLimit()));

	if(includePinnedMessage && !m_pinnedMessage.isEmpty()) {
		snapshot.prepend(net::makeChatMessage(
			m_localUserId, 0, DP_MSG_CHAT_OFLAGS_PIN, m_pinnedMessage));
	}

	int defaultLayerId = m_layerlist->defaultLayer();
	if(defaultLayerId > 0) {
		snapshot.append(net::makeDefaultLayerMessage(0, defaultLayerId));
	}

	m_paintengine->aclState().toResetImage(
		snapshot, m_localUserId, aclIncludeFlags);
}

void CanvasModel::pickLayer(int x, int y)
{
	int layerId = m_paintengine->pickLayer(x, y);
	if(layerId > 0) {
		emit layerAutoselectRequest(layerId);
	}
}

void CanvasModel::pickColor(int x, int y, int layer, int diameter)
{
	QColor color = m_paintengine->sampleColor(x, y, layer, diameter);
	if(color.alpha() > 0) {
		color.setAlpha(255);
		emit colorPicked(color);
	}
}

void CanvasModel::inspectCanvas(int x, int y, bool clobber, bool showTiles)
{
	unsigned int contextId = m_paintengine->pickContextId(x, y);
	bool haveContextId = contextId != 0;
	if(haveContextId) {
		inspectCanvas(contextId, showTiles);
	}
	if(haveContextId || clobber) {
		emit canvasInspected(contextId);
	}
}

void CanvasModel::inspectCanvas(int contextId, bool showTiles)
{
	Q_ASSERT(contextId > 0 && contextId < 256);
	m_paintengine->setInspect(contextId, showTiles);
}

void CanvasModel::stopInspectingCanvas()
{
	m_paintengine->setInspect(0, false);
}

void CanvasModel::setSelectInterpolation(int selectInterpolation)
{
	m_selectInterpolation = selectInterpolation;
}

void CanvasModel::setSelection(Selection *selection)
{
	if(m_selection != selection) {
		m_paintengine->clearCutPreview();

		const bool hadSelection = m_selection != nullptr;

		if(hadSelection && m_selection->parent() == this)
			m_selection->deleteLater();

		if(selection && !selection->parent())
			selection->setParent(this);

		m_selection = selection;

		emit selectionChanged(selection);
		if(hadSelection && !selection)
			emit selectionRemoved();
	}
}

QImage CanvasModel::selectionToImage(int layerId, bool *outFound) const
{
	if(m_selection && !m_selection->pasteImage().isNull() && layerId > 0) {
		if(outFound) {
			*outFound = true;
		}
		return m_selection->transformedPasteImage(m_selectInterpolation);
	}

	drawdance::CanvasState canvasState = m_paintengine->viewCanvasState();
	QRect rect{QPoint(), canvasState.size()};
	if(m_selection) {
		rect = rect.intersected(m_selection->boundingRect());
	}
	if(rect.isEmpty()) {
		qWarning("selectionToImage: empty selection");
		return QImage{};
	}

	QImage img;
	if(layerId == 0) {
		drawdance::ViewModeBuffer vmb;
		img = m_paintengine->getFlatImage(vmb, canvasState, true, true, &rect);
	} else if(layerId == -1) {
		drawdance::ViewModeBuffer vmb;
		img = m_paintengine->getFlatImage(vmb, canvasState, false, true, &rect);
	} else {
		drawdance::LayerContent layerContent =
			canvasState.searchLayerContent(layerId, false);
		if(layerContent.isNull()) {
			qWarning("selectionToImage: layer %d not found", layerId);
			img = QImage(rect.size(), QImage::Format_ARGB32_Premultiplied);
			img.fill(0);
			if(outFound) {
				*outFound = false;
			}
			return img;
		}
		img = layerContent.toImage(rect);
	}

	if(m_selection && !m_selection->isAxisAlignedRectangle()) {
		// Mask out pixels outside the selection
		QPainter mp(&img);
		mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);

		QRect maskBounds;
		const QImage mask = m_selection->shapeMask(Qt::white, &maskBounds);

		mp.drawImage(
			qMin(0, maskBounds.left()), qMin(0, maskBounds.top()), mask);
	}

	if(outFound) {
		*outFound = true;
	}
	return img;
}

void CanvasModel::pasteFromImage(
	const QImage &image, const QPoint &defaultPoint, bool forceDefault)
{
	QPoint center;
	if(forceDefault) {
		// Explicitly pasting in view center.
		center = defaultPoint;
	} else if(m_selection) {
		// There's already a selection present, paste it there.
		QRect bounds = m_selection->boundingRect();
		center = QPoint(
			bounds.x() + bounds.width() / 2, bounds.y() + bounds.height() / 2);
	} else {
		// No explicit paste position. Paste in the center of the canvas if the
		// image is equal or larger than the canvas, else use the default point.
		QSize canvasSize = size();
		int w = canvasSize.width();
		int h = canvasSize.height();
		center = QPoint(
			w <= 0 || image.width() < w ? defaultPoint.x() : w / 2,
			h <= 0 || image.height() < h ? defaultPoint.y() : h / 2);
	}

	Selection *paste = new Selection;
	paste->setShapeRect(QRect(
		center.x() - image.width() / 2, center.y() - image.height() / 2,
		image.width(), image.height()));
	paste->setPasteImage(image);

	setSelection(paste);
}

void CanvasModel::onCanvasResize(const QSize &newSize, const QPoint &offset)
{
	Q_UNUSED(newSize);

	// Adjust selection when new space was added to the left or top side
	// so it remains visually in the same place
	if(m_selection && !offset.isNull()) {
		m_selection->translate(offset);
	}
}

void CanvasModel::resetCanvas()
{
	setTitle(QString());
	m_paintengine->enqueueReset();
}

drawdance::RecordStartResult CanvasModel::startRecording(const QString &path)
{
	return m_paintengine->startRecording(path);
}

bool CanvasModel::stopRecording()
{
	return m_paintengine->stopRecording();
}

bool CanvasModel::isRecording() const
{
	return m_paintengine->isRecording();
}

}
