#include "pch.h"
#include "md5.h" //Credit to Frank Thilo
#include "Interp.h"
#include "Config.h"
//#include "out/detours/detours.h"


#define NOP 0x90 //NOP instruction

//Variable pointers
#define tftPtr 0x0090D398 //Min Frametime 1
#define tft1Ptr 0x0090E0C0 //Min Frametime 2 (Doesn't seem to do anything)
#define rftPtr 0x01F97058  //Reported frametime 1
#define rft1Ptr 0x01F98170 //Reported frametime 2 (Not to be trusted)
#define igmPtr 0x0207D798 /*
In game multiplier
Generally becomes 1,2,4 though you can make it anything. Disabling instructions related to this can have some weird results.
*/
#define worldMPtr 0x0207D794 /*
Wolrd multiplier (Eg skies and grass anims) @ < 1 this freezes the game, but there is still camera control
This is equal to the igm * the global time multiplier
*/
#define mouseMPtr 0x01E1A820 /*
Mouse ~~multiplier~~ Min angle coef
Realative to all other multipliers
*/
#define tScalePtr 0x0207D79C /*
Global time multiplier
When I found this everything else made so much more sense, this is what we will be changing
*/
#define framerateScalePtr 0x01E160BC /*
1 at 30 fps, 0.5 at 60
This should fix everything.
*/
#define igmSPtr 0x01FED754 /*
Currently selected state of igmptr
*/
#define uiOPtr 0x01E160C0 //Menu enabled
#define titlPtr 0x020A3380 //0 In game 1 In title
//Instruction pointers
#define cMenPtr 0x01FED3A4 /*
Combat menu pointer.
*/
#define cScenePtr 0x020AB454 /*
In cutscene pointer, is 1 when in a cutscene or at the title.
*/
#define moviePtr 0x022C0C51 /*
In Move pointer, is 1 when in a movie.
*/
#define gammaPtr 0x00900150 /*
In game gamma
*/
#define mouseFPtr 0x01F111E0 /*
Chaning this does some weird stuff to the input
*/
#define fovPtr 0x020B0258 /*
The in game FOV. This does not effect cutscenes (They use different camera)
*/
#define gammaPtr 0x00900150 /*
The in game gamma. 
*/
#define shadowTypePtr 0x0090FF10 /*
One byte, 0-6 (modulus) changes quality of shadow.
*/
#define igmUnlockPtr 0x0022AC27 /*
First overwrite to fully control the igm*/
#define igmUnlockPtr0 0x0022AC1D /*
Second overwrite to fully control the igm*/
#define igmUnlockPtr1 0x0022AC1D /*
Third overwrite to fully control the igm*/
#define igmUnlockPtr2 0x04556E00
#define igmUnlockPtr3 0x0022AC3A
#define mouseUnlockPtr 0x0022AC58 /*
Instruction writes to the mouse multi.*/
#define mouseUnlockPtr0 0x04551428 /*
Instruction writes to the mouse multi.
*/
#define fullSpeedUnlockPtr 0x00622274 /*
Erasing this results in completely unlocked framerate*/
#define fovUnlockPtr 0x06E63EE0 /*
Fixes fullscreen problem
*/
#define actionAoeFixPtr 0x0024B84C /*
This is overwritten to fix the lock that happens when a character uses an AOE, Mist, or Summon.
This was caused when the FPS coef was < 0.5, which I presume is because it was programmed to be 0.5 or 1.0
This was terribly hard to find!
*/

#define D3D11EndScene 0x7FFAD2985070 /*DX11*/

//Console color
#define cc_NORM 15
#define cc_VERBOSE 2
#define cc_FUN 5
#define cc_ERROR 12
#define cc_WARN 14

HWND FindFFXII() {
	return FindWindow(0, L"FINAL FANTASY XII THE ZODIAC AGE");
}

