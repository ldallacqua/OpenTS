/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "mainopt.h"

#include "_map.h"
#include "_mixfile.h"
#include "_rect.h"
#include "_surface.h"
#include "_tactica.h"
#include "_xmouse.h"
#include "audio/audioengine.h"
#include "convert.h"
#include "data.h"
#include "dbgprint.h"
#include "dsurface.h"
#include "gamedlg.h"
#include "globals.h"
#include "init.h"
#include "interfacescale.h"
#include "language/language.h"
#include "misc.h"
#include "mixfile.h"
#include "msgbox.h"
#include "newmenu.h"
#include "sdl/sdlwindow.h"
#include "sidebar.h"
#include "sounddlg.h"
#include "stimer.h"
#include "surface.h"
#include "tactical.h"
#include "ui/screens/display/uidisplay.h"
#include "ui/screens/mainopt/uimainopt.h"
#include "video.h"

#include "color.hh"

#include <algorithm>
#include <optional>


bool Test_Display_Mode_Dialog(int width, int height);
static void Display_Options_Dialog(void);

static int _PendingZoomSteps = 0;

// Has the player zoomed since the settings were last written?
static bool _ZoomUnsaved = false;


/// <summary>
/// Brings up the main options dialog.
/// Opens the sound, display, keyboard and game settings screens on request until the player
/// backs out. A resolution change is offered as a trial first, and the settings are written
/// out when the player leaves.
/// </summary>
/// <remarks>Game logic is suspended for the duration of this routine.</remarks>
void Main_Options_Dialog(void)
{
	bool old_game_active = GameActive;
	GameActive = false;

	while (true) {
		switch (UI_Main_Options_Dialog()) {
			case UI_MAIN_OPTIONS_SOUND:
				SoundControlsClass().Dialog();
				break;

			case UI_MAIN_OPTIONS_DISPLAY:
				Display_Options_Dialog();
				break;

			case UI_MAIN_OPTIONS_KEYBOARD:
				Options.Hotkey_Dialog();
				break;

			case UI_MAIN_OPTIONS_SETTINGS:
				GameControlsClass().Dialog();
				break;

			default:
				Options.Save_Settings();
				GameActive = old_game_active;
				return;
		}
	}
}


/// <summary>
/// Replaces every drawing surface but the visible one with a set sized for the current frame
/// and its multiples, so any pointer held across this call is stale.
/// </summary>
/// <returns>Returns with the rectangle the tactical view is laid out to.</returns>
static Rect Lay_Out_Surfaces(void)
{
	int width = Tactical_Layout_Width();
	int height = Tactical_Layout_Height();

	Allocate_Surfaces(VisibleRect, Rect(0, 0, width, height), Rect(0, 0, width, height), Rect(0, 0, SidebarClass::SIDE_WIDTH, Sidebar_Layout_Height()));
	LogicalSurface = HiddenSurface;

	return(Rect(((Options.IsSidebarOnRight || Debug_Map) ? 0 : SidebarClass::SIDE_WIDTH), 16, width, height - 16));
}


