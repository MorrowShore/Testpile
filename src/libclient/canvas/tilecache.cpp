// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" {
#include <dpengine/tile.h>
}
#include "libclient/canvas/tilecache.h"
#include <QPainter>
#include <QtGlobal>

namespace canvas {

TileCache::~TileCache()
{
	DP_free(m_pixels);
}

void TileCache::clear()
{
	m_width = 0;
	m_height = 0;
	m_lastWidth = 0;
	m_lastHeight = 0;
	m_lastTileX = -1;
	m_lastTileY = -1;
	m_xtiles = 0;
	m_ytiles = 0;
}

void TileCache::resize(int width, int height)
{
	Q_ASSERT(width >= 0);
	Q_ASSERT(height >= 0);
	DP_TileCounts tc = DP_tile_counts_round(width, height);
	size_t requiredCapacity =
		size_t(tc.x) * size_t(tc.y) * DP_TILE_LENGTH * sizeof(*m_pixels);
	if(m_capacity < requiredCapacity) {
		m_pixels =
			static_cast<DP_Pixel8 *>(DP_realloc(m_pixels, requiredCapacity));
		m_capacity = requiredCapacity;
	}

	m_width = width;
	m_height = height;
	m_lastWidth = width % DP_TILE_SIZE;
	m_lastHeight = height % DP_TILE_SIZE;
	m_lastTileX = m_lastWidth == 0 ? tc.x : tc.x - 1;
	m_lastTileY = m_lastHeight == 0 ? tc.y : tc.y - 1;
	m_xtiles = tc.x;
	m_ytiles = tc.y;
}

void TileCache::render(int tileX, int tileY, const DP_Pixel8 *src)
{
	bool plainX = tileX < m_lastTileX;
	bool plainY = tileY < m_lastTileY;
	DP_Pixel8 *dst = mutablePixelsAt(tileX, tileY);
	if(plainX && plainY) {
		memcpy(dst, src, DP_TILE_LENGTH * sizeof(*dst));
	} else {
		int w = plainX ? DP_TILE_SIZE : m_lastWidth;
		int h = plainY ? DP_TILE_SIZE : m_lastHeight;
		size_t row_size = size_t(w) * sizeof(*dst);
		for(int y = 0; y < h; ++y) {
			memcpy(dst + y * w, src + y * DP_TILE_SIZE, row_size);
		}
	}
}

QRect TileCache::rectAt(int tileX, int tileY) const
{
	return QRect(
		tileX * DP_TILE_SIZE, tileY * DP_TILE_SIZE,
		tileX < m_lastTileX ? DP_TILE_SIZE : m_lastWidth,
		tileY < m_lastTileY ? DP_TILE_SIZE : m_lastHeight);
}

QImage TileCache::toImage() const
{
	QImage img(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
	QPainter painter;
	painter.begin(&img);
	for(int tileY = 0; tileY < m_ytiles; ++tileY) {
		int tileH = tileY < m_lastTileY ? DP_TILE_SIZE : m_lastHeight;
		for(int tileX = 0; tileX < m_xtiles; ++tileX) {
			int tileW = tileX < m_lastTileX ? DP_TILE_SIZE : m_lastWidth;
			painter.drawImage(
				QRect(tileX * DP_TILE_SIZE, tileY * DP_TILE_SIZE, tileW, tileH),
				QImage(
					reinterpret_cast<const uchar *>(pixelsAt(tileX, tileY)),
					tileW, tileH, QImage::Format_ARGB32_Premultiplied));
		}
	}
	painter.end();
	return img;
}

DP_Pixel8 *TileCache::mutablePixelsAt(int tileX, int tileY) const
{
	return m_pixels + (tileY * m_xtiles + tileX) * DP_TILE_LENGTH;
}

}
