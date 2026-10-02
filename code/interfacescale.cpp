/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "interfacescale.h"

#include "_map.h"
#include "_rect.h"
#include "globals.h"
#include "goptions.h"
#include "misc.h"
#include "rect.h"
#include "sidebar.h"

#include <algorithm>


// Nothing is laid out smaller than it is at 640 by 400.
static const int MIN_FRAME_WIDTH = 640;
static const int MIN_FRAME_HEIGHT = 400;

// How many pixels of the resolution a pixel of the current frame covers.
static int _FrameScale = 1;

// How many pixels of the resolution a pixel of the interface's artwork covers in the current frame.
static int _FrameInterfaceScale = 1;

// How many pixels of the resolution a pixel of the battlefield covers in the current frame.
static int _FrameViewScale = 1;


static int Largest_Scale(int wanted, int width, int height)
{
	int scale = std::max(wanted, 1);

	while (scale > 1 && (width / scale < MIN_FRAME_WIDTH || height / scale < MIN_FRAME_HEIGHT)) {
		scale--;
	}
	return(scale);
}


// With MenuScale at 0 the menu frame is the smallest that holds 640 by 400 in the shape of
// the resolution, so the menu artwork reaches two opposite edges of the screen. The factor
// it is enlarged by is then seldom whole.
static double Menu_Fit_Factor(int width, int height)
{
	if (Options.MenuScale > 0) {
		return(0.0);
	}
	return(std::max(std::min((double)width / MIN_FRAME_WIDTH, (double)height / MIN_FRAME_HEIGHT), 1.0));
}


/// <summary>
/// Fetches how many times larger than their artwork the menus are shown at the given
/// resolution.
/// </summary>
/// <returns>Returns with the MenuScale setting, lowered to the largest multiple that leaves
/// the menu frame at least 640 by 400, and never less than one. With the setting at 0 it is
/// the whole part of the factor that fits 640 by 400 to the resolution.</returns>
int Menu_Scale_For(int width, int height)
{
	double fit = Menu_Fit_Factor(width, height);
	if (fit > 0.0) {
		return((int)fit);
	}
	return(Largest_Scale(Options.MenuScale, width, height));
}


/// <summary>
/// Fetches the width of the frame the menus are drawn in at the given resolution.
/// </summary>
int Menu_Frame_Width_For(int width, int height)
{
	double fit = Menu_Fit_Factor(width, height);
	if (fit > 0.0) {
		return(std::max((int)((double)width / fit + 0.5), MIN_FRAME_WIDTH));
	}
	return(width / Menu_Scale_For(width, height));
}


/// <summary>
/// Fetches the height of the frame the menus are drawn in at the given resolution.
/// </summary>
int Menu_Frame_Height_For(int width, int height)
{
	double fit = Menu_Fit_Factor(width, height);
	if (fit > 0.0) {
		return(std::max((int)((double)height / fit + 0.5), MIN_FRAME_HEIGHT));
	}
	return(height / Menu_Scale_For(width, height));
}


/// <summary>
/// Fetches how many times larger than their artwork the menus are shown at the resolution the
/// player chose.
/// </summary>
int Menu_Scale(void)
{
	return(Menu_Scale_For(Options.ScreenWidth, Options.ScreenHeight));
}


/// <summary>
/// Fetches how many times larger than its artwork the interface of a match is shown at the
/// resolution the player chose.
/// </summary>
/// <returns>Returns with the InterfaceScale setting, lowered to the largest multiple that
/// leaves a dialog the room it has at 640 by 400, and never less than one.</returns>
int Interface_Scale(void)
{
	return(Largest_Scale(Options.InterfaceScale, Options.ScreenWidth, Options.ScreenHeight));
}


