/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * Tantric 2008-2010
 *
 * menu.cpp
 *
 * Menu flow routines - handles all menu logic
 ***************************************************************************/
#include <xetypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <xenon_soc/xenon_power.h>
#include <ppc/atomic.h>
#include "w_input.h"
#include <sys/stat.h>
#include "emu.h"
#include "video.h"
#include "filebrowser.h"
#include "fileop.h"
#include "sram.h"
#include "freeze.h"
#include "preferences.h"
#include "button_mapping.h"
#include "input.h"
#include "filelist.h"
#include "gui/gui.h"
#include "menu.h"
#include "utils/gettext.h"

#define NO_CONTROLLER_CONFIG
#define NO_CHEAT_CONFIG

#define THREAD_SLEEP 100

#ifdef HW_RVL
static GuiImageData * pointer[4];
#endif

static GuiTrigger * trigA = NULL;
static GuiTrigger * trig2 = NULL;

static GuiButton * btnLogo = NULL;
static GuiImageData * gameScreen = NULL;
static GuiImage * gameScreenImg = NULL;
static GuiImage * bgTopImg = NULL;
static GuiImage * bgBottomImg = NULL;
static GuiSound * bgMusic = NULL;
static GuiSound * enterSound = NULL;
static GuiSound * exitSound = NULL;
static GuiWindow * mainWindow = NULL;
static GuiText * settingText = NULL;
static GuiText * settingText2 = NULL;
static int lastMenu = MENU_NONE;
static int mapMenuCtrl = 0;
static int mapMenuCtrlSNES = 0;

static bool guiHalt = true;
static int showProgress = 0;

static char progressTitle[101];
static char progressMsg[201];
static int progressDone = 0;
static int progressTotal = 0;

static unsigned char xenon_thread_stack[6 * 0x10000];
static unsigned int __attribute__((aligned(128))) _gui_lock = 0;
static unsigned int __attribute__((aligned(128))) _progress_lock = 0;


static void * UGUI();

/****************************************************************************
 * ResumeGui
 *
 * Signals the GUI thread to start, and resumes the thread. This is called
 * after finishing the removal/insertion of new elements, and after initial
 * GUI setup.
 ***************************************************************************/
static void
ResumeGui() {
	lock(&_gui_lock);
	guiHalt = false;
	unlock(&_gui_lock);
}

/****************************************************************************
 * HaltGui
 *
 * Signals the GUI thread to stop, and waits for GUI thread to stop
 * This is necessary whenever removing/inserting new elements into the GUI.
 * This eliminates the possibility that the GUI is in the middle of accessing
 * an element that is being changed.
 ***************************************************************************/
static void
HaltGui() {
	lock(&_gui_lock);
	guiHalt = true;
	unlock(&_gui_lock);
}

void ResetText() {
	LoadLanguage();

	if (mainWindow)
		mainWindow->ResetText();
}

/****************************************************************************
 * WindowPrompt
 *
 * Displays a prompt window to user, with information, an error message, or
 * presenting a user with a choice
 ***************************************************************************/
int
WindowPrompt(const char *title, const char *msg, const char *btn1Label, const char *btn2Label) {
	if (!mainWindow || ExitRequested || ShutdownRequested)
		return 0;

	HaltGui();

	int choice = -1;

	GuiWindow promptWindow(448, 288);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	promptWindow.SetPosition(0, -10);
	GuiImageData btnOutline(button_prompt_png);
	GuiImageData btnOutlineOver(button_prompt_over_png);

	GuiImageData dialogBox(dialogue_box_png);
	GuiImage dialogBoxImg(&dialogBox);

	GuiText titleTxt(title, 26, (GXColor) {
		210, 210, 210, 255
	});
	titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	titleTxt.SetPosition(0, 14);

	GuiText msgTxt(msg, 26, (GXColor) {
		0, 0, 0, 255
	});
	msgTxt.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	msgTxt.SetPosition(0, -20);
	msgTxt.SetWrap(true, 430);

	GuiText btn1Txt(btn1Label, 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage btn1Img(&btnOutline);
	GuiImage btn1ImgOver(&btnOutlineOver);
	GuiButton btn1(btnOutline.GetWidth(), btnOutline.GetHeight());

	if (btn2Label) {
		btn1.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
		btn1.SetPosition(20, -25);
	} else {
		btn1.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
		btn1.SetPosition(0, -25);
	}

	btn1.SetLabel(&btn1Txt);
	btn1.SetImage(&btn1Img);
	btn1.SetImageOver(&btn1ImgOver);
	btn1.SetTrigger(trigA);
	btn1.SetTrigger(trig2);
	btn1.SetState(STATE_SELECTED);
	btn1.SetEffectGrow();

	GuiText btn2Txt(btn2Label, 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage btn2Img(&btnOutline);
	GuiImage btn2ImgOver(&btnOutlineOver);
	GuiButton btn2(btnOutline.GetWidth(), btnOutline.GetHeight());
	btn2.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	btn2.SetPosition(-20, -25);
	btn2.SetLabel(&btn2Txt);
	btn2.SetImage(&btn2Img);
	btn2.SetImageOver(&btn2ImgOver);
	btn2.SetTrigger(trigA);
	btn2.SetTrigger(trig2);
	btn2.SetEffectGrow();

	promptWindow.Append(&dialogBoxImg);
	promptWindow.Append(&titleTxt);
	promptWindow.Append(&msgTxt);
	promptWindow.Append(&btn1);

	if (btn2Label)
		promptWindow.Append(&btn2);

	promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 50);
	CancelAction();
	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&promptWindow);
	mainWindow->ChangeFocus(&promptWindow);
	if (btn2Label) {
		btn1.ResetState();
		btn2.SetState(STATE_SELECTED);
	}
	ResumeGui();

	while (choice == -1) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (btn1.GetState() == STATE_CLICKED)
			choice = 1;
		else if (btn2.GetState() == STATE_CLICKED)
			choice = 0;
	}

	promptWindow.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 50);
	while (promptWindow.GetEffect() > 0) {
		UGUI();
		usleep(THREAD_SLEEP);
	}
	HaltGui();
	mainWindow->Remove(&promptWindow);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();
	return choice;
}

/****************************************************************************
 * UpdateGUI
 *
 * Primary thread to allow GUI to respond to state changes, and draws GUI
 ***************************************************************************/
#ifdef USE_HTTP
extern "C" void network_poll(void);
#endif
static void * UGUI() {
	UpdatePads();
	mainWindow->Draw();

	if (mainWindow->GetState() != STATE_DISABLED)
		mainWindow->DrawTooltip();

	Menu_Render();

	mainWindow->Update(&userInput[3]);
	mainWindow->Update(&userInput[2]);
	mainWindow->Update(&userInput[1]);
	mainWindow->Update(&userInput[0]);

	if (ExitRequested || ShutdownRequested) {
		for (int i = 0; i <= 255; i += 15) {
			mainWindow->Draw();

			Menu_DrawRectangle(0, 0, screenwidth, screenheight, (GXColor) {
				0, 0, 0, i
			}, 1);
			Menu_Render();
		}
		ExitApp();
	}
#ifdef USE_HTTP
	network_poll();
#endif	
	return NULL;
}

static void *
UpdateGUI(void *arg) {
	int i;
	while (exitThreads == 0) {
		lock(&_gui_lock);
		if (guiHalt == false) {


			UpdatePads();
			mainWindow->Draw();

			//                        if (mainWindow->GetState() != STATE_DISABLED)
			//                                mainWindow->DrawTooltip();

			Menu_Render();

			mainWindow->Update(&userInput[3]);
			mainWindow->Update(&userInput[2]);
			mainWindow->Update(&userInput[1]);
			mainWindow->Update(&userInput[0]);

			if (ExitRequested || ShutdownRequested) {
				for (i = 0; i <= 255; i += 15) {
					mainWindow->Draw();

					Menu_DrawRectangle(0, 0, screenwidth, screenheight, (GXColor) {
						0, 0, 0, i
					}, 1);
					Menu_Render();
				}
				ExitApp();
			}

		}
		unlock(&_gui_lock);

		usleep(THREAD_SLEEP);
	}
	return NULL;
}

/****************************************************************************
 * ProgressWindow
 *
 * Opens a window, which displays progress to the user. Can either display a
 * progress bar showing % completion, or a throbber that only shows that an
 * action is in progress.
 ***************************************************************************/
static int progsleep = 0;
static int progress_suspended = 1;

static void
ProgressWindow(char *title, char *msg) {
	HaltGui();

	GuiWindow promptWindow(448, 288);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	promptWindow.SetPosition(0, -10);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	GuiImageData dialogBox(dialogue_box_png);
	GuiImage dialogBoxImg(&dialogBox);

	GuiImageData progressbarOutline(progressbar_outline_png);
	GuiImage progressbarOutlineImg(&progressbarOutline);
	progressbarOutlineImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	progressbarOutlineImg.SetPosition(25, 40);

	GuiImageData progressbarEmpty(progressbar_empty_png);
	GuiImage progressbarEmptyImg(&progressbarEmpty);
	progressbarEmptyImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	progressbarEmptyImg.SetPosition(25, 40);
	progressbarEmptyImg.SetTile(100);

	GuiImageData progressbar(progressbar_png);
	GuiImage progressbarImg(&progressbar);
	progressbarImg.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	progressbarImg.SetPosition(25, 40);

	GuiImageData throbber(throbber_png);
	GuiImage throbberImg(&throbber);
	throbberImg.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	throbberImg.SetPosition(0, 40);

	GuiText titleTxt(title, 26, (GXColor) {
		70, 70, 10, 255
	});
	titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	titleTxt.SetPosition(0, 14);

	GuiText msgTxt(msg, 26, (GXColor) {
		0, 0, 0, 255
	});
	msgTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	msgTxt.SetPosition(0, 80);

	promptWindow.Append(&dialogBoxImg);
	promptWindow.Append(&titleTxt);
	promptWindow.Append(&msgTxt);

	if (showProgress == 1) {
		promptWindow.Append(&progressbarEmptyImg);
		promptWindow.Append(&progressbarImg);
		promptWindow.Append(&progressbarOutlineImg);
	} else {
		promptWindow.Append(&throbberImg);
	}

	// wait to see if progress flag changes soon
	progsleep = 400000;

	while (progsleep > 0) {
		UGUI();
		if (!showProgress)
			break;
		usleep(THREAD_SLEEP);
		progsleep -= THREAD_SLEEP;
	}

	if (!showProgress)
		return;


	int oldState = mainWindow->GetState();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&promptWindow);
	mainWindow->ChangeFocus(&promptWindow);
	ResumeGui();

	float angle = 0;
	u32 count = 0;

	while (showProgress) {
		UGUI();
		progsleep = 20000;

		while (progsleep > 0) {
			if (!showProgress)
				break;
			usleep(THREAD_SLEEP);
			UGUI();
			progsleep -= THREAD_SLEEP;
		}

		if (showProgress == 1) {
			progressbarImg.SetTile(100 * progressDone / progressTotal);
		} else if (showProgress == 2) {
			if (count % 5 == 0) {
				angle += 45.0f;
				if (angle >= 360.0f)
					angle = 0;
				throbberImg.SetAngle(angle);
			}
			++count;
		}
	}

	HaltGui();
	mainWindow->Remove(&promptWindow);
	mainWindow->SetState(oldState);
	ResumeGui();
}

static void * ProgressThread(void *arg) {
	while (exitThreads == 0) {
		lock(&_progress_lock);
		if (progress_suspended==0) {			
			ProgressWindow(progressTitle, progressMsg);			
		}
		unlock(&_progress_lock);
		usleep(THREAD_SLEEP);
	}
	return NULL;
}

