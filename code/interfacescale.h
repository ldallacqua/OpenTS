/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The menus, the battlefield and the interface of a match can each be shown at a whole
// multiple of their artwork's size. The menus and the screens between missions are drawn in
// the menu frame, which is the resolution the player chose divided by the menus' multiple and
// is enlarged as a whole when presented. A match is drawn in the game frame, which is that
// resolution itself: the tactical view and the sidebar are each drawn at their artwork's size
// and enlarged onto the frame by their own multiple, and the dialogs and the pointer follow
// the interface's.
//
// The game lays a match out and takes its input in layout coordinates, which count the
// tactical view in battlefield pixels and the sidebar column to its right in sidebar pixels.

#pragma once

#include "point.h"


int Menu_Scale_For(int width, int height);
int Menu_Scale(void);
int Interface_Scale(void);
int View_Scale(void);

int Menu_Frame_Width(void);
int Menu_Frame_Height(void);
int Game_Frame_Width(void);
int Game_Frame_Height(void);

// Call before the frame is set, so the dialogs laid out as it changes use the new multiples.
void Set_Frame_Scales(int frame_scale, int interface_scale, int view_scale);
int Frame_Scale(void);
int Frame_Interface_Scale(void);
int Frame_View_Scale(void);
float Dialog_Scale(void);

int Sidebar_Scale(void);
int Sidebar_Frame_Width(void);
int Sidebar_Layout_Height(void);

int View_Zoom(void);
int Tactical_Frame_Width(void);
int Tactical_Layout_Width(void);
int Tactical_Layout_Height(void);

bool Layout_Scaling_Active(void);

void Frame_Point_To_Layout(Point2D & point);
void Layout_Point_To_Frame(Point2D & point);
