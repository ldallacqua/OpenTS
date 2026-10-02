/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "widepicture.h"

#include "ccfile.h"
#include "crc.h"
#include "dbgprint.h"
#include "gamedirs.h"
#include "rawfile.h"
#include "surface.h"
#include "ui/rml/rmltexture.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>


// A wide picture stands in for a full-screen picture laid out for 640 by 400.
static const int ART_WIDTH = 640;
static const int ART_HEIGHT = 400;

// The folder of the data directory the wide pictures are read from.
static char const FOLDER[] = "HD";

// How many pictures drawn lately are remembered, so one drawn on a hidden surface does not
// displace the one on screen.
static const std::size_t KEPT = 4;

// The share of the picture's pixels, in hundredths, a frame must still show for the wide
// picture to be drawn over it.
static const int LEAST_MATCH = 30;

// A wide picture this many pixels short of the frame's edge has its edge repeated up to it.
static const int EDGE_GAP = 8;


struct Backdrop
{
	// The wide picture's file name.
	std::string Name;

	// The picture as it was drawn, ART_WIDTH by ART_HEIGHT.
	std::vector<unsigned short> Reference;

	// The wide picture at the height it is shown at, and that size.
	std::vector<unsigned short> Shown;
	int ShownWidth = 0;
	int ShownHeight = 0;

	bool IsUnreadable = false;
};

static std::vector<Backdrop> _Backdrops;


static std::string Path_Of(char const * name)
{
	return(Data_Directory() + FOLDER + "\\" + name);
}


/// <summary>
/// Names the wide picture that stands in for a picture file. The name carries the checksum of
/// the picture file's contents, so a different picture under the same name has none.
/// </summary>
/// <param name="picture">The picture file's name, as the game opens it.</param>
/// <returns>The wide picture's file name, or nothing if the picture or its wide picture is
/// not there.</returns>
std::string Wide_Picture_Name(char const * picture)
{
	if (picture == nullptr) {
		return(std::string());
	}

	CCFileClass file(picture);
	if (!file.Is_Available()) {
		return(std::string());
	}

	int size = file.Size();
	std::vector<unsigned char> bytes((std::size_t)std::max(size, 0));
	if (size <= 0 || !file.Open(FileClass::READ) || file.Read(bytes.data(), size) != size) {
		return(std::string());
	}
	file.Close();

	std::string stem(picture);
	std::string::size_type dot = stem.find_last_of('.');
	if (dot != std::string::npos) {
		stem.erase(dot);
	}
	std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char letter) { return((char)std::tolower(letter)); });

	char checksum[16];
	std::snprintf(checksum, sizeof(checksum), ".%08x", CRC::Memory(bytes.data(), (unsigned int)size));
	std::string name = stem + checksum + ".png";

	RawFileClass wide(Path_Of(name.c_str()).c_str());
	return(wide.Is_Available() ? name : std::string());
}


