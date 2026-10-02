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

// How bright the mirrored picture is beside the artwork, and from FALLOFF frame pixels
// away from it, in 256ths.
static const int NEAR_SHADE = 128;
static const int FAR_SHADE = 64;
static const int FALLOFF = 36;

// How many frame pixels to each side the mirrored picture is averaged over.
static const int SOFTEN = 2;

// A 16 bit pixel with its green moved up, so the three colors can be weighed in one sum.
static const unsigned int SPREAD = 0x07E0F81F;


// Where one row or column of the shown frame reads the bars: two neighbors that both lie on
// its side of the artwork, and the share of the second in 32nds.
struct BarTap
{
	int First;
	int Second;
	unsigned int Share;
	bool IsBar;
};


static int Mirrored(int position, int start, int end)
{
	if (position < start) {
		position = start * 2 - 1 - position;
	} else if (position >= end) {
		position = end * 2 - 1 - position;
	}
	return(std::clamp(position, start, end - 1));
}


static void Find_Taps(std::vector<BarTap> & taps, int count, int length, int start, int end)
{
	taps.resize(length);

	for (int pixel = 0; pixel < length; pixel++) {
		BarTap & tap = taps[pixel];
		int shown = (int)((long long)pixel * count / length);
		int low = 0;
		int high = count - 1;

		tap.IsBar = shown < start || shown >= end;
		if (shown < start) {
			high = start - 1;
		} else if (shown >= end) {
			low = end;
		}

		// The middle of the shown pixel, in 32nds of a frame pixel past the middle of the
		// frame's first.
		long long place = ((long long)pixel * 2 + 1) * count * 16 / length - 16;
		int first = (int)((place + 32) >> 5) - 1;
		tap.Share = (unsigned int)(place - (long long)first * 32);
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


/// <returns>bool; Does a menu frame of this size leave room around the artwork?</returns>
bool Menu_Bars_Wanted(int columns, int rows)
{
	return(columns > ART_WIDTH || rows > ART_HEIGHT);
}


/// <summary>
/// Fills the room a menu frame leaves around its artwork with a darkened, softened mirror
/// image of the artwork's edge.
/// </summary>
/// <param name="frame">The menu frame's 16 bit pixels, which are only read.</param>
/// <param name="stride">The length of a frame row in bytes.</param>
/// <param name="pixels">The frame as it is shown, enlarged or not, a row after the other. Only
/// the pixels around the artwork are written.</param>
void Menu_Bars_Draw(unsigned short const * frame, int columns, int rows, int stride, unsigned short * pixels, int width, int height)
{
	if (frame == nullptr || pixels == nullptr || !Menu_Bars_Wanted(columns, rows) || width < columns || height < rows) {
		return;
	}

	int left = std::max((columns - ART_WIDTH) / 2, 0);
	int right = std::min(left + ART_WIDTH, columns);
	int top = std::max((rows - ART_HEIGHT) / 2, 0);
	int bottom = std::min(top + ART_HEIGHT, rows);

	auto source = [&](int x, int y) -> unsigned int {
		unsigned short const * row = (unsigned short const *)((unsigned char const *)frame + (std::ptrdiff_t)Mirrored(y, top, bottom) * stride);
		return(row[Mirrored(x, left, right)]);
	};

	static std::vector<unsigned int> bars;
	bars.resize((std::size_t)columns * rows);

	for (int y = 0; y < rows; y++) {
		bool between = y >= top && y < bottom;
		for (int x = 0; x < columns; x++) {
			if (between && x == left) {
				x = right - 1;
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

			int away = std::max({left - x, x - right + 1, top - y, y - bottom + 1}) - 1;
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
	Find_Taps(across, columns, width, left, right);
	Find_Taps(down, rows, height, top, bottom);

	int artleft = 0;
	while (artleft < width && across[artleft].IsBar) {
		artleft++;
	}
	int artright = artleft;
	while (artright < width && !across[artright].IsBar) {
		artright++;
	}

	for (int y = 0; y < height; y++) {
		BarTap const & row = down[y];
		unsigned int const * upper = bars.data() + (std::size_t)row.First * columns;
		unsigned int const * lower = bars.data() + (std::size_t)row.Second * columns;
		unsigned short * out = pixels + (std::size_t)y * width;

		if (row.IsBar) {
			Fill_Row(out, 0, width, row, across, upper, lower);
		} else {
			Fill_Row(out, 0, artleft, row, across, upper, lower);
			Fill_Row(out, artright, width, row, across, upper, lower);
		}
	}
}
