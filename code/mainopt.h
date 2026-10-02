/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

bool Change_Display_Mode(int width, int height, int window_width, int window_height, int interface_scale, int view_scale);
bool Enter_Menu_Frame(void);
bool Enter_Game_Frame(void);
void Request_View_Zoom(int steps);
bool Service_View_Zoom(void);
void Main_Options_Dialog(void);
