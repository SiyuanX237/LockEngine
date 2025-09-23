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


char *UnicodeToUtf8(const wchar_t *unicode)
{
    int len;
    len = WideCharToMultiByte(CP_UTF8, 0, unicode, -1, NULL, 0, NULL, NULL);
    char *szUtf8 = (char *)malloc(len + 1);
    memset(szUtf8, 0, len + 1);
    WideCharToMultiByte(CP_UTF8, 0, unicode, -1, szUtf8, len, NULL, NULL);
    return szUtf8;
}


void getAllFiles(const wstring &path, vector<wstring> &files, const vector<wstring> &filters)
{
    long hFile = 0;
    struct _wfinddata_t fileinfo;
    wstring p;

    if((hFile = _wfindfirst(p.assign(path).append(L"\\*").c_str(), &fileinfo)) != -1)
    {
        do
        {
            if(!(fileinfo.attrib & _A_SUBDIR)) // 只处理文件
            {
                wstring fullpath = path + L"\\" + fileinfo.name;

                const wstring filename = fileinfo.name;
                size_t dotPos = filename.rfind(L'.');
                if(dotPos == wstring::npos) continue;
                wstring ext = filename.substr(dotPos);
                // 转小写比较
                transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                for(const auto &filter : filters)
                {
                    if(ext == filter) files.push_back(fullpath);
                }
            }
            // 如果需要递归子目录，可以在这里加递归逻辑（可选）
        } while(_wfindnext(hFile, &fileinfo) == 0);

        _findclose(hFile);
    }
}

//变量区
vector<string> FileList;
vector<int> Playlist;
libvlc_instance_t *vlcInstance = nullptr;
libvlc_media_player_t *mediaPlayer = nullptr;
libvlc_media_t *media = nullptr;
bool isScreenOn = true;
bool Enable = true;//启用？
bool Mute = true;//静音？
bool AutoScale = true;//去除黑边？
bool Screenoff = false;//人为息屏控制
UINT Timeout;//超时时间
BYTE TimeoutType;//超时类型
const BYTE TimeoutFirst = 6;//超时优先
const BYTE PlayFirst = 7;//完成播放优先
const BYTE PlayThenTimeout = 8;//完成播放后超时
chrono::steady_clock::time_point g_LastTime;
HHOOK g_MouseHook;
bool FinishPlay;

HINSTANCE g_hInstance;
NOTIFYICONDATA nid = { };
wchar_t ConfigPath[MAX_PATH];
char Scale[16];
int WindowWidth;
int WindowHeight;
SIZE g_ClientSize;
SIZE g_WindowSize;
HWND MainHwnd;
HWND AboutHwnd;
bool Playing = false;
bool LogonStatus;
const wchar_t CLASS_NAME[] = L"LockEngine";
const wchar_t ABOUT_CLASS_NAME[] = L"LockEngineAbout";


//函数声明区
BOOL IsInputDesktopSecure();
bool IsLocked();
DWORD WINAPI LookForTarget(LPVOID lParam);
void PlayMedia();
void StopMedia();
void LoadPlayList();
DWORD WINAPI Reset(LPVOID lpParameter);
void handle_event(const struct libvlc_event_t *event, void *userdata);
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if(nCode >= 0)
    {
        if(wParam == WM_MOUSEMOVE) // 捕获鼠标移动
        {
            g_LastTime = chrono::steady_clock::now();
        }
    }
    return CallNextHookEx(g_MouseHook, nCode, wParam, lParam);
}

