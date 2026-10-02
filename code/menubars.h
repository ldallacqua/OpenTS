/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once


bool Menu_Bars_Wanted(int columns, int rows);
void Menu_Bars_Draw(unsigned short const * frame, int columns, int rows, int stride, unsigned short * pixels, int width, int height);
