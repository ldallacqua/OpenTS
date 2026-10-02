/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "sharptext.h"

#include "_surface.h"
#include "convert.h"
#include "draw.h"
#include "dsurface.h"
#include "globals.h"
#include "goptions.h"
#include "interfacescale.h"
#include "menubars.h"
#include "scaledface.h"
#include "shapeset.h"
#include "surface.h"
#include "utf8.h"
#include "wwfont.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


// Keep the software surfaces intact because later animations restore their bitmap pixels.

// The smallest size a line is drawn at when it has to shrink to fit the bitmap line's width.
static const int SMALLEST_SIZE = 6;

// How narrow and how wide a shape set's characters are drawn, in percent of the face's design.
static const int NARROWEST_SHARE = 90;
static const int WIDEST_SHARE = 115;

// How narrow a character of a set without letters is drawn, to stay inside its bitmap cell.
static const int NARROWEST_LONE_SHARE = 65;

// The letters a shape set's widths are compared by with the scalable face's.
static char const SAMPLE_LETTERS[] = "etaoinshrdlucmETAOINSHRDLUCM";

// A pixel of a shape character counts as part of the letter, and not of its shadow or glow,
// from this percentage of the character's brightest pixel.
static const int LIT_SHARE = 40;

// The pen position of a shape character whose word has not been laid out.
static const int UNPLACED = INT_MIN;

// What is known of a shape set's letters: the lit rows and columns of its capital H, from
// the shape's own origin, and how far each of the sample letters it has moves the pen.
struct ShapeFont
{
	Rect Frame;
	int Top = 0;
	int Rows = 0;
	int Width = 0;
	std::vector<std::pair<char32_t, int>> Cells;

	// The one character measured in place of the H, for a set that has no H.
	char32_t Lone = 0;

	// The size, the width and the spacing between letters, in 64ths of a pixel, that the
	// scalable face stands in with at one enlargement.
	float FitScale = 0.0f;
	int FitSize = 0;
	int FitWide = 0;
	int FitSpacing = 0;
};

struct TextLine
{
	std::string Text;

	// The left end and the top of the bitmap line, and the width it advances over.
	int X;
	int Y;
	int Width;
	int Height;
};

struct TextRecord
{
	// A character drawn from a shape set: where the drawn frame and the finished character
	// stand, the letter's color in that frame, and the rows of the set's capital H.
	char32_t GlyphCode = 0;
	Rect GlyphRect;
	Rect Body;
	unsigned short GlyphColor = 0;
	int CapTop = 0;
	int CapRows = 0;
	ShapeFont * Font = nullptr;

	// Where the pen stands for this character in the enlarged picture once its word is laid
	// out.
	int Pen = UNPLACED;
	Rect Bounds;
	Rect Clip;
	std::vector<TextLine> Lines;
	WWFontClass::LetterShape Shape = {};
	SharpTextAlign Align = SHARP_TEXT_LEFT;

	unsigned short Color = 0;
	unsigned short DropColor = 0;
	unsigned short EdgeColor = 0;
	bool HasDrop = false;
	bool HasEdge = false;

	std::vector<unsigned short> Clean;
	std::vector<unsigned short> After;

	// While the letters are off the surface: what it held, which pixels had been drawn over
	// since the print, and which pixels the scalable face stays out of.
	std::vector<unsigned short> Held;
	std::vector<unsigned char> Covered;
	std::vector<unsigned char> Blocked;
	bool IsCovered = false;
	bool IsHidden = false;
};

// A pixel a remembered text changed, with its value after and before that print.
struct LetterPixel
{
	int X;
	int Y;
	unsigned short After;
	unsigned short Clean;
};

static std::vector<TextRecord> _CompositeRecords;
static std::vector<TextRecord> _SidebarRecords;
static std::unordered_map<Surface const *, std::vector<TextRecord>> _MenuRecords;
static SharpTextAlign _Align = SHARP_TEXT_LEFT;

// Set while this module prints through the bitmap font, so that print is not remembered.
static bool _Printing = false;


// A length of the picture in pixels of the enlarged one. The menu frame is enlarged by a
// factor that need not be whole.
static int Enlarged(int length, float scale)
{
	return((int)std::lround((float)length * scale));
}


static std::vector<TextRecord> * Records_For(Surface const & surface)
{
	if (Frame_Scale() > 1) {
		return(&_MenuRecords[&surface]);
	}
	if (&surface == CompositeSurface) {
		return(&_CompositeRecords);
	}
	if (&surface == SidebarSurface) {
		return(&_SidebarRecords);
	}
	return(NULL);
}


static bool Copy_Out(Surface const & surface, Rect const & rect, std::vector<unsigned short> & pixels)
{
	unsigned char const * buffer = (unsigned char const *)surface.Lock(Point2D(0, 0));
	if (buffer == NULL) {
		return(false);
	}

	pixels.resize((std::size_t)rect.Width * rect.Height);
	for (int row = 0; row < rect.Height; row++) {
		std::memcpy(pixels.data() + (std::size_t)row * rect.Width, buffer + (rect.Y + row) * surface.Stride() + rect.X * sizeof(unsigned short), rect.Width * sizeof(unsigned short));
	}

	surface.Unlock();
	return(true);
}


static void Copy_In(Surface & surface, Rect const & rect, std::vector<unsigned short> const & pixels)
{
	unsigned char * buffer = (unsigned char *)surface.Lock(Point2D(0, 0));
	if (buffer == NULL) {
		return;
	}

	for (int row = 0; row < rect.Height; row++) {
		std::memcpy(buffer + (rect.Y + row) * surface.Stride() + rect.X * sizeof(unsigned short), pixels.data() + (std::size_t)row * rect.Width, rect.Width * sizeof(unsigned short));
	}

	surface.Unlock();
}


