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


Rect Menu_Bars_Art(int columns, int rows);
void Menu_Bars_Draw(unsigned short const * art, int artwidth, int artheight, int artstride, unsigned short * pixels, int width, int height, int stride, Rect const & shown);
