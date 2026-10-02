/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "menubars.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>


// The menu artwork is laid out for 640 by 400 and stands in the middle of the menu frame.
static const int ART_WIDTH = 640;
static const int ART_HEIGHT = 400;

// How bright the mirrored picture is beside the picture, and from FALLOFF of the picture's
// pixels away from it, in 256ths.
static const int NEAR_SHADE = 128;
static const int FAR_SHADE = 64;
static const int FALLOFF = 36;

// How many of the picture's pixels to each side the mirrored picture is averaged over.
static const int SOFTEN = 2;

// A 16 bit pixel with its green moved up, so the three colors can be weighed in one sum.
static const unsigned int SPREAD = 0x07E0F81F;


// Where one row or column of the destination reads the bars: two neighbors that both lie on
// its side of the picture, and the share of the second in 32nds.
struct BarTap
{
	int First;
	int Second;
	unsigned int Share;
	bool IsBar;
};


static int Mirrored(int position, int length)
{
	if (position < 0) {
		position = -1 - position;
	} else if (position >= length) {
		position = length * 2 - 1 - position;
	}
	return(std::clamp(position, 0, length - 1));
}


// How many of the picture's pixels fit into the room beside it, and one more for the
// neighbor the outermost is mixed with.
static int Room(int room, int length, int shown)
{
	if (room <= 0) {
		return(0);
	}
	return((int)(((long long)room * length + shown - 1) / shown) + 1);
}


static void Find_Taps(std::vector<BarTap> & taps, int length, int start, int shown, int art, int before, int count)
{
	taps.resize(length);

	for (int pixel = 0; pixel < length; pixel++) {
		BarTap & tap = taps[pixel];
		int low = 0;
		int high = count - 1;

		tap.IsBar = pixel < start || pixel >= start + shown;
		if (pixel < start) {
			high = before - 1;
		} else if (pixel >= start + shown) {
			low = before + art;
		}

		// The middle of the pixel, in 32nds of a picture pixel past the middle of the bars'
		// first. The room counted before the picture keeps it from going below zero.
		long long place = ((long long)(pixel - start) * 2 + 1) * art * 16;
		place = (place >= 0) ? place / shown : -((-place + shown - 1) / shown);
		place += (long long)before * 32 - 16;

		int first = (int)(place >> 5);
		tap.Share = (unsigned int)(place & 31);
		tap.First = std::clamp(first, low, high);
		tap.Second = std::clamp(first + 1, low, high);
	}
}


static void Fill_Row(unsigned short * out, int from, int to, BarTap const & row, std::vector<BarTap> const & across, unsigned int const * upper, unsigned int const * lower)
{
	for (int x = from; x < to; x++) {
		BarTap const & column = across[x];
		unsigned int above = ((upper[column.First] * (32 - column.Share) + upper[column.Second] * column.Share) >> 5) & SPREAD;
		unsigned int below = ((lower[column.First] * (32 - column.Share) + lower[column.Second] * column.Share) >> 5) & SPREAD;
		unsigned int mixed = ((above * (32 - row.Share) + below * row.Share) >> 5) & SPREAD;
		out[x] = (unsigned short)(mixed | (mixed >> 16));
	}
}


/// <returns>Where the artwork stands in a menu frame of this size.</returns>
Rect Menu_Bars_Art(int columns, int rows)
{
	int width = std::min(ART_WIDTH, columns);
	int height = std::min(ART_HEIGHT, rows);
	return(Rect((columns - width) / 2, (rows - height) / 2, width, height));
}


/// <summary>
/// Fills the room around a picture with a darkened, softened mirror image of the picture's
/// edge.
/// </summary>
/// <param name="art">The picture's 16 bit pixels at its own size, which are only read.</param>
/// <param name="artstride">The length of a row of the picture in bytes.</param>
/// <param name="pixels">The 16 bit pixels the picture is shown in, enlarged or not. Only the
/// pixels around the picture are written.</param>
/// <param name="stride">The length of a row of those pixels in bytes.</param>
/// <param name="shown">Where the picture stands in those pixels. Nothing is drawn unless it
/// lies inside them and leaves room.</param>
void Menu_Bars_Draw(unsigned short const * art, int artwidth, int artheight, int artstride, unsigned short * pixels, int width, int height, int stride, Rect const & shown)
{
	Rect whole(0, 0, width, height);
	if (art == nullptr || pixels == nullptr || artwidth <= 0 || artheight <= 0 || !shown.Is_Valid() || Intersect(shown, whole) != shown || shown == whole) {
		return;
	}

	int left = Room(shown.X, artwidth, shown.Width);
	int right = Room(width - shown.X - shown.Width, artwidth, shown.Width);
	int top = Room(shown.Y, artheight, shown.Height);
	int bottom = Room(height - shown.Y - shown.Height, artheight, shown.Height);
	int columns = left + artwidth + right;
	int rows = top + artheight + bottom;

	auto source = [&](int x, int y) -> unsigned int {
		unsigned short const * row = (unsigned short const *)((unsigned char const *)art + (std::ptrdiff_t)Mirrored(y - top, artheight) * artstride);
		return(row[Mirrored(x - left, artwidth)]);
	};

	static std::vector<unsigned int> bars;
	bars.resize((std::size_t)columns * rows);

	for (int y = 0; y < rows; y++) {
		bool between = y >= top && y < top + artheight;
		for (int x = 0; x < columns; x++) {
			if (between && x == left) {
				x = left + artwidth - 1;
				continue;
			}

			unsigned int red = 0;
			unsigned int green = 0;
			unsigned int blue = 0;
			unsigned int total = 0;
			for (int down = -SOFTEN; down <= SOFTEN; down++) {
				for (int across = -SOFTEN; across <= SOFTEN; across++) {
					unsigned int weight = (unsigned int)((SOFTEN + 1 - std::abs(down)) * (SOFTEN + 1 - std::abs(across)));
					unsigned int pixel = source(x + across, y + down);
					red += (pixel >> 11) * weight;
					green += ((pixel >> 5) & 63) * weight;
					blue += (pixel & 31) * weight;
					total += weight;
				}
			}

			int away = std::max({left - x, x - left - artwidth + 1, top - y, y - top - artheight + 1}) - 1;
			unsigned int shade = (unsigned int)(NEAR_SHADE + (FAR_SHADE - NEAR_SHADE) * std::min(away, FALLOFF) / FALLOFF);
			total *= 256;
			red = red * shade / total;
			green = green * shade / total;
			blue = blue * shade / total;
			bars[(std::size_t)y * columns + x] = (green << 21) | (red << 11) | blue;
		}
	}

	static std::vector<BarTap> across;
	static std::vector<BarTap> down;
	Find_Taps(across, width, shown.X, shown.Width, artwidth, left, columns);
	Find_Taps(down, height, shown.Y, shown.Height, artheight, top, rows);

	for (int y = 0; y < height; y++) {
		BarTap const & row = down[y];
		unsigned int const * upper = bars.data() + (std::size_t)row.First * columns;
		unsigned int const * lower = bars.data() + (std::size_t)row.Second * columns;
		unsigned short * out = (unsigned short *)((unsigned char *)pixels + (std::ptrdiff_t)y * stride);

		if (row.IsBar) {
			Fill_Row(out, 0, width, row, across, upper, lower);
		} else {
			Fill_Row(out, 0, shown.X, row, across, upper, lower);
			Fill_Row(out, shown.X + shown.Width, width, row, across, upper, lower);
		}
	}
}
