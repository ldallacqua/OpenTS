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
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
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

// A changed pixel of a lettered picture can be the letters' color from this much light, as
// the sum of its red, green and blue out of 255 each.
static const int LETTER_LIGHT = 150;

// How far a pixel may be from the letters' color, as the same kind of sum, and still be a
// whole pixel of a letter.
static const int LETTER_SPREAD = 24;

// A pixel beside a letter is the letters' shadow up to this much red, green or blue.
static const int SHADOW_LIGHT = 40;

// How far from its letters a shadow is looked for, in pixels.
static const int SHADOW_REACH = 3;

// Over how many pixels a picture's glow fades out toward the picture's edge.
static const int GLOW_FADE = 3;

// The size the title face is measured at to compare it with a picture's lettering.
static const int LABEL_MEASURE_SIZE = 128;

// How narrow and how wide the title face is drawn to match a picture's lettering, in percent
// of its design. Lettering that needs more is taken to say something else.
static const int LABEL_NARROWEST = 90;
static const int LABEL_WIDEST = 120;

// One line of the lettering painted into a picture.
struct LabelLine
{
	std::string Text;

	// The box the bitmap letters fill, in picture pixels from the picture's top left corner.
	float Left = 0.0f;
	float Top = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;
};

// What is known of the lettering painted into a picture.
struct PictureLabel
{
	int Width = 0;
	int Height = 0;
	std::vector<LabelLine> Lines;
	unsigned short Color = 0;

	// How far right and down of the letters their black shadow stands, in picture pixels.
	// Both are zero for lettering without one.
	int ShadowX = 0;
	int ShadowY = 0;

	// The glow around the letters: its color, and how much of it each picture pixel shows.
	unsigned short GlowColor = 0;
	std::vector<unsigned char> Glow;

	// The glow at the size it was last drawn at.
	float ShownScale = 0.0f;
	int ShownWidth = 0;
	int ShownHeight = 0;
	std::vector<unsigned char> Shown;
};

// Where the ink of a line of text stands: from the pen's start to its first column, from the
// baseline up to its first row, and its size.
struct InkBox
{
	int Left = 0;
	int Top = 0;
	int Width = 0;
	int Height = 0;
};

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

	// The lettering of a picture, and where the picture's top left corner stands.
	std::shared_ptr<PictureLabel> Label;
	Point2D LabelOrigin;
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
static void Draw_Line(TextCanvas const & canvas, char const * text, int size, int x, int baseline, unsigned short color, int wide = 0, ScaledFaceType face = SCALED_FACE_TEXT)
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
		ScaledGlyph const * glyph = Scaled_Face_Glyph(UTF8::Decode(text), size, wide, face);
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


static bool Measure_Ink(char const * text, int size, int wide, ScaledFaceType face, InkBox & box)
{
	int pen = 0;
	int left = INT_MAX;
	int right = INT_MIN;
	int top = INT_MIN;
	int bottom = INT_MAX;

	while (*text != '\0') {
		ScaledGlyph const * glyph = Scaled_Face_Glyph(UTF8::Decode(text), size, wide, face);
		if (glyph == nullptr) {
			continue;
		}
		if (glyph->Width > 0 && glyph->Height > 0) {
			int start = ((pen + 32) >> 6) + glyph->Left;
			left = std::min(left, start);
			right = std::max(right, start + glyph->Width);
			top = std::max(top, glyph->Top);
			bottom = std::min(bottom, glyph->Top - glyph->Height);
		}
		pen += glyph->Advance;
	}
	if (right <= left || top <= bottom) {
		return(false);
	}

	box.Left = left;
	box.Top = top;
	box.Width = right - left;
	box.Height = top - bottom;
	return(true);
}


/*
 * A line of a picture's lettering is drawn in the title face at the size whose ink is as
 * tall as the bitmap letters', widened or narrowed until it is as wide as theirs.
 */