/// <returns>bool; Was the wide picture's size read from its file?</returns>
bool Wide_Picture_Size(char const * name, int & width, int & height)
{
	unsigned char header[24];
	RawFileClass file(Path_Of(name).c_str());
	if (!file.Is_Available() || !file.Open(FileClass::READ)) {
		return(false);
	}
	bool read = file.Read(header, sizeof(header)) == (int)sizeof(header);
	file.Close();

	static unsigned char const SIGNATURE[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	if (!read || std::memcmp(header, SIGNATURE, sizeof(SIGNATURE)) != 0) {
		return(false);
	}

	width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
	height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
	return(width > 0 && height > 0);
}


/// <param name="rgba">Receives the wide picture's pixels, four bytes each.</param>
/// <returns>bool; Was the wide picture read?</returns>
bool Wide_Picture_Read(char const * name, std::vector<unsigned char> & rgba, int & width, int & height)
{
	return(name != nullptr && UI_Load_Image_File(Path_Of(name).c_str(), rgba, width, height, false) == UI_IMAGE_LOADED);
}


/// <summary>
/// Remembers a full-screen picture that was just drawn, if it has a wide picture, so that the
/// wide picture can be shown where a frame still shows the picture.
/// </summary>
/// <param name="picture">The picture file's name.</param>
/// <param name="area">Where on the surface the picture was drawn. A picture of another size
/// than 640 by 400 is not remembered.</param>
void Wide_Picture_Note(char const * picture, Surface const & surface, Rect const & area)
{
	if (surface.Bytes_Per_Pixel() != 2 || area.Width != ART_WIDTH || area.Height != ART_HEIGHT || Intersect(area, surface.Get_Rect()) != area) {
		return;
	}

	std::string name = Wide_Picture_Name(picture);
	if (name.empty()) {
		return;
	}

	Backdrop backdrop;
	auto known = std::find_if(_Backdrops.begin(), _Backdrops.end(), [&](Backdrop const & other) { return(other.Name == name); });
	if (known != _Backdrops.end()) {
		backdrop = std::move(*known);
		_Backdrops.erase(known);
	}
	backdrop.Name = name;
	backdrop.Reference.resize((std::size_t)ART_WIDTH * ART_HEIGHT);

	unsigned char const * from = (unsigned char const *)surface.Lock();
	if (from == nullptr) {
		return;
	}
	for (int y = 0; y < ART_HEIGHT; y++) {
		std::memcpy(backdrop.Reference.data() + (std::size_t)y * ART_WIDTH, from + (std::ptrdiff_t)(area.Y + y) * surface.Stride() + (std::ptrdiff_t)area.X * 2, (std::size_t)ART_WIDTH * 2);
	}
	surface.Unlock();

	_Backdrops.insert(_Backdrops.begin(), std::move(backdrop));
	if (_Backdrops.size() > KEPT) {
		_Backdrops.resize(KEPT);
	}
}


// Which source pixels one pixel of a resized row or column is made of, and their weights.
struct Taps
{
	std::vector<int> First;
	std::vector<int> Count;
	std::vector<float> Weights;
};


static void Find_Taps(Taps & taps, int from, int to)
{
	taps.First.resize(to);
	taps.Count.resize(to);
	taps.Weights.clear();

	double step = (double)from / (double)to;
	for (int pixel = 0; pixel < to; pixel++) {
		if (step > 1.0) {
			// Shrinking averages every source pixel the new pixel covers, by how much it covers.
			double start = pixel * step;
			double end = start + step;
			int first = (int)start;
			int last = std::min((int)end, from - 1);
			taps.First[pixel] = first;
			taps.Count[pixel] = last - first + 1;
			for (int source = first; source <= last; source++) {
				double covered = std::min(end, (double)source + 1.0) - std::max(start, (double)source);
				taps.Weights.push_back((float)(std::max(covered, 0.0) / step));
			}
		} else {
			double place = ((double)pixel + 0.5) * step - 0.5;
			int first = std::clamp((int)std::floor(place), 0, from - 1);
			int second = std::min(first + 1, from - 1);
			float share = (float)std::clamp(place - (double)first, 0.0, 1.0);
			taps.First[pixel] = first;
			taps.Count[pixel] = second - first + 1;
			taps.Weights.push_back((second > first) ? 1.0f - share : 1.0f);
			if (second > first) {
				taps.Weights.push_back(share);
			}
		}
	}
}


// The picture is shown in 16 bit color. An ordered pattern of half a step keeps its smooth
// shades from breaking into bands.
static unsigned short To_Pixel(float red, float green, float blue, int x, int y)
{
	static const int PATTERN[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	float nudge = ((float)PATTERN[y & 3][x & 3] + 0.5f) / 16.0f;

	int r = std::clamp((int)(red * (31.0f / 255.0f) + nudge), 0, 31);
	int g = std::clamp((int)(green * (63.0f / 255.0f) + nudge), 0, 63);
	int b = std::clamp((int)(blue * (31.0f / 255.0f) + nudge), 0, 31);
	return((unsigned short)((r << 11) | (g << 5) | b));
}


static bool Prepare(Backdrop & backdrop, int shownheight)
{
	if (backdrop.ShownHeight == shownheight && !backdrop.Shown.empty()) {
		return(true);
	}

	std::vector<unsigned char> rgba;
	int width = 0;
	int height = 0;
	if (!Wide_Picture_Read(backdrop.Name.c_str(), rgba, width, height) || width <= 0 || height <= 0) {
		DebugString("Wide picture %s could not be read\n", backdrop.Name.c_str());
		backdrop.IsUnreadable = true;
		return(false);
	}

	int shownwidth = std::max((int)(((long long)width * shownheight + height / 2) / height), 1);
	backdrop.Shown.resize((std::size_t)shownwidth * shownheight);
	backdrop.ShownWidth = shownwidth;
	backdrop.ShownHeight = shownheight;

	if (shownwidth == width && shownheight == height) {
		for (int y = 0; y < height; y++) {
			unsigned char const * from = rgba.data() + (std::size_t)y * width * 4;
			unsigned short * out = backdrop.Shown.data() + (std::size_t)y * width;
			for (int x = 0; x < width; x++, from += 4) {
				out[x] = To_Pixel(from[0], from[1], from[2], x, y);
			}
		}
		return(true);
	}

	Taps across;
	Taps down;
	Find_Taps(across, width, shownwidth);
	Find_Taps(down, height, shownheight);

	std::vector<float> row((std::size_t)shownwidth * 3);
	std::vector<float> sum((std::size_t)shownwidth * 3);
	std::size_t weight = 0;
	for (int y = 0; y < shownheight; y++) {
		std::fill(sum.begin(), sum.end(), 0.0f);
		for (int tap = 0; tap < down.Count[y]; tap++, weight++) {
			unsigned char const * from = rgba.data() + (std::size_t)(down.First[y] + tap) * width * 4;
			std::size_t place = 0;
			for (int x = 0; x < shownwidth; x++) {
				float red = 0.0f;
				float green = 0.0f;
				float blue = 0.0f;
				unsigned char const * pixel = from + (std::size_t)across.First[x] * 4;
				for (int count = 0; count < across.Count[x]; count++, pixel += 4, place++) {
					red += pixel[0] * across.Weights[place];
					green += pixel[1] * across.Weights[place];
					blue += pixel[2] * across.Weights[place];
				}
				row[(std::size_t)x * 3] = red;
				row[(std::size_t)x * 3 + 1] = green;
				row[(std::size_t)x * 3 + 2] = blue;
			}
			for (std::size_t index = 0; index < sum.size(); index++) {
				sum[index] += row[index] * down.Weights[weight];
			}
		}

		unsigned short * out = backdrop.Shown.data() + (std::size_t)y * shownwidth;
		for (int x = 0; x < shownwidth; x++) {
			out[x] = To_Pixel(sum[(std::size_t)x * 3], sum[(std::size_t)x * 3 + 1], sum[(std::size_t)x * 3 + 2], x, y);
		}
	}
	return(true);
}


static bool Shows(Backdrop const & backdrop, unsigned short const * frame, int framestride, Rect const & art)
{
	static const int STEP = 4;
	int same = 0;
	int total = 0;

	for (int y = 0; y < ART_HEIGHT; y += STEP) {
		unsigned short const * line = (unsigned short const *)((unsigned char const *)frame + (std::ptrdiff_t)(art.Y + y) * framestride) + art.X;
		unsigned short const * reference = backdrop.Reference.data() + (std::size_t)y * ART_WIDTH;
		for (int x = 0; x < ART_WIDTH; x += STEP, total++) {
			same += (line[x] == reference[x]);
		}
	}
	return(same * 100 >= total * LEAST_MATCH);
}


/// <summary>
/// Draws the wide picture of the full-screen picture a frame shows over the enlarged frame,
/// wherever the frame still shows that picture or the empty room around it. Anything drawn
/// over the picture keeps its enlarged pixels.
/// </summary>
/// <param name="frame">The frame's 16 bit pixels at its own size, which are only read.</param>
/// <param name="art">Where the 640 by 400 picture stands in the frame.</param>
/// <param name="pixels">The enlarged frame, width by height 16 bit pixels in unbroken rows.</param>
/// <param name="shown">Where the picture stands in the enlarged frame.</param>
/// <param name="across">For every enlarged column, the frame column it shows.</param>
/// <param name="acrossshare">For every enlarged column, how much of the next frame column it
/// mixes in, zero for none.</param>
/// <param name="down">The same as across, for rows.</param>
/// <param name="downshare">The same as acrossshare, for rows.</param>
/// <returns>bool; Was a wide picture drawn? None is when no remembered picture with a wide
/// picture is on the frame.</returns>
bool Wide_Picture_Draw(unsigned short const * frame, int columns, int rows, int framestride, Rect const & art, unsigned short * pixels, int width, int height, Rect const & shown, int const * across, unsigned char const * acrossshare, int const * down, unsigned char const * downshare)
{
	if (frame == nullptr || pixels == nullptr || art.Width != ART_WIDTH || art.Height != ART_HEIGHT || shown.Height <= 0) {
		return(false);
	}

	Backdrop * backdrop = nullptr;
	for (Backdrop & candidate : _Backdrops) {
		if (!candidate.IsUnreadable && Shows(candidate, frame, framestride, art)) {
			backdrop = &candidate;
			break;
		}
	}
	if (backdrop == nullptr || !Prepare(*backdrop, shown.Height)) {
		return(false);
	}

	// Which frame pixels show the picture, or the black around it, and which rows do throughout.
	static std::vector<unsigned char> same;
	static std::vector<unsigned char> whole;
	same.resize((std::size_t)columns * rows);
	whole.resize(rows);
	for (int y = 0; y < rows; y++) {
		unsigned short const * line = (unsigned short const *)((unsigned char const *)frame + (std::ptrdiff_t)y * framestride);
		unsigned char * out = same.data() + (std::size_t)y * columns;
		bool between = y >= art.Y && y < art.Y + ART_HEIGHT;
		unsigned short const * reference = between ? backdrop->Reference.data() + (std::size_t)(y - art.Y) * ART_WIDTH : nullptr;
		unsigned char all = 1;

		for (int x = 0; x < columns; x++) {
			bool inside = between && x >= art.X && x < art.X + ART_WIDTH;
			out[x] = inside ? (line[x] == reference[x - art.X]) : (line[x] == 0);
			all &= out[x];
		}
		whole[y] = all;
	}

	int left = shown.X + shown.Width / 2 - backdrop->ShownWidth / 2;
	int from = std::max(left - EDGE_GAP, 0);
	int to = std::min(left + backdrop->ShownWidth + EDGE_GAP, width);
	int last = backdrop->ShownWidth - 1;

	for (int y = std::max(shown.Y, 0); y < std::min(shown.Y + shown.Height, height); y++) {
		int row = down[y];
		int next = (downshare[y] != 0 && row + 1 < rows) ? row + 1 : row;
		unsigned short const * picture = backdrop->Shown.data() + (std::size_t)(y - shown.Y) * backdrop->ShownWidth;
		unsigned short * out = pixels + (std::size_t)y * width;

		if (whole[row] && whole[next]) {
			for (int x = from; x < std::min(left, to); x++) {
				out[x] = picture[0];
			}
			int first = std::max(left, 0);
			int end = std::min(left + backdrop->ShownWidth, width);
			if (end > first) {
				std::memcpy(out + first, picture + (first - left), (std::size_t)(end - first) * 2);
			}
			for (int x = std::max(end, from); x < to; x++) {
				out[x] = picture[last];
			}
			continue;
		}

		unsigned char const * upper = same.data() + (std::size_t)row * columns;
		unsigned char const * lower = same.data() + (std::size_t)next * columns;
		for (int x = from; x < to; x++) {
			int column = across[x];
			if (!upper[column] || !lower[column]) {
				continue;
			}
			if (acrossshare[x] != 0 && column + 1 < columns && (!upper[column + 1] || !lower[column + 1])) {
				continue;
			}
			out[x] = picture[std::clamp(x - left, 0, last)];
		}
	}
	return(true);
}
