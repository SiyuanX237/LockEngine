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
#define WM_TRAY WM_USER+1
#pragma comment(linker,	"\"/manifestdependency:type='win32' \
							name='Microsoft.Windows.Common-Controls' \
							version='6.0.0.0' \
							processorArchitecture='*' \
							publicKeyToken='6595b64144ccf1df' \
							language='*'\"")
const BYTE ID_Menu_Enable = 1;
const BYTE ID_Menu_LoadPlayList = 2;
const BYTE ID_Menu_Mute = 3;
const BYTE ID_Menu_AutoScale = 4;
const BYTE ID_Menu_ScreenOff = 5;
const BYTE ID_Menu_TimeoutFirst = 6;//超时优先
const BYTE ID_Menu_PlayFirst = 7;//完成播放优先
const BYTE ID_Menu_PlayThenTimeout = 8;//完成播放后超时
const BYTE ID_Menu_HideTray = 9;
const BYTE ID_Menu_About = 10;
const BYTE ID_Menu_Quit = 11;