/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "ui/uiscreen.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class UIViewClass;


class UIRestateServiceClass
{
	public:
		virtual ~UIRestateServiceClass(void) = default;
		virtual void Bleep(void) = 0;

		// The file name of the background's wide stand-in and its width over its height, or
		// no name when there is none.
		virtual std::string Wide_Background(float & shape) { return(std::string()); }
};


class UIRestatePresenterClass : public UIPresenterClass
{
	public:
		enum {
			START_DELAY = 144,
			STEP_DELAY = 64,
		};

		UIRestatePresenterClass(UIRestateServiceClass & service, UIClockClass & clock, std::string text, bool video);
		void Lay_Out(std::function<int(std::string const &)> const & width, int line_width, int page_lines);
		virtual void Execute(UIIntent const & intent) override;
		virtual void Refresh(void) override;
		std::vector<std::string> const & Lines(void) const;
		int Frame_Of(int line) const;	// -1 before its turn, then 0 to 2
		std::string Wide_Background(float & shape) { return(Service.Wide_Background(shape)); }

		std::vector<std::vector<std::string>> Pages;
		int Page = 0;
		int Generation = 0;
		bool LaidOut = false;
		bool Typing = false;
		bool More = false;
		bool Done = false;
		bool Video;
		bool ChoseVideo = false;

	private:
		void Start_Page(int page);
		void Step(void);
		void Finish_Page(void);

		UIRestateServiceClass & Service;
		UIClockClass & Clock;
		std::string Text;
		int Steps = 0;
		std::optional<int> Due;
};


std::unique_ptr<UIViewClass> UI_Restate_View(UIRestatePresenterClass & presenter);

bool UI_Restate_Mission(char const * text, bool video);