__forceinline void writeMinFrametime(HANDLE&hProcess, double&targetFrameTime) 
{
	DWORD protection;
	VirtualProtectEx(hProcess, (LPVOID)tftPtr, sizeof(double), PAGE_READWRITE, &protection);
	VirtualProtectEx(hProcess, (LPVOID)tft1Ptr, sizeof(double), PAGE_READWRITE, &protection);

	WriteProcessMemory(hProcess, (LPVOID)tftPtr, &targetFrameTime, sizeof(double), NULL);
	WriteProcessMemory(hProcess, (LPVOID)tft1Ptr, &targetFrameTime, sizeof(double), NULL);
	//float framerateScale = 30 / (1 / targetFrameTime);
	//WriteProcessMemory(hProcess, (LPVOID)framerateScalePtr, &framerateScale, sizeof(float), NULL);
}

__forceinline void tickf(HANDLE&hProcess, double&cTime, float&worldTime, float&inGameMouseMultiplier, float&inGameMultiplier, uint8_t&useMenuLimit, uint8_t&inCutscene, uint8_t&igmState, uint8_t&lastigm, Interp::Interp&igmInterp, UserConfig&uConfig)
{
	float nTime = (float)cTime;
	if (!useMenuLimit == 1 && !inCutscene == 1 && lastigm != igmState)
	{
		float base0 = (lastigm == 0 ? uConfig.igmState0Override
			: (lastigm == 1 ? uConfig.igmState1Override : uConfig.igmState2Override));
		float base = (igmState == 0 ? uConfig.igmState0Override
			: (igmState == 1 ? uConfig.igmState1Override : uConfig.igmState2Override));

		igmInterp.position = base0;
		igmInterp.position0 = base0;
		igmInterp.smoothTime = uConfig.easingTime;
		igmInterp.target = base;
		igmInterp.time0 = nTime;

		std::cout << "Starting igm interp: " << base << " " << base0 << "\n";		
	}
	else if (!useMenuLimit == 1 && !inCutscene == 1) 
	{
		float base = 1;
		switch (uConfig.bEnableEasing) 
		{
		case true:
			base = igmInterp.interp(nTime);

			if (nTime > igmInterp.time1) base = (igmState == 0 ? uConfig.igmState0Override
				: (igmState == 1 ? uConfig.igmState1Override : uConfig.igmState2Override));
			if (base != base || base == 0) base = 1; //If NaN set to 1
			break;
		case false:
			base = (igmState == 0 ? uConfig.igmState0Override
				: (igmState == 1 ? uConfig.igmState1Override : uConfig.igmState2Override));
			break;
		}

		inGameMultiplier = base;
	}
	else inGameMultiplier = 1; //Attempt to fix menu and loading slowdowns, as well as cutscene scaling

	uint8_t wtChanged = 0;
	if (worldTime > inGameMultiplier * 2)
	{
		worldTime = inGameMultiplier;
		wtChanged = 1;
	}

	WriteProcessMemory(hProcess, (LPVOID)igmPtr, &inGameMultiplier, sizeof(float), NULL);
	WriteProcessMemory(hProcess, (LPVOID)mouseMPtr, &inGameMouseMultiplier, sizeof(float), NULL);
	if (wtChanged==1) WriteProcessMemory(hProcess, (LPVOID)worldMPtr, &worldTime, sizeof(float), NULL);

	lastigm = igmState;
}

__forceinline void failedToErase(int x, HANDLE&hConsole) 
{
	SetConsoleTextAttribute(hConsole, cc_ERROR);
	std::cout << "Failed to erase instructions... ";
	SetConsoleTextAttribute(hConsole, cc_WARN);
	switch (x) 
	{
	case 0:
		std::cout << "mouseUnlock\n";
		break;
	case 1:
		std::cout << "mouseUnlock0\n";
		break;
	case 2:
		std::cout << "inGameMultiUnlock\n";
		break;
	case 3:
		std::cout << "inGameMultiUnlock0\n";
		break;
	case 4:
		std::cout << "inGameMultiUnlock1\n";
		break;
	case 5:
		std::cout << "inGameMultiUnlock2\n";
		break;
	case 6:
		std::cout << "inGameMultiUnlock3\n";
		break;
	case 7:
		std::cout << "fovUnlock\n";
		break;
	case 8:
		std::cout << "fullSpeedUnlock\n";
		break;
	case 9:
		std::cout << "aoefix\n";
		break;
	}
};