static bool Fit_Label_Line(LabelLine const & line, float scale, int & size, int & wide, InkBox & box)
{
	char const * text = line.Text.c_str();
	float tall = (line.Bottom - line.Top) * scale;
	float broad = (line.Right - line.Left) * scale;

	InkBox measured;
	if (!Measure_Ink(text, LABEL_MEASURE_SIZE, 0, SCALED_FACE_TITLE, measured)) {
		return(false);
	}

	// The ink does not grow evenly with the size, so the sizes beside the estimate are tried.
	int estimate = std::max((int)std::lround((float)LABEL_MEASURE_SIZE * tall / (float)measured.Height), SMALLEST_SIZE);
	float nearest = FLT_MAX;
	size = estimate;
	for (int tried = std::max(estimate - 1, 1); tried <= estimate + 1; tried++) {
		if (Measure_Ink(text, tried, 0, SCALED_FACE_TITLE, measured) && std::fabs((float)measured.Height - tall) < nearest) {
			nearest = std::fabs((float)measured.Height - tall);
			size = tried;
		}
	}
	if (!Measure_Ink(text, size, 0, SCALED_FACE_TITLE, measured)) {
		return(false);
	}

	wide = std::clamp((int)std::lround((float)size * broad / (float)measured.Width), size * LABEL_NARROWEST / 100, size * LABEL_WIDEST / 100);
	return(Measure_Ink(text, size, wide, SCALED_FACE_TITLE, box));
}


static void Show_Glow(PictureLabel & label, float scale)
{
	if (label.ShownScale == scale) {
		return;
	}
	label.ShownScale = scale;
	label.ShownWidth = Enlarged(label.Width, scale);
	label.ShownHeight = Enlarged(label.Height, scale);
	label.Shown.assign((std::size_t)label.ShownWidth * label.ShownHeight, 0);
	if (label.Glow.empty()) {
		return;
	}

	// Each shown pixel takes its share of the four picture pixels around its middle.
	std::vector<int> column(label.ShownWidth);
	std::vector<int> share(label.ShownWidth);
	for (int x = 0; x < label.ShownWidth; x++) {
		float place = std::clamp(((float)x + 0.5f) / scale - 0.5f, 0.0f, (float)(label.Width - 1));
		column[x] = std::min((int)place, std::max(label.Width - 2, 0));
		share[x] = (int)((place - (float)column[x]) * 256.0f);
	}

	int beside = (label.Width > 1) ? 1 : 0;
	for (int y = 0; y < label.ShownHeight; y++) {
		float place = std::clamp(((float)y + 0.5f) / scale - 0.5f, 0.0f, (float)(label.Height - 1));
		int row = std::min((int)place, std::max(label.Height - 2, 0));
		int lower = (int)((place - (float)row) * 256.0f);
		unsigned char const * above = label.Glow.data() + (std::size_t)row * label.Width;
		unsigned char const * below = above + ((label.Height > 1) ? label.Width : 0);
		unsigned char * out = label.Shown.data() + (std::size_t)y * label.ShownWidth;

		for (int x = 0; x < label.ShownWidth; x++) {
			int first = above[column[x]] * (256 - share[x]) + above[column[x] + beside] * share[x];
			int second = below[column[x]] * (256 - share[x]) + below[column[x] + beside] * share[x];
			out[x] = (unsigned char)((first * (256 - lower) + second * lower) >> 16);
		}
	}
}