/****************************************************************************
 * InitGUIThread
 *
 * Startup GUI threads
 ***************************************************************************/
void
InitGUIThreads() {
	//	LWP_CreateThread (&guithread, UpdateGUI, NULL, NULL, 0, 70);
	//	LWP_CreateThread (&progressthread, ProgressThread, NULL, NULL, 0, 40);

//	xenon_run_thread_task(1, xenon_thread_stack + (1 * 0x10000) - 0x100, UpdateGUI);
//	xenon_run_thread_task(2, xenon_thread_stack + (2 * 0x10000) - 0x100, ProgressThread);
}

/****************************************************************************
 * CancelAction
 *
 * Signals the GUI progress window thread to halt, and waits for it to
 * finish. Prevents multiple progress window events from interfering /
 * overriding each other.
 ***************************************************************************/
void
CancelAction() {
	showProgress = 0;
	lock(&_progress_lock);
	progress_suspended = 1;
	unlock(&_progress_lock);

	// wait for thread to finish
	//	while(!LWP_ThreadIsSuspended(progressthread))
	//		usleep(THREAD_SLEEP);
}

/****************************************************************************
 * ShowProgress
 *
 * Updates the variables used by the progress window for drawing a progress
 * bar. Also resumes the progress window thread if it is suspended.
 ***************************************************************************/
void
ShowProgress(const char *msg, int done, int total) {
	if (!mainWindow || ExitRequested || ShutdownRequested)
		return;

	if (total < (256 * 1024))
		return;
	else if (done > total) // this shouldn't happen
		done = total;

	if (done / total > 0.99)
		done = total;

	if (showProgress != 1)
		CancelAction(); // wait for previous progress window to finish

	snprintf(progressMsg, 200, "%s", msg);
	sprintf(progressTitle, "Please Wait");
	showProgress = 1;
	progressTotal = total;
	progressDone = done;
	lock(&_progress_lock);
	progress_suspended = 0;
	unlock(&_progress_lock);
	//	LWP_ResumeThread (progressthread);
}

/****************************************************************************
 * ShowAction
 *
 * Shows that an action is underway. Also resumes the progress window thread
 * if it is suspended.
 ***************************************************************************/
void
ShowAction(const char *msg) {
	if (!mainWindow || ExitRequested || ShutdownRequested)
		return;

	if (showProgress != 0)
		CancelAction(); // wait for previous progress window to finish

	snprintf(progressMsg, 200, "%s", msg);
	sprintf(progressTitle, "Please Wait");
	showProgress = 2;
	progressDone = 0;
	progressTotal = 0;
	lock(&_progress_lock);
	progress_suspended = 0;
	unlock(&_progress_lock);
	//	LWP_ResumeThread (progressthread);
}

void ErrorPrompt(const char *msg) {
	WindowPrompt("Error", msg, "OK", NULL);
}

int ErrorPromptRetry(const char *msg) {
	return WindowPrompt("Error", msg, "Retry", "Cancel");
}

void InfoPrompt(const char *msg) {
	WindowPrompt("Information", msg, "OK", NULL);
}

/****************************************************************************
 * AutoSave
 *
 * Automatically saves SRAM/snapshot when returning from in-game to the menu
 ***************************************************************************/
void AutoSave() {
	if (EMUSettings.AutoSave == 1) {
		SaveSRAMAuto(SILENT);
	} else if (EMUSettings.AutoSave == 2) {
		if (WindowPrompt("Save", "Save Snapshot?", "Save", "Don't Save"))
			SaveSnapshotAuto(NOTSILENT);
	} else if (EMUSettings.AutoSave == 3) {
		if (WindowPrompt("Save", "Save SRAM and Snapshot?", "Save", "Don't Save")) {
			SaveSRAMAuto(NOTSILENT);
			SaveSnapshotAuto(NOTSILENT);
		}
	}
}

/****************************************************************************
 * OnScreenKeyboard
 *
 * Opens an on-screen keyboard window, with the data entered being stored
 * into the specified variable.
 ***************************************************************************/
static void OnScreenKeyboard(char * var, u32 maxlen) {
	int save = -1;

	GuiKeyboard keyboard(var, maxlen);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	GuiText okBtnTxt("OK", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage okBtnImg(&btnOutline);
	GuiImage okBtnImgOver(&btnOutlineOver);
	GuiButton okBtn(btnOutline.GetWidth(), btnOutline.GetHeight());

	okBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	okBtn.SetPosition(25, -25);

	okBtn.SetLabel(&okBtnTxt);
	okBtn.SetImage(&okBtnImg);
	okBtn.SetImageOver(&okBtnImgOver);
	okBtn.SetTrigger(trigA);
	okBtn.SetTrigger(trig2);
	okBtn.SetEffectGrow();

	GuiText cancelBtnTxt("Cancel", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage cancelBtnImg(&btnOutline);
	GuiImage cancelBtnImgOver(&btnOutlineOver);
	GuiButton cancelBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	cancelBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	cancelBtn.SetPosition(-25, -25);
	cancelBtn.SetLabel(&cancelBtnTxt);
	cancelBtn.SetImage(&cancelBtnImg);
	cancelBtn.SetImageOver(&cancelBtnImgOver);
	cancelBtn.SetTrigger(trigA);
	cancelBtn.SetTrigger(trig2);
	cancelBtn.SetEffectGrow();

	keyboard.Append(&okBtn);
	keyboard.Append(&cancelBtn);

	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&keyboard);
	mainWindow->ChangeFocus(&keyboard);
	ResumeGui();

	while (save == -1) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (okBtn.GetState() == STATE_CLICKED)
			save = 1;
		else if (cancelBtn.GetState() == STATE_CLICKED)
			save = 0;
	}

	if (save) {
		snprintf(var, maxlen, "%s", keyboard.kbtextstr);
	}

	HaltGui();
	mainWindow->Remove(&keyboard);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();
}

/****************************************************************************
 * SettingWindow
 *
 * Opens a new window, with the specified window element appended. Allows
 * for a customizable prompted setting.
 ***************************************************************************/
static int
SettingWindow(const char * title, GuiWindow * w) {
	int save = -1;

	GuiWindow promptWindow(448, 288);
	promptWindow.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	GuiImageData dialogBox(dialogue_box_png);
	GuiImage dialogBoxImg(&dialogBox);

	GuiText titleTxt(title, 26, (GXColor) {
		70, 70, 10, 255
	});
	titleTxt.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	titleTxt.SetPosition(0, 14);

	GuiText okBtnTxt("OK", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage okBtnImg(&btnOutline);
	GuiImage okBtnImgOver(&btnOutlineOver);
	GuiButton okBtn(btnOutline.GetWidth(), btnOutline.GetHeight());

	okBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	okBtn.SetPosition(20, -25);

	okBtn.SetLabel(&okBtnTxt);
	okBtn.SetImage(&okBtnImg);
	okBtn.SetImageOver(&okBtnImgOver);
	okBtn.SetTrigger(trigA);
	okBtn.SetTrigger(trig2);
	okBtn.SetEffectGrow();

	GuiText cancelBtnTxt("Cancel", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage cancelBtnImg(&btnOutline);
	GuiImage cancelBtnImgOver(&btnOutlineOver);
	GuiButton cancelBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	cancelBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	cancelBtn.SetPosition(-20, -25);
	cancelBtn.SetLabel(&cancelBtnTxt);
	cancelBtn.SetImage(&cancelBtnImg);
	cancelBtn.SetImageOver(&cancelBtnImgOver);
	cancelBtn.SetTrigger(trigA);
	cancelBtn.SetTrigger(trig2);
	cancelBtn.SetEffectGrow();

	promptWindow.Append(&dialogBoxImg);
	promptWindow.Append(&titleTxt);
	promptWindow.Append(&okBtn);
	promptWindow.Append(&cancelBtn);

	HaltGui();
	mainWindow->SetState(STATE_DISABLED);
	mainWindow->Append(&promptWindow);
	mainWindow->Append(w);
	mainWindow->ChangeFocus(w);
	ResumeGui();

	while (save == -1) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (okBtn.GetState() == STATE_CLICKED)
			save = 1;
		else if (cancelBtn.GetState() == STATE_CLICKED)
			save = 0;
	}
	HaltGui();
	mainWindow->Remove(&promptWindow);
	mainWindow->Remove(w);
	mainWindow->SetState(STATE_DEFAULT);
	ResumeGui();
	return save;
}

/****************************************************************************
 * WindowCredits
 * Display credits, legal copyright and licence
 *
 * THIS MUST NOT BE REMOVED OR DISABLED IN ANY DERIVATIVE WORK
 ***************************************************************************/
static void WindowCredits(void * ptr) {
	if (btnLogo->GetState() != STATE_CLICKED)
		return;

	btnLogo->ResetState();

	bool exit = false;
	int i = 0;
	int y = 20;

	GuiWindow creditsWindow(screenwidth, screenheight);
	GuiWindow creditsWindowBox(580, 448);
	creditsWindowBox.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);

	GuiImageData creditsBox(credits_box_png);
	GuiImage creditsBoxImg(&creditsBox);
	creditsBoxImg.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	creditsWindowBox.Append(&creditsBoxImg);

	int numEntries = 24;
	GuiText * txt[numEntries];

	txt[i] = new GuiText("Credits", 30, (GXColor) {
		0, 0, 0, 255
	});
	txt[i]->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	txt[i]->SetPosition(0, y);
	i++;
	y += 32;

	txt[i] = new GuiText("Official Site: http://code.google.com/p/snes9x-gx/", 20, (GXColor) {
		0, 0, 0, 255
	});
	txt[i]->SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	txt[i]->SetPosition(0, y);
	i++;
	y += 40;

	txt[i]->SetPresets(20, (GXColor) {
		0, 0, 0, 255
	}, 0,
	FTGX_JUSTIFY_LEFT | FTGX_ALIGN_TOP, ALIGN_LEFT, ALIGN_TOP);

	txt[i] = new GuiText("Coding & menu design");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("Tantric");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;
	txt[i] = new GuiText("Coding");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("michniewski");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;
	txt[i] = new GuiText("Menu artwork");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("the3seashells");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;
	txt[i] = new GuiText("Menu sound");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("Peter de Man");
	txt[i]->SetPosition(350, y);
	i++;
	y += 48;

	txt[i] = new GuiText("Snes9x GX GameCube");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("SoftDev, crunchy2,");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;
	txt[i] = new GuiText("eke-eke, others");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;
	txt[i] = new GuiText("Snes9x");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("Snes9x Team");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;

	txt[i] = new GuiText("libogc / devkitPPC");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("shagkur & wintermute");
	txt[i]->SetPosition(350, y);
	i++;
	y += 24;
	txt[i] = new GuiText("FreeTypeGX");
	txt[i]->SetPosition(60, y);
	i++;
	txt[i] = new GuiText("Armin Tamzarian");
	txt[i]->SetPosition(350, y);
	i++;
	y += 48;

	txt[i]->SetPresets(18, (GXColor) {
		0, 0, 0, 255
	}, 0,
	FTGX_JUSTIFY_CENTER | FTGX_ALIGN_TOP, ALIGN_CENTRE, ALIGN_TOP);

	txt[i] = new GuiText("Snes9x - Copyright (c) Snes9x Team 1996 - 2006");
	txt[i]->SetPosition(0, y);
	i++;
	y += 20;
	txt[i] = new GuiText("This software is open source and may be copied,");
	txt[i]->SetPosition(0, y);
	i++;
	y += 20;
	txt[i] = new GuiText("distributed, or modified under the terms of the");
	txt[i]->SetPosition(0, y);
	i++;
	y += 20;
	txt[i] = new GuiText("GNU General Public License (GPL) Version 2.");
	txt[i]->SetPosition(0, y);
	i++;
	y += 20;

	char iosVersion[20];

#ifdef HW_RVL
	sprintf(iosVersion, "IOS: %d", IOS_GetVersion());
#endif

	txt[i] = new GuiText(iosVersion, 18, (GXColor) {
		0, 0, 0, 255
	});
	txt[i]->SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	txt[i]->SetPosition(20, -20);

	for (i = 0; i < numEntries; i++)
		creditsWindowBox.Append(txt[i]);

	creditsWindow.Append(&creditsWindowBox);

	while (!exit) {
		UpdatePads();

		gameScreenImg->Draw();
		bgBottomImg->Draw();
		bgTopImg->Draw();
		creditsWindow.Draw();

#ifdef HW_RVL
		i = 3;
		do {
			if (userInput[i].wpad->ir.valid)
				Menu_DrawImg(userInput[i].wpad->ir.x - 48, userInput[i].wpad->ir.y - 48,
					96, 96, pointer[i]->GetImage(), userInput[i].wpad->ir.angle, 1, 1, 255);
			DoRumble(i);
			--i;
		} while (i >= 0);
#endif

		Menu_Render();

		if ((userInput[0].wpad->btns_d || userInput[0].pad.btns_d) ||
				(userInput[1].wpad->btns_d || userInput[1].pad.btns_d) ||
				(userInput[2].wpad->btns_d || userInput[2].pad.btns_d) ||
				(userInput[3].wpad->btns_d || userInput[3].pad.btns_d)) {
			exit = true;
		}
		usleep(THREAD_SLEEP);
	}

	// clear buttons pressed
	for (i = 0; i < 4; i++) {
		userInput[i].wpad->btns_d = 0;
		userInput[i].pad.btns_d = 0;
	}

	for (i = 0; i < numEntries; i++)
		delete txt[i];
}

