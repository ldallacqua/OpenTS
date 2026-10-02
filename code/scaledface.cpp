/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "scaledface.h"

#include "dbgprint.h"
#include "utf8.h"
#include "win.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>


// The faces tried in the Windows font folder, in order, before the one shipped with the game.
static char const * const SYSTEM_FACES[] = {
	"seguisb.ttf",
	"segoeui.ttf",
	"arial.ttf",
};
static char const * const SHIPPED_FACE = "ui\\Arimo.ttf";

// The title face is only ever the one shipped with the game.
static char const * const TITLE_FACE = "ui\\texgyreadventor-bold.otf";

// The size the capital is measured at, large enough that rounding does not show.
static const int MEASURE_SIZE = 256;

struct LoadedFace
{
	bool Tried = false;
	FT_Face Face = NULL;
	std::vector<unsigned char> Data;
	int Size = 0;
	int Width = 0;
	std::unordered_map<std::uint64_t, ScaledGlyph> Glyphs;
};

static bool _LibraryTried = false;
static FT_Library _Library = NULL;
static LoadedFace _Faces[SCALED_FACE_COUNT];
static float _CapitalShare = 0.7f;


static bool Read_Face(LoadedFace & face, std::string const & path)
{
	FILE * file = std::fopen(path.c_str(), "rb");
	if (file == NULL) {
		return(false);
	}

	std::fseek(file, 0, SEEK_END);
	long size = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);

	face.Data.resize(size > 0 ? (std::size_t)size : 0);
	bool read = size > 0 && std::fread(face.Data.data(), 1, face.Data.size(), file) == face.Data.size();
	std::fclose(file);

	if (!read || FT_New_Memory_Face(_Library, face.Data.data(), (FT_Long)face.Data.size(), 0, &face.Face) != 0) {
		face.Face = NULL;
		face.Data.clear();
		return(false);
	}

	DebugString("Scaled face: %s\n", path.c_str());
	return(true);
}


static bool Read_Shipped_Face(LoadedFace & face, char const * name)
{
	char program[MAX_PATH];
	unsigned int length = GetModuleFileNameA(NULL, program, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return(false);
	}

	std::string folder(program);
	return(Read_Face(face, folder.substr(0, folder.find_last_of('\\') + 1) + name));
}


static bool Set_Size(LoadedFace & face, int size, int width)
{
	if (size == face.Size && width == face.Width) {
		return(true);
	}
	if (FT_Set_Pixel_Sizes(face.Face, (FT_UInt)width, (FT_UInt)size) != 0) {
		face.Size = 0;
		return(false);
	}
	face.Size = size;
	face.Width = width;
	return(true);
}


/// <summary>
/// Loads the face on the first call for it. The face stays loaded until the game exits.
/// </summary>
/// <returns>bool; Is there a face to draw with? A face that could not be read is
/// remembered, so later calls do not try again.</returns>
bool Scaled_Face_Ready(ScaledFaceType which)
{
	LoadedFace & face = _Faces[which];
	if (face.Tried) {
		return(face.Face != NULL);
	}
	face.Tried = true;

	if (!_LibraryTried) {
		_LibraryTried = true;
		if (FT_Init_FreeType(&_Library) != 0) {
			_Library = NULL;
			DebugString("Scaled face: FreeType did not start\n");
		}
	}
	if (_Library == NULL) {
		return(false);
	}

	if (which == SCALED_FACE_TITLE) {
		if (!Read_Shipped_Face(face, TITLE_FACE)) {
			DebugString("Scaled face: the title face could not be read\n");
		}
		return(face.Face != NULL);
	}

	char directory[MAX_PATH];
	unsigned int length = GetWindowsDirectoryA(directory, MAX_PATH);
	if (length > 0 && length < MAX_PATH) {
		for (char const * const name : SYSTEM_FACES) {
			if (Read_Face(face, std::string(directory) + "\\Fonts\\" + name)) {
				break;
			}
		}
	}

	if (face.Face == NULL) {
		Read_Shipped_Face(face, SHIPPED_FACE);
	}

	if (face.Face == NULL) {
		DebugString("Scaled face: no face could be read\n");
		return(false);
	}

	if (Set_Size(face, MEASURE_SIZE, 0) && FT_Load_Char(face.Face, 'H', FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) == 0 && face.Face->glyph->metrics.height > 0) {
		_CapitalShare = (float)face.Face->glyph->metrics.height / 64.0f / (float)MEASURE_SIZE;
	}
	return(true);
}


/// <returns>The size at which the face's capital letters are this many pixels tall.</returns>
int Scaled_Face_Size_For_Capital(int height)
{
	int size = (int)((float)height / _CapitalShare + 0.5f);
	return(size < 1 ? 1 : size);
}


/// <param name="width">The size that sets the character's width, for a face drawn wider or
/// narrower than it is designed; 0 keeps the designed width.</param>
/// <param name="which">The face to draw in.</param>
/// <returns>The character drawn at the size, or NULL when there is no face or the character
/// cannot be drawn. The glyph stays valid until the game exits.</returns>
ScaledGlyph const * Scaled_Face_Glyph(char32_t code, int size, int width, ScaledFaceType which)
{
	if (!Scaled_Face_Ready(which) || size < 1 || size > 0xFFFF || width < 0 || width > 0xFFFF || code > 0x10FFFF) {
		return(NULL);
	}
	if (width == size) {
		width = 0;
	}

	LoadedFace & face = _Faces[which];
	std::uint64_t key = ((std::uint64_t)width << 48) | ((std::uint64_t)size << 32) | (std::uint32_t)code;
	std::unordered_map<std::uint64_t, ScaledGlyph>::const_iterator found = face.Glyphs.find(key);
	if (found != face.Glyphs.end()) {
		return(&found->second);
	}

	if (!Set_Size(face, size, width) || FT_Load_Char(face.Face, (FT_ULong)code, FT_LOAD_RENDER) != 0) {
		return(NULL);
	}

	FT_GlyphSlot slot = face.Face->glyph;
	if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
		return(NULL);
	}

	ScaledGlyph glyph;
	glyph.Left = slot->bitmap_left;
	glyph.Top = slot->bitmap_top;
	glyph.Width = (int)slot->bitmap.width;
	glyph.Height = (int)slot->bitmap.rows;
	glyph.Advance = (int)slot->advance.x;
	glyph.Coverage.resize((std::size_t)glyph.Width * glyph.Height);
	for (int row = 0; row < glyph.Height; row++) {
		unsigned char const * from = slot->bitmap.buffer + (std::ptrdiff_t)row * slot->bitmap.pitch;
		std::copy(from, from + glyph.Width, glyph.Coverage.begin() + (std::size_t)row * glyph.Width);
	}

	return(&face.Glyphs.emplace(key, std::move(glyph)).first->second);
}


/// <returns>How many pixels wide one line of text is at the size.</returns>
int Scaled_Face_String_Width(char const * text, int size)
{
	int width = 0;

	while (text != NULL && *text != '\0') {
		ScaledGlyph const * glyph = Scaled_Face_Glyph(UTF8::Decode(text), size);
		if (glyph != NULL) {
			width += glyph->Advance;
		}
	}
	return((width + 32) >> 6);
}