BOOL IsInputDesktopSecure()
{
    BOOL bInputDesktopIsSecure = FALSE;
    HDESK hDesktop = NULL;

    // 关键：尝试以“读”权限打开当前的输入桌面。
    // 一个普通权限的进程在用户桌面运行时，如果输入桌面是安全的，
    // 此调用将会失败。
    hDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);

    if(hDesktop == NULL)
    {
        // 打开失败，检查错误代码
        DWORD dwLastError = GetLastError();

        if(dwLastError == ERROR_ACCESS_DENIED)
        {
            // 访问被拒绝！这是一个非常强烈的信号，
            // 表明输入桌面是一个安全桌面，而我们没有权限访问它。
            bInputDesktopIsSecure = TRUE;
        }
        // 其他错误（如 ERROR_DESKTOP_NOT_FOUND）可能意味着其他问题，
        // 但我们默认输入桌面不是安全的。
        // 可以根据需要处理其他错误。
    }
    else
    {
        // 打开成功！这意味着输入桌面不是安全桌面，
        // 或者我们正在一个拥有足够权限的上下文中运行（例如已经在安全桌面上）。
        // 由于我们知道自己在普通桌面，所以这意味着输入桌面就是普通桌面。
        CloseDesktop(hDesktop);
        bInputDesktopIsSecure = FALSE;
    }

    return bInputDesktopIsSecure;
}

bool IsLocked()
{
    typedef BOOL(PASCAL *WTSQuerySessionInformation)(HANDLE hServer, DWORD SessionId, WTS_INFO_CLASS WTSInfoClass, LPTSTR *ppBuffer, DWORD *pBytesReturned);
    typedef void (PASCAL *WTSFreeMemory)(PVOID pMemory);

    WTSINFOEXW *pInfo = NULL;
    WTS_INFO_CLASS wtsic = WTSSessionInfoEx;
    bool bRet = false;
    LPTSTR ppBuffer = NULL;
    DWORD dwBytesReturned = 0;
    LONG dwFlags = 0;
    WTSQuerySessionInformation pWTSQuerySessionInformation = NULL;
    WTSFreeMemory pWTSFreeMemory = NULL;

    HMODULE hLib = LoadLibrary(L"wtsapi32.dll");
    if(!hLib)
    {
        return false;
    }
    pWTSQuerySessionInformation = (WTSQuerySessionInformation)GetProcAddress(hLib, "WTSQuerySessionInformationW");
    if(pWTSQuerySessionInformation)
    {
        pWTSFreeMemory = (WTSFreeMemory)GetProcAddress(hLib, "WTSFreeMemory");
        if(pWTSFreeMemory != NULL)
        {
            DWORD dwSessionID = WTSGetActiveConsoleSessionId();
            if(pWTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, dwSessionID, wtsic, &ppBuffer, &dwBytesReturned))
            {
                if(dwBytesReturned > 0)
                {
                    pInfo = (WTSINFOEXW *)ppBuffer;
                    if(pInfo->Level == 1)
                    {
                        dwFlags = pInfo->Data.WTSInfoExLevel1.SessionFlags;
                    }
                    if(dwFlags == WTS_SESSIONSTATE_LOCK)
                    {
                        bRet = true;
                    }
                }
                pWTSFreeMemory(ppBuffer);
                ppBuffer = NULL;
            }
        }
    }
    if(hLib != NULL)
    {
        FreeLibrary(hLib);
    }
    return bRet;
}