/****************************************************************************
 * MenuGameSelection
 *
 * Displays a list of games on the specified load device, and allows the user
 * to browse and select from this list.
 ***************************************************************************/
static int MenuGameSelection() {
	int menu = MENU_NONE;
	bool res;
	int i;

	GuiText titleTxt("Choose Game", 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData iconHome(icon_home_png);
	GuiImageData iconSettings(icon_settings_png);
	GuiImageData btnOutline(button_long_png);
	GuiImageData btnOutlineOver(button_long_over_png);

	GuiTrigger trigHome;
	trigHome.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);

	GuiText settingsBtnTxt("Settings", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage settingsBtnIcon(&iconSettings);
	settingsBtnIcon.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	settingsBtnIcon.SetPosition(14, 0);
	GuiImage settingsBtnImg(&btnOutline);
	GuiImage settingsBtnImgOver(&btnOutlineOver);
	GuiButton settingsBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	settingsBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	settingsBtn.SetPosition(90, -35);
	settingsBtn.SetLabel(&settingsBtnTxt);
	settingsBtn.SetIcon(&settingsBtnIcon);
	settingsBtn.SetImage(&settingsBtnImg);
	settingsBtn.SetImageOver(&settingsBtnImgOver);
	settingsBtn.SetTrigger(trigA);
	settingsBtn.SetTrigger(trig2);
	settingsBtn.SetEffectGrow();

	GuiText exitBtnTxt("Exit", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage exitBtnIcon(&iconHome);
	exitBtnIcon.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	exitBtnIcon.SetPosition(14, 0);
	GuiImage exitBtnImg(&btnOutline);
	GuiImage exitBtnImgOver(&btnOutlineOver);
	GuiButton exitBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	exitBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	exitBtn.SetPosition(-90, -35);
	exitBtn.SetLabel(&exitBtnTxt);
	exitBtn.SetIcon(&exitBtnIcon);
	exitBtn.SetImage(&exitBtnImg);
	exitBtn.SetImageOver(&exitBtnImgOver);
	exitBtn.SetTrigger(trigA);
	exitBtn.SetTrigger(trig2);
	exitBtn.SetTrigger(&trigHome);
	exitBtn.SetEffectGrow();

	GuiWindow buttonWindow(screenwidth, screenheight);
	buttonWindow.Append(&settingsBtn);
	buttonWindow.Append(&exitBtn);

	//GuiFileBrowser gameBrowser(424, 268);
	GuiFileBrowser gameBrowser(1200, 426);
	//gameBrowser.SetPosition(50, 98);
	gameBrowser.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	ResetBrowser();


	HaltGui();
	btnLogo->SetAlignment(ALIGN_RIGHT, ALIGN_TOP);
	btnLogo->SetPosition(-50, 24);
	mainWindow->Append(&titleTxt);
	mainWindow->Append(&gameBrowser);
	mainWindow->Append(&buttonWindow);
	ResumeGui();

	// populate initial directory listing
	selectLoadedFile = 1;
	OpenGameList();

	gameBrowser.ResetState();
	gameBrowser.fileList[0]->SetState(STATE_SELECTED);
	gameBrowser.TriggerUpdate();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (selectLoadedFile == 2) {
			selectLoadedFile = 0;
			mainWindow->ChangeFocus(&gameBrowser);
			gameBrowser.TriggerUpdate();
		}

		// update gameWindow based on arrow buttons
		// set MENU_EXIT if A button pressed on a game
		for (i = 0; i < FILE_PAGESIZE; i++) {
			if (gameBrowser.fileList[i]->GetState() == STATE_CLICKED) {
				gameBrowser.fileList[i]->ResetState();
				// check corresponding browser entry
				if (browserList[browser.selIndex].isdir) {
					res = BrowserChangeFolder();

					if (res) {
						gameBrowser.ResetState();
						gameBrowser.fileList[0]->SetState(STATE_SELECTED);
						gameBrowser.TriggerUpdate();
					} else {
						menu = MENU_GAMESELECTION;
						break;
					}
				} else {
					mainWindow->SetState(STATE_DISABLED);
					if (BrowserLoadFile())
						menu = MENU_EXIT;
					else
						mainWindow->SetState(STATE_DEFAULT);
				}
			}
		}

		if (settingsBtn.GetState() == STATE_CLICKED)
			menu = MENU_SETTINGS;
		else if (exitBtn.GetState() == STATE_CLICKED)
			ExitRequested = 1;
	}
	
	HaltParseThread(); // halt parsing
	HaltGui();
	ResetBrowser();
	mainWindow->Remove(&titleTxt);
	mainWindow->Remove(&buttonWindow);
	mainWindow->Remove(&gameBrowser);
	return menu;
}

/****************************************************************************
 * ControllerWindowUpdate
 *
 * Callback for controller window. Responds to clicks on window elements.
 ***************************************************************************/
static void ControllerWindowUpdate(void * ptr, int dir) {
	GuiButton * b = (GuiButton *) ptr;
	if (b->GetState() == STATE_CLICKED) {
		EMUSettings.Controller += dir;

		if (EMUSettings.Controller > CTRL_PAD4)
			EMUSettings.Controller = CTRL_MOUSE;
		if (EMUSettings.Controller < CTRL_MOUSE)
			EMUSettings.Controller = CTRL_PAD4;

		settingText->SetText(ctrlName[EMUSettings.Controller]);
		b->ResetState();
	}
}

/****************************************************************************
 * ControllerWindowLeftClick / ControllerWindowRightsClick
 *
 * Callbacks for controller window arrows. Responds arrow clicks.
 ***************************************************************************/
static void ControllerWindowLeftClick(void * ptr) {
	ControllerWindowUpdate(ptr, -1);
}

static void ControllerWindowRightClick(void * ptr) {
	ControllerWindowUpdate(ptr, +1);
}

/****************************************************************************
 * ControllerWindow
 *
 * Opens a window to allow the user to select the controller to be used.
 ***************************************************************************/
static void ControllerWindow() {
	GuiWindow * w = new GuiWindow(300, 250);
	w->SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);

	GuiTrigger trigLeft;
	trigLeft.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT, PAD_BUTTON_LEFT);

	GuiTrigger trigRight;
	trigRight.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT, PAD_BUTTON_RIGHT);

	GuiImageData arrowLeft(button_arrow_left_png);
	GuiImage arrowLeftImg(&arrowLeft);
	GuiImageData arrowLeftOver(button_arrow_left_over_png);
	GuiImage arrowLeftOverImg(&arrowLeftOver);
	GuiButton arrowLeftBtn(arrowLeft.GetWidth(), arrowLeft.GetHeight());
	arrowLeftBtn.SetImage(&arrowLeftImg);
	arrowLeftBtn.SetImageOver(&arrowLeftOverImg);
	arrowLeftBtn.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	arrowLeftBtn.SetTrigger(trigA);
	arrowLeftBtn.SetTrigger(trig2);
	arrowLeftBtn.SetTrigger(&trigLeft);
	arrowLeftBtn.SetSelectable(false);
	arrowLeftBtn.SetUpdateCallback(ControllerWindowLeftClick);

	GuiImageData arrowRight(button_arrow_right_png);
	GuiImage arrowRightImg(&arrowRight);
	GuiImageData arrowRightOver(button_arrow_right_over_png);
	GuiImage arrowRightOverImg(&arrowRightOver);
	GuiButton arrowRightBtn(arrowRight.GetWidth(), arrowRight.GetHeight());
	arrowRightBtn.SetImage(&arrowRightImg);
	arrowRightBtn.SetImageOver(&arrowRightOverImg);
	arrowRightBtn.SetAlignment(ALIGN_RIGHT, ALIGN_MIDDLE);
	arrowRightBtn.SetTrigger(trigA);
	arrowRightBtn.SetTrigger(trig2);
	arrowRightBtn.SetTrigger(&trigRight);
	arrowRightBtn.SetSelectable(false);
	arrowRightBtn.SetUpdateCallback(ControllerWindowRightClick);

	settingText = new GuiText(ctrlName[EMUSettings.Controller], 22, (GXColor) {
		0, 0, 0, 255
	});

	int currentController = EMUSettings.Controller;

	w->Append(&arrowLeftBtn);
	w->Append(&arrowRightBtn);
	w->Append(settingText);

	if (!SettingWindow("Controller", w))
		EMUSettings.Controller = currentController; // undo changes

	delete(w);
	delete(settingText);
}

/****************************************************************************
 * MenuGame
 *
 * Menu displayed when returning to the menu from in-game.
 ***************************************************************************/
