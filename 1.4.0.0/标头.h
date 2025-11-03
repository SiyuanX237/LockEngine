#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <iostream>
#include <io.h>
#include <wtsapi32.h>
#include <random>
#include "resource.h"
#include "D:\C files\mywin32func.h"
#pragma comment(lib, "Wtsapi32.lib")
#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif
#include <vlc/vlc.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <chrono>

using namespace std;
#pragma comment(lib, "libvlc.lib")
#pragma comment(lib, "libvlccore.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker,	"\"/manifestdependency:type='win32' \
							name='Microsoft.Windows.Common-Controls' \
							version='6.0.0.0' \
							processorArchitecture='*' \
							publicKeyToken='6595b64144ccf1df' \
							language='*'\"")

#define WM_TRAY WM_USER+1
#define WM_ResetPower WM_USER+2


constexpr BYTE ID_Menu_Enable = 1;
constexpr BYTE ID_Menu_LoadPlayList = 2;
constexpr BYTE ID_Menu_Mute = 3;
constexpr BYTE ID_Menu_AutoScale = 4;
constexpr BYTE ID_Menu_ScreenOff = 5;
constexpr BYTE ID_Menu_Battery = 6;
constexpr BYTE ID_Menu_TimeoutFirst = 7;//超时优先
constexpr BYTE ID_Menu_PlayFirst = 8;//完成播放优先
constexpr BYTE ID_Menu_HideTray = 9;
constexpr BYTE ID_Menu_AutoRun = 10;
constexpr BYTE ID_Menu_About = 11;
constexpr BYTE ID_Menu_Quit = 12;