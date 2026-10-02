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

#include <string>
#include <vector>

class Surface;


std::string Wide_Picture_Name(char const * picture);
bool Wide_Picture_Size(char const * name, int & width, int & height);
bool Wide_Picture_Read(char const * name, std::vector<unsigned char> & rgba, int & width, int & height, bool premultiply);

void Wide_Picture_Note(std::string const & name, Surface const & surface, Rect const & area);
bool Wide_Picture_Draw(unsigned short const * frame, int columns, int rows, int framestride, Rect const & art, unsigned short * pixels, int width, int height, Rect const & shown, int const * across, unsigned char const * acrossshare, int const * down, unsigned char const * downshare);