static int MenuGame() {
	int menu = MENU_NONE;

	GuiText titleTxt(ROMInfo.diplayname, 22, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiImageData btnCloseOutline(button_small_png);
	GuiImageData btnCloseOutlineOver(button_small_over_png);
	GuiImageData btnLargeOutline(button_large_png);
	GuiImageData btnLargeOutlineOver(button_large_over_png);
	GuiImageData iconGameSettings(icon_game_settings_png);
	GuiImageData iconLoad(icon_game_load_png);
	GuiImageData iconSave(icon_game_save_png);
	GuiImageData iconReset(icon_game_reset_png);

	GuiImageData battery(battery_png);
	GuiImageData batteryRed(battery_red_png);
	GuiImageData batteryBar(battery_bar_png);

	GuiTrigger trigHome;
	trigHome.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);

	GuiText saveBtnTxt("Save", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage saveBtnImg(&btnLargeOutline);
	GuiImage saveBtnImgOver(&btnLargeOutlineOver);
	GuiImage saveBtnIcon(&iconSave);
	GuiButton saveBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	saveBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	saveBtn.SetPosition(-125, 200);
	saveBtn.SetLabel(&saveBtnTxt);
	saveBtn.SetImage(&saveBtnImg);
	saveBtn.SetImageOver(&saveBtnImgOver);
	saveBtn.SetIcon(&saveBtnIcon);
	saveBtn.SetTrigger(trigA);
	saveBtn.SetTrigger(trig2);
	saveBtn.SetEffectGrow();

	GuiText loadBtnTxt("Load", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage loadBtnImg(&btnLargeOutline);
	GuiImage loadBtnImgOver(&btnLargeOutlineOver);
	GuiImage loadBtnIcon(&iconLoad);
	GuiButton loadBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	loadBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	loadBtn.SetPosition(125, 200);
	loadBtn.SetLabel(&loadBtnTxt);
	loadBtn.SetImage(&loadBtnImg);
	loadBtn.SetImageOver(&loadBtnImgOver);
	loadBtn.SetIcon(&loadBtnIcon);
	loadBtn.SetTrigger(trigA);
	loadBtn.SetTrigger(trig2);
	loadBtn.SetEffectGrow();

	GuiText resetBtnTxt("Reset", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage resetBtnImg(&btnLargeOutline);
	GuiImage resetBtnImgOver(&btnLargeOutlineOver);
	GuiImage resetBtnIcon(&iconReset);
	GuiButton resetBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	resetBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	resetBtn.SetPosition(125, 400);
	resetBtn.SetLabel(&resetBtnTxt);
	resetBtn.SetImage(&resetBtnImg);
	resetBtn.SetImageOver(&resetBtnImgOver);
	resetBtn.SetIcon(&resetBtnIcon);
	resetBtn.SetTrigger(trigA);
	resetBtn.SetTrigger(trig2);
	resetBtn.SetEffectGrow();

	GuiText gameSettingsBtnTxt("Game Settings", 22, (GXColor) {
		0, 0, 0, 255
	});
	gameSettingsBtnTxt.SetWrap(true, btnLargeOutline.GetWidth() - 20);
	GuiImage gameSettingsBtnImg(&btnLargeOutline);
	GuiImage gameSettingsBtnImgOver(&btnLargeOutlineOver);
	GuiImage gameSettingsBtnIcon(&iconGameSettings);
	GuiButton gameSettingsBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	gameSettingsBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	gameSettingsBtn.SetPosition(-125, 400);
	gameSettingsBtn.SetLabel(&gameSettingsBtnTxt);
	gameSettingsBtn.SetImage(&gameSettingsBtnImg);
	gameSettingsBtn.SetImageOver(&gameSettingsBtnImgOver);
	gameSettingsBtn.SetIcon(&gameSettingsBtnIcon);
	gameSettingsBtn.SetTrigger(trigA);
	gameSettingsBtn.SetTrigger(trig2);
	gameSettingsBtn.SetEffectGrow();

	GuiText mainmenuBtnTxt("Main Menu", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage mainmenuBtnImg(&btnOutline);
	GuiImage mainmenuBtnImgOver(&btnOutlineOver);
	GuiButton mainmenuBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	mainmenuBtn.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
	mainmenuBtn.SetPosition(0, -35);
	mainmenuBtn.SetLabel(&mainmenuBtnTxt);
	mainmenuBtn.SetImage(&mainmenuBtnImg);
	mainmenuBtn.SetImageOver(&mainmenuBtnImgOver);
	mainmenuBtn.SetTrigger(trigA);
	mainmenuBtn.SetTrigger(trig2);
	mainmenuBtn.SetEffectGrow();

	GuiText closeBtnTxt("Close", 20, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage closeBtnImg(&btnCloseOutline);
	GuiImage closeBtnImgOver(&btnCloseOutlineOver);
	GuiButton closeBtn(btnCloseOutline.GetWidth(), btnCloseOutline.GetHeight());
	closeBtn.SetAlignment(ALIGN_RIGHT, ALIGN_TOP);
	closeBtn.SetPosition(-50, 35);
	closeBtn.SetLabel(&closeBtnTxt);
	closeBtn.SetImage(&closeBtnImg);
	closeBtn.SetImageOver(&closeBtnImgOver);
	closeBtn.SetTrigger(trigA);
	closeBtn.SetTrigger(trig2);
	closeBtn.SetTrigger(&trigHome);
	closeBtn.SetEffectGrow();

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&saveBtn);
	w.Append(&loadBtn);
	w.Append(&resetBtn);
	w.Append(&gameSettingsBtn);

	w.Append(&mainmenuBtn);
	w.Append(&closeBtn);

	btnLogo->SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	btnLogo->SetPosition(-50, -40);
	mainWindow->Append(&w);

	if (lastMenu == MENU_NONE) {
		enterSound->Play();
		bgTopImg->SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 35);
		closeBtn.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 35);
		titleTxt.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_IN, 35);
		mainmenuBtn.SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_IN, 35);
		bgBottomImg->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_IN, 35);
		btnLogo->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_IN, 35);
		w.SetEffect(EFFECT_FADE, 15);
	}

	ResumeGui();

	if (lastMenu == MENU_NONE)
		AutoSave();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (saveBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAME_SAVE;
		} else if (loadBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAME_LOAD;
		} else if (resetBtn.GetState() == STATE_CLICKED) {
			if (WindowPrompt("Reset Game", "Are you sure that you want to reset this game? Any unsaved progress will be lost.", "OK", "Cancel")) {
				EMUInterface.Reset();
				menu = MENU_EXIT;
			}
		} else if (gameSettingsBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAMESETTINGS;
		} else if (mainmenuBtn.GetState() == STATE_CLICKED) {
			if (WindowPrompt("Quit Game", "Quit this game? Any unsaved progress will be lost.", "OK", "Cancel")) {
				HaltGui();
				mainWindow->Remove(gameScreenImg);
				delete gameScreenImg;
				delete gameScreen;
				gameScreen = NULL;
				//				free(gameScreenPng);
				//				gameScreenPng = NULL;

				gameScreenImg = new GuiImage(screenwidth, screenheight, (GXColor) {
					255, 255, 255, 255
				});
				// gameScreenImg->ColorStripe(10);
				mainWindow->Insert(gameScreenImg, 0);
				ResumeGui();
#ifndef NO_SOUND
				bgMusic->Play(); // startup music
#endif
				menu = MENU_GAMESELECTION;
			}
		} else if (closeBtn.GetState() == STATE_CLICKED) {
			menu = MENU_EXIT;

			exitSound->Play();
			bgTopImg->SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			closeBtn.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			titleTxt.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			mainmenuBtn.SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);
			bgBottomImg->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);
			btnLogo->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);

			w.SetEffect(EFFECT_FADE, -15);
			usleep(350000); // wait for effects to finish
		}
	}

	HaltGui();

	mainWindow->Remove(&w);
	return menu;
}

/****************************************************************************
 * FindGameSaveNum
 *
 * Determines the save file number of the given file name
 * Returns -1 if none is found
 ***************************************************************************/
 #include <libgen.h>

static int FindGameSaveNum(char * savefile, int device) {
	int n = -1;
	int d = 0;
	int romlen = strlen(ROMInfo.diplayname);
	int savelen = strlen(savefile);

	int diff = savelen - romlen;

	d = strncmp(savefile, ROMInfo.diplayname, romlen);	
	
	if (d != 0) {
		return -1;
	}

	if (savefile[romlen] == ' ') {
		if (diff == 5 && strncmp(&savefile[romlen + 1], "Auto", 4) == 0)
			n = 0; // found Auto save
		else if (diff == 2 || diff == 3)
			n = atoi(&savefile[romlen + 1]);
	}

	if (n >= 0 && n < MAX_SAVES)
		return n;
	else
		return -1;
}

/****************************************************************************
 * MenuGameSaves
 *
 * Allows the user to load or save progress.
 ***************************************************************************/
static int MenuGameSaves(int action) {
	int menu = MENU_NONE;
	int ret;
	int i, n, type, len, len2;
	int j = 0;
	SaveList saves;
	char filepath[1024];
	char scrfile[1024];
	char tmp[MAXJOLIET + 1];
	struct stat filestat;
	struct tm * timeinfo;
	int device = EMUSettings.SaveMethod;

	if (device == DEVICE_AUTO)
		autoSaveMethod(NOTSILENT);

	if (!ChangeInterface(device, NOTSILENT))
		return MENU_GAME;

	GuiText titleTxt(NULL, 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	if (action == 0)
		titleTxt.SetText("Load Game");
	else
		titleTxt.SetText("Save Game");

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiImageData btnCloseOutline(button_small_png);
	GuiImageData btnCloseOutlineOver(button_small_over_png);

	GuiTrigger trigHome;
	trigHome.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);

	GuiText backBtnTxt("Go Back", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(50, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetTrigger(trigA);
	backBtn.SetTrigger(trig2);
	backBtn.SetEffectGrow();

	GuiText closeBtnTxt("Close", 20, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage closeBtnImg(&btnCloseOutline);
	GuiImage closeBtnImgOver(&btnCloseOutlineOver);
	GuiButton closeBtn(btnCloseOutline.GetWidth(), btnCloseOutline.GetHeight());
	closeBtn.SetAlignment(ALIGN_RIGHT, ALIGN_TOP);
	closeBtn.SetPosition(-50, 35);
	closeBtn.SetLabel(&closeBtnTxt);
	closeBtn.SetImage(&closeBtnImg);
	closeBtn.SetImageOver(&closeBtnImgOver);
	closeBtn.SetTrigger(trigA);
	closeBtn.SetTrigger(trig2);
	closeBtn.SetTrigger(&trigHome);
	closeBtn.SetEffectGrow();

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&backBtn);
	w.Append(&closeBtn);
	mainWindow->Append(&w);
	mainWindow->Append(&titleTxt);
	ResumeGui();

	memset(&saves, 0, sizeof (saves));
	
	// correct load/save methods out of bounds
	if (EMUSettings.SaveMethod >= devsinfo.save.nbr)
		EMUSettings.SaveMethod = 0;
	
	sprintf(browser.dir, "%s%s", devsinfo.save.path[EMUSettings.SaveMethod], EMUSettings.SaveFolder);
	ParseDirectory(true, false);

	len = strlen(ROMInfo.diplayname);

	// find matching files
	AllocSaveBuffer();

	for (i = 0; i < browser.numEntries; i++) {
		printf("[MenuGameSaves] %s\n", browserList[i].filename);
		len2 = strlen(browserList[i].filename);

		if (len2 < 6 || len2 - len < 5)
			continue;

		if (strncmp(&browserList[i].filename[len2 - 4], ".srm", 4) == 0)
			type = FILE_SRAM;
		else if (strncmp(&browserList[i].filename[len2 - 4], ".frz", 4) == 0)
			type = FILE_SNAPSHOT;
		else
			continue;

		strcpy(tmp, browserList[i].filename);
		tmp[len2 - 4] = 0;
		n = FindGameSaveNum(tmp, device);

		if (n >= 0) {
			saves.type[j] = type;
			saves.files[saves.type[j]][n] = 1;
			strcpy(saves.filename[j], browserList[i].filename);

			if (saves.type[j] == FILE_SNAPSHOT) {
				sprintf(scrfile, "%s%s/%s.png", devsinfo.save.path[EMUSettings.SaveMethod], EMUSettings.SaveFolder, tmp);

				memset(savebuffer, 0, SAVEBUFFERSIZE);
				if (LoadFile(scrfile, SILENT))
					saves.previewImg[j] = new GuiImageData(savebuffer, 64, 48);
			}
			snprintf(filepath, 1024, "%s%s/%s", devsinfo.save.path[EMUSettings.SaveMethod], EMUSettings.SaveFolder, saves.filename[j]);
			if (stat(filepath, &filestat) == 0) {
				timeinfo = localtime(&filestat.st_mtime);
				strftime(saves.date[j], 20, "%a %b %d", timeinfo);
				strftime(saves.time[j], 10, "%I:%M %p", timeinfo);
			}
			j++;
		}
	}

	FreeSaveBuffer();
	saves.length = j;

	if (saves.length == 0 && action == 0) {
		InfoPrompt("No game saves found.");
		menu = MENU_GAME;
	}

	GuiSaveBrowser saveBrowser(720, 426, &saves, action);
	saveBrowser.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);

	HaltGui();
	mainWindow->Append(&saveBrowser);
	mainWindow->ChangeFocus(&saveBrowser);
	ResumeGui();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		ret = saveBrowser.GetClickedSave();

		// load or save game
		if (ret > -3) {
			int result = 0;

			if (action == 0) // load
			{
				MakeFilePath(filepath, saves.type[ret], saves.filename[ret]);
				switch (saves.type[ret]) {
					case FILE_SRAM:
						result = LoadSRAM(filepath, NOTSILENT);
						break;
					case FILE_SNAPSHOT:
						result = LoadSnapshot(filepath, NOTSILENT);
						break;
				}
				if (result)
					menu = MENU_EXIT;
			} else // save
			{
				if (ret == -2) // new SRAM
				{
					for (i = 1; i < 100; i++)
						if (saves.files[FILE_SRAM][i] == 0)
							break;

					if (i < 100) {
						MakeFilePath(filepath, FILE_SRAM, ROMInfo.filename, i);
						SaveSRAM(filepath, NOTSILENT);
						menu = MENU_GAME_SAVE;
					}
				} else if (ret == -1) // new Snapshot
				{
					for (i = 1; i < 100; i++)
						if (saves.files[FILE_SNAPSHOT][i] == 0)
							break;

					if (i < 100) {
						MakeFilePath(filepath, FILE_SNAPSHOT, ROMInfo.filename, i);
						SaveSnapshot(filepath, NOTSILENT);
						menu = MENU_GAME_SAVE;
					}
				} else // overwrite SRAM/Snapshot
				{
					MakeFilePath(filepath, saves.type[ret], saves.filename[ret]);
					switch (saves.type[ret]) {
						case FILE_SRAM:
							SaveSRAM(filepath, NOTSILENT);
							break;
						case FILE_SNAPSHOT:
							SaveSnapshot(filepath, NOTSILENT);
							break;
					}
					menu = MENU_GAME_SAVE;
				}
			}
		}

		if (backBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAME;
		} else if (closeBtn.GetState() == STATE_CLICKED) {
			menu = MENU_EXIT;

			exitSound->Play();
			bgTopImg->SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			closeBtn.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			titleTxt.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			backBtn.SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);
			bgBottomImg->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);
			btnLogo->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);

			w.SetEffect(EFFECT_FADE, -15);

			usleep(350000); // wait for effects to finish
		}
	}

	HaltGui();

	for (i = 0; i < saves.length; i++)
		if (saves.previewImg[i])
			delete saves.previewImg[i];

	mainWindow->Remove(&saveBrowser);
	mainWindow->Remove(&w);
	mainWindow->Remove(&titleTxt);
	ResetBrowser();
	return menu;
}

