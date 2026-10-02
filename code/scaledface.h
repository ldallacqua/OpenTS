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


bool Scaled_Face_Ready(void);
int Scaled_Face_Size_For_Capital(int height);
ScaledGlyph const * Scaled_Face_Glyph(char32_t code, int size, int width = 0);
int Scaled_Face_String_Width(char const * text, int size);