/// <summary>
/// Fetches how many times larger than its artwork the battlefield is shown at the resolution
/// the player chose.
/// </summary>
/// <returns>Returns with the ViewScale setting, lowered to the largest multiple that leaves
/// the tactical view the room it has at 640 by 400 beside the sidebar, and never less than
/// one.</returns>
int View_Scale(void)
{
	int scale = std::max(Options.ViewScale, 1);
	int width = Options.ScreenWidth - SidebarClass::SIDE_WIDTH * Interface_Scale();

	while (scale > 1 && (width / scale < MIN_FRAME_WIDTH - SidebarClass::SIDE_WIDTH || Options.ScreenHeight / scale < MIN_FRAME_HEIGHT)) {
		scale--;
	}
	return(scale);
}


/// <summary>
/// Fetches the width of the frame the menus are drawn in.
/// </summary>
int Menu_Frame_Width(void)
{
	return(Menu_Frame_Width_For(Options.ScreenWidth, Options.ScreenHeight));
}


/// <summary>
/// Fetches the height of the frame the menus are drawn in.
/// </summary>
int Menu_Frame_Height(void)
{
	return(Menu_Frame_Height_For(Options.ScreenWidth, Options.ScreenHeight));
}


/// <summary>
/// Fetches the width of the frame a match is drawn in.
/// </summary>
int Game_Frame_Width(void)
{
	return(Options.ScreenWidth);
}


/// <summary>
/// Fetches the height of the frame a match is drawn in.
/// </summary>
int Game_Frame_Height(void)
{
	return(Options.ScreenHeight);
}


/// <summary>
/// Records the multiples of the frame about to be set, which decide how much the dialogs, the
/// pointer, the sidebar and the tactical view are enlarged within that frame.
/// </summary>
/// <param name="frame_scale">How many pixels of the resolution a frame pixel covers.</param>
/// <param name="interface_scale">How many pixels of the resolution a pixel of the interface's
/// artwork is to cover.</param>
/// <param name="view_scale">How many pixels of the resolution a pixel of the battlefield is to
/// cover. The menu frame passes its own multiple for both, since it is enlarged as a
/// whole.</param>
void Set_Frame_Scales(int frame_scale, int interface_scale, int view_scale)
{
	_FrameScale = std::max(frame_scale, 1);
	_FrameInterfaceScale = std::max(interface_scale, 1);
	_FrameViewScale = std::max(view_scale, 1);
}


int Frame_Scale(void)
{
	return(_FrameScale);
}


int Frame_Interface_Scale(void)
{
	return(_FrameInterfaceScale);
}


int Frame_View_Scale(void)
{
	return(_FrameViewScale);
}


/// <summary>
/// Fetches how many times larger than a frame pixel a dialog's unit of length is drawn.
/// </summary>
float Dialog_Scale(void)
{
	return((float)_FrameInterfaceScale / (float)_FrameScale);
}


/// <summary>
/// Fetches how many frame pixels a pixel of the sidebar's artwork covers.
/// </summary>
/// <returns>Returns with the whole number that brings the sidebar nearest to the interface's
/// multiple, the larger of two equally near, lowered to the largest that leaves the tactical
/// view and the sidebar the room they have at 640 by 400. Never less than one.</returns>
int Sidebar_Scale(void)
{
	int scale = std::max((_FrameInterfaceScale * 2 + _FrameScale) / (_FrameScale * 2), 1);

	while (scale > 1 && (VisibleRect.Width - SidebarClass::SIDE_WIDTH * scale < MIN_FRAME_WIDTH - SidebarClass::SIDE_WIDTH || VisibleRect.Height / scale < MIN_FRAME_HEIGHT)) {
		scale--;
	}
	return(scale);
}


/// <summary>
/// Fetches the width of the column of the frame that the sidebar covers.
/// </summary>
int Sidebar_Frame_Width(void)
{
	return(SidebarClass::SIDE_WIDTH * Sidebar_Scale());
}


/// <summary>
/// Fetches the height the sidebar is laid out to, in its own pixels. A frame height that is
/// not a whole multiple of the scale leaves the last row partly off the bottom of the frame.
/// </summary>
int Sidebar_Layout_Height(void)
{
	int scale = Sidebar_Scale();
	return((VisibleRect.Height + scale - 1) / scale);
}