/****************************************************************************
 * MenuGameSettings
 ***************************************************************************/
static int MenuGameSettings() {
	int menu = MENU_NONE;

	GuiText titleTxt("Game Settings", 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiImageData btnLargeOutline(button_large_png);
	GuiImageData btnLargeOutlineOver(button_large_over_png);
	GuiImageData iconMappings(icon_settings_mappings_png);
	GuiImageData iconVideo(icon_settings_video_png);
	GuiImageData iconController(icon_game_controllers_png);
	GuiImageData iconCheats(icon_game_cheats_png);
	GuiImageData btnCloseOutline(button_small_png);
	GuiImageData btnCloseOutlineOver(button_small_over_png);

	GuiTrigger trigHome;
	trigHome.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);

	GuiText mappingBtnTxt("Button Mappings", 22, (GXColor) {
		0, 0, 0, 255
	});
	mappingBtnTxt.SetWrap(true, btnLargeOutline.GetWidth() - 30);
	GuiImage mappingBtnImg(&btnLargeOutline);
	GuiImage mappingBtnImgOver(&btnLargeOutlineOver);
	GuiImage mappingBtnIcon(&iconMappings);
	GuiButton mappingBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	mappingBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	mappingBtn.SetPosition(-125, 200);
	mappingBtn.SetLabel(&mappingBtnTxt);
	mappingBtn.SetImage(&mappingBtnImg);
	mappingBtn.SetImageOver(&mappingBtnImgOver);
	mappingBtn.SetIcon(&mappingBtnIcon);
	mappingBtn.SetTrigger(trigA);
	mappingBtn.SetTrigger(trig2);
	mappingBtn.SetEffectGrow();

	GuiText videoBtnTxt("Video", 22, (GXColor) {
		0, 0, 0, 255
	});
	videoBtnTxt.SetWrap(true, btnLargeOutline.GetWidth() - 20);
	GuiImage videoBtnImg(&btnLargeOutline);
	GuiImage videoBtnImgOver(&btnLargeOutlineOver);
	GuiImage videoBtnIcon(&iconVideo);
	GuiButton videoBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	videoBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	videoBtn.SetPosition(125, 200);
	videoBtn.SetLabel(&videoBtnTxt);
	videoBtn.SetImage(&videoBtnImg);
	videoBtn.SetImageOver(&videoBtnImgOver);
	videoBtn.SetIcon(&videoBtnIcon);
	videoBtn.SetTrigger(trigA);
	videoBtn.SetTrigger(trig2);
	videoBtn.SetEffectGrow();

#ifndef NO_CONTROLLER_CONFIG
	GuiText controllerBtnTxt("Controller", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage controllerBtnImg(&btnLargeOutline);
	GuiImage controllerBtnImgOver(&btnLargeOutlineOver);
	GuiImage controllerBtnIcon(&iconController);
	GuiButton controllerBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	controllerBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	controllerBtn.SetPosition(-125, 400);
	controllerBtn.SetLabel(&controllerBtnTxt);
	controllerBtn.SetImage(&controllerBtnImg);
	controllerBtn.SetImageOver(&controllerBtnImgOver);
	controllerBtn.SetIcon(&controllerBtnIcon);
	controllerBtn.SetTrigger(trigA);
	controllerBtn.SetTrigger(trig2);
	controllerBtn.SetEffectGrow();
#endif

#ifndef NO_CHEAT_CONFIG
	GuiText cheatsBtnTxt("Cheats", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage cheatsBtnImg(&btnLargeOutline);
	GuiImage cheatsBtnImgOver(&btnLargeOutlineOver);
	GuiImage cheatsBtnIcon(&iconCheats);
	GuiButton cheatsBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	cheatsBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	cheatsBtn.SetPosition(125, 400);
	cheatsBtn.SetLabel(&cheatsBtnTxt);
	cheatsBtn.SetImage(&cheatsBtnImg);
	cheatsBtn.SetImageOver(&cheatsBtnImgOver);
	cheatsBtn.SetIcon(&cheatsBtnIcon);
	cheatsBtn.SetTrigger(trigA);
	cheatsBtn.SetTrigger(trig2);
	cheatsBtn.SetEffectGrow();
#endif

	GuiText closeBtnTxt("Close", 20, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage closeBtnImg(&btnCloseOutline);
	GuiImage closeBtnImgOver(&btnCloseOutlineOver);
	GuiButton closeBtn(btnCloseOutline.GetWidth(), btnCloseOutline.GetHeight());
	closeBtn.SetAlignment(ALIGN_RIGHT, ALIGN_TOP);
	closeBtn.SetPosition(-50, 35);
	closeBtn.SetLabel(&closeBtnTxt);
	closeBtn.SetImage(&closeBtnImg);
	closeBtn.SetImageOver(&closeBtnImgOver);
	closeBtn.SetTrigger(trigA);
	closeBtn.SetTrigger(trig2);
	closeBtn.SetTrigger(&trigHome);
	closeBtn.SetEffectGrow();

	GuiText backBtnTxt("Go Back", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(50, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetTrigger(trigA);
	backBtn.SetTrigger(trig2);
	backBtn.SetEffectGrow();

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&mappingBtn);
	w.Append(&videoBtn);
#ifndef NO_CONTROLLER_CONFIG	
	w.Append(&controllerBtn);
#endif
#ifndef NO_CHEAT_CONFIG
	w.Append(&cheatsBtn);
#endif
	w.Append(&closeBtn);
	w.Append(&backBtn);

	mainWindow->Append(&w);

	ResumeGui();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (mappingBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAMESETTINGS_MAPPINGS;
		} else if (videoBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAMESETTINGS_VIDEO;
#ifndef NO_CONTROLLER_CONFIG			
		} else if (controllerBtn.GetState() == STATE_CLICKED) {
			ControllerWindow();
#endif
#ifndef NO_CHEAT_CONFIG	
		} else if (cheatsBtn.GetState() == STATE_CLICKED) {
			cheatsBtn.ResetState();
			if (Cheat.num_cheats > 0)
				menu = MENU_GAMESETTINGS_CHEATS;
			else
				InfoPrompt("Cheats file not found!");
#endif				
		} else if (closeBtn.GetState() == STATE_CLICKED) {
			menu = MENU_EXIT;

			exitSound->Play();
			bgTopImg->SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			closeBtn.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			titleTxt.SetEffect(EFFECT_SLIDE_TOP | EFFECT_SLIDE_OUT, 15);
			backBtn.SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);
			bgBottomImg->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);
			btnLogo->SetEffect(EFFECT_SLIDE_BOTTOM | EFFECT_SLIDE_OUT, 15);

			w.SetEffect(EFFECT_FADE, -15);

			usleep(350000); // wait for effects to finish
		} else if (backBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAME;
		}
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

/****************************************************************************
 * MenuSettingsVideo
 ***************************************************************************/

static void ScreenZoomWindowUpdate(void * ptr, float h, float v) {
	GuiButton * b = (GuiButton *) ptr;
	if (b->GetState() == STATE_CLICKED) {
		EMUSettings.zoomHor += h;
		EMUSettings.zoomVert += v;

		char zoom[10];
		sprintf(zoom, "%.2f%%", EMUSettings.zoomHor * 100);
		settingText->SetText(zoom);
		sprintf(zoom, "%.2f%%", EMUSettings.zoomVert * 100);
		settingText2->SetText(zoom);
		b->ResetState();
	}
}

static void ScreenZoomWindowLeftClick(void * ptr) {
	ScreenZoomWindowUpdate(ptr, -0.01, 0);
}

static void ScreenZoomWindowRightClick(void * ptr) {
	ScreenZoomWindowUpdate(ptr, +0.01, 0);
}

static void ScreenZoomWindowUpClick(void * ptr) {
	ScreenZoomWindowUpdate(ptr, 0, +0.01);
}

static void ScreenZoomWindowDownClick(void * ptr) {
	ScreenZoomWindowUpdate(ptr, 0, -0.01);
}

static void ScreenZoomWindow() {
	GuiWindow * w = new GuiWindow(200, 200);
	w->SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);

	GuiTrigger trigLeft;
	trigLeft.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT, PAD_BUTTON_LEFT);

	GuiTrigger trigRight;
	trigRight.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT, PAD_BUTTON_RIGHT);

	GuiTrigger trigUp;
	trigUp.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP, PAD_BUTTON_UP);

	GuiTrigger trigDown;
	trigDown.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN, PAD_BUTTON_DOWN);

	GuiImageData arrowLeft(button_arrow_left_png);
	GuiImage arrowLeftImg(&arrowLeft);
	GuiImageData arrowLeftOver(button_arrow_left_over_png);
	GuiImage arrowLeftOverImg(&arrowLeftOver);
	GuiButton arrowLeftBtn(arrowLeft.GetWidth(), arrowLeft.GetHeight());
	arrowLeftBtn.SetImage(&arrowLeftImg);
	arrowLeftBtn.SetImageOver(&arrowLeftOverImg);
	arrowLeftBtn.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	arrowLeftBtn.SetPosition(50, 0);
	arrowLeftBtn.SetTrigger(trigA);
	arrowLeftBtn.SetTrigger(trig2);
	arrowLeftBtn.SetTrigger(&trigLeft);
	arrowLeftBtn.SetSelectable(false);
	arrowLeftBtn.SetUpdateCallback(ScreenZoomWindowLeftClick);

	GuiImageData arrowRight(button_arrow_right_png);
	GuiImage arrowRightImg(&arrowRight);
	GuiImageData arrowRightOver(button_arrow_right_over_png);
	GuiImage arrowRightOverImg(&arrowRightOver);
	GuiButton arrowRightBtn(arrowRight.GetWidth(), arrowRight.GetHeight());
	arrowRightBtn.SetImage(&arrowRightImg);
	arrowRightBtn.SetImageOver(&arrowRightOverImg);
	arrowRightBtn.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	arrowRightBtn.SetPosition(164, 0);
	arrowRightBtn.SetTrigger(trigA);
	arrowRightBtn.SetTrigger(trig2);
	arrowRightBtn.SetTrigger(&trigRight);
	arrowRightBtn.SetSelectable(false);
	arrowRightBtn.SetUpdateCallback(ScreenZoomWindowRightClick);

	GuiImageData arrowUp(button_arrow_up_png);
	GuiImage arrowUpImg(&arrowUp);
	GuiImageData arrowUpOver(button_arrow_up_over_png);
	GuiImage arrowUpOverImg(&arrowUpOver);
	GuiButton arrowUpBtn(arrowUp.GetWidth(), arrowUp.GetHeight());
	arrowUpBtn.SetImage(&arrowUpImg);
	arrowUpBtn.SetImageOver(&arrowUpOverImg);
	arrowUpBtn.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	arrowUpBtn.SetPosition(-76, -27);
	arrowUpBtn.SetTrigger(trigA);
	arrowUpBtn.SetTrigger(trig2);
	arrowUpBtn.SetTrigger(&trigUp);
	arrowUpBtn.SetSelectable(false);
	arrowUpBtn.SetUpdateCallback(ScreenZoomWindowUpClick);

	GuiImageData arrowDown(button_arrow_down_png);
	GuiImage arrowDownImg(&arrowDown);
	GuiImageData arrowDownOver(button_arrow_down_over_png);
	GuiImage arrowDownOverImg(&arrowDownOver);
	GuiButton arrowDownBtn(arrowDown.GetWidth(), arrowDown.GetHeight());
	arrowDownBtn.SetImage(&arrowDownImg);
	arrowDownBtn.SetImageOver(&arrowDownOverImg);
	arrowDownBtn.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	arrowDownBtn.SetPosition(-76, 27);
	arrowDownBtn.SetTrigger(trigA);
	arrowDownBtn.SetTrigger(trig2);
	arrowDownBtn.SetTrigger(&trigDown);
	arrowDownBtn.SetSelectable(false);
	arrowDownBtn.SetUpdateCallback(ScreenZoomWindowDownClick);

	GuiImageData screenPosition(screen_position_png);
	GuiImage screenPositionImg(&screenPosition);
	screenPositionImg.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	screenPositionImg.SetPosition(0, 0);

	settingText = new GuiText(NULL, 20, (GXColor) {
		0, 0, 0, 255
	});

	settingText2 = new GuiText(NULL, 20, (GXColor) {
		0, 0, 0, 255
	});
	char zoom[10];
	sprintf(zoom, "%.2f%%", EMUSettings.zoomHor * 100);
	settingText->SetText(zoom);
	settingText->SetPosition(108, 0);
	sprintf(zoom, "%.2f%%", EMUSettings.zoomVert * 100);
	settingText2->SetText(zoom);
	settingText2->SetPosition(-76, 0);

	float currentZoomHor = EMUSettings.zoomHor;
	float currentZoomVert = EMUSettings.zoomVert;

	w->Append(&arrowLeftBtn);
	w->Append(&arrowRightBtn);
	w->Append(&arrowUpBtn);
	w->Append(&arrowDownBtn);
	w->Append(&screenPositionImg);
	w->Append(settingText);
	w->Append(settingText2);

	if (!SettingWindow("Screen Zoom", w)) {
		// undo changes
		EMUSettings.zoomHor = currentZoomHor;
		EMUSettings.zoomVert = currentZoomVert;
	}

	delete(w);
	delete(settingText);
	delete(settingText2);
}