/// <summary>
/// Switches the game over to a new render resolution.
/// Every drawing surface is destroyed and recreated at the new size, so any pointer held
/// across this call is stale.
/// </summary>
/// <param name="width">The width to render at.</param>
/// <param name="height">The height to render at.</param>
/// <param name="window_width">The width of the resolution the frame divides, which a window
/// that tracks the frame takes.</param>
/// <param name="window_height">The height of that resolution.</param>
/// <param name="interface_scale">How many pixels of the resolution a pixel of the interface's
/// artwork is to cover in the new frame.</param>
/// <param name="view_scale">How many pixels of the resolution a pixel of the battlefield is to
/// cover in the new frame.</param>
/// <returns>bool; Was the mode changed? If not, nothing has been disturbed.</returns>
bool Change_Display_Mode(int width, int height, int window_width, int window_height, int interface_scale, int view_scale)
{
	DebugString("About to set video mode\n");

	Hide_Mouse();

	int old_frame_scale = Frame_Scale();
	int old_interface_scale = Frame_Interface_Scale();
	int old_view_scale = Frame_View_Scale();
	Set_Frame_Scales(window_width / width, interface_scale, view_scale);

	if (!Video_Set_Mode(width, height)) {
		DebugString("Video_Set_Mode failed.\n");
		Set_Frame_Scales(old_frame_scale, old_interface_scale, old_view_scale);
		Show_Mouse();
		return(false);
		}

	VisibleRect = Rect(0, 0, width, height);
	DebugString("VisibleRect: %dx%d\n", width, height);

	if (VisibleSurface != NULL) {
		delete VisibleSurface;
		VisibleSurface = NULL;
	}

	if (AlternateSurface != NULL) {
		delete AlternateSurface;
		AlternateSurface = NULL;
	}

	if (HiddenSurface != NULL) {
		delete HiddenSurface;
		HiddenSurface = NULL;
	}

	if (TileSurface != NULL) {
		delete TileSurface;
		TileSurface = NULL;
	}

	if (SidebarSurface != NULL) {
		delete SidebarSurface;
		SidebarSurface = NULL;
	}

	if (CompositeSurface != NULL) {
		delete CompositeSurface;
		CompositeSurface = NULL;
	}

	VisibleSurface = DSurface::Create_Primary();
	if (VisibleSurface == NULL) {
		Show_Mouse();
		return(false);
	}

	/*
	 * A window that is tracking the frame takes the size of the resolution, and keeps it
	 * while the menu frame is scaled into it. One the player sized themselves, and a window
	 * covering the screen, both stay as they are and the frame is scaled into them instead.
	 */
	if (WindowedMode && Options.WindowWidth <= 0 && Options.WindowHeight <= 0) {
		// The window grows about its middle, so the picture stays where the player was looking.
		Main_Window_Resize(window_width, window_height);
	}

	Map.Set_View_Dimensions(Lay_Out_Surfaces());

	Map.Init_IO();
	Map.Activate(
#ifdef _DEBUG
		Debug_Map == true ? 1 : 0
#else
		1
#endif
	);
	Map.Reposition_Sidebar();
	Map.Flag_To_Redraw(GS_REDRAW_ALL);
	Show_Mouse();

	DebugString("Mode change complete.\n");

	return(true);
}


/// <summary>
/// Moves the game into the frame the menus and the screens between missions are drawn in.
/// Every drawing surface is replaced when the frame changes, so any pointer held across this
/// call is stale. The settings are written first if the player zoomed during the match, so
/// the next match opens at the same zoom.
/// </summary>
/// <returns>bool; Did the frame change? It does not when the menu frame is already in use,
/// which is always the case while none of the three multiples is above one.</returns>
bool Enter_Menu_Frame(void)
{
	if (_ZoomUnsaved) {
		_ZoomUnsaved = false;
		Options.Save_Settings();
	}

	int width = Menu_Frame_Width();
	int height = Menu_Frame_Height();
	int scale = Menu_Scale();

	if (VisibleRect.Width == width && VisibleRect.Height == height && Frame_Scale() == scale && Frame_Interface_Scale() == scale && Frame_View_Scale() == scale) {
		return(false);
	}
	return(Change_Display_Mode(width, height, Options.ScreenWidth, Options.ScreenHeight, scale, scale));
}


/// <summary>
/// Moves the game into the frame a match is drawn in. Every drawing surface is replaced when
/// the frame changes, so any pointer held across this call is stale.
/// </summary>
/// <returns>bool; Did the frame change? It does not when the game frame is already in use,
/// which is always the case while none of the three multiples is above one.</returns>
bool Enter_Game_Frame(void)
{
	int width = Game_Frame_Width();
	int height = Game_Frame_Height();

	if (VisibleRect.Width == width && VisibleRect.Height == height && Frame_Scale() == 1 && Frame_Interface_Scale() == Interface_Scale() && Frame_View_Scale() == View_Scale()) {
		return(false);
	}
	return(Change_Display_Mode(width, height, Options.ScreenWidth, Options.ScreenHeight, Interface_Scale(), View_Scale()));
}


/// <summary>
/// Asks for the battlefield to be shown larger or smaller. Service_View_Zoom makes the change.
/// </summary>
/// <param name="steps">How many multiples to add to the view's; negative to zoom out.</param>
void Request_View_Zoom(int steps)
{
	_PendingZoomSteps += steps;
}