/// <summary>
/// Fetches how many frame pixels a pixel of the battlefield covers. It is one in the map
/// editor.
/// </summary>
int View_Zoom(void)
{
	if (Debug_Map) {
		return(1);
	}
	return(std::max(_FrameViewScale / _FrameScale, 1));
}


/// <summary>
/// Fetches the width of the part of the frame beside the sidebar.
/// </summary>
int Tactical_Frame_Width(void)
{
	return(VisibleRect.Width - Sidebar_Frame_Width());
}


/// <summary>
/// Fetches the width the part of the frame beside the sidebar is laid out to, in battlefield
/// pixels. A width that is not a whole multiple of the zoom leaves the last column partly
/// under the sidebar.
/// </summary>
int Tactical_Layout_Width(void)
{
	int zoom = View_Zoom();
	return((Tactical_Frame_Width() + zoom - 1) / zoom);
}


/// <summary>
/// Fetches the height the frame is laid out to beside the sidebar, in battlefield pixels. A
/// height that is not a whole multiple of the zoom leaves the last row partly off the bottom
/// of the frame.
/// </summary>
int Tactical_Layout_Height(void)
{
	int zoom = View_Zoom();
	return((VisibleRect.Height + zoom - 1) / zoom);
}


/// <summary>
/// Is a match on the screen with its tactical view or its sidebar enlarged, so that frame and
/// layout coordinates differ?
/// </summary>
bool Layout_Scaling_Active(void)
{
	if (Debug_Map || !GameActive || !ScenarioActive) {
		return(false);
	}
	return(View_Zoom() > 1 || (Sidebar_Scale() > 1 && Map.IsSidebarActive));
}


/// <summary>
/// Converts a position in the frame into layout coordinates. A position outside the frame is
/// left alone.
/// </summary>
/// <param name="point">The position to convert in place.</param>
void Frame_Point_To_Layout(Point2D & point)
{
	if (!Layout_Scaling_Active()) {
		return;
	}

	if (point.X < 0 || point.X >= VisibleRect.Width || point.Y < 0 || point.Y >= VisibleRect.Height) {
		return;
	}

	int left = Tactical_Frame_Width();

	if (point.X < left) {
		int zoom = View_Zoom();
		point.X = point.X / zoom;
		point.Y = point.Y / zoom;
	} else {
		int scale = Sidebar_Scale();
		point.X = Tactical_Layout_Width() + (point.X - left) / scale;
		point.Y = point.Y / scale;
	}
}


/// <summary>
/// Converts a position in layout coordinates into the frame. The last row of the tactical
/// view, and the last column and the last row of the sidebar, come back as the frame's last,
/// so a position on the outer edge of the layout stays on the edge of the frame.
/// </summary>
/// <param name="point">The position to convert in place. Any other position comes back at the
/// top left corner of the block of frame pixels it covers.</param>
void Layout_Point_To_Frame(Point2D & point)
{
	if (!Layout_Scaling_Active()) {
		return;
	}

	int columns = Tactical_Layout_Width();

	if (point.X < columns) {
		int zoom = View_Zoom();
		point.X = point.X * zoom;

		if (point.Y >= Tactical_Layout_Height() - 1) {
			point.Y = VisibleRect.Height - 1;
		} else {
			point.Y = point.Y * zoom;
		}
	} else {
		int scale = Sidebar_Scale();

		if (point.X >= columns + SidebarClass::SIDE_WIDTH - 1) {
			point.X = VisibleRect.Width - 1;
		} else {
			point.X = Tactical_Frame_Width() + (point.X - columns) * scale;
		}

		if (point.Y >= Sidebar_Layout_Height() - 1) {
			point.Y = VisibleRect.Height - 1;
		} else {
			point.Y = point.Y * scale;
		}
	}
}