static void ScreenPositionWindowUpdate(void * ptr, int x, int y) {
	GuiButton * b = (GuiButton *) ptr;
	if (b->GetState() == STATE_CLICKED) {
		EMUSettings.xshift += x;
		EMUSettings.yshift += y;

		char shift[10];
		sprintf(shift, "%i, %i", EMUSettings.xshift, EMUSettings.yshift);
		settingText->SetText(shift);
		b->ResetState();
	}
}

static void ScreenPositionWindowLeftClick(void * ptr) {
	ScreenPositionWindowUpdate(ptr, -1, 0);
}

static void ScreenPositionWindowRightClick(void * ptr) {
	ScreenPositionWindowUpdate(ptr, +1, 0);
}

static void ScreenPositionWindowUpClick(void * ptr) {
	ScreenPositionWindowUpdate(ptr, 0, -1);
}

static void ScreenPositionWindowDownClick(void * ptr) {
	ScreenPositionWindowUpdate(ptr, 0, +1);
}

static void ScreenPositionWindow() {
	GuiWindow * w = new GuiWindow(150, 150);
	w->SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	w->SetPosition(0, -10);

	GuiTrigger trigLeft;
	trigLeft.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT, PAD_BUTTON_LEFT);

	GuiTrigger trigRight;
	trigRight.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT, PAD_BUTTON_RIGHT);

	GuiTrigger trigUp;
	trigUp.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP, PAD_BUTTON_UP);

	GuiTrigger trigDown;
	trigDown.SetButtonOnlyInFocusTrigger(-1, WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN, PAD_BUTTON_DOWN);

	GuiImageData arrowLeft(button_arrow_left_png);
	GuiImage arrowLeftImg(&arrowLeft);
	GuiImageData arrowLeftOver(button_arrow_left_over_png);
	GuiImage arrowLeftOverImg(&arrowLeftOver);
	GuiButton arrowLeftBtn(arrowLeft.GetWidth(), arrowLeft.GetHeight());
	arrowLeftBtn.SetImage(&arrowLeftImg);
	arrowLeftBtn.SetImageOver(&arrowLeftOverImg);
	arrowLeftBtn.SetAlignment(ALIGN_LEFT, ALIGN_MIDDLE);
	arrowLeftBtn.SetTrigger(trigA);
	arrowLeftBtn.SetTrigger(trig2);
	arrowLeftBtn.SetTrigger(&trigLeft);
	arrowLeftBtn.SetSelectable(false);
	arrowLeftBtn.SetUpdateCallback(ScreenPositionWindowLeftClick);

	GuiImageData arrowRight(button_arrow_right_png);
	GuiImage arrowRightImg(&arrowRight);
	GuiImageData arrowRightOver(button_arrow_right_over_png);
	GuiImage arrowRightOverImg(&arrowRightOver);
	GuiButton arrowRightBtn(arrowRight.GetWidth(), arrowRight.GetHeight());
	arrowRightBtn.SetImage(&arrowRightImg);
	arrowRightBtn.SetImageOver(&arrowRightOverImg);
	arrowRightBtn.SetAlignment(ALIGN_RIGHT, ALIGN_MIDDLE);
	arrowRightBtn.SetTrigger(trigA);
	arrowRightBtn.SetTrigger(trig2);
	arrowRightBtn.SetTrigger(&trigRight);
	arrowRightBtn.SetSelectable(false);
	arrowRightBtn.SetUpdateCallback(ScreenPositionWindowRightClick);

	GuiImageData arrowUp(button_arrow_up_png);
	GuiImage arrowUpImg(&arrowUp);
	GuiImageData arrowUpOver(button_arrow_up_over_png);
	GuiImage arrowUpOverImg(&arrowUpOver);
	GuiButton arrowUpBtn(arrowUp.GetWidth(), arrowUp.GetHeight());
	arrowUpBtn.SetImage(&arrowUpImg);
	arrowUpBtn.SetImageOver(&arrowUpOverImg);
	arrowUpBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	arrowUpBtn.SetTrigger(trigA);
	arrowUpBtn.SetTrigger(trig2);
	arrowUpBtn.SetTrigger(&trigUp);
	arrowUpBtn.SetSelectable(false);
	arrowUpBtn.SetUpdateCallback(ScreenPositionWindowUpClick);

	GuiImageData arrowDown(button_arrow_down_png);
	GuiImage arrowDownImg(&arrowDown);
	GuiImageData arrowDownOver(button_arrow_down_over_png);
	GuiImage arrowDownOverImg(&arrowDownOver);
	GuiButton arrowDownBtn(arrowDown.GetWidth(), arrowDown.GetHeight());
	arrowDownBtn.SetImage(&arrowDownImg);
	arrowDownBtn.SetImageOver(&arrowDownOverImg);
	arrowDownBtn.SetAlignment(ALIGN_CENTRE, ALIGN_BOTTOM);
	arrowDownBtn.SetTrigger(trigA);
	arrowDownBtn.SetTrigger(trig2);
	arrowDownBtn.SetTrigger(&trigDown);
	arrowDownBtn.SetSelectable(false);
	arrowDownBtn.SetUpdateCallback(ScreenPositionWindowDownClick);

	GuiImageData screenPosition(screen_position_png);
	GuiImage screenPositionImg(&screenPosition);
	screenPositionImg.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);

	settingText = new GuiText(NULL, 20, (GXColor) {
		0, 0, 0, 255
	});
	char shift[10];
	sprintf(shift, "%i, %i", EMUSettings.xshift, EMUSettings.yshift);
	settingText->SetText(shift);

	int currentX = EMUSettings.xshift;
	int currentY = EMUSettings.yshift;

	w->Append(&arrowLeftBtn);
	w->Append(&arrowRightBtn);
	w->Append(&arrowUpBtn);
	w->Append(&arrowDownBtn);
	w->Append(&screenPositionImg);
	w->Append(settingText);

	if (!SettingWindow("Screen Position", w)) {
		// undo changes
		EMUSettings.xshift = currentX;
		EMUSettings.yshift = currentY;
	}

	delete(w);
	delete(settingText);
}