static bool Holds(Surface const & surface, Rect const & rect, std::vector<unsigned short> const & pixels)
{
	if (pixels.size() != (std::size_t)rect.Width * rect.Height || Intersect(rect, surface.Get_Rect()) != rect) {
		return(false);
	}

	unsigned char const * buffer = (unsigned char const *)surface.Lock(Point2D(0, 0));
	if (buffer == NULL) {
		return(false);
	}

	bool same = true;
	for (int row = 0; row < rect.Height && same; row++) {
		same = std::memcmp(pixels.data() + (std::size_t)row * rect.Width, buffer + (rect.Y + row) * surface.Stride() + rect.X * sizeof(unsigned short), rect.Width * sizeof(unsigned short)) == 0;
	}

	surface.Unlock();
	return(same);
}


/// <summary>
/// Compares the surface with a text's pixels from after its print, and notes what the
/// surface holds and which pixels have been drawn over.
/// </summary>
/// <returns>bool; Does the surface still hold at least half of the pixels the print
/// changed?</returns>
static bool Find_Covered(Surface const & surface, TextRecord & record)
{
	if (!Copy_Out(surface, record.Bounds, record.Held) || record.Held.size() != record.After.size()) {
		return(false);
	}

	record.Covered.assign(record.Held.size(), 0);
	record.IsCovered = false;

	int letters = 0;
	int left = 0;
	for (std::size_t pixel = 0; pixel < record.Held.size(); pixel++) {
		bool changed = record.After[pixel] != record.Clean[pixel];
		letters += changed;

		if (record.Held[pixel] != record.After[pixel]) {
			record.Covered[pixel] = 1;
			record.IsCovered = true;
		} else {
			left += changed;
		}
	}

	/*
	 * Whatever was drawn over a text can match the text's own pixels here and there, as
	 * a black box does a black outline. Such a pixel has drawn-over pixels beside it, so
	 * the scalable face stays out of those neighbours as well.
	 */
	if (record.IsCovered) {
		int width = record.Bounds.Width;
		int height = record.Bounds.Height;

		record.Blocked = record.Covered;
		for (int row = 0; row < height; row++) {
			for (int column = 0; column < width; column++) {
				if (!record.Covered[(std::size_t)row * width + column]) {
					continue;
				}
				for (int down = std::max(row - 1, 0); down <= std::min(row + 1, height - 1); down++) {
					for (int across = std::max(column - 1, 0); across <= std::min(column + 1, width - 1); across++) {
						record.Blocked[(std::size_t)down * width + across] = 1;
					}
				}
			}
		}
	}

	return(left * 2 >= letters);
}


static void Take_Off(Surface & surface, TextRecord const & record)
{
	unsigned char * buffer = (unsigned char *)surface.Lock(Point2D(0, 0));
	if (buffer == NULL) {
		return;
	}

	for (int row = 0; row < record.Bounds.Height; row++) {
		unsigned short * out = (unsigned short *)(buffer + (record.Bounds.Y + row) * surface.Stride()) + record.Bounds.X;
		std::size_t pixel = (std::size_t)row * record.Bounds.Width;

		for (int column = 0; column < record.Bounds.Width; column++, pixel++) {
			if (!record.Covered[pixel]) {
				out[column] = record.Clean[pixel];
			}
		}
	}

	surface.Unlock();
}


/// <returns>Whether enlarged text on this surface can use the scalable face.</returns>
bool Sharp_Text_Wanted(Surface const & surface)
{
	if (_Printing || Options.BitmapGameFont || surface.Bytes_Per_Pixel() != 2) {
		return(false);
	}

	bool enlarged = Frame_Scale() > 1 || (&surface == CompositeSurface && View_Zoom() > 1) || (&surface == SidebarSurface && Sidebar_Scale() > 1);
	return(enlarged && Scaled_Face_Ready());
}


/// <summary>
/// Says which edge the next print is anchored to. The scalable face's lines are not as wide
/// as the bitmap font's, so a centered or right aligned line has to be placed again.
/// </summary>
void Sharp_Text_Set_Alignment(SharpTextAlign align)
{
	_Align = align;
}


