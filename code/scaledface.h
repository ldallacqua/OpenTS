/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>


// One character of the scalable face, drawn at one size.
struct ScaledGlyph
{
	// From the pen position to the left column of the picture, and from the baseline up to
	// its top row.
	int Left = 0;
	int Top = 0;

	int Width = 0;
	int Height = 0;

	// How far the pen moves, in 64ths of a pixel.
	int Advance = 0;

	// One byte a pixel, row after row, from 0 for untouched to 255 for covered.
	std::vector<std::uint8_t> Coverage;
};


// The faces text is drawn in: the one of dialogs, labels and messages, the geometric one the
// menu pictures are lettered in, and the heavy one of the score screen's side names.
enum ScaledFaceType {
	SCALED_FACE_TEXT,
	SCALED_FACE_TITLE,
	SCALED_FACE_HEAVY,

	SCALED_FACE_COUNT
};


bool Scaled_Face_Ready(ScaledFaceType face = SCALED_FACE_TEXT);
int Scaled_Face_Size_For_Capital(int height);
ScaledGlyph const * Scaled_Face_Glyph(char32_t code, int size, int width = 0, ScaledFaceType face = SCALED_FACE_TEXT);
int Scaled_Face_String_Width(char const * text, int size);
