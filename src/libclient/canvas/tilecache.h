// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LIBCLIENT_CANVAS_TILECACHE_H
#define LIBCLIENT_CANVAS_TILECACHE_H
extern "C" {
#include <dpengine/pixels.h>
}
#include <QImage>
#include <QRect>

namespace canvas {

class TileCache final {
public:
	TileCache() {}
	~TileCache();

	TileCache(const TileCache &) = delete;
	TileCache(TileCache &&) = delete;
	TileCache &operator=(const TileCache &) = delete;
	TileCache &operator=(TileCache &&) = delete;

	int width() const { return m_width; }
	int height() const { return m_height; }
	int xtiles() const { return m_xtiles; }
	int ytiles() const { return m_ytiles; }

	void clear();
	void resize(int width, int height);
	void render(int tileX, int tileY, const DP_Pixel8 *src);

	const DP_Pixel8 *pixelsAt(int tileX, int tileY) const
	{
		return mutablePixelsAt(tileX, tileY);
	}

	QRect rectAt(int tileX, int tileY) const;

	QImage toImage() const;

private:
	DP_Pixel8 *mutablePixelsAt(int tileX, int tileY) const;

	DP_Pixel8 *m_pixels = nullptr;
	size_t m_capacity = 0;
	int m_width = 0;
	int m_height = 0;
	int m_lastWidth = 0;
	int m_lastHeight = 0;
	int m_lastTileX = -1;
	int m_lastTileY = -1;
	int m_xtiles = 0;
	int m_ytiles = 0;
};

}

#endif