static int MenuSettingsVideo() {
	int menu = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;

	sprintf(options.name[i++], "Rendering");
	sprintf(options.name[i++], "Scaling");
	sprintf(options.name[i++], "Filtering");
	sprintf(options.name[i++], "Screen Zoom");
	sprintf(options.name[i++], "Screen Position");
	sprintf(options.name[i++], "Crosshair");
	sprintf(options.name[i++], "Video Mode");
	options.length = i;

#ifdef HW_DOL
	options.name[2][0] = 0; // disable hq2x on GameCube
#endif

	for (i = 0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Game Settings - Video", 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	GuiText backBtnTxt("Go Back", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(50, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetTrigger(trigA);
	backBtn.SetTrigger(trig2);
	backBtn.SetEffectGrow();

	GuiOptionBrowser optionBrowser(720, 426, &options);
	optionBrowser.SetCol2Position(200);
	optionBrowser.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&backBtn);
	mainWindow->Append(&optionBrowser);
	mainWindow->Append(&w);
	mainWindow->Append(&titleTxt);
	ResumeGui();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		ret = optionBrowser.GetClickedOption();

		switch (ret) {
			case 0:
				EMUSettings.render++;
				if (EMUSettings.render > 2)
					EMUSettings.render = 0;
				break;

			case 1:
				EMUSettings.widescreen ^= 1;
				break;

			case 2:
				EMUSettings.FilterMethod++;
				if (EMUSettings.FilterMethod >= GetFilterNumber())
					EMUSettings.FilterMethod = 0;
				break;

			case 3:
				ScreenZoomWindow();
				break;

			case 4:
				ScreenPositionWindow();
				break;

			case 5:
				EMUSettings.crosshair ^= 1;
				break;

			case 6:
				EMUSettings.videomode++;
				if (EMUSettings.videomode > 4)
					EMUSettings.videomode = 0;
				break;
		}

		if (ret >= 0 || firstRun) {
			firstRun = false;

			// don't allow original render mode if progressive video mode detected
			//			if (EMUSettings.render==0 && progressive)
			//				EMUSettings.render++;

			if (EMUSettings.render == 0)
				sprintf(options.value[0], "Original");
			else if (EMUSettings.render == 1)
				sprintf(options.value[0], "Filtered");
			else if (EMUSettings.render == 2)
				sprintf(options.value[0], "Unfiltered");

			if (EMUSettings.widescreen)
				sprintf(options.value[1], "16:9 Correction");
			else
				sprintf(options.value[1], "Default");
			//#ifdef HW_RVL
			#if 0
			sprintf(options.value[2], "%s", GetFilterName((RenderFilter) EMUSettings.FilterMethod));
			#endif
			sprintf(options.value[3], "%.2f%%, %.2f%%", EMUSettings.zoomHor * 100, EMUSettings.zoomVert * 100);
			sprintf(options.value[4], "%d, %d", EMUSettings.xshift, EMUSettings.yshift);
			sprintf(options.value[5], "%s", EMUSettings.crosshair == 1 ? "On" : "Off");

			switch (EMUSettings.videomode) {
				case 0:
					sprintf(options.value[6], "Automatic (Recommended)");
					break;
				case 1:
					sprintf(options.value[6], "NTSC (480i)");
					break;
				case 2:
					sprintf(options.value[6], "Progressive (480p)");
					break;
				case 3:
					sprintf(options.value[6], "PAL (50Hz)");
					break;
				case 4:
					sprintf(options.value[6], "PAL (60Hz)");
					break;
			}
			optionBrowser.TriggerUpdate();
		}

		if (backBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAMESETTINGS;
		}
	}
	HaltGui();
	mainWindow->Remove(&optionBrowser);
	mainWindow->Remove(&w);
	mainWindow->Remove(&titleTxt);
	return menu;
}

/****************************************************************************
 * MenuSettings
 ***************************************************************************/
