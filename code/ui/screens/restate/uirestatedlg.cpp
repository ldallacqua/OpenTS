/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "_mixfile.h"
#include "_ui.h"
#include "audio/audioengine.h"
#include "mixfile.h"
#include "ui/screens/restate/uirestate.h"
#include "ui/uienginehost.h"
#include "ui/uishell.h"
#include "ui/uiview.h"
#include "widepicture.h"


namespace
{

class UIRestateEngineServiceClass : public UIRestateServiceClass
{
	public:
		virtual void Bleep(void) override
		{
			void const * sample = MFCD::Retrieve("BLEEP1.AUD");
			if (sample != NULL) {
				AudioEngine.Play_Sample(sample, AUDIO_GROUP_SFX, 64.0f / 255.0f, 10);
			}
		}

		virtual std::string Wide_Background(float & shape) override
		{
			std::string name = Wide_Picture_Name("SCORE.PCX");
			int width = 0;
			int height = 0;
			if (name.empty() || !Wide_Picture_Size(name.c_str(), width, height)) {
				return(std::string());
			}

			shape = (float)width / (float)height;
			return(name);
		}
};

}


bool UI_Restate_Mission(char const * text, bool video)
{
	UIRestateEngineServiceClass service;
	UIRestatePresenterClass presenter(service, UIShell.Clock(), (text != NULL) ? text : "", video);
	std::unique_ptr<UIViewClass> view = UI_Restate_View(presenter);
	UIResult result = UI_Run_Modal(*view);

	return(result == UI_RESULT_ACCEPTED && presenter.ChoseVideo);
}
