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

// The size the capital is measured at, large enough that rounding does not show.
static const int MEASURE_SIZE = 256;

static bool _Tried = false;
static FT_Library _Library = NULL;
static FT_Face _Face = NULL;
static std::vector<unsigned char> _FaceData;
static float _CapitalShare = 0.7f;
static int _Size = 0;
static std::unordered_map<std::uint64_t, ScaledGlyph> _Glyphs;


static bool Read_Face(std::string const & path)
{
	FILE * file = std::fopen(path.c_str(), "rb");
	if (file == NULL) {
		return(false);
	}

	std::fseek(file, 0, SEEK_END);
	long size = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);

	_FaceData.resize(size > 0 ? (std::size_t)size : 0);
	bool read = size > 0 && std::fread(_FaceData.data(), 1, _FaceData.size(), file) == _FaceData.size();
	std::fclose(file);

	if (!read || FT_New_Memory_Face(_Library, _FaceData.data(), (FT_Long)_FaceData.size(), 0, &_Face) != 0) {
		_Face = NULL;
		_FaceData.clear();
		return(false);
	}

	DebugString("Scaled face: %s\n", path.c_str());
	return(true);
}


static bool Set_Size(int size)
{
	if (size == _Size) {
		return(true);
	}
	if (FT_Set_Pixel_Sizes(_Face, 0, (FT_UInt)size) != 0) {
		_Size = 0;
		return(false);
	}
	_Size = size;
	return(true);
}


/// <summary>
/// Loads the scalable face on the first call. The face stays loaded until the game exits.
/// </summary>
/// <returns>bool; Is there a face to draw with? None of the faces being readable is
/// remembered, so later calls do not try again.</returns>
bool Scaled_Face_Ready(void)
{
	if (_Tried) {
		return(_Face != NULL);
	}
	_Tried = true;

	if (FT_Init_FreeType(&_Library) != 0) {
		_Library = NULL;
		DebugString("Scaled face: FreeType did not start\n");
		return(false);
	}

	char directory[MAX_PATH];
	unsigned int length = GetWindowsDirectoryA(directory, MAX_PATH);
	if (length > 0 && length < MAX_PATH) {
		for (char const * const name : SYSTEM_FACES) {
			if (Read_Face(std::string(directory) + "\\Fonts\\" + name)) {
				break;
			}
		}
	}

	if (_Face == NULL) {
		char program[MAX_PATH];
		length = GetModuleFileNameA(NULL, program, MAX_PATH);
		if (length > 0 && length < MAX_PATH) {
			std::string folder(program);
			Read_Face(folder.substr(0, folder.find_last_of('\\') + 1) + SHIPPED_FACE);
		}
	}

	if (_Face == NULL) {
		DebugString("Scaled face: no face could be read\n");
		return(false);
	}

	if (Set_Size(MEASURE_SIZE) && FT_Load_Char(_Face, 'H', FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) == 0 && _Face->glyph->metrics.height > 0) {
		_CapitalShare = (float)_Face->glyph->metrics.height / 64.0f / (float)MEASURE_SIZE;
	}
	return(true);
}


/// <returns>The size at which the face's capital letters are this many pixels tall.</returns>
int Scaled_Face_Size_For_Capital(int height)
{
	int size = (int)((float)height / _CapitalShare + 0.5f);
	return(size < 1 ? 1 : size);
}


/// <returns>The character drawn at the size, or NULL when there is no face or the character
/// cannot be drawn. The glyph stays valid until the game exits.</returns>
ScaledGlyph const * Scaled_Face_Glyph(char32_t code, int size)
{
	if (!Scaled_Face_Ready() || size < 1) {
		return(NULL);
	}

	std::uint64_t key = ((std::uint64_t)size << 32) | (std::uint32_t)code;
	std::unordered_map<std::uint64_t, ScaledGlyph>::const_iterator found = _Glyphs.find(key);
	if (found != _Glyphs.end()) {
		return(&found->second);
	}

	if (!Set_Size(size) || FT_Load_Char(_Face, (FT_ULong)code, FT_LOAD_RENDER) != 0) {
		return(NULL);
	}

	FT_GlyphSlot slot = _Face->glyph;
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

	return(&_Glyphs.emplace(key, std::move(glyph)).first->second);
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