static int MenuSettings() {
	int menu = MENU_NONE;

	GuiText titleTxt("Settings", 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData btnOutline(button_long_png);
	GuiImageData btnOutlineOver(button_long_over_png);
	GuiImageData btnLargeOutline(button_large_png);
	GuiImageData btnLargeOutlineOver(button_large_over_png);
	GuiImageData iconFile(icon_settings_file_png);
	GuiImageData iconMenu(icon_settings_menu_png);
	GuiImageData iconNetwork(icon_settings_network_png);

	GuiText savingBtnTxt1("Saving", 22, (GXColor) {
		0, 0, 0, 255
	});

	GuiText savingBtnTxt2("&", 18, (GXColor) {
		0, 0, 0, 255
	});

	GuiText savingBtnTxt3("Loading", 22, (GXColor) {
		0, 0, 0, 255
	});
	savingBtnTxt1.SetPosition(0, -20);
	savingBtnTxt3.SetPosition(0, +20);
	GuiImage savingBtnImg(&btnLargeOutline);
	GuiImage savingBtnImgOver(&btnLargeOutlineOver);
	GuiImage fileBtnIcon(&iconFile);
	GuiButton savingBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	savingBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	savingBtn.SetPosition(-125, 200);
	savingBtn.SetLabel(&savingBtnTxt1, 0);
	savingBtn.SetLabel(&savingBtnTxt2, 1);
	savingBtn.SetLabel(&savingBtnTxt3, 2);
	savingBtn.SetImage(&savingBtnImg);
	savingBtn.SetImageOver(&savingBtnImgOver);
	savingBtn.SetIcon(&fileBtnIcon);
	savingBtn.SetTrigger(trigA);
	savingBtn.SetTrigger(trig2);
	savingBtn.SetEffectGrow();

	GuiText menuBtnTxt("Settings", 22, (GXColor) {
		0, 0, 0, 255
	});
	menuBtnTxt.SetWrap(true, btnLargeOutline.GetWidth() - 20);
	GuiImage menuBtnImg(&btnLargeOutline);
	GuiImage menuBtnImgOver(&btnLargeOutlineOver);
	GuiImage menuBtnIcon(&iconMenu);
	GuiButton menuBtn(btnLargeOutline.GetWidth(), btnLargeOutline.GetHeight());
	menuBtn.SetAlignment(ALIGN_CENTRE, ALIGN_TOP);
	menuBtn.SetPosition(125, 200);
	menuBtn.SetLabel(&menuBtnTxt);
	menuBtn.SetImage(&menuBtnImg);
	menuBtn.SetImageOver(&menuBtnImgOver);
	menuBtn.SetIcon(&menuBtnIcon);
	menuBtn.SetTrigger(trigA);
	menuBtn.SetTrigger(trig2);
	menuBtn.SetEffectGrow();
	
	GuiText backBtnTxt("Go Back", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(90, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetTrigger(trigA);
	backBtn.SetTrigger(trig2);
	backBtn.SetEffectGrow();

	GuiText resetBtnTxt("Reset Settings", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage resetBtnImg(&btnOutline);
	GuiImage resetBtnImgOver(&btnOutlineOver);
	GuiButton resetBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	resetBtn.SetAlignment(ALIGN_RIGHT, ALIGN_BOTTOM);
	resetBtn.SetPosition(-90, -35);
	resetBtn.SetLabel(&resetBtnTxt);
	resetBtn.SetImage(&resetBtnImg);
	resetBtn.SetImageOver(&resetBtnImgOver);
	resetBtn.SetTrigger(trigA);
	resetBtn.SetTrigger(trig2);
	resetBtn.SetEffectGrow();

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&savingBtn);
	w.Append(&menuBtn);
	w.Append(&backBtn);
	w.Append(&resetBtn);

	mainWindow->Append(&w);

	ResumeGui();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		if (savingBtn.GetState() == STATE_CLICKED) {
			menu = MENU_SETTINGS_FILE;
		} else if (menuBtn.GetState() == STATE_CLICKED) {
			menu = MENU_SETTINGS_MENU;
		} else if (backBtn.GetState() == STATE_CLICKED) {
			menu = MENU_GAMESELECTION;
		} else if (resetBtn.GetState() == STATE_CLICKED) {
			resetBtn.ResetState();

			int choice = WindowPrompt(
					"Reset Settings",
					"Are you sure that you want to reset your settings?",
					"Yes",
					"No");
/*
			if (choice == 1)
				DefaultSettings();
*/
		}
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

/****************************************************************************
 * MenuSettingsFile
 ***************************************************************************/

static int MenuSettingsFile() {
	int menu = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;
	sprintf(options.name[i++], "Save / Load Device");
	sprintf(options.name[i++], "Save / Load Folder");
	sprintf(options.name[i++], "Cheats Folder");
	sprintf(options.name[i++], "Auto Load");
	sprintf(options.name[i++], "Auto Save");
	options.length = i;

	for (i = 0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Settings - Saving & Loading", 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData btnOutline(button_long_png);
	GuiImageData btnOutlineOver(button_long_over_png);

	GuiText backBtnTxt("Go Back", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(90, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetTrigger(trigA);
	backBtn.SetTrigger(trig2);
	backBtn.SetEffectGrow();

	GuiOptionBrowser optionBrowser(720, 426, &options);
	optionBrowser.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	optionBrowser.SetCol2Position(215);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&backBtn);
	mainWindow->Append(&optionBrowser);
	mainWindow->Append(&w);
	mainWindow->Append(&titleTxt);
	ResumeGui();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		ret = optionBrowser.GetClickedOption();

		switch (ret) {
			case 0:
				EMUSettings.SaveMethod++;
				break;

			case 1:
				OnScreenKeyboard(EMUSettings.SaveFolder, MAXPATHLEN);
				break;

			case 2:
				OnScreenKeyboard(EMUSettings.CheatFolder, MAXPATHLEN);
				break;

			case 3:
				EMUSettings.AutoLoad++;
				if (EMUSettings.AutoLoad > 2)
					EMUSettings.AutoLoad = 0;
				break;

			case 4:
				EMUSettings.AutoSave++;
				if (EMUSettings.AutoSave > 3)
					EMUSettings.AutoSave = 0;
				break;
		}

		if (ret >= 0 || firstRun) {
			firstRun = false;
			
			// correct load/save methods out of bounds
			if (EMUSettings.SaveMethod >= devsinfo.save.nbr)
				EMUSettings.SaveMethod = 0;

			sprintf(options.value[0], devsinfo.save.path[EMUSettings.SaveMethod]);
			snprintf(options.value[1], 35, "%s", EMUSettings.SaveFolder);
			snprintf(options.value[2], 35, "%s", EMUSettings.CheatFolder);

			if (EMUSettings.AutoLoad == 0) sprintf(options.value[3], "Off");
			else if (EMUSettings.AutoLoad == 1) sprintf(options.value[3], "SRAM");
			else if (EMUSettings.AutoLoad == 2) sprintf(options.value[3], "Snapshot");

			if (EMUSettings.AutoSave == 0) sprintf(options.value[4], "Off");
			else if (EMUSettings.AutoSave == 1) sprintf(options.value[4], "SRAM");
			else if (EMUSettings.AutoSave == 2) sprintf(options.value[4], "Snapshot");
			else if (EMUSettings.AutoSave == 3) sprintf(options.value[4], "Both");

			optionBrowser.TriggerUpdate();
		}

		if (backBtn.GetState() == STATE_CLICKED) {
			menu = MENU_SETTINGS;
		}
	}
	HaltGui();
	mainWindow->Remove(&optionBrowser);
	mainWindow->Remove(&w);
	mainWindow->Remove(&titleTxt);
	return menu;
}

/****************************************************************************
 * MenuSettingsMenu
 ***************************************************************************/
enum {
	SETTING_LANG,
	SETTING_EXIT_ACTION,
	SETTING_CPU,
	SETTING_GPU,
	SETIING_FRAMELIMIT,
	SETTING_HW_FILTER,
	SETTING_SW_FILTER,
	SETTING_MAX,
};

static int MenuSettingsMenu() {
	int menu = MENU_NONE;
	int ret;
	bool firstRun = true;
	OptionList options;

	sprintf(options.name[SETTING_LANG], "Language");
	sprintf(options.name[SETTING_EXIT_ACTION], "Exit Action");
	sprintf(options.name[SETTING_CPU], "CPU Mode");
	sprintf(options.name[SETIING_FRAMELIMIT], "Framelimit");
	sprintf(options.name[SETTING_GPU], "GPU Plugin");
	sprintf(options.name[SETTING_HW_FILTER], "HARDWARE GPU Filter");
	sprintf(options.name[SETTING_SW_FILTER], "SOFT GPU Filter");
	options.length = SETTING_MAX;

	for (int i = 0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Settings - Menu", 26, (GXColor) {
		255, 255, 255, 255
	});
	titleTxt.SetAlignment(ALIGN_LEFT, ALIGN_TOP);
	titleTxt.SetPosition(50, 50);

	GuiImageData btnOutline(button_long_png);
	GuiImageData btnOutlineOver(button_long_over_png);

	GuiText backBtnTxt("Go Back", 22, (GXColor) {
		0, 0, 0, 255
	});
	GuiImage backBtnImg(&btnOutline);
	GuiImage backBtnImgOver(&btnOutlineOver);
	GuiButton backBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	backBtn.SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	backBtn.SetPosition(90, -35);
	backBtn.SetLabel(&backBtnTxt);
	backBtn.SetImage(&backBtnImg);
	backBtn.SetImageOver(&backBtnImgOver);
	backBtn.SetTrigger(trigA);
	backBtn.SetTrigger(trig2);
	backBtn.SetEffectGrow();

	GuiOptionBrowser optionBrowser(720, 426, &options);
	optionBrowser.SetAlignment(ALIGN_CENTRE, ALIGN_MIDDLE);
	optionBrowser.SetCol2Position(275);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&backBtn);
	mainWindow->Append(&optionBrowser);
	mainWindow->Append(&w);
	mainWindow->Append(&titleTxt);
	ResumeGui();

	while (menu == MENU_NONE) {
		UGUI();
		usleep(THREAD_SLEEP);

		ret = optionBrowser.GetClickedOption();

		switch (ret) {
			case SETTING_EXIT_ACTION:
				EMUSettings.ExitAction++;
				if (EMUSettings.ExitAction > 1)
					EMUSettings.ExitAction = 0;
				break;
			
			case SETTING_LANG:
				EMUSettings.language++;

				if (EMUSettings.language >= LANG_LENGTH)
					EMUSettings.language = LANG_JAPANESE;

				if (EMUSettings.language == LANG_SIMP_CHINESE)
					EMUSettings.language = LANG_PORTUGUESE;
				else if (EMUSettings.language == LANG_JAPANESE)
					EMUSettings.language = LANG_ENGLISH;

				break;
			
			case SETTING_CPU:
				EMUSettings.use_interpreter++;

				if (EMUSettings.use_interpreter > 1)
					EMUSettings.use_interpreter = 0;

				break;
				
			case SETTING_GPU:
				EMUSettings.use_gpu_soft_plugin++;

				if (EMUSettings.use_gpu_soft_plugin > 1)
					EMUSettings.use_gpu_soft_plugin = 0;

				break;
	
			case SETTING_HW_FILTER:
				EMUSettings.hw_filter++;

				if (EMUSettings.hw_filter > 1)
					EMUSettings.hw_filter = 0;

				break;	
				
			case SETTING_SW_FILTER:
				EMUSettings.sw_filter++;

				if (EMUSettings.sw_filter > 1)
					EMUSettings.sw_filter = 0;

				break;
				
			case SETIING_FRAMELIMIT:
				EMUSettings.framelimit++;

				if (EMUSettings.framelimit > 1)
					EMUSettings.framelimit = 0;

				break;
		}

		if (ret >= 0 || firstRun) {
			firstRun = false;
			if (EMUSettings.ExitAction == 0)
				sprintf(options.value[SETTING_EXIT_ACTION], "Return to Xell");
			else
				sprintf(options.value[SETTING_EXIT_ACTION], "Shutdown");
				
			switch (EMUSettings.language) {
				case LANG_JAPANESE: sprintf(options.value[SETTING_LANG], "Japanese");
					break;
				case LANG_ENGLISH: sprintf(options.value[SETTING_LANG], "English");
					break;
				case LANG_GERMAN: sprintf(options.value[SETTING_LANG], "German");
					break;
				case LANG_FRENCH: sprintf(options.value[SETTING_LANG], "French");
					break;
				case LANG_SPANISH: sprintf(options.value[SETTING_LANG], "Spanish");
					break;
				case LANG_ITALIAN: sprintf(options.value[SETTING_LANG], "Italian");
					break;
				case LANG_DUTCH: sprintf(options.value[SETTING_LANG], "Dutch");
					break;
				case LANG_SIMP_CHINESE: sprintf(options.value[SETTING_LANG], "Chinese (Simplified)");
					break;
				case LANG_TRAD_CHINESE: sprintf(options.value[SETTING_LANG], "Chinese (Traditional)");
					break;
				case LANG_KOREAN: sprintf(options.value[SETTING_LANG], "Korean");
					break;
				case LANG_PORTUGUESE: sprintf(options.value[SETTING_LANG], "Portuguese");
					break;
				case LANG_BRAZILIAN_PORTUGUESE: sprintf(options.value[SETTING_LANG], "Brazilian Portuguese");
					break;
				case LANG_CATALAN: sprintf(options.value[SETTING_LANG], "Catalan");
					break;
				case LANG_TURKISH: sprintf(options.value[SETTING_LANG], "Turkish");
					break;
			}

			if (EMUSettings.use_interpreter > 0)
				sprintf(options.value[SETTING_CPU], "Interpreter");
			else
				sprintf(options.value[SETTING_CPU], "Dynarec");

			if (EMUSettings.use_gpu_soft_plugin > 0)
				sprintf(options.value[SETTING_GPU], "Software");
			else
				sprintf(options.value[SETTING_GPU], "Hardware");

			if (EMUSettings.framelimit == 1)
				sprintf(options.value[SETIING_FRAMELIMIT], "Enabled");
			else
				sprintf(options.value[SETIING_FRAMELIMIT], "Disabled");
				
			if (EMUSettings.hw_filter == 1)
				sprintf(options.value[SETTING_HW_FILTER], "2xSai");
			else
				sprintf(options.value[SETTING_HW_FILTER], "Disabled");
				
			if (EMUSettings.sw_filter == 1)
				sprintf(options.value[SETTING_SW_FILTER], "Xbr");
			else
				sprintf(options.value[SETTING_SW_FILTER], "Disabled");


			optionBrowser.TriggerUpdate();
		}

		if (backBtn.GetState() == STATE_CLICKED) {
			menu = MENU_SETTINGS;
		}
	}
	HaltGui();
	mainWindow->Remove(&optionBrowser);
	mainWindow->Remove(&w);
	mainWindow->Remove(&titleTxt);
	ResetText();
	return menu;
}

/****************************************************************************
 * MainMenu
 ***************************************************************************/

void
MainMenu(int menu) {
	static bool init = false;
	int currentMenu = menu;
	lastMenu = MENU_NONE;

	if (!init) {
		init = true;
		trigA = new GuiTrigger;
		trigA->SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
		trig2 = new GuiTrigger;
		trig2->SetSimpleTrigger(-1, WPAD_BUTTON_2, 0);
	}

	mainWindow = new GuiWindow(screenwidth, screenheight);

	XenosSurface * snes_surface = EMUInterface.GetSurface();
	gameScreenImg = new GuiImage(snes_surface, snes_surface->width, snes_surface->height);

	if (menu == MENU_GAME) {
		gameScreenImg->SetAlpha(192);
		//                gameScreenImg->ColorStripe(30);
	} else {
		//                gameScreenImg->ColorStripe(10);
	}

	mainWindow->Append(gameScreenImg);

	GuiImageData bgTop(bg_top_png);
	bgTopImg = new GuiImage(&bgTop);
	GuiImageData bgBottom(bg_bottom_png);
	bgBottomImg = new GuiImage(&bgBottom);
	bgBottomImg->SetAlignment(ALIGN_LEFT, ALIGN_BOTTOM);
	GuiImageData logo(logo_png);
	GuiImage logoImg(&logo);
	GuiImageData logoOver(logo_over_png);
	GuiImage logoImgOver(&logoOver);

	GuiText logoTxt(APPVERSION, 18, (GXColor) {
		255, 255, 255, 255
	});
	logoTxt.SetAlignment(ALIGN_RIGHT, ALIGN_TOP);
	logoTxt.SetPosition(0, 4);
	btnLogo = new GuiButton(logoImg.GetWidth(), logoImg.GetHeight());
	btnLogo->SetAlignment(ALIGN_RIGHT, ALIGN_TOP);
	btnLogo->SetPosition(-50, 24);
	btnLogo->SetImage(&logoImg);
	btnLogo->SetImageOver(&logoImgOver);
	btnLogo->SetLabel(&logoTxt);
	btnLogo->SetTrigger(trigA);
	btnLogo->SetTrigger(trig2);
	btnLogo->SetUpdateCallback(WindowCredits);

	mainWindow->Append(bgTopImg);
	mainWindow->Append(bgBottomImg);
	mainWindow->Append(btnLogo);

	if (currentMenu == MENU_GAMESELECTION)
		ResumeGui();

	// Load preferences
	if (!LoadPrefs())
		SavePrefs(SILENT);

	while (currentMenu != MENU_EXIT || EMUInterface.Running() <= 0) {
		switch (currentMenu) {
			case MENU_GAMESELECTION:
				currentMenu = MenuGameSelection();
				break;
			case MENU_GAME:
				currentMenu = MenuGame();
				break;
			case MENU_GAME_LOAD:
				currentMenu = MenuGameSaves(0);
				break;
			case MENU_GAME_SAVE:
				currentMenu = MenuGameSaves(1);
				break;
			case MENU_GAMESETTINGS:
				currentMenu = MenuGameSettings();
				break;
#ifndef NO_CONTROLLER_CONFIG				
			case MENU_GAMESETTINGS_MAPPINGS:
				currentMenu = MenuSettingsMappings();
				break;
			case MENU_GAMESETTINGS_MAPPINGS_CTRL:
				currentMenu = MenuSettingsMappingsController();
				break;
			case MENU_GAMESETTINGS_MAPPINGS_MAP:
				currentMenu = MenuSettingsMappingsMap();
				break;
#endif
			case MENU_GAMESETTINGS_VIDEO:
				currentMenu = MenuSettingsVideo();
				break;
#ifndef NO_CHEAT_CONFIG
			case MENU_GAMESETTINGS_CHEATS:
				currentMenu = MenuGameCheats();
				break;
#endif				
			case MENU_SETTINGS:
				currentMenu = MenuSettings();
				break;
			case MENU_SETTINGS_FILE:
				currentMenu = MenuSettingsFile();
				break;
			case MENU_SETTINGS_MENU:
				currentMenu = MenuSettingsMenu();
				break;
			default: // unrecognized menu
				currentMenu = MenuGameSelection();
				break;
		}
		lastMenu = currentMenu;
		usleep(THREAD_SLEEP);
	}


	CancelAction();
	HaltGui();

#ifndef NO_SOUND
	delete bgMusic;
	delete enterSound;
	delete exitSound;
#endif

	delete btnLogo;
	delete gameScreenImg;
	delete bgTopImg;
	delete bgBottomImg;
	delete mainWindow;

	mainWindow = NULL;

	if (gameScreen)
		delete gameScreen;

	//	if(gameScreenPng)
	//	{
	//		free(gameScreenPng);
	//		gameScreenPng = NULL;
	//	}

	// wait for keys to be depressed
	while (MenuRequested()) {
		UpdatePads();
		usleep(THREAD_SLEEP);
	}
}