static void Draw_Label(TextCanvas const & whole, TextRecord const & record, Rect const & sourcerect, Point2D const & destorigin, float scale)
{
	PictureLabel & label = *record.Label;
	int left = destorigin.X + Enlarged(record.LabelOrigin.X - sourcerect.X, scale);
	int top = destorigin.Y + Enlarged(record.LabelOrigin.Y - sourcerect.Y, scale);

	// A record can hold a part of its picture only, and draws that part alone.
	TextCanvas canvas = whole;
	int right = destorigin.X + Enlarged(record.Bounds.X + record.Bounds.Width - sourcerect.X, scale);
	int bottom = destorigin.Y + Enlarged(record.Bounds.Y + record.Bounds.Height - sourcerect.Y, scale);
	canvas.Clip = Intersect(whole.Clip, Rect(whole.Origin.X, whole.Origin.Y, right - whole.Origin.X, bottom - whole.Origin.Y));

	Show_Glow(label, scale);
	Rect area = Intersect(canvas.Clip, Rect(left, top, label.ShownWidth, label.ShownHeight));
	int red = label.GlowColor >> 11;
	int green = (label.GlowColor >> 5) & 63;
	int blue = label.GlowColor & 31;

	for (int y = area.Y; y < area.Y + area.Height; y++) {
		unsigned char const * glow = label.Shown.data() + (std::size_t)(y - top) * label.ShownWidth + (area.X - left);
		unsigned short * out = (unsigned short *)(canvas.Buffer + (std::ptrdiff_t)y * canvas.Stride) + area.X;

		for (int x = area.X; x < area.X + area.Width; x++, glow++, out++) {
			int amount = *glow;
			if (amount == 0 || (record.IsCovered && Is_Blocked(canvas, x, y))) {
				continue;
			}
			int under = *out;
			int r = under >> 11;
			int g = (under >> 5) & 63;
			int b = under & 31;
			r += (red - r) * amount / 255;
			g += (green - g) * amount / 255;
			b += (blue - b) * amount / 255;
			*out = (unsigned short)((r << 11) | (g << 5) | b);
		}
	}

	for (LabelLine const & line : label.Lines) {
		int size;
		int wide;
		InkBox box;
		if (!Fit_Label_Line(line, scale, size, wide, box)) {
			continue;
		}

		int x = left + (int)std::lround(line.Left * scale) - box.Left;
		int baseline = top + (int)std::lround(line.Top * scale) + box.Top;
		if (label.ShadowX != 0 || label.ShadowY != 0) {
			// The bitmap letters' soft edges lie over the nearest half pixel of their shadow.
			int across = (int)std::lround(std::max((float)label.ShadowX - 0.5f, 0.0f) * scale);
			int down = (int)std::lround(std::max((float)label.ShadowY - 0.5f, 0.0f) * scale);
			Draw_Line(canvas, line.Text.c_str(), size, x + across, baseline + down, 0, wide, SCALED_FACE_TITLE);
		}
		Draw_Line(canvas, line.Text.c_str(), size, x, baseline, label.Color, wide, SCALED_FACE_TITLE);
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
	if (record.Label != nullptr) {
		Draw_Label(canvas, record, sourcerect, destorigin, scale);
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
			record.LabelOrigin.X += dx;
			record.LabelOrigin.Y += dy;
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


static void To_Colors(std::vector<unsigned short> const & pixels, std::vector<float> & colors)
{
	colors.resize(pixels.size() * 3);
	for (std::size_t pixel = 0; pixel < pixels.size(); pixel++) {
		unsigned short color = pixels[pixel];
		colors[pixel * 3] = (float)((color >> 11) * 255 / 31);
		colors[pixel * 3 + 1] = (float)(((color >> 5) & 63) * 255 / 63);
		colors[pixel * 3 + 2] = (float)((color & 31) * 255 / 31);
	}
}


// Adds to a mask every pixel within the given number of steps of one it holds.
static void Grow(std::vector<unsigned char> & mask, int width, int height, int steps)
{
	std::vector<unsigned char> before;

	for (int step = 0; step < steps; step++) {
		before = mask;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				if (before[(std::size_t)y * width + x]) {
					continue;
				}
				for (int down = std::max(y - 1, 0); down <= std::min(y + 1, height - 1); down++) {
					for (int across = std::max(x - 1, 0); across <= std::min(x + 1, width - 1); across++) {
						if (before[(std::size_t)down * width + across]) {
							mask[(std::size_t)y * width + x] = 1;
						}
					}
				}
			}
		}
	}
}


// Averages every value with its neighbors, weighed by a bell curve of the given width.
static void Soften(std::vector<float> & field, int width, int height, float sigma)
{
	int reach = std::max((int)(sigma * 3.0f + 0.5f), 1);
	std::vector<float> weights(reach * 2 + 1);
	float total = 0.0f;
	for (int tap = -reach; tap <= reach; tap++) {
		weights[tap + reach] = std::exp(-0.5f * (float)(tap * tap) / (sigma * sigma));
		total += weights[tap + reach];
	}
	for (float & weight : weights) {
		weight /= total;
	}

	std::vector<float> pass(field.size());
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0.0f;
			for (int tap = -reach; tap <= reach; tap++) {
				sum += field[(std::size_t)y * width + std::clamp(x + tap, 0, width - 1)] * weights[tap + reach];
			}
			pass[(std::size_t)y * width + x] = sum;
		}
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float sum = 0.0f;
			for (int tap = -reach; tap <= reach; tap++) {
				sum += pass[(std::size_t)std::clamp(y + tap, 0, height - 1) * width + x] * weights[tap + reach];
			}
			field[(std::size_t)y * width + x] = sum;
		}
	}
}