DWORD WINAPI LookForTarget(LPVOID lParam)
{
    HWND hBackstop;
    LONG_PTR exStyle;
    for(int i = 0; i < 10; ++i)
    {
        hBackstop = FindWindow(L"LockScreenBackstopFrame", L"Backstop Window");
        if(hBackstop)break;
        Sleep(50);
    }
    if(!hBackstop)
    {
        // 退路方案
        //MessageBox(NULL, L"找不到 Backstop Window！", L"失败", MB_ICONERROR);
        return 0;
    }
    //设置窗口透明度
    exStyle = GetWindowLongPtr(hBackstop, GWL_EXSTYLE);
    SetWindowLongPtr(hBackstop, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

    SetLayeredWindowAttributes(hBackstop, 0, (lParam ? 255 : 0), LWA_ALPHA);
    return 0;
};

void ShuffleList()
{
    Playlist.reserve(FileList.size());
    for(int i = 0; i < FileList.size(); ++i)
    {
        Playlist.push_back(i);
    }
    random_device rd;
    mt19937 engine(rd());
    shuffle(Playlist.begin(), Playlist.end(), engine);
}

//开始播放视频
void PlayMedia()
{
    Playing = true;
    if(FileList.empty())
    {
        return;
    }
    
    //生成伪随机数
    if(Playlist.empty())
    {
        ShuffleList();
    }

    printf("本次列表:\n");
    for(auto x : Playlist)
    {
        printf("%d ", x);
    }
    printf("\n\n");

    int index = Playlist.back();
    Playlist.pop_back();
    //载入媒体
    media = libvlc_media_new_path(vlcInstance, FileList[index].c_str());//准备节目
    libvlc_media_parse(media);//获取属性
    if(Mute)libvlc_media_add_option(media, ":no-audio");
    libvlc_media_player_set_media(mediaPlayer, media);
    if(AutoScale)
    {
        int width = libvlc_video_get_width(mediaPlayer);
        int height = libvlc_video_get_height(mediaPlayer);
        if(WindowHeight * width < WindowWidth * height)
        {
            libvlc_video_set_scale(mediaPlayer, WindowWidth * 1.0 / width);
        }
        else
        {
            libvlc_video_set_scale(mediaPlayer, WindowHeight * 1.0 / height);
        }
    }
    else
    {
        libvlc_video_set_scale(mediaPlayer, 0);
    }

    libvlc_media_player_set_hwnd(mediaPlayer, MainHwnd);
    libvlc_media_player_play(mediaPlayer);
   
    //显示这个窗口并更新位置
    ShowWindow(MainHwnd, SW_SHOW);
    SetWindowPos(MainHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOREDRAW | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    CloseHandle(CreateThread(NULL, 0, LookForTarget, (LPVOID)0, 0, NULL));
    SetTimer(MainHwnd, 1, 100, NULL);
	if(Screenoff && TimeoutType != PlayFirst)
    {
        g_LastTime = chrono::steady_clock::now();
        g_MouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, NULL, 0);
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    }
    return;
}

void StopMedia()
{
    CloseHandle(CreateThread(NULL, 0, LookForTarget, (LPVOID)1, 0, NULL));
    if(Playing)
    {
        Playing = false;
    }
    else
    {
        return;
    }
    if(FileList.empty())
    {
        return;
    }
    ShowWindow(MainHwnd, SW_HIDE);
    libvlc_media_player_stop(mediaPlayer);
    libvlc_media_release(media);
    UnhookWindowsHookEx(g_MouseHook);
    
    return;
}

void LoadPlayList()
{
    int args;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &args);
    wchar_t ListPath[MAX_PATH];

    getfname(argv[0], NULL, ListPath, NULL, NULL);
    StrCpyNW(ConfigPath, ListPath, MAX_PATH);
    StrNCatW(ConfigPath, L"\\Config.ini", MAX_PATH);
    if(!PathFileExists(ConfigPath))
    {
        
        WritePrivateProfileString(L"Setting", L"Enable", L"1", ConfigPath);
        WritePrivateProfileString(L"Setting", L"Mute", L"1", ConfigPath);
        WritePrivateProfileString(L"Setting", L"AutoScale", L"1", ConfigPath);
        WritePrivateProfileString(L"Setting", L"Screenoff", L"0", ConfigPath);
        wchar_t temp[3];
        wsprintf(temp, L"%d", TimeoutFirst);
        WritePrivateProfileString(L"Setting", L"TimeoutType", temp, ConfigPath);
        WritePrivateProfileString(L"Setting", L"Timeout", L"60", ConfigPath);
    }
    else
    {
        Enable = GetPrivateProfileInt(L"Setting", L"Enable", true, ConfigPath);
        Mute = GetPrivateProfileInt(L"Setting", L"Mute", true, ConfigPath);
        AutoScale = GetPrivateProfileInt(L"Setting", L"AutoScale", true, ConfigPath);
        Screenoff = GetPrivateProfileInt(L"Setting", L"Screenoff", false, ConfigPath);
        TimeoutType = GetPrivateProfileInt(L"Setting", L"TimeoutType", TimeoutFirst, ConfigPath);
        Timeout = GetPrivateProfileInt(L"Setting", L"Timeout", 60, ConfigPath);
    }

    StrNCatW(ListPath, L"\\PlayList", MAX_PATH);
    vector<wstring> FILELIST;
    if(FolderPathExists(ListPath) == false)
    {
        CreateDirectory(ListPath, NULL);
    }
    getAllFiles(ListPath, FILELIST, { L".mp4",L".avi",L".mkv",L".mov",L".ts",L".flv" });
    vector<string>().swap(FileList);
    FileList.reserve(FILELIST.size());
    for(auto &wstr : FILELIST)
    {
        FileList.push_back(UnicodeToUtf8(wstr.c_str()));
    }
    vector<int>().swap(Playlist);
    ShuffleList();
}