/// <summary>
/// Records a bitmap print for scalable rendering when its surface is enlarged.
/// </summary>
/// <returns>Where the next print should begin to continue this one.</returns>
Point2D Sharp_Text_Print(WWFontClass const & font, char const * string, Surface & surface, Rect const & cliprect, Point2D const & point, ConvertClass const & converter, unsigned char const * remap)
{
	static unsigned char const identity[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	unsigned char const * map = (remap != NULL) ? remap : identity;

	_Printing = true;

	TextRecord record;
	record.Align = _Align;
	record.Clip = Intersect(cliprect, surface.Get_Rect());
	record.IsCovered = false;
	record.IsHidden = false;

	std::vector<TextRecord> * records = Records_For(surface);
	bool known = records != NULL && string != NULL && font.Letter_Shape(record.Shape) && map[record.Shape.Value] != 0;

	if (known) {
		Point2D start = Bias_To(point, cliprect);
		int advance = font.Line_Advance();

		TextLine line;
		line.X = start.X;
		line.Y = start.Y;
		line.Width = 0;
		line.Height = advance;

		Rect bounds;
		char const * text = string;
		for (;;) {
			char const * from = text;
			char32_t code = (*text != '\0') ? UTF8::Decode(text) : U'\0';

			if (code == U'\0' || code == U'\r' || code == U'\n') {
				if (!line.Text.empty()) {
					// A letter's shadow can reach past the place the next letter starts at.
					bounds = Union(bounds, Rect(line.X, line.Y, line.Width + 2, advance));
					record.Lines.push_back(line);
				}
				if (code == U'\0') {
					break;
				}
				line.Text.clear();
				line.X = (code == U'\r') ? start.X : cliprect.X;
				line.Y += advance;
				line.Width = 0;
				continue;
			}

			line.Text.append(from, text);
			line.Width += font.Char_Pixel_Width(code);
		}

		record.Bounds = Intersect(bounds, record.Clip);
		known = record.Bounds.Is_Valid();
	}

	if (!known) {
		Point2D result = font.Print(string, surface, cliprect, point, converter, remap);
		_Printing = false;
		return(result);
	}

	/*
	 * A text printed again where it already stands, over its own pixels, keeps the pixels
	 * from before its first print. A text this one covers half of or more is taken to be
	 * the same label in an earlier state and is forgotten; one it only touches stays.
	 */
	bool inherited = false;
	std::vector<LetterPixel> leftovers;
	for (std::vector<TextRecord>::iterator other = records->begin(); other != records->end(); ) {
		Rect shared = Intersect(other->Bounds, record.Bounds);
		if (!shared.Is_Valid() || shared.Width * shared.Height * 2 < other->Bounds.Width * other->Bounds.Height) {
			++other;
			continue;
		}
		if (!inherited && other->Bounds == record.Bounds && Holds(surface, other->Bounds, other->After)) {
			record.Clean = std::move(other->Clean);
			inherited = true;
		} else if (other->GlyphCode == 0 && other->Clean.size() == other->After.size()) {
			for (int y = shared.Y; y < shared.Y + shared.Height; y++) {
				for (int x = shared.X; x < shared.X + shared.Width; x++) {
					std::size_t pixel = (std::size_t)(y - other->Bounds.Y) * other->Bounds.Width + x - other->Bounds.X;
					if (other->After[pixel] != other->Clean[pixel]) {
						leftovers.push_back(LetterPixel{x, y, other->After[pixel], other->Clean[pixel]});
					}
				}
			}
		}
		other = records->erase(other);
	}

	if (!inherited) {

		// A print that fills behind its letters is run once for the fill alone.
		if (map[0] != 0) {
			unsigned char fill[16] = {};
			fill[0] = map[0];
			font.Print(string, surface, cliprect, point, converter, fill);
		}
		known = Copy_Out(surface, record.Bounds, record.Clean);

		// Letters of a forgotten text that the surface still holds are not background.
		if (known) {
			for (LetterPixel const & letter : leftovers) {
				std::size_t pixel = (std::size_t)(letter.Y - record.Bounds.Y) * record.Bounds.Width + letter.X - record.Bounds.X;
				if (record.Clean[pixel] == letter.After) {
					record.Clean[pixel] = letter.Clean;
				}
			}
		}
	}

	Point2D result = font.Print(string, surface, cliprect, point, converter, remap);
	_Printing = false;

	if (!known || !Copy_Out(surface, record.Bounds, record.After) || record.After == record.Clean) {
		return(result);
	}

	record.Color = (unsigned short)converter.Convert_Pixel(map[record.Shape.Value]);
	record.HasDrop = record.Shape.HasDrop && map[2] != 0 && map[2] != map[0];
	record.DropColor = (unsigned short)converter.Convert_Pixel(map[2]);
	record.HasEdge = record.Shape.HasEdge && map[3] != 0 && map[3] != map[0];
	record.EdgeColor = (unsigned short)converter.Convert_Pixel(map[3]);

	records->push_back(std::move(record));
	return(result);
}


struct TextCanvas
{
	unsigned char * Buffer;
	int Stride;
	Rect Clip;

	// The text being drawn, where its bounds landed on the destination, and how many
	// destination pixels one of its pixels became.
	TextRecord const * Record;
	Point2D Origin;
	float Scale;
};


static bool Is_Blocked(TextCanvas const & canvas, int x, int y)
{
	Rect const & bounds = canvas.Record->Bounds;

	if (x < canvas.Origin.X || y < canvas.Origin.Y) {
		return(false);
	}

	int column = (int)((float)(x - canvas.Origin.X) / canvas.Scale);
	int row = (int)((float)(y - canvas.Origin.Y) / canvas.Scale);
	return(column < bounds.Width && row < bounds.Height && canvas.Record->Blocked[(std::size_t)row * bounds.Width + column] != 0);
}


// The surfaces hold 16 bit pixels of five bits of red, six of green and five of blue.
static void Draw_Line(TextCanvas const & canvas, char const * text, int size, int x, int baseline, unsigned short color)
{
	unsigned char * buffer = canvas.Buffer;
	int stride = canvas.Stride;
	Rect const & clip = canvas.Clip;
	bool covered = canvas.Record->IsCovered;

	int red = color >> 11;
	int green = (color >> 5) & 0x3F;
	int blue = color & 0x1F;
	int pen = x << 6;

	while (*text != '\0') {
		ScaledGlyph const * glyph = Scaled_Face_Glyph(UTF8::Decode(text), size);
		if (glyph == NULL) {
			continue;
		}

		int left = ((pen + 32) >> 6) + glyph->Left;
		int top = baseline - glyph->Top;
		int first = std::max(left, clip.X);
		int last = std::min(left + glyph->Width, clip.X + clip.Width);
		int bottom = std::min(top + glyph->Height, clip.Y + clip.Height);

		for (int y = std::max(top, clip.Y); y < bottom && first < last; y++) {
			std::uint8_t const * coverage = glyph->Coverage.data() + (std::size_t)(y - top) * glyph->Width + (first - left);
			unsigned short * out = (unsigned short *)(buffer + (std::ptrdiff_t)y * stride) + first;

			for (int column = first; column < last; column++, coverage++, out++) {
				int amount = *coverage;
				if (amount == 0 || (covered && Is_Blocked(canvas, column, y))) {
					continue;
				}
				if (amount == 255) {
					*out = color;
				} else {
					int under = *out;
					int r = under >> 11;
					int g = (under >> 5) & 0x3F;
					int b = under & 0x1F;
					r += (red - r) * amount / 255;
					g += (green - g) * amount / 255;
					b += (blue - b) * amount / 255;
					*out = (unsigned short)((r << 11) | (g << 5) | b);
				}
			}
		}

		pen += glyph->Advance;
	}
}


/*
 * Every character of a shape set is drawn at one size, with capitals as tall as the set's.
 * The face is widened or narrowed until the sample letters are as wide on average as the
 * set's, as far as the limits allow, and what is left over goes between the letters.
 */
static void Fit_Shape_Font(ShapeFont & font, float scale)
{
	if (font.FitScale == scale) {
		return;
	}
	font.FitScale = scale;
	font.FitSize = Scaled_Face_Size_For_Capital(Enlarged(font.Rows, scale));
	font.FitWide = font.FitSize;
	font.FitSpacing = 0;

	if (font.Lone != 0) {
		ScaledGlyph const * glyph = Scaled_Face_Glyph(font.Lone, font.FitSize);
		if (glyph != nullptr && glyph->Width > 0) {
			// One bitmap pixel narrower than the bitmap character, which fills its cell.
			int room = Enlarged(std::max(font.Width - 1, 1), scale);
			font.FitWide = std::clamp(font.FitSize * room / glyph->Width, font.FitSize * NARROWEST_LONE_SHARE / 100, font.FitSize * WIDEST_SHARE / 100);
		}
		return;
	}

	long long cells = 0;
	long long designed = 0;
	for (std::pair<char32_t, int> const & cell : font.Cells) {
		ScaledGlyph const * glyph = Scaled_Face_Glyph(cell.first, font.FitSize);
		if (glyph != nullptr) {
			cells += std::lround((float)cell.second * scale * 64.0f);
			designed += glyph->Advance;
		}
	}
	if (designed <= 0) {
		return;
	}
	font.FitWide = std::clamp((int)(font.FitSize * cells / designed), font.FitSize * NARROWEST_SHARE / 100, font.FitSize * WIDEST_SHARE / 100);

	long long fitted = 0;
	int count = 0;
	cells = 0;
	for (std::pair<char32_t, int> const & cell : font.Cells) {
		ScaledGlyph const * glyph = Scaled_Face_Glyph(cell.first, font.FitSize, font.FitWide);
		if (glyph != nullptr) {
			cells += std::lround((float)cell.second * scale * 64.0f);
			fitted += glyph->Advance;
			count++;
		}
	}
	if (count > 0) {
		font.FitSpacing = (int)((cells - fitted) / count);
	}
}


static ScaledGlyph const * Shape_Glyph(TextRecord const & record, float scale, char32_t code)
{
	if (record.Font == nullptr || record.Font->Rows <= 0) {
		return(nullptr);
	}
	Fit_Shape_Font(*record.Font, scale);
	return(Scaled_Face_Glyph(code, record.Font->FitSize, record.Font->FitWide));
}


static bool Same_Line(TextRecord const & one, TextRecord const & other)
{
	return(other.GlyphCode != 0 && one.Font == other.Font && one.CapTop == other.CapTop);
}


/*
 * A shape set's characters stand in cells as wide as the bitmap letters, which the scalable
 * face's letters do not fill evenly. A word, a run of cells that touch, is laid out again
 * from the left edge of its first cell with the face's own spacing, so a letter stays in
 * place while more are typed after it.
 */
static void Place_Shape_Words(std::vector<TextRecord> & records, float scale)
{
	for (TextRecord & record : records) {
		record.Pen = UNPLACED;
		if (record.GlyphCode == 0 || record.Font == nullptr || record.Font->Cells.empty()) {
			continue;
		}
		Fit_Shape_Font(*record.Font, scale);
		int spacing = record.Font->FitSpacing;

		// Walk back through the cells that touch, to the first of the word.
		TextRecord const * first = &record;
		int travelled = 0;
		for (int steps = 0; steps < 256; steps++) {
			TextRecord const * before = nullptr;
			for (TextRecord const & other : records) {
				if (&other != first && Same_Line(*first, other) && other.Body.X + other.Body.Width + 1 == first->Body.X) {
					before = &other;
					break;
				}
			}
			ScaledGlyph const * glyph = (before != nullptr) ? Shape_Glyph(*before, scale, before->GlyphCode) : nullptr;
			if (glyph == nullptr) {
				break;
			}
			travelled += glyph->Advance + spacing;
			first = before;
		}

		ScaledGlyph const * opening = Shape_Glyph(*first, scale, first->GlyphCode);
		if (opening != nullptr) {
			record.Pen = Enlarged(first->Body.X, scale) - opening->Left + ((travelled + 32) >> 6);
		}
	}
}


static void Draw_Shape_Record(TextCanvas const & canvas, TextRecord const & record, Rect const & sourcerect, Point2D const & destorigin, float scale)
{
	if (record.CapRows <= 0) {
		return;
	}

	ScaledGlyph const * glyph = Shape_Glyph(record, scale, record.GlyphCode);
	if (glyph == nullptr || glyph->Width <= 0 || glyph->Height <= 0) {
		return;
	}

	int left = destorigin.X - Enlarged(sourcerect.X, scale);
	if (record.Pen != UNPLACED) {
		left += record.Pen + glyph->Left;
	} else {
		left += Enlarged(record.Body.X, scale) + (Enlarged(record.Body.Width, scale) - glyph->Width) / 2;
	}
	int top = destorigin.Y + Enlarged(record.CapTop + record.CapRows - sourcerect.Y, scale) - glyph->Top;
	unsigned short color = record.GlyphColor;
	int edge = std::max((int)scale / 3, 1);
	Rect area = Intersect(canvas.Clip, Rect(left - edge, top - edge, glyph->Width + edge * 2, glyph->Height + edge * 2));

	auto coverage = [glyph](int x, int y) -> int {
		if (x < 0 || y < 0 || x >= glyph->Width || y >= glyph->Height) {
			return(0);
		}
		return(glyph->Coverage[(std::size_t)y * glyph->Width + x]);
	};

	for (int y = area.Y; y < area.Y + area.Height; y++) {
		unsigned short * out = (unsigned short *)(canvas.Buffer + (std::ptrdiff_t)y * canvas.Stride);
		for (int x = area.X; x < area.X + area.Width; x++) {
			if (record.IsCovered && Is_Blocked(canvas, x, y)) {
				continue;
			}

			int alpha = coverage(x - left, y - top);
			int outline = alpha;
			for (int down = -edge; down <= edge; down++) {
				for (int across = -edge; across <= edge; across++) {
					outline = std::max(outline, coverage(x - left + across, y - top + down));
				}
			}
			if (outline == 0) {
				continue;
			}

			int under = out[x];
			int red = (under >> 11) * (255 - outline) / 255 + (color >> 11) * alpha / 255;
			int green = ((under >> 5) & 63) * (255 - outline) / 255 + ((color >> 5) & 63) * alpha / 255;
			int blue = (under & 31) * (255 - outline) / 255 + (color & 31) * alpha / 255;
			out[x] = (unsigned short)((red << 11) | (green << 5) | blue);
		}
	}
}


static void Draw_Record(TextRecord const & record, Rect const & sourcerect, unsigned char * buffer, int stride, Rect const & destclip, Point2D const & destorigin, float scale)
{
	Rect shown = Intersect(record.Clip, sourcerect);
	int shownleft = Enlarged(shown.X - sourcerect.X, scale);
	int showntop = Enlarged(shown.Y - sourcerect.Y, scale);
	Rect clip = Intersect(destclip, Rect(destorigin.X + shownleft, destorigin.Y + showntop, Enlarged(shown.X + shown.Width - sourcerect.X, scale) - shownleft, Enlarged(shown.Y + shown.Height - sourcerect.Y, scale) - showntop));
	if (!clip.Is_Valid()) {
		return;
	}

	TextCanvas canvas;
	canvas.Buffer = buffer;
	canvas.Stride = stride;
	canvas.Clip = clip;
	canvas.Record = &record;
	canvas.Origin = Point2D(destorigin.X + Enlarged(record.Bounds.X - sourcerect.X, scale), destorigin.Y + Enlarged(record.Bounds.Y - sourcerect.Y, scale));
	canvas.Scale = scale;

	if (record.GlyphCode != 0) {
		Draw_Shape_Record(canvas, record, sourcerect, destorigin, scale);
		return;
	}

	int wanted = Scaled_Face_Size_For_Capital(Enlarged(record.Shape.Bottom - record.Shape.Top + 1, scale));
	int drop = std::max(((int)scale + 1) / 2, 1);
	int edge = std::max((int)scale / 2, 1);

	for (TextLine const & line : record.Lines) {
		int room = Enlarged(line.Width, scale);
		int size = wanted;
		int width = Scaled_Face_String_Width(line.Text.c_str(), size);

		while (width > room && size > SMALLEST_SIZE) {
			size = std::max(std::min(size * room / width, size - 1), SMALLEST_SIZE);
			width = Scaled_Face_String_Width(line.Text.c_str(), size);
		}

		int ascent = 0;
		int descent = 0;
		int above = record.HasEdge ? edge : 0;
		int below = std::max(above, record.HasDrop ? drop : 0);
		for (;;) {
			ascent = descent = 0;
			char const * cursor = line.Text.c_str();
			while (*cursor != '\0') {
				ScaledGlyph const * glyph = Scaled_Face_Glyph(UTF8::Decode(cursor), size);
				if (glyph != nullptr) {
					ascent = std::max(ascent, glyph->Top);
					descent = std::max(descent, glyph->Height - glyph->Top);
				}
			}
			if (ascent + descent + above + below <= Enlarged(line.Height, scale) || size <= SMALLEST_SIZE) {
				break;
			}
			size--;
		}
		width = Scaled_Face_String_Width(line.Text.c_str(), size);
		int x = destorigin.X + Enlarged(line.X - sourcerect.X, scale);
		if (record.Align == SHARP_TEXT_CENTER) {
			x += (room - width) / 2;
		} else if (record.Align == SHARP_TEXT_RIGHT) {
			x += room - width;
		} else {
			x += Enlarged(record.Shape.Left, scale);
		}
		int top = destorigin.Y + Enlarged(line.Y - sourcerect.Y, scale);
		int baseline = top + Enlarged(record.Shape.Bottom + 1, scale);
		int low = top + ascent + above;
		int high = top + Enlarged(line.Height, scale) - descent - below;
		if (low <= high) {
			baseline = std::clamp(baseline, low, high);
		}

		if (record.HasEdge) {
			for (int down = -edge; down <= edge; down++) {
				for (int across = -edge; across <= edge; across++) {
					if (down != 0 || across != 0) {
						Draw_Line(canvas, line.Text.c_str(), size, x + across, baseline + down, record.EdgeColor);
					}
				}
			}
		}
		if (record.HasDrop) {
			Draw_Line(canvas, line.Text.c_str(), size, x + drop, baseline + drop, record.DropColor);
		}
		Draw_Line(canvas, line.Text.c_str(), size, x, baseline, record.Color);
	}
}


/// <summary>
/// Takes the bitmap letters of every remembered text that touches the rectangle off the
/// surface, ahead of the rectangle being enlarged. A text the surface holds less than half
/// of is forgotten instead. Sharp_Text_Show must follow before anything else draws on the
/// surface.
/// </summary>
void Sharp_Text_Hide(Surface & source, Rect const & sourcerect)
{
	std::vector<TextRecord> * records = Records_For(source);
	if (records == NULL) {
		return;
	}

	for (std::vector<TextRecord>::iterator record = records->begin(); record != records->end(); ) {
		if (!record->Bounds.Is_Overlapping(sourcerect)) {
			++record;
			continue;
		}
		if (Intersect(record->Bounds, source.Get_Rect()) != record->Bounds || !Find_Covered(source, *record)) {
			record = records->erase(record);
			continue;
		}

		record->IsHidden = true;
		++record;
	}

	// A later print's pixels from before it hold an earlier text's letters where the two
	// touch, so the later one comes off first.
	for (std::vector<TextRecord>::reverse_iterator record = records->rbegin(); record != records->rend(); ++record) {
		if (record->IsHidden) {
			Take_Off(source, *record);
		}
	}
}


/// <summary>
/// Puts back the bitmap letters Sharp_Text_Hide took off the surface and, when asked to
/// draw, draws those texts in the scalable face on the destination.
/// </summary>
/// <param name="sourcerect">The rectangle that was enlarged.</param>
/// <param name="destclip">The part of the destination that may be written.</param>
/// <param name="destorigin">Where the rectangle's top left corner went.</param>
/// <param name="scale">How many destination pixels a source pixel became.</param>
/// <param name="draw">False when the rectangle was not enlarged after all, so the texts are
/// only put back.</param>
void Sharp_Text_Show(Surface & source, Rect const & sourcerect, Surface & dest, Rect const & destclip, Point2D const & destorigin, int scale, bool draw)
{
	std::vector<TextRecord> * records = Records_For(source);
	if (records == NULL) {
		return;
	}

	bool any = false;
	for (TextRecord & record : *records) {
		if (record.IsHidden) {
			Copy_In(source, record.Bounds, record.Held);
			any = true;
		}
	}
	if (!any) {
		return;
	}

	unsigned char * buffer = (draw && dest.Bytes_Per_Pixel() == 2) ? (unsigned char *)dest.Lock(Point2D(0, 0)) : NULL;
	Rect clip = Intersect(destclip, dest.Get_Rect());

	for (TextRecord & record : *records) {
		if (record.IsHidden) {
			record.IsHidden = false;
			if (buffer != NULL) {
				Draw_Record(record, sourcerect, buffer, dest.Stride(), clip, destorigin, (float)scale);
			}
		}
	}

	if (buffer != NULL) {
		dest.Unlock();
	}
}


/// <summary>
/// Forgets every remembered text. Call it when the surfaces are replaced.
/// </summary>
void Sharp_Text_Forget(void)
{
	_CompositeRecords.clear();
	_SidebarRecords.clear();
	_MenuRecords.clear();
}


/// <summary>Removes records belonging to a surface being destroyed.</summary>
void Sharp_Text_Forget(Surface const & surface)
{
	_MenuRecords.erase(&surface);
}


/// <summary>Carries recorded text through an opaque copy without changing its pixels.</summary>
void Sharp_Text_Copy(Surface const & source, Rect const & sourcerect, Surface & dest, Rect const & destrect)
{
	if (_Printing || Frame_Scale() <= 1 || Options.BitmapGameFont || &source == &dest) {
		return;
	}
	if (sourcerect.Width != destrect.Width || sourcerect.Height != destrect.Height) {
		return;
	}

	int dx = destrect.X - sourcerect.X;
	int dy = destrect.Y - sourcerect.Y;
	Rect copied = Intersect(sourcerect, source.Get_Rect());
	Rect available = dest.Get_Rect();
	available.X -= dx;
	available.Y -= dy;
	copied = Intersect(copied, available);
	Rect damaged(copied.X + dx, copied.Y + dy, copied.Width, copied.Height);

	std::vector<TextRecord> additions;
	auto found = _MenuRecords.find(&source);
	if (found != _MenuRecords.end()) {
		for (TextRecord const & original : found->second) {
			Rect part = Intersect(original.Bounds, copied);
			if (!part.Is_Valid()) {
				continue;
			}
			TextRecord record = original;
			record.Clean.resize((std::size_t)part.Width * part.Height);
			record.After.resize(record.Clean.size());
			for (int y = 0; y < part.Height; y++) {
				std::size_t offset = (std::size_t)(part.Y - original.Bounds.Y + y) * original.Bounds.Width + part.X - original.Bounds.X;
				std::copy_n(original.Clean.data() + offset, part.Width, record.Clean.data() + (std::size_t)y * part.Width);
				std::copy_n(original.After.data() + offset, part.Width, record.After.data() + (std::size_t)y * part.Width);
			}
			record.Bounds = Rect(part.X + dx, part.Y + dy, part.Width, part.Height);
			record.Clip = Intersect(original.Clip, copied);
			record.Clip.X += dx;
			record.Clip.Y += dy;
			record.GlyphRect.X += dx;
			record.GlyphRect.Y += dy;
			record.Body.X += dx;
			record.Body.Y += dy;
			record.CapTop += dy;
			for (TextLine & line : record.Lines) {
				line.X += dx;
				line.Y += dy;
			}
			record.IsHidden = false;
			additions.push_back(std::move(record));
		}
	}

	if (additions.empty() && _MenuRecords.find(&dest) == _MenuRecords.end()) {
		return;
	}
	auto & records = _MenuRecords[&dest];
	std::erase_if(records, [&damaged](TextRecord const & record) { return Intersect(record.Bounds, damaged) == record.Bounds; });
	for (TextRecord & record : additions) {
		records.push_back(std::move(record));
	}
}


static int Light_Of(unsigned short color)
{
	return((color >> 11) * 2 + ((color >> 5) & 63) * 3 + (color & 31));
}


static bool Sample_Frame(ConvertClass & converter, ShapeSet const & shapes, int frame, std::vector<unsigned short> & colors, Rect & rect)
{
	rect = shapes.Get_Rect(frame);
	if (rect.Width <= 0 || rect.Height <= 0) {
		return(false);
	}

	bool printing = _Printing;
	_Printing = true;
	DSurface sample(rect.Width, rect.Height);
	sample.Fill(0);
	Draw_Shape(sample, converter, &shapes, frame, Point2D(-rect.X, -rect.Y), sample.Get_Rect(), SHAPE_WIN_REL);
	bool sampled = Copy_Out(sample, sample.Get_Rect(), colors);
	_Printing = printing;
	return(sampled);
}


static std::map<std::pair<ShapeSet const *, int>, ShapeFont> _ShapeFonts;


// A set's characters are in code order, three frames each, so the frame of its H leads to
// the frames of the other letters.
static ShapeFont & Shape_Font_Of(ConvertClass & converter, ShapeSet const & shapes, int frame, bool letters)
{
	Rect rect = shapes.Get_Rect(frame);
	std::pair<ShapeSet const *, int> key(&shapes, frame);
	std::map<std::pair<ShapeSet const *, int>, ShapeFont>::iterator found = _ShapeFonts.find(key);
	if (found != _ShapeFonts.end() && found->second.Frame == rect) {
		return(found->second);
	}

	ShapeFont font;
	font.Frame = rect;
	std::vector<unsigned short> colors;
	if (Sample_Frame(converter, shapes, frame, colors, rect)) {
		int brightest = 0;
		for (unsigned short color : colors) {
			brightest = std::max(brightest, Light_Of(color));
		}

		int top = rect.Height;
		int bottom = -1;
		int left = rect.Width;
		int right = -1;
		for (int y = 0; y < rect.Height; y++) {
			for (int x = 0; x < rect.Width; x++) {
				if (brightest > 0 && Light_Of(colors[(std::size_t)y * rect.Width + x]) * 100 >= brightest * LIT_SHARE) {
					top = std::min(top, y);
					bottom = std::max(bottom, y);
					left = std::min(left, x);
					right = std::max(right, x);
				}
			}
		}
		if (bottom >= top) {
			font.Top = rect.Y + top;
			font.Rows = bottom - top + 1;
			font.Width = right - left + 1;
		}
	}

	if (letters && font.Rows > 0) {
		for (char const * letter = SAMPLE_LETTERS; *letter != '\0'; letter++) {
			int other = frame + 3 * (*letter - 'H');
			if (other >= 0 && other < shapes.Get_Count() && shapes.Get_Rect(other).Width > 0) {
				font.Cells.push_back(std::pair<char32_t, int>((char32_t)*letter, shapes.Get_Rect(other).Width + 1));
			}
		}
	}

	_ShapeFonts[key] = font;
	return(_ShapeFonts[key]);
}


/// <summary>
/// Draws one frame of a character from a shape set and remembers it, so the character is
/// drawn in the scalable face when the surface is enlarged.
/// </summary>
/// <param name="finalframe">The last of the character's three fade frames.</param>
/// <param name="capitalframe">The last fade frame of the set's capital H.</param>
void Sharp_Text_Draw_Glyph(Surface & surface, ConvertClass & converter, ShapeSet const & shapes, int frame, int finalframe, int capitalframe, char32_t code, Point2D const & point)
{
	Rect glyphrect = shapes.Get_Rect(frame);
	glyphrect.X += point.X;
	glyphrect.Y += point.Y;
	Rect extent;
	for (int fade = finalframe - 2; fade <= finalframe; fade++) {
		Rect area = shapes.Get_Rect(fade);
		area.X += point.X;
		area.Y += point.Y;
		extent = Union(extent, area);
	}
	Rect bounds = Intersect(extent, surface.Get_Rect());
	bool wanted = Sharp_Text_Wanted(surface) && Frame_Scale() > 1 && bounds.Is_Valid();
	TextRecord record;
	if (wanted) {
		record.Bounds = bounds;
		record.Clip = surface.Get_Rect();
		record.GlyphRect = glyphrect;
		record.Body = shapes.Get_Rect(finalframe);
		record.Body.X += point.X;
		record.Body.Y += point.Y;
		record.GlyphCode = code;
		record.IsHidden = false;
		record.IsCovered = false;
		auto & records = *Records_For(surface);
		wanted = Copy_Out(surface, bounds, record.Clean);
		if (wanted) {
			// Neighboring glow frames overlap; every glyph keeps the background without text.
			for (auto other = records.rbegin(); other != records.rend(); ++other) {
				if (other->GlyphCode == 0) continue;
				Rect shared = Intersect(bounds, other->Bounds);
				for (int y = shared.Y; y < shared.Y + shared.Height; y++) {
					for (int x = shared.X; x < shared.X + shared.Width; x++) {
						std::size_t pixel = (std::size_t)(y - bounds.Y) * bounds.Width + x - bounds.X;
						std::size_t previous = (std::size_t)(y - other->Bounds.Y) * other->Bounds.Width + x - other->Bounds.X;
						if (record.Clean[pixel] == other->After[previous]) {
							record.Clean[pixel] = other->Clean[previous];
						}
					}
				}
			}
		}
		std::erase_if(records, [&bounds](TextRecord const & other) { return other.GlyphCode != 0 && other.Bounds == bounds; });
	}

	Draw_Shape(surface, converter, &shapes, frame, point, surface.Get_Rect(), SHAPE_WIN_REL);
	if (!wanted || !Copy_Out(surface, bounds, record.After) || record.After == record.Clean) {
		return;
	}

	std::vector<unsigned short> sharedpixels;
	for (TextRecord & other : *Records_For(surface)) {
		if (other.GlyphCode == 0) continue;
		Rect shared = Intersect(bounds, other.Bounds);
		if (!shared.Is_Valid() || !Copy_Out(surface, shared, sharedpixels)) continue;
		for (int y = 0; y < shared.Height; y++) {
			std::size_t offset = (std::size_t)(shared.Y - other.Bounds.Y + y) * other.Bounds.Width + shared.X - other.Bounds.X;
			std::copy_n(sharedpixels.data() + (std::size_t)y * shared.Width, shared.Width, other.After.data() + offset);
		}
	}

	// A set without an H, such as one of figures alone, is measured by the character itself.
	ShapeFont * font = &Shape_Font_Of(converter, shapes, capitalframe, true);
	bool lettered = font->Rows > 0;
	if (!lettered) {
		font = &Shape_Font_Of(converter, shapes, finalframe, false);
		font->Lone = code;
	}

	// The bitmap letters are one color with softened edges, and a thin character may hold
	// edge pixels only, so the color is read from the set's H in the same fade frame.
	std::vector<unsigned short> colors;
	Rect rect;
	if (font->Rows <= 0 || !Sample_Frame(converter, shapes, lettered ? capitalframe - (finalframe - frame) : frame, colors, rect)) {
		return;
	}
	record.Font = font;
	record.CapTop = point.Y + font->Top;
	record.CapRows = font->Rows;

	int brightest = 0;
	for (unsigned short color : colors) {
		if (Light_Of(color) > brightest) {
			brightest = Light_Of(color);
			record.GlyphColor = color;
		}
	}
	if (brightest == 0) {
		return;
	}

	Records_For(surface)->push_back(std::move(record));
}


// Which pixel of a row or column each pixel of the enlarged one shows, and how much of the
// next it takes where it lies across two, in 32nds.
static void Spread(std::vector<int> & index, std::vector<unsigned char> & share, int from, int to)
{
	index.resize(to);
	share.resize(to);

	for (int pixel = 0; pixel < to; pixel++) {
		long long start = (long long)pixel * from;
		long long end = start + from;
		int first = (int)(start / to);
		int last = (int)((end - 1) / to);

		index[pixel] = first;
		share[pixel] = 0;
		if (last > first && last < from) {
			share[pixel] = (unsigned char)((end - (long long)last * to) * 32 / from);
		}
	}
}


static unsigned short Mix(unsigned short one, unsigned short other, unsigned int share)
{
	unsigned int from = (one | ((unsigned int)one << 16)) & 0x07E0F81F;
	unsigned int to = (other | ((unsigned int)other << 16)) & 0x07E0F81F;
	unsigned int mixed = ((from * (32 - share) + to * share) >> 5) & 0x07E0F81F;
	return((unsigned short)(mixed | (mixed >> 16)));
}


/// <summary>
/// Enlarges the menu frame to the size it is shown at and draws its remembered text over it
/// in the scalable face. The surface is left as it was.
/// </summary>
/// <param name="width">The width the frame is shown at.</param>
/// <param name="height">The height the frame is shown at.</param>
/// <returns>bool; Do the pixels hold the frame to show in place of the surface? They do not
/// when the menu frame is not enlarged, BitmapGameFont is on or no face could be read.</returns>
bool Sharp_Text_Menu_Frame(Surface & source, std::vector<unsigned short> & pixels, int width, int height)
{
	int columns = source.Get_Width();
	int rows = source.Get_Height();
	if (Frame_Scale() <= 1 || width <= columns || height <= rows || source.Bytes_Per_Pixel() != 2 || Options.BitmapGameFont || !Scaled_Face_Ready()) {
		return(false);
	}

	Rect rect = source.Get_Rect();
	Sharp_Text_Hide(source, rect);
	unsigned char const * from = (unsigned char const *)source.Lock();
	if (from == nullptr) {
		Sharp_Text_Show(source, rect, source, rect, Point2D(0, 0), 1, false);
		return(false);
	}

	static std::vector<int> across;
	static std::vector<int> down;
	static std::vector<unsigned char> acrossshare;
	static std::vector<unsigned char> downshare;
	static std::vector<unsigned short> upper;
	static std::vector<unsigned short> lower;
	Spread(across, acrossshare, columns, width);
	Spread(down, downshare, rows, height);
	upper.resize(width);
	lower.resize(width);
	pixels.resize((std::size_t)width * height);

	auto widen = [&](int row, unsigned short * out) {
		unsigned short const * line = (unsigned short const *)(from + (std::ptrdiff_t)row * source.Stride());
		for (int x = 0; x < width; x++) {
			unsigned short pixel = line[across[x]];
			if (acrossshare[x] != 0) {
				pixel = Mix(pixel, line[across[x] + 1], acrossshare[x]);
			}
			out[x] = pixel;
		}
	};

	for (int y = 0; y < height; y++) {
		unsigned short * out = pixels.data() + (std::size_t)y * width;
		if (downshare[y] != 0) {
			widen(down[y], upper.data());
			widen(down[y] + 1, lower.data());
			for (int x = 0; x < width; x++) {
				out[x] = Mix(upper[x], lower[x], downshare[y]);
			}
		} else if (y > 0 && downshare[y - 1] == 0 && down[y - 1] == down[y]) {
			std::copy_n(out - width, width, out);
		} else {
			widen(down[y], out);
		}
	}

	// The letters are off the surface here, so the bars do not mirror them.
	Menu_Bars_Draw((unsigned short const *)from, columns, rows, source.Stride(), pixels.data(), width, height);
	source.Unlock();

	float scale = (float)height / (float)rows;
	std::vector<TextRecord> & records = *Records_For(source);
	Place_Shape_Words(records, scale);
	for (TextRecord & record : records) {
		if (record.IsHidden) {
			Copy_In(source, record.Bounds, record.Held);
			record.IsHidden = false;
			Draw_Record(record, rect, (unsigned char *)pixels.data(), width * 2, Rect(0, 0, width, height), Point2D(0, 0), scale);
		}
	}
	return(true);
}
