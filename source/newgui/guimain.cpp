#if 1
/****************************************************************************
 * Snes9x Nintendo Wii/Gamecube Port
 *
 * softdev July 2006
 * crunchy2 May 2007-July 2007
 * Michniewski 2008
 * Tantric 2008-2010
 *
 * snes9xgx.cpp
 *
 * This file controls overall program flow. Most things start and end here!
 ***************************************************************************/

#include <xetypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include "w_input.h"
#include <libfat/fat.h>
#include <libntfs/ntfs.h>
#include <libxtaf/xtaf.h>
#include <debug.h>
#include <sys/iosupport.h>
#include <diskio/ata.h>
#include <usb/usbmain.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>
#include <debug.h>

#include "cmount.h"

#include "filelist.h"
#include "emu.h"
#include "video.h"
#include "menu.h"
#include "sram.h"
#include "freeze.h"
#include "preferences.h"
#include "button_mapping.h"
#include "fileop.h"
#include "filebrowser.h"
#include "input.h"
#include "FreeTypeGX.h"
#include "utils/gettext.h"

extern "C" {
	#include "config.h"
	#include "r3000a.h"
	#include "psxcommon.h"
	#include "debug.h"
	#include "sio.h"
	#include "misc.h"
}

int ScreenshotRequested = 0;
int ConfigRequested = 0;
int ShutdownRequested = 0;
int ResetRequested = 0;
int ExitRequested = 0;
char appPath[1024] = { 0 };
char loadedFile[1024] = { 0 };
static int currentMode;
int exitThreads = 0;

extern uint32_t prevRenderedFrameCount;

/****************************************************************************
 * Shutdown / Reboot / Exit
 ***************************************************************************/

void ExitCleanup()
{
	HaltDeviceThread();
	UnmountAllFAT();
}

void ExitApp()
{
	SavePrefs(SILENT);

	SaveSRAMAuto(SILENT);

	ExitCleanup();

	exitThreads = 1;
	
	if(EMUSettings.ExitAction==0)
		exit(0);
	else
		xenon_smc_power_shutdown();
}

int main(int argc, char *argv[])
{
	xenon_make_it_faster(XENON_SPEED_FULL);
	usb_init();
	usb_do_poll();
	xenon_ata_init();
	xenon_atapi_init();
	xenon_sound_init();

	InitDeviceThread();
	//InitGCVideo(); // Initialise video
	InitVideo();
	ResetVideo_Menu (); // change to menu video mode
	SetupPads();
	// XTAFMount();
	
	//fatInitDefault(); // Initialize libFAT for SD and USB
	mount_all();
#if 0
	DefaultSettings (); // Set defaults
#endif

	InitFreeType((u8*)font_ttf, font_ttf_size); // Initialize font system
	
	savebuffer = (unsigned char *)malloc(SAVEBUFFERSIZE);
	browserList = (BROWSERENTRY *)malloc(sizeof(BROWSERENTRY)*MAX_BROWSER_SIZE);
	
	InitGUIThreads();
	LoadLanguage();
	
	EMUInterface.ScanRootdir();
	EMUInterface.PowerOff();
	
	while (1) // main loop
	{
		ResumeDeviceThread();

		if(EMUInterface.Running() == 0)
			MainMenu(MENU_GAMESELECTION);
		else
			MainMenu(MENU_GAME);
		
		ConfigRequested = 0;
		ScreenshotRequested = 0;

		while(1) // emulation loop
		{
			EMUInterface.Step();
			
			if (EMUInterface.ResetRequested() > 0) {
				EMUInterface.Reset();
			}
			if (EMUInterface.ConfigRequested() > 0) {			
				ResetVideo_Menu();
				break;
			}
			
		} // emulation loop
	} // main loop
}
#endif