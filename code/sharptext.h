/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "rect.h"

class ConvertClass;
class Surface;
class WWFontClass;


// Which edge of its position a line of text is anchored to.
enum SharpTextAlign {
	SHARP_TEXT_LEFT,
	SHARP_TEXT_CENTER,
	SHARP_TEXT_RIGHT,
};


bool Sharp_Text_Wanted(Surface const & surface);
void Sharp_Text_Set_Alignment(SharpTextAlign align);
Point2D Sharp_Text_Print(WWFontClass const & font, char const * string, Surface & surface, Rect const & cliprect, Point2D const & point, ConvertClass const & converter, unsigned char const * remap);

void Sharp_Text_Hide(Surface & source, Rect const & sourcerect);
void Sharp_Text_Show(Surface & source, Rect const & sourcerect, Surface & dest, Rect const & destclip, Point2D const & destorigin, int scale, bool draw);
void Sharp_Text_Forget(void);