DWORD WINAPI Reset(LPVOID lpParameter) 
{ 
    // 重新设置媒体
    libvlc_media_player_set_media(mediaPlayer, media);

    // 重新播放
    libvlc_media_player_play(mediaPlayer);
    
    return 0;
}

void handle_event(const struct libvlc_event_t *event, void *userdata)
{
    if(event->type == libvlc_MediaPlayerEndReached)
    {
        if(Screenoff)
        {
            switch(TimeoutType)
            {
                case PlayFirst:
                {
                    SetThreadExecutionState(ES_CONTINUOUS);
                    ShutMonitor();
                    return;
                }
                case PlayThenTimeout:
                {
                    auto now = chrono::steady_clock::now();
                    auto idleTime = chrono::duration_cast<chrono::seconds>(now - g_LastTime).count();
                    if(idleTime > Timeout - 1)
                    {
                        SetThreadExecutionState(ES_CONTINUOUS);
                        ShutMonitor();
                        return;
                    }
                    break;
                }
            }
        }
        CloseHandle(CreateThread(NULL, 0, Reset, NULL, 0, 0));
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_CREATE:
        {
            nid.cbSize = sizeof(NOTIFYICONDATA);//结构的大小
            nid.hWnd = hwnd;//接收与通知区域中图标关联的通知的窗口句柄
            nid.uID = 1;//托盘图标ID
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;//想干嘛
            nid.uCallbackMessage = WM_TRAY;// 自定义消息，请在消息循环内处理WM_TRAY消息
            nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1));//图标
            lstrcpy(nid.szTip, TEXT("LockEngine"));//定义托盘文本
            Shell_NotifyIcon(NIM_ADD, &nid);//添加托盘

            WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
            RegisterPowerSettingNotification(hwnd, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);




            //读取配置并加载播放列表
            LoadPlayList();

            const char *vlcargs[] = {
                "--quiet",
                "--intf=dummy",              // 无UI
                "--no-video-title-show",        // 不显示视频标题
                "--video-on-top",
                "--no-snapshot-preview",        // 禁用截图预览
                "--no-disable-screensaver",
            };
            vlcInstance = libvlc_new(sizeof(vlcargs) / sizeof(vlcargs[0]), vlcargs);//播放器实例句柄(剧场)
            mediaPlayer = libvlc_media_player_new(vlcInstance);//播放器(舞台)
            libvlc_video_set_mouse_input(mediaPlayer, false);//禁用鼠标事件
            libvlc_video_set_key_input(mediaPlayer, false);//禁用键盘事件
            libvlc_media_player_set_hwnd(mediaPlayer, hwnd);
            
            libvlc_event_manager_t *ev = libvlc_media_player_event_manager(mediaPlayer);
            libvlc_event_attach(ev, libvlc_MediaPlayerEndReached, handle_event, NULL);

            break;
        }
        case WM_POWERBROADCAST:
        {
            if(!Enable)break;
            if(wParam == PBT_POWERSETTINGCHANGE)
            {
                POWERBROADCAST_SETTING *pbs = (POWERBROADCAST_SETTING *)lParam;
                if(IsEqualGUID(pbs->PowerSetting, GUID_MONITOR_POWER_ON))
                {
                    if(pbs->Data[0] == 0)
                    {
                        isScreenOn = false;//黑屏一定关闭
                        StopMedia();
                    }
                    else if(pbs->Data[0] == 1)
                    {
                        isScreenOn = true;
                        if(IsLocked())
                        {
                            LogonStatus = true;
                            PlayMedia();
                            SetTimer(hwnd, 1, 100, NULL);
                        }
                    }
                }
            }
            break;
        }
        case WM_WTSSESSION_CHANGE:
        {
            if(!Enable)break;
            if(!isScreenOn)break;//黑屏一定不处理
            if(wParam == WTS_SESSION_LOCK)
            {
                LogonStatus = true;//默认是普通桌面那个
                SetTimer(hwnd, 1, 100, NULL);//锁住了开定时器
            }
            else if(wParam == WTS_SESSION_UNLOCK)
            {
                StopMedia();//解锁了一定停止播放
                SetThreadExecutionState(ES_CONTINUOUS);
                KillTimer(hwnd, 1);
            }
            break;
        }
        case WM_TRAY:
        {
            if(lParam == WM_RBUTTONUP)
            {
                HMENU hPopupMenu = CreatePopupMenu();
                if(!hPopupMenu) break;

                // 根据 status 添加第一个项
                if(Enable)AppendMenu(hPopupMenu, MF_STRING, 1, L"服务：启用");
                else AppendMenu(hPopupMenu, MF_STRING, 1, L"服务：禁用");

                AppendMenu(hPopupMenu, MF_STRING, 2, L"刷新列表");

                if(Mute)AppendMenu(hPopupMenu, MF_STRING, 3, L"音频：禁用");
                else AppendMenu(hPopupMenu, MF_STRING, 3, L"音频：启用");

                if(AutoScale)AppendMenu(hPopupMenu, MF_STRING, 4, L"黑边：禁止");
                else AppendMenu(hPopupMenu, MF_STRING, 4, L"黑边：允许");

                AppendMenu(hPopupMenu, MF_STRING, 5, L"独立息屏控制");
                if(Screenoff)
                {
                    CheckMenuItem(hPopupMenu, 5, MF_BYCOMMAND | MF_CHECKED);
                    AppendMenu(hPopupMenu, MF_STRING, TimeoutFirst, L"┣超时优先");
                    AppendMenu(hPopupMenu, MF_STRING, PlayFirst, L"┣完成播放优先");
                    AppendMenu(hPopupMenu, MF_STRING, PlayThenTimeout, L"┗播放优先后超时");
                    CheckMenuItem(hPopupMenu, TimeoutType, MF_BYCOMMAND | MF_CHECKED);
                }
                else
                {
                    CheckMenuItem(hPopupMenu, 5, MF_BYCOMMAND | MF_UNCHECKED);
                }
                AppendMenu(hPopupMenu, MF_STRING, 9, L"关于");
                AppendMenu(hPopupMenu, MF_STRING, 10, L"退出");
                // 获取鼠标位置（屏幕坐标）
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                // 显示菜单
                TrackPopupMenu(hPopupMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

                DestroyMenu(hPopupMenu); // 别忘了销毁
            }
            break;
        }
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case 1:
                    Enable = !Enable;
                    WritePrivateProfileString(L"Setting", L"Enable", Enable ? L"1" : L"0", ConfigPath);
                    break;
                case 2:
                    LoadPlayList();
                    break;
                case 3:
                    Mute = !Mute;
                    WritePrivateProfileString(L"Setting", L"Mute", Mute ? L"1" : L"0", ConfigPath);
                    break;
                case 4:
                    AutoScale = !AutoScale;
                    WritePrivateProfileString(L"Setting", L"AutoScale", AutoScale ? L"1" : L"0", ConfigPath);
                    break;
                case 5:
                    Screenoff = !Screenoff;
                    WritePrivateProfileString(L"Setting", L"Screenoff", Screenoff ? L"1" : L"0", ConfigPath);
                    break;
                case TimeoutFirst:
                case PlayFirst:
                case PlayThenTimeout:
                {
                    TimeoutType = LOWORD(wParam);
                    wchar_t value[3];
                    swprintf_s(value, 3, L"%d", TimeoutType);
                    WritePrivateProfileString(L"Setting", L"TimeoutType", value, ConfigPath);
                    break;
                }
                case 9:
                {
                    AboutHwnd = CreateWindowEx(
                        NULL,
                        ABOUT_CLASS_NAME,
                        L"关于\"LockEngine\"",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
                        CW_USEDEFAULT,
                        CW_USEDEFAULT,
                        g_WindowSize.cx,
                        g_WindowSize.cy,
                        NULL,
                        NULL, NULL, NULL
                    );
                    break;
                }
                case 10:
                    PostMessage(hwnd, WM_DESTROY, 0, 0);
                    break;
            }
            break;
        }
        case WM_TIMER:
        {
            if(LogonStatus)//在普通桌面?
            {
                if(IsInputDesktopSecure())//不符合，立即停止
                {
                    StopMedia();
                    LogonStatus = false;
                }
                else if(Screenoff && TimeoutType == TimeoutFirst)//超时优先
                {
                    auto now = chrono::steady_clock::now();
                    auto idleTime = chrono::duration_cast<std::chrono::seconds>(now - g_LastTime).count();
                    if(idleTime > Timeout - 1)
                    {
                        SetThreadExecutionState(ES_CONTINUOUS);
                        KillTimer(hwnd, 1);
                        ShutMonitor();
                    }
                }
                else
                {
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOOWNERZORDER);
                }
            }
            else//转到安全桌面了?
            {
                if(!IsInputDesktopSecure())//不符合，赶紧播放
                {
                    PlayMedia();
                    LogonStatus = true;
                }
            }
            break;
        }
        case WM_DESTROY:
        {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            if(mediaPlayer)libvlc_media_player_release(mediaPlayer);
            if(vlcInstance)libvlc_release(vlcInstance);
            PostQuitMessage(0);
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK AboutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HFONT Font;
    static HFONT BoldFont;
    static SIZE FontSize;
    static int LineAnchorY[6];
    static HICON hIcon;
    static HWND LinkHwnd_Home;
    static HWND LinkHwnd_Patron;
    switch(msg)
    {
        case WM_CREATE:
        {
            HDC hdc = GetDC(hwnd);
            Font = CreateFont(
                -g_ClientSize.cy / 19,
                0,
                0,
                0,
                FW_NORMAL,
                false,
                false,
                false,
                GB2312_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY,
                FF_SWISS,
                TEXT("微软雅黑")
            );
            BoldFont = CreateFont(
                -g_ClientSize.cy / 15,
                0,
                0,
                0,
                FW_BOLD,
                false,
                false,
                false,
                GB2312_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY,
                FF_SWISS,
                TEXT("微软雅黑")
            );
            HGDIOBJ hOldFont = SelectObject(hdc, Font);
            GetTextExtentPoint32(hdc, TEXT("中"), 1, &FontSize);
            SelectObject(hdc, hOldFont);
            ReleaseDC(hwnd, hdc);
            hIcon= (HICON)LoadImage(g_hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);

            LineAnchorY[0] = FontSize.cy;
            int LineAnchorYStep = FontSize.cy * 1.8;
            for(int i = 1; i < 6; ++i)
            {
                LineAnchorY[i] = LineAnchorY[i - 1] + LineAnchorYStep;
            }

            LinkHwnd_Home = CreateWindowEx(
                NULL, L"SysLink",
                L"本软件由 <a href=\"https://space.bilibili.com/1081364881\">Bilibili-个人隐思</a> 用心打造",
                WS_CHILD | WS_VISIBLE,
                FontSize.cx * 2,
                LineAnchorY[3] + FontSize.cy,
                g_ClientSize.cx,
                FontSize.cy * 1.5,
                hwnd,
                (HMENU)ID_LINK,
                NULL,
                NULL
            );
            SendMessage(LinkHwnd_Home, WM_SETFONT, (WPARAM)Font, 0);

            LinkHwnd_Patron = CreateWindowEx(
                NULL, L"SysLink",
                L"如果觉得对您有所帮助，不妨在 <a href=\"https://afdian.com/a/X1415\">爱发电</a> 赞助一下我，以支持我做出更多优质作品",
                WS_CHILD | WS_VISIBLE,
                FontSize.cx * 2,
                LineAnchorY[4] + FontSize.cy,
                g_ClientSize.cx,
                FontSize.cy * 1.5,
                hwnd,
                (HMENU)ID_LINK,
                NULL,
                NULL
            );
            SendMessage(LinkHwnd_Patron, WM_SETFONT, (WPARAM)Font, 0);

            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HGDIOBJ OldFont = SelectObject(hdc, BoldFont);
            DrawIconEx(hdc, FontSize.cx * 4, LineAnchorY[0], hIcon, FontSize.cx * 4, FontSize.cx * 4, 0, NULL, DI_NORMAL);
            RECT RectDrawText;
            RectDrawText.left = FontSize.cx * 11;
            RectDrawText.right = g_ClientSize.cx - FontSize.cx * 2;

            RectDrawText.top = LineAnchorY[0];
            RectDrawText.bottom = RectDrawText.top + FontSize.cy * 2;
            DrawText(hdc, TEXT("LockEngine - 锁屏壁纸引擎"), -1, &RectDrawText, DT_LEFT | DT_SINGLELINE);
            
            
            SelectObject(hdc, Font);
            RectDrawText.top = LineAnchorY[1];
            RectDrawText.bottom = RectDrawText.top + FontSize.cy * 1.3;
            DrawText(hdc, TEXT("版本：1.2.0.0                日期：2025.9.23"), -1, &RectDrawText, DT_LEFT | DT_SINGLELINE);
            MoveToEx(hdc, FontSize.cx * 2, LineAnchorY[2], NULL);
            LineTo(hdc, g_ClientSize.cx - FontSize.cx * 2, LineAnchorY[2]);

            RectDrawText.left = FontSize.cx * 2;
            RectDrawText.right = g_ClientSize.cx - FontSize.cx * 2;
            RectDrawText.top = LineAnchorY[2] + FontSize.cy;
            RectDrawText.bottom = RectDrawText.top + FontSize.cy * 2;
            DrawText(hdc, TEXT("LockEngine是免费软件，您可以自由分发、复制、运行于任何非商业用途"), -1, &RectDrawText, DT_LEFT | DT_SINGLELINE);
            SelectObject(hdc, OldFont);
            EndPaint(hwnd, &ps);
            
            break;
        }
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            mmi->ptMinTrackSize.x = 170; // 设置最小宽度
            mmi->ptMinTrackSize.y = 96; // 设置最小高度
            return 0;
        }
        case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS *wp = (WINDOWPOS *)lParam;
            wp->flags |= SWP_NOSIZE;
            break;
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = (HDC)wParam;
            HWND hwndStatic = (HWND)lParam;
            return (INT_PTR)GetStockBrush(WHITE_BRUSH);
        }
        case WM_NOTIFY:
        {
            LPNMHDR lpnmh = (LPNMHDR)lParam;
            if(lpnmh->code == NM_CLICK || lpnmh->code == NM_RETURN)
            {
                if(lpnmh->idFrom == ID_LINK) // ID 匹配
                {
                    NMLINK *pNMLink = (NMLINK *)lParam;
                    ShellExecute(NULL, L"open", pNMLink->item.szUrl, NULL, NULL, SW_NORMAL);
                }
            }
            break;
        }
        case WM_DESTROY:
        {
            DestroyIcon(hIcon);
            //DeleteObject(hIcon);
            DeleteObject(Font);
            DeleteObject(BoldFont);
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    if(FindWindow(L"LockEngine", L"Wallpaper"))
    {
        MessageBox(NULL, TEXT("已有程序实例在运行"), TEXT("提示"), MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    g_hInstance = hInstance;
    //主窗口播放器注册类
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    RegisterClassEx(&wc);

    WindowWidth = GetSystemMetrics(SM_CXSCREEN);
    WindowHeight = GetSystemMetrics(SM_CYSCREEN);
    MainHwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        CLASS_NAME, L"Wallpaper",
        WS_POPUP,
        0, 0, WindowWidth, WindowHeight,
        NULL, NULL, hInstance, NULL
    );

    //关于页的窗口类注册
    WNDCLASSEX wca = {};
    wca.cbSize = sizeof(WNDCLASSEX);
    wca.lpfnWndProc = AboutProc;
    wca.hInstance = g_hInstance;
    wca.hIcon = wc.hIcon;
    wca.hCursor = wc.hCursor;
    wca.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wca.lpszClassName = ABOUT_CLASS_NAME;
    wca.hIconSm = wc.hIconSm;
    RegisterClassEx(&wca);
    g_WindowSize = AdjustClientSize(2, 1, 14, true, &g_ClientSize, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, false, NULL);
    


    MSG msg;
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