/*
 * A lettered picture is the background under it with three things painted on: a soft glow
 * of one color, the letters' black shadow, and the letters in one color. The glow comes
 * back as its color and how much of it each pixel shows, with what the letters and their
 * shadow hide filled in from around them, so letters of the scalable face can stand on it.
 */
static std::shared_ptr<PictureLabel> Read_Label(std::vector<unsigned short> const & clean, std::vector<unsigned short> const & after, int width, int height, char const * text)
{
	std::size_t count = (std::size_t)width * height;
	std::vector<float> shown;
	std::vector<float> under;
	To_Colors(after, shown);
	To_Colors(clean, under);

	// The letters are the most common color among the changed pixels that is not dark.
	std::unordered_map<unsigned short, int> tally;
	for (std::size_t pixel = 0; pixel < count; pixel++) {
		if (after[pixel] != clean[pixel] && shown[pixel * 3] + shown[pixel * 3 + 1] + shown[pixel * 3 + 2] > (float)LETTER_LIGHT) {
			tally[after[pixel]]++;
		}
	}
	unsigned short color = 0;
	int most = 0;
	for (std::pair<unsigned short const, int> const & entry : tally) {
		if (entry.second > most || (entry.second == most && entry.first > color)) {
			most = entry.second;
			color = entry.first;
		}
	}
	if (most == 0) {
		return(nullptr);
	}

	std::vector<unsigned short> one(1, color);
	std::vector<float> letter;
	To_Colors(one, letter);

	std::vector<unsigned char> letters(count, 0);
	for (std::size_t pixel = 0; pixel < count; pixel++) {
		float spread = std::fabs(shown[pixel * 3] - letter[0]) + std::fabs(shown[pixel * 3 + 1] - letter[1]) + std::fabs(shown[pixel * 3 + 2] - letter[2]);
		letters[pixel] = after[pixel] != clean[pixel] && spread <= (float)LETTER_SPREAD;
	}

	// The rows that hold letters, in runs, are the lines of the lettering.
	std::shared_ptr<PictureLabel> label = std::make_shared<PictureLabel>();
	label->Width = width;
	label->Height = height;
	label->Color = color;

	std::vector<Rect> boxes;
	bool open = false;
	for (int y = 0; y < height; y++) {
		int first = width;
		int last = -1;
		for (int x = 0; x < width; x++) {
			if (letters[(std::size_t)y * width + x]) {
				first = std::min(first, x);
				last = std::max(last, x);
			}
		}
		if (last < 0) {
			open = false;
			continue;
		}
		Rect row(first, y, last - first + 1, 1);
		if (open) {
			boxes.back() = Union(boxes.back(), row);
		} else {
			boxes.push_back(row);
		}
		open = true;
	}

	char const * cursor = text;
	for (;;) {
		char const * end = std::strchr(cursor, '\n');
		LabelLine line;
		line.Text = (end != nullptr) ? std::string(cursor, end) : std::string(cursor);
		label->Lines.push_back(line);
		if (end == nullptr) {
			break;
		}
		cursor = end + 1;
	}
	if (boxes.size() != label->Lines.size()) {
		return(nullptr);
	}

	// The shadow is a copy of the letters, moved right and down and painted black.
	int best = 0;
	for (int down = 0; down <= SHADOW_REACH; down++) {
		for (int across = 0; across <= SHADOW_REACH; across++) {
			int score = 0;
			for (int y = down; y < height; y++) {
				for (int x = across; x < width; x++) {
					std::size_t pixel = (std::size_t)y * width + x;
					if (letters[pixel - (std::size_t)down * width - across] && !letters[pixel]) {
						bool dark = std::max({shown[pixel * 3], shown[pixel * 3 + 1], shown[pixel * 3 + 2]}) <= (float)SHADOW_LIGHT;
						score += dark ? 1 : -1;
					}
				}
			}
			if (score > best) {
				best = score;
				label->ShadowX = across;
				label->ShadowY = down;
			}
		}
	}

	/*
	 * The glow is read from the changed pixels that are neither letter nor shadow. The
	 * shadow is taken to be every dark pixel near a letter, and the soft edges of both add
	 * one more pixel around them.
	 */
	std::vector<unsigned char> solid = letters;
	Grow(solid, width, height, SHADOW_REACH + 1);
	for (std::size_t pixel = 0; pixel < count; pixel++) {
		bool dark = std::max({shown[pixel * 3], shown[pixel * 3 + 1], shown[pixel * 3 + 2]}) <= (float)SHADOW_LIGHT;
		solid[pixel] = letters[pixel] || (solid[pixel] && dark);
	}
	Grow(solid, width, height, 1);

	// The glow is taken to be one color, mixed into each pixel's background by its own share.
	float glow[3] = {0.0f, 0.0f, 0.0f};
	int glowing = 0;
	for (std::size_t pixel = 0; pixel < count; pixel++) {
		if (after[pixel] != clean[pixel] && !solid[pixel]) {
			for (int part = 0; part < 3; part++) {
				glow[part] += shown[pixel * 3 + part];
			}
			glowing++;
		}
	}

	auto share_of = [&](std::size_t pixel) -> float {
		float along = 0.0f;
		float length = 0.0f;
		for (int part = 0; part < 3; part++) {
			float reach = glow[part] - under[pixel * 3 + part];
			along += (shown[pixel * 3 + part] - under[pixel * 3 + part]) * reach;
			length += reach * reach;
		}
		return(std::clamp(along / std::max(length, 1.0f), 0.0f, 1.0f));
	};

	if (glowing > 0) {
		for (int part = 0; part < 3; part++) {
			glow[part] /= (float)glowing;
		}
		for (int round = 0; round < 40; round++) {
			float sum[3] = {0.0f, 0.0f, 0.0f};
			float weight = 0.0f;
			for (std::size_t pixel = 0; pixel < count; pixel++) {
				if (after[pixel] == clean[pixel] || solid[pixel]) {
					continue;
				}
				float share = share_of(pixel);
				for (int part = 0; part < 3; part++) {
					sum[part] += share * (shown[pixel * 3 + part] - (1.0f - share) * under[pixel * 3 + part]);
				}
				weight += share * share;
			}
			if (weight < 0.001f) {
				break;
			}
			for (int part = 0; part < 3; part++) {
				glow[part] = std::clamp(sum[part] / weight, 0.0f, 255.0f);
			}
		}

		/*
		 * A pixel says more about the glow the further its background is from the glow's
		 * color. What the letters and their shadow cover says nothing, and is filled in
		 * from a wider neighborhood.
		 */
		std::vector<float> shares(count, 0.0f);
		std::vector<float> trust(count, 0.0f);
		for (std::size_t pixel = 0; pixel < count; pixel++) {
			if (solid[pixel]) {
				continue;
			}
			for (int part = 0; part < 3; part++) {
				float reach = glow[part] - under[pixel * 3 + part];
				trust[pixel] += reach * reach;
			}
			shares[pixel] = (after[pixel] != clean[pixel]) ? share_of(pixel) * trust[pixel] : 0.0f;
		}
		std::vector<float> wideshares = shares;
		std::vector<float> widetrust = trust;
		Soften(shares, width, height, 1.0f);
		Soften(trust, width, height, 1.0f);
		Soften(wideshares, width, height, 2.5f);
		Soften(widetrust, width, height, 2.5f);

		label->Glow.resize(count);
		for (std::size_t pixel = 0; pixel < count; pixel++) {
			float share = 0.0f;
			if (!solid[pixel] && trust[pixel] > 1.0f) {
				share = shares[pixel] / trust[pixel];
			} else if (widetrust[pixel] > 0.01f) {
				share = wideshares[pixel] / widetrust[pixel];
			}

			// A glow that reaches the picture's edge is cut off there, so it is faded out
			// over the last pixels before the edge.
			int x = (int)(pixel % (std::size_t)width);
			int y = (int)(pixel / (std::size_t)width);
			int inside = std::min({x, y, width - 1 - x, height - 1 - y});
			share *= std::min((float)(inside + 1) / (float)GLOW_FADE, 1.0f);
			label->Glow[pixel] = (unsigned char)std::lround(std::clamp(share, 0.0f, 1.0f) * 255.0f);
		}
		label->GlowColor = (unsigned short)(((int)std::lround(glow[0] * 31.0f / 255.0f) << 11) | ((int)std::lround(glow[1] * 63.0f / 255.0f) << 5) | (int)std::lround(glow[2] * 31.0f / 255.0f));
	}

	/*
	 * The whole pixels of a line's letters give its box. The letters' soft edges reach on
	 * into the pixels around it by the share of the letters' color those hold, between the
	 * glow's color and the letters'.
	 */
	auto cover_of = [&](int x, int y) -> float {
		if (x < 0 || y < 0 || x >= width || y >= height) {
			return(0.0f);
		}
		std::size_t pixel = (std::size_t)y * width + x;
		float along = 0.0f;
		float length = 0.0f;
		for (int part = 0; part < 3; part++) {
			float reach = letter[part] - glow[part];
			along += (shown[pixel * 3 + part] - glow[part]) * reach;
			length += reach * reach;
		}
		return((after[pixel] != clean[pixel]) ? std::clamp(along / std::max(length, 1.0f), 0.0f, 1.0f) : 0.0f);
	};

	for (std::size_t index = 0; index < boxes.size(); index++) {
		Rect const & box = boxes[index];
		LabelLine & line = label->Lines[index];
		float left = 0.0f;
		float right = 0.0f;
		float top = 0.0f;
		float bottom = 0.0f;
		for (int y = box.Y; y < box.Y + box.Height; y++) {
			left = std::max(left, cover_of(box.X - 1, y));
			right = std::max(right, cover_of(box.X + box.Width, y));
		}
		for (int x = box.X; x < box.X + box.Width; x++) {
			top = std::max(top, cover_of(x, box.Y - 1));
			bottom = std::max(bottom, cover_of(x, box.Y + box.Height));
		}
		line.Left = (float)box.X - left;
		line.Right = (float)(box.X + box.Width) + right;
		line.Top = (float)box.Y - top;
		line.Bottom = (float)(box.Y + box.Height) + bottom;

		// Lettering whose shape the title face cannot take says something else than the text.
		InkBox ink;
		if (!Measure_Ink(line.Text.c_str(), LABEL_MEASURE_SIZE, 0, SCALED_FACE_TITLE, ink)) {
			return(nullptr);
		}
		float stretch = (line.Right - line.Left) / (line.Bottom - line.Top) * (float)ink.Height / (float)ink.Width;
		if (stretch * 100.0f < (float)LABEL_NARROWEST || stretch * 100.0f > (float)LABEL_WIDEST) {
			return(nullptr);
		}
	}

	return(label);
}