/// <summary>
/// Makes the zoom changes asked for since the last call, during a match. The ViewScale setting
/// takes the new multiple, and the spot of the battlefield under the pointer stays under it.
/// Every drawing surface but the visible one is replaced when the zoom changes, so call this
/// only where no pointer to one is held.
/// </summary>
/// <returns>bool; Did the zoom change? It does not past the largest and smallest multiples
/// the resolution allows.</returns>
bool Service_View_Zoom(void)
{
	int steps = _PendingZoomSteps;
	_PendingZoomSteps = 0;

	if (steps == 0 || !GameActive || !ScenarioActive || Debug_Map || TacticalMap == NULL || Frame_Scale() != 1) {
		return(false);
	}

	Options.ViewScale = std::max(Frame_View_Scale() + steps, 1);
	Options.ViewScale = View_Scale();
	if (Options.ViewScale == Frame_View_Scale()) {
		return(false);
	}

	Point2D pointer(Get_Mouse_X(), Get_Mouse_Y());
	bool anchored = TacticalRect.Is_Point_Within(pointer);
	Point2D offset = pointer - TacticalRect.Top_Left() - Point2D(TacticalRect.Width / 2, TacticalRect.Height / 2);
	Point2D centre = TacticalMap->Get_Tactical_Position();
	Layout_Point_To_Frame(pointer);

	Hide_Mouse();
	Set_Frame_Scales(Frame_Scale(), Frame_Interface_Scale(), Options.ViewScale);
	Map.Set_View_Dimensions(Lay_Out_Surfaces());

	if (anchored) {
		Frame_Point_To_Layout(pointer);
		offset -= pointer - TacticalRect.Top_Left() - Point2D(TacticalRect.Width / 2, TacticalRect.Height / 2);
		TacticalMap->Set_Tactical_Position(centre + offset);
	}

	Map.Flag_To_Redraw(GS_REDRAW_ALL);
	Show_Mouse();

	_ZoomUnsaved = true;
	DebugString("View zoom is now %d\n", Options.ViewScale);
	return(true);
}


/// <summary>
/// Tries a display mode out and asks the player to confirm it.
/// This routine switches to the requested mode and puts up a confirmation dialog. If the
/// player does not accept the mode -- or says nothing at all, because a bad mode may well
/// leave the screen unreadable -- the previous resolution is restored.
/// </summary>
/// <param name="width">The width of the display mode to try.</param>
/// <param name="height">The height of the display mode to try.</param>
/// <returns>bool; Was the new display mode accepted and left in place?</returns>
bool Test_Display_Mode_Dialog(int width, int height)
{
	DebugString("Testing display mode @ %dx%d\n", width, height);
	Hide_Mouse();
	HiddenSurface->Fill(TBLACK);
	Update_Visible_Surface();

	// The trial runs in the menus, so it shows the menu frame the new resolution would have.
	int scale = Menu_Scale_For(width, height);

	if (!Change_Display_Mode(Menu_Frame_Width_For(width, height), Menu_Frame_Height_For(width, height), width, height, scale, scale)) {
		return(false);
	}

	HiddenSurface->Fill(TBLACK);
	Update_Visible_Surface();
	Show_Mouse();
	Draw_Menu_Background();

	if (!UI_Confirm_Mode_Dialog()) {
		DebugString("Resetting display mode @ %dx%d\n", Options.ScreenWidth, Options.ScreenHeight);
		Change_Display_Mode(Menu_Frame_Width(), Menu_Frame_Height(), Options.ScreenWidth, Options.ScreenHeight, Menu_Scale(), Menu_Scale());
		LogicalSurface = HiddenSurface;

		Draw_Menu_Background();
		return(false);
	}

	DebugString("Keeping display mode @ %dx%d\n", width, height);
	LogicalSurface = HiddenSurface;
	return(true);
}


/// <summary>
/// Shows the display options until the player leaves them or keeps a new display mode.
/// A picked mode is applied as a trial; if the player does not confirm it, the screen opens
/// again.
/// </summary>
static void Display_Options_Dialog(void)
{
	while (true) {
		std::optional<UIDisplayMode> picked = UI_Display_Dialog();
		if (!picked.has_value()) {
			break;
		}

		if (WWMessageBox().Process(TXT_ABOUT_TO_TRY_MODE, TXT_OK, TXT_CANCEL) != 0) {
			break;
		}
		if (Test_Display_Mode_Dialog(picked->Width, picked->Height)) {
			Options.ScreenWidth = picked->Width;
			Options.ScreenHeight = picked->Height;
			break;
		}
	}
}