int main()
{
	//Process handles
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	HWND FFXIIWND = FindFFXII();
	HANDLE hProcess;
	DWORD pId;

	//Prefabricated variables
	const double oneOverSixty = (double)1L / (double)60L;
	const double Rad2Deg = 0.0174532925199L;

	//In program; in game variables
	double defaultTargetFrameTime = 0;
	double targetFrameTime = 0;
	double realFrametime = 0;
	float timeScale = 1;
	float framerateScale = 1;
	float inGameMouseMultiplier = 1;
	float inGameMultiplier = 1;
	float worldTime = 1;
	uint8_t igmState = 0;
	uint8_t titleState = 1;
	uint8_t uiState = 0;
	uint8_t cMenState = 0;
	uint8_t focusState = 0;
	uint8_t lastUseMenuLimitState = 0;
	uint8_t lastFocusState = 0;
	uint8_t lastigm = 0;
	uint8_t inMovie = 0;
	uint8_t inCutscene = 0;
	uint8_t useMenuLimit = 0;

	//Config initialization
	UserConfig uConfig;
	Config::UpdateUserConfig(uConfig);

	//Interpolator initialization
	Interp::Interp igmInterp;
	igmInterp.setType(uConfig.easingType);
	igmInterp.position = 1; igmInterp.position0 = 1; igmInterp.target = 1;

	//Give the user understanding of the console text colors
	SetConsoleTextAttribute(hConsole, cc_NORM); std::cout << "Normal Text; ";
	SetConsoleTextAttribute(hConsole, cc_VERBOSE); std::cout << "Verbose Text; ";
	SetConsoleTextAttribute(hConsole, cc_FUN); std::cout << "Fun Text; ";
	SetConsoleTextAttribute(hConsole, cc_ERROR); std::cout << "Error Text; ";
	SetConsoleTextAttribute(hConsole, cc_WARN); std::cout << "Warning Text\n\n";

	//Do this math only one time
	SetConsoleTextAttribute(hConsole, cc_VERBOSE); std::cout << "Normalizing config variables...\n";

	framerateScale = 30 / uConfig.requestedMinFramerate;
	uConfig.requestedMinFramerate = 1 / uConfig.requestedMinFramerate;
	uConfig.requestedMinFramerateMenus = 1 / uConfig.requestedMinFramerateMenus;
	uConfig.requestedMinFramerateNoFocus = 1 / uConfig.requestedMinFramerateNoFocus;
	uConfig.requestedMinFramerateMovies = 1 / uConfig.requestedMinFramerateMovies;
	uConfig.fov = uConfig.fov * Rad2Deg;
	defaultTargetFrameTime = uConfig.requestedMinFramerate;

	//Wait until the game it open before applying hacks and memory changes
	while (!FFXIIWND)
	{
		SetConsoleTextAttribute(hConsole, cc_WARN); std::cout << "Could not find FFXII, make sure the game is open!\nChecking again...\n";
		FFXIIWND = FindFFXII();
		Sleep(3000);
	}
	SetConsoleTextAttribute(hConsole, cc_NORM);

	//Attempt to open the process
	GetWindowThreadProcessId(FFXIIWND, &pId);
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pId);

	//Check the MD5 of the file; warn the user if it is awry; if it is a known MD5 of a stolen copy, report to the user that the program will not function at all.
	std::ifstream gameExecutable;
	std::string sMD5;
	char executableFullPath[MAX_PATH];
	long fSize;

	GetModuleFileNameExA(hProcess, NULL, executableFullPath, MAX_PATH);
	gameExecutable.open(executableFullPath, std::ios::binary | std::ios::in); gameExecutable.seekg(0, std::ios::end);
	fSize = gameExecutable.tellg(); gameExecutable.seekg(0, std::ios::beg);

	char* executableData = new char[fSize];

	gameExecutable.read(executableData, fSize);
	sMD5 = md5(executableData, fSize);
	std::transform(sMD5.begin(), sMD5.end(), sMD5.begin(), ::toupper);
	SetConsoleTextAttribute(hConsole, cc_WARN);  std::cout << "File MD5: ";
	SetConsoleTextAttribute(hConsole, cc_VERBOSE); std::cout << sMD5 << std::endl;

	if (sMD5 == "D032914AC59BAFDD62F038832CC14525") //The MD5 of FFXII_TZA.exe from CPY
	{
		std::string yarrthatsmyshanty[] = {
			"Yar! That\'s be me shanty!\n",
			"Yar har har!\n",
			"Being a pirate\'s alright to be!\n",
			"Do what\'cha want \'cause a pirate is free!\n",
			"You should be ashamed of yourself!\n", //Hehe Turok joke
			"Take what you want, give nothing back!\n",
			"Do what \'cha want \'cause a pirate is free!\n",
			"If ye can’t trust a pirate, ye damn well can’t trust a merchant either!\n",
			"Not all treasure is silver & gold!\n",
			"Yar har fiddle dee dee\n",
			"Yohoho that be a pirate\'s life for me!\n",
			"I love this shanty!\n",
			"Where there is a sea, there are pirates.\n",
			"If ye thinks he be ready to sail a beauty, ye bett\'r be willin’ to sink with \'er!\n",
			"Ahh shiver me timbers!\n",
			"Keep calm and say Arrr\n",
			"Avast thur be somethin\' awry...\n",
			"All hands hoay; for today we\'ve a new crewmate!\n",
			"Shiver me timbers- you\'re dancing the hempen jig!\n",
			"I expected nothing more than hornswaggle matey!\n",
			"\'es a landlubber, I swea\'!\n",
			"Ah blimey cap\'n, we\'ve been caught!\n",
			"Yarr today you feed the fish.\n",
			"Heave ho! You can do better than that!\n",
			"Yerr... not much old salt in ye...\n",
			"Damned scallywag!\n",
			"Cap\'n we\'ve been scuttled!\n",
			"Yer\' sharkbait y\'hea?\n",
			"Yo ho ho today ye\' walk the plank.\n",
			"Yar har fiddle dee dee\nBeing a pirate is alright to be\nDo what \'cha want \'cause a pirate is free!\nYou are a pirate!\n"
		};
		size_t nShanties = yarrthatsmyshanty->size();
		int cShanty;
		int rError; //Fake error
		srand(time(NULL)); Sleep(rand() % 2 * nShanties); srand(time(NULL)); //Randomness
		rError = rand() % (999999999 - 100000000) + 9999999;
		for (int i = 0; i < 8; i++) { rError += rand() % 99999999; srand(time(NULL)); }
		cShanty = rand() % nShanties - 1;
		SetConsoleTextAttribute(hConsole, cc_WARN); //"Error" = warn
		std::cout << "\n\nError: ";
		SetConsoleTextAttribute(hConsole, cc_ERROR);
		std::cout << "0x" << rError << std::endl;
		Sleep(rand() % 1333);
		SetConsoleTextAttribute(hConsole, cc_FUN);
		std::cout << std::endl << yarrthatsmyshanty[cShanty] << std::endl;
		for (;;) { Sleep(1); } //Infinite loop
	}
	//FFXII_TZA.exe from Steam
	else if (sMD5 != "F88350D39C8EDEECC3728A4ABC89289C") { SetConsoleTextAttribute(hConsole, cc_VERBOSE); std::cout << "Your file seems to be patched or updated. You may experience problems; if you experience issues please post them here: https://github.com/Drahsid/FFXIITZA-FPS-Unlocker/issues\n"; }

	SetConsoleTextAttribute(hConsole, cc_VERBOSE); std::cout << "Found FFXII Window at PID: " << pId << "!\n";


	if (!hProcess) { SetConsoleTextAttribute(hConsole, cc_ERROR); std::cout << "Failed to open process...\n"; }
	else 
	{
		SetConsoleTextAttribute(hConsole, cc_VERBOSE);
		std::cout << "Target frame time: " << uConfig.requestedMinFramerate << "\n";
		std::cout << "Setting up... \n";

		
		
		//Overwriting instructions
		if (uConfig.bEnableLockedMouseMulti || uConfig.bEnableAdaptiveMouse) 
		{
			if (!WriteProcessMemory(hProcess, (LPVOID)mouseUnlockPtr, "\x90\x90\x90\x90\x90\x90\x90\x90", 8, NULL))
				failedToErase(0, hConsole);
			if (!WriteProcessMemory(hProcess, (LPVOID)mouseUnlockPtr0, "\x90\x90\x90\x90\x90\x90\x90\x90", 8, NULL))
				failedToErase(1, hConsole);
		}
		if (!WriteProcessMemory(hProcess, (LPVOID)igmUnlockPtr, "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 10, NULL))
			failedToErase(2, hConsole);
		if (!WriteProcessMemory(hProcess, (LPVOID)igmUnlockPtr0, "\x90\x90\x90\x90\x90\x90\x90\x90", 8, NULL))
			failedToErase(3, hConsole);
		if (!WriteProcessMemory(hProcess, (LPVOID)igmUnlockPtr1, "\x90\x90\x90\x90\x90\x90\x90\x90", 8, NULL))
			failedToErase(4, hConsole);
		if (!WriteProcessMemory(hProcess, (LPVOID)igmUnlockPtr2, "\x90\x90\x90\x90\x90\x90\x90\x90", 8, NULL))
			failedToErase(5, hConsole);
		if (!WriteProcessMemory(hProcess, (LPVOID)igmUnlockPtr3, "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 10, NULL))
			failedToErase(6, hConsole);
		if (!WriteProcessMemory(hProcess, (LPVOID)fovUnlockPtr, "\x90\x90\x90\x90\x90\x90\x90", 7, NULL))
			failedToErase(7, hConsole);
		//movss xmm7, 0x0207D79C ; nop ; nop
		if (!WriteProcessMemory(hProcess, (LPVOID)actionAoeFixPtr, "\xF3\x0F\x10\x3C\x25\x9C\xD7\x07\x02\x90\x90", 11, NULL))
			failedToErase(9, hConsole);
		if (uConfig.bEnableFullSpeedMode) 
			if (!WriteProcessMemory(hProcess, (LPVOID)fullSpeedUnlockPtr, "\x90\x90\x90\x90\x90\x90\x90\x90", 8, NULL))
				failedToErase(8, hConsole);


		
		//Setting default frametimes, loop until this works
		while (targetFrameTime != uConfig.requestedMinFramerate)
		{
			writeMinFrametime(hProcess, uConfig.requestedMinFramerate);
			ReadProcessMemory(hProcess, (LPVOID)tftPtr, (LPVOID)&targetFrameTime, sizeof(double), NULL);
		}

		std::cout << "Setting desired FOV and Gamma...\n";
		DWORD protection;
		VirtualProtectEx(hProcess, (LPVOID)gammaPtr, sizeof(float), PAGE_READWRITE, &protection);
		VirtualProtectEx(hProcess, (LPVOID)fovPtr, sizeof(float), PAGE_READWRITE, &protection);

		WriteProcessMemory(hProcess, (LPVOID)gammaPtr, &uConfig.gamma, sizeof(float), NULL);
		WriteProcessMemory(hProcess, (LPVOID)fovPtr, &uConfig.fov, sizeof(float), NULL);
		WriteProcessMemory(hProcess, (LPVOID)framerateScalePtr, &framerateScale, sizeof(float), NULL);

		SetConsoleTextAttribute(hConsole, cc_WARN);

		clock_t tick = clock() / CLOCKS_PER_SEC;

		for (;;)
		{
			if (!IsWindow(FFXIIWND)) 
			{
				SetConsoleTextAttribute(hConsole, cc_ERROR);
				std::cout << "Window closed, stopping...\n";
				break;
			}

			ReadProcessMemory(hProcess, (LPVOID)rftPtr, (LPVOID)&realFrametime, sizeof(double), NULL);
			ReadProcessMemory(hProcess, (LPVOID)worldMPtr, (LPVOID)&worldTime, sizeof(float), NULL);
			ReadProcessMemory(hProcess, (LPVOID)igmSPtr, (LPVOID)&igmState, sizeof(uint8_t), NULL);
			ReadProcessMemory(hProcess, (LPVOID)uiOPtr, (LPVOID)&uiState, sizeof(uint8_t), NULL);
			ReadProcessMemory(hProcess, (LPVOID)titlPtr, (LPVOID)&titleState, sizeof(uint8_t), NULL);
			ReadProcessMemory(hProcess, (LPVOID)cMenPtr, (LPVOID)&cMenState, sizeof(uint8_t), NULL);
			ReadProcessMemory(hProcess, (LPVOID)cScenePtr, (LPVOID)&inCutscene, sizeof(uint8_t), NULL);
			ReadProcessMemory(hProcess, (LPVOID)moviePtr, (LPVOID)&inMovie, sizeof(uint8_t), NULL);
			
			focusState = FFXIIWND != GetForegroundWindow();

			//Faster than previous method; should cause less bugs
			useMenuLimit =
				inCutscene == 1
				? 4 : inMovie == 1 && uiState == 0
				? 3 : focusState == 1 
				? 2 : (uiState == 1 || cMenState == 1) //(uiState == 1 || titleState == 1 || cMenState == 1) TITLESTATE BREAKS REKS (Must find a more accurate value)
				? 1 
				: 0; 

			if (useMenuLimit != lastUseMenuLimitState) 
			{
				switch (useMenuLimit) 
				{
				case 0:
					std::cout << "User has exited a menu!\n";
					targetFrameTime = uConfig.requestedMinFramerate;
					break;
				case 1:
					std::cout << "User has entered a menu!\n";
					targetFrameTime = uConfig.requestedMinFramerateMenus;
					break;
				case 2:
					std::cout << "Game window has lost focus!\n";
					targetFrameTime = uConfig.requestedMinFramerateNoFocus;
					break;
				case 3:
					std::cout << "User has entered a movie!\n";
					targetFrameTime = uConfig.requestedMinFramerateMovies;
					break;
				case 4:
					std::cout << "The user is in a cutscene; reducing framerate!\n";
					targetFrameTime = uConfig.requestedMinFramerateMovies;
					break;
				}
				writeMinFrametime(hProcess, targetFrameTime);
				lastUseMenuLimitState = useMenuLimit; //Only write back to the frametime when needed
			}

			//timeScale = realFrametime / targetFrameTime;
			inGameMouseMultiplier = uConfig.bEnableAdaptiveMouse ?
				(uConfig.lockedMouseMulti * inGameMultiplier) / timeScale
				: uConfig.lockedMouseMulti;

			
			//WriteProcessMemory(hProcess, (LPVOID)tScalePtr, &timeScale, sizeof(float), NULL);

			double cTime = ((double)clock() / CLOCKS_PER_SEC);
			if (cTime - tick >= (realFrametime))
			{ 
				tickf(hProcess, cTime, worldTime, inGameMouseMultiplier, inGameMultiplier, useMenuLimit, inCutscene, igmState, lastigm, igmInterp, uConfig);
				WriteProcessMemory(hProcess, (LPVOID)fovPtr, &uConfig.fov, sizeof(float), NULL);
				tick = cTime;
			}

			//IN MS NOT SECONDS
			Sleep(1000 * (realFrametime / mainThreadUpdateCoef)); /*Improve CPU time
									  Also accounts for framerate fluctuations, with an effort to update x times per frame.*/
		}
	}

	CloseHandle(hProcess);
	std::cin.get();
	return 0;
}