/// <summary>
/// Draws a picture that has lettering painted into it and remembers the lettering, so it is
/// drawn in the title face over the picture's own glow when the surface is enlarged. The
/// picture is drawn as it is when the lettering cannot be made out as the text given.
/// </summary>
/// <param name="area">Where the picture goes, at its own size.</param>
/// <param name="text">What the lettering says, a line of it after the other.</param>
void Sharp_Text_Draw_Picture(Surface & surface, Rect const & area, Surface const & picture, char const * text)
{
	bool wanted = text != nullptr && *text != '\0' && Sharp_Text_Wanted(surface) && Frame_Scale() > 1 && Scaled_Face_Ready(SCALED_FACE_TITLE);
	wanted = wanted && Intersect(area, surface.Get_Rect()) == area && picture.Get_Width() == area.Width && picture.Get_Height() == area.Height;

	TextRecord record;
	if (wanted) {
		wanted = Copy_Out(surface, area, record.Clean);
	}
	if (wanted) {

		// What an earlier text or picture left here is not the background; that one holds it.
		std::vector<TextRecord> & records = *Records_For(surface);
		for (std::vector<TextRecord>::reverse_iterator other = records.rbegin(); other != records.rend(); ++other) {
			Rect shared = Intersect(area, other->Bounds);
			if (!shared.Is_Valid() || other->Clean.size() != other->After.size()) {
				continue;
			}
			for (int y = shared.Y; y < shared.Y + shared.Height; y++) {
				for (int x = shared.X; x < shared.X + shared.Width; x++) {
					std::size_t pixel = (std::size_t)(y - area.Y) * area.Width + x - area.X;
					std::size_t previous = (std::size_t)(y - other->Bounds.Y) * other->Bounds.Width + x - other->Bounds.X;
					if (record.Clean[pixel] == other->After[previous]) {
						record.Clean[pixel] = other->Clean[previous];
					}
				}
			}
		}
	}

	surface.Blit_From(area, picture, picture.Get_Rect());
	if (!wanted || !Copy_Out(surface, area, record.After) || record.After == record.Clean) {
		return;
	}

	record.Label = Read_Label(record.Clean, record.After, area.Width, area.Height, text);
	if (record.Label == nullptr) {
		return;
	}
	record.Bounds = area;
	record.Clip = surface.Get_Rect();
	record.LabelOrigin = area.Top_Left();
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
