#include "标头.h"

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
vector<string> g_FileList;
vector<int> g_Playlist;
libvlc_instance_t *vlcInstance = nullptr;
libvlc_media_player_t *mediaPlayer = nullptr;
libvlc_media_t *media = nullptr;
bool g_isScreenOn;//屏幕开着？
bool g_Enable;//启用？
bool g_Mute;//静音？
bool g_AutoScale;//去除黑边？
bool g_Screenoff;//人为息屏控制？
int g_Timeout;//超时时间
BYTE g_TimeoutType;//超时类型
bool g_HideTray;//隐藏托盘？
bool g_Battery;//允许使用电池时播放？
UINT WM_KeepTray;//恢复托盘消息

HINSTANCE g_hInstance;
NOTIFYICONDATA g_nid = {};
wchar_t g_ConfigPath[MAX_PATH];
char Scale[16];
int g_WindowWidth;
int g_WindowHeight;
SIZE g_ClientSize;
SIZE g_WindowSize;
HWND g_MainHwnd;
HWND g_AboutHWND;
bool g_Playing = false;//避免重复StopMedia导致错误
bool g_LogonStatus;
bool g_AutoRun;
int g_argc;
LPWSTR *g_argv;
HWND g_TaskBarHWND;
DWORD g_backupTimeout;
const wchar_t CLASS_NAME[] = L"LockEngine";
const wchar_t ABOUT_CLASS_NAME[] = L"LockEngineAbout";
HPOWERNOTIFY g_hPowerNotify;
LASTINPUTINFO g_lii;
DWORD g_LastInputTimeFix = 0;
bool g_TimerRunning;
HMENU g_Menu;
HHOOK g_hMouseHook = NULL;

//函数声明区
BOOL IsInputDesktopSecure();
bool IsLocked();
DWORD WINAPI LookForTarget(LPVOID lParam);
void PlayMedia();
void StopMedia();
void LoadPlayList();
void handle_event(const struct libvlc_event_t *event, void *userdata);

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
    for(int i = 0; i < 50; ++i)
    {
        hBackstop = FindWindow(L"LockScreenBackstopFrame", L"Backstop Window");
        if(hBackstop)break;
        Sleep(40);
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

    //隐藏窗口
    //ShowWindow(hBackstop, (lParam ? SW_RESTORE : SW_HIDE));
    return 0;
};

void ShuffleList()
{
    g_Playlist.reserve(g_FileList.size());
    for(int i = 0; i < g_FileList.size(); ++i)
    {
        g_Playlist.push_back(i);
    }
    random_device rd;
    mt19937 engine(rd());
    shuffle(g_Playlist.begin(), g_Playlist.end(), engine);
}

//开始播放视频
void PlayMedia()
{
    if(g_Playing)//避免重复播放
    {
        return;
        
    }
    if(g_FileList.empty())
    {
        return;
    }
    
    //生成伪随机数
    if(g_Playlist.empty())
    {
        ShuffleList();
    }

    int index = g_Playlist.back();
    g_Playlist.pop_back();
    //载入媒体
    media = libvlc_media_new_path(vlcInstance, g_FileList[index].c_str());//准备节目
    libvlc_media_parse(media);//获取属性
    if(g_Mute)libvlc_media_add_option(media, ":no-audio");
    libvlc_media_player_set_media(mediaPlayer, media);
    libvlc_media_player_set_hwnd(mediaPlayer, g_MainHwnd);
    if(libvlc_media_player_play(mediaPlayer))
    {
        return;//非0不正常，不放了
    }
    g_Playing = true;//正常播放
    float scale = 0;
    if(g_AutoScale)
    {
        int width = libvlc_video_get_width(mediaPlayer);
        int height = libvlc_video_get_height(mediaPlayer);
        if(width * height == 0)
        {
            ;
        }
        else if(g_WindowHeight * width < g_WindowWidth * height)//视频比屏幕扁，以高为主
        {
			scale = (g_WindowWidth * 1.0) / width;
        }
        else//视频比屏幕窄，以宽为主
        {
            scale = (g_WindowHeight * 1.0) / height;
        }
    }
    libvlc_video_set_scale(mediaPlayer, scale);
    
   
    //显示这个窗口并更新位置
    ShowWindow(g_MainHwnd, SW_SHOW);
    g_TaskBarHWND = FindWindow(L"Shell_TrayWnd", NULL);
    ShowWindow(g_TaskBarHWND, SW_HIDE);
    SetWindowPos(g_MainHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_MainHwnd);
    SetFocus(g_MainHwnd);
    CloseHandle(CreateThread(NULL, 0, LookForTarget, (LPVOID)0, 0, NULL));
    SetTimer(g_MainHwnd, 1, 100, NULL);//开始循环检查桌面
    if(g_Screenoff)
    {
        g_lii.cbSize = sizeof(LASTINPUTINFO);
        //先锁住屏幕超时
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
        if(g_TimeoutType == ID_Menu_TimeoutFirst)//超时优先，使用内置时钟
        {
            g_LastInputTimeFix = 0;//防止污染数据
            SetTimer(g_MainHwnd, 2, 1000, NULL);//开启内置时钟
        }
        //ID_Menu_PlayFirst无需时钟，播放完让主线程清除SetThreadExecutionState即可
        //ID_Menu_PlayThenTimeout时钟是在播放完成后才设置
        else if(g_TimeoutType == ID_Menu_PlayThenTimeout)
        {
            g_TimerRunning = false;
            g_LastInputTimeFix = GetTickCount();
        }
    }
    return;
}

void StopMedia()
{
    if(g_Playing)
    {
        g_Playing = false;
    }
    else
    {
        return;
    }
    if(g_FileList.empty())
    {
        return;
    }
    CloseHandle(CreateThread(NULL, 0, LookForTarget, (LPVOID)1, 0, NULL));
    ShowWindow(g_MainHwnd, SW_HIDE);
    ShowWindow(g_TaskBarHWND, SW_SHOW);
    libvlc_media_player_stop(mediaPlayer);
    libvlc_media_release(media);
    return;
}

void LoadPlayList()
{
    g_argv = CommandLineToArgvW(GetCommandLine(), &g_argc);
    wchar_t ListPath[MAX_PATH];

    getfname(g_argv[0], NULL, ListPath, NULL, NULL);
    StrCpyNW(g_ConfigPath, ListPath, MAX_PATH);
    StrNCatW(g_ConfigPath, L"\\Config.ini", MAX_PATH);
    if(!PathFileExists(g_ConfigPath))
    {
        WritePrivateProfileString(L"Setting", L"Enable", L"1", g_ConfigPath);
        g_Enable = true;
        WritePrivateProfileString(L"Setting", L"Mute", L"1", g_ConfigPath);
        g_Mute = true;
        WritePrivateProfileString(L"Setting", L"AutoScale", L"1", g_ConfigPath);
        g_AutoScale = true;
        WritePrivateProfileString(L"Setting", L"Screenoff", L"0", g_ConfigPath);
        g_Screenoff = false;
        wchar_t ch[3];
        wsprintf(ch, L"%d", ID_Menu_TimeoutFirst);
        WritePrivateProfileString(L"Setting", L"TimeoutType", ch, g_ConfigPath);
        g_TimeoutType = ID_Menu_TimeoutFirst;
        WritePrivateProfileString(L"Setting", L"Timeout", L"60", g_ConfigPath);
        g_Timeout = 60;
        WritePrivateProfileString(L"Setting", L"HideTray", L"0", g_ConfigPath);
        g_HideTray = false;
        WritePrivateProfileString(L"Setting", L"AutoRun", L"0", g_ConfigPath);
        g_AutoRun = false;
        WritePrivateProfileString(L"Setting", L"AllowBattery", L"1", g_ConfigPath);
        g_Battery = true;
    }
    else
    {
        int temp;
        temp = GetPrivateProfileInt(L"Setting", L"Enable", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"Enable", L"1", g_ConfigPath);
            g_Enable = true;
        }
        else g_Enable = (temp == 0 ? false : true);

        temp = GetPrivateProfileInt(L"Setting", L"Mute", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"Mute", L"1", g_ConfigPath);
            g_Mute = true;
        }
        else g_Mute = (temp == 0 ? false : true);

        temp = GetPrivateProfileInt(L"Setting", L"AutoScale", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"AutoScale", L"1", g_ConfigPath);
            g_AutoScale = true;
        }
        else g_AutoScale = (temp == 0 ? false : true);

        temp = GetPrivateProfileInt(L"Setting", L"Screenoff", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"Screenoff", L"0", g_ConfigPath);
            g_Screenoff = false;
        }
        else g_Screenoff = (temp == 0 ? false : true);

        temp = GetPrivateProfileInt(L"Setting", L"TimeoutType", -1, g_ConfigPath);
        if(temp == -1)
        {
            wchar_t ch[3];
            wsprintf(ch, L"%d", ID_Menu_TimeoutFirst);
            WritePrivateProfileString(L"Setting", L"TimeoutType", ch, g_ConfigPath);
            g_TimeoutType = ID_Menu_TimeoutFirst;
        }
        else g_TimeoutType = temp;

        temp = GetPrivateProfileInt(L"Setting", L"Timeout", -1, g_ConfigPath);
        if(temp < 5)
        {
            WritePrivateProfileString(L"Setting", L"Timeout", L"60", g_ConfigPath);
            g_Timeout = 60;
        }
        else g_Timeout = temp;

        temp = GetPrivateProfileInt(L"Setting", L"HideTray", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"HideTray", L"0", g_ConfigPath);
            g_HideTray = false;
        }
        else g_HideTray = (temp == 0 ? false : true);

        temp = GetPrivateProfileInt(L"Setting", L"AutoRun", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"AutoRun", L"0", g_ConfigPath);
            g_AutoRun = false;
        }
        else g_AutoRun = (temp == 0 ? false : true);

        temp = GetPrivateProfileInt(L"Setting", L"AllowBattery", -1, g_ConfigPath);
        if(temp == -1)
        {
            WritePrivateProfileString(L"Setting", L"AllowBattery", L"1", g_ConfigPath);
            g_Battery = true;
        }
        else g_Battery = (temp == 0 ? false : true);
    }

    StrNCatW(ListPath, L"\\PlayList", MAX_PATH);
    vector<wstring> FILELIST;
    if(FolderPathExists(ListPath) == false)
    {
        CreateDirectory(ListPath, NULL);
    }
    getAllFiles(ListPath, FILELIST, { L".mp4",L".avi",L".mkv",L".mov",L".ts",L".flv"});
    vector<string>().swap(g_FileList);
    g_FileList.reserve(FILELIST.size());
    for(auto &wstr : FILELIST)
    {
        g_FileList.push_back(UnicodeToUtf8(wstr.c_str()));
    }
    vector<int>().swap(g_Playlist);
    ShuffleList();
}

void handle_event(const struct libvlc_event_t *event, void *userdata)
{
    if(event->type == libvlc_MediaPlayerEndReached)
    {
        if(g_Screenoff)
        {
            if(g_TimeoutType == ID_Menu_PlayFirst)
            {
                //让主线程主动解除SetThreadExecutionState
                PostMessage(g_MainHwnd, WM_ResetPower, 0, 0);
                return;
            }
            else if(g_TimeoutType == ID_Menu_PlayThenTimeout)
            {
                if(!g_TimerRunning)
                {
                    g_LastInputTimeFix = GetTickCount() - g_LastInputTimeFix;//计算时间差，用以修正视频播放
                    g_TimerRunning = true;
                    SetTimer(g_MainHwnd, 2, 1000, NULL);
                }
            }
        }
        

        CloseHandle(CreateThread(NULL, 0, 
        [](LPVOID lParam)->DWORD
        {
            //重新设置媒体
            libvlc_media_player_set_media(mediaPlayer, media);
            //重新播放
            libvlc_media_player_play(mediaPlayer);
            return 0;
        }
        , NULL, 0, 0));
    }
}

void RestoreTray(HWND hwnd)
{
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1));
    lstrcpy(g_nid.szTip, TEXT("LockEngine"));
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

//滚轮修改息屏秒数
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if(nCode >= 0 && g_Menu && wParam == WM_MOUSEWHEEL)
    {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;
        int delta = GET_WHEEL_DELTA_WPARAM(ms->mouseData);

        RECT rc;
		GetMenuItemRect(FindWindow(L"#32768", NULL), g_Menu, ID_Menu_ScreenOff - 1, &rc); //ID_Menu_ScreenOff-1表示绝对位置


        if(PtInRect(&rc, ms->pt))
        {
            g_Timeout += (delta > 0 ? 1 : -1);
            g_Timeout = max(5, min(g_Timeout, 999));

            wchar_t buf[22];
            _snwprintf(buf, ARRAYSIZE(buf), L"独立息屏控制 滚轮修改时间:%d秒", g_Timeout);

            MENUITEMINFO mii{};
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = buf;

            SetMenuItemInfo(g_Menu, ID_Menu_ScreenOff, false, &mii);

            return 1; // 拦截滚轮消息
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_CREATE:
        {
            WM_KeepTray = RegisterWindowMessage(L"TaskbarCreated");
            //读取配置并加载播放列表
            LoadPlayList();

            if(!g_HideTray)
            {
                RestoreTray(hwnd);
            }
            WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
            g_hPowerNotify = RegisterPowerSettingNotification(hwnd, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);


            const char *vlcargs[] = {
                "--quiet",
                "--intf=dummy",              //无UI
                "--no-video-title-show",     //不显示视频标题
                "--video-on-top",
                "--no-snapshot-preview",     //禁用截图预览
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
            if(!g_Enable)break;
            SYSTEM_POWER_STATUS sps = { 0 };
            GetSystemPowerStatus(&sps);
            if(sps.ACLineStatus == 0)//电池
            {
                if(!g_Battery)break;
            }
            if(wParam == PBT_POWERSETTINGCHANGE)
            {
                POWERBROADCAST_SETTING *pbs = (POWERBROADCAST_SETTING *)lParam;
                if(IsEqualGUID(pbs->PowerSetting, GUID_MONITOR_POWER_ON))
                {
                    if(pbs->Data[0] == 0)
                    {
                        g_isScreenOn = false;//黑屏一定关闭
                        SetThreadExecutionState(ES_CONTINUOUS);
                        StopMedia();
                        KillTimer(hwnd, 2);//关闭内置时钟
                    }
                    else if(pbs->Data[0] == 1)
                    {
                        g_isScreenOn = true;
                        if(IsLocked())
                        {
                            g_LogonStatus = true;//默认是普通桌面
                            PlayMedia();//随后播放
                            SetTimer(hwnd, 1, 100, NULL);//开始循环检查桌面
                        }
                    }
                }
            }
            break;
        }
        case WM_WTSSESSION_CHANGE:
        {
            if(!g_Enable)break;
            if(!g_isScreenOn)break;//黑屏一定不处理
            SYSTEM_POWER_STATUS sps = { 0 };
            GetSystemPowerStatus(&sps);
            if(sps.ACLineStatus == 0)//电池
            {
                if(!g_Battery)break;
            }
            if(wParam == WTS_SESSION_LOCK)
            {
                g_LogonStatus = false;//默认是安全桌面，以便进入播放
                SetTimer(hwnd, 1, 100, NULL);//定时器检查桌面情况
            }
            else if(wParam == WTS_SESSION_UNLOCK)
            {
                SetThreadExecutionState(ES_CONTINUOUS);
                StopMedia();//解锁了一定停止播放
                KillTimer(hwnd, 1);//关闭桌面检查
                KillTimer(hwnd, 2);//关闭内置时钟
            }
            break;
        }
        case WM_TRAY:
        {
            if(lParam == WM_RBUTTONUP)
            {
                g_Menu = CreatePopupMenu();
                if(!g_Menu) break;

                // 根据 status 添加第一个项
                if(g_Enable)AppendMenu(g_Menu, MF_STRING, ID_Menu_Enable, L"服务：启用");
                else AppendMenu(g_Menu, MF_STRING, ID_Menu_Enable, L"服务：禁用");

                AppendMenu(g_Menu, MF_STRING, ID_Menu_LoadPlayList, L"刷新列表");

                if(g_Mute) AppendMenu(g_Menu, MF_STRING, ID_Menu_Mute, L"静音：是");
                else AppendMenu(g_Menu, MF_STRING, ID_Menu_Mute, L"静音：否");

                if(g_AutoScale)AppendMenu(g_Menu, MF_STRING, ID_Menu_AutoScale, L"去除黑边：是");
                else AppendMenu(g_Menu, MF_STRING, ID_Menu_AutoScale, L"去除黑边：否");

                if(g_Battery)AppendMenu(g_Menu, MF_STRING, ID_Menu_Battery, L"使用电池时允许播放：是");
                else AppendMenu(g_Menu, MF_STRING, ID_Menu_Battery, L"使用电池时允许播放：否");
                wchar_t buf[22];
                _snwprintf(buf, ARRAYSIZE(buf), L"独立息屏控制 滚轮修改时间:%d秒", g_Timeout);
                AppendMenu(g_Menu, MF_STRING, ID_Menu_ScreenOff, buf);
                if(g_Screenoff)
                {
                    CheckMenuItem(g_Menu, ID_Menu_ScreenOff, MF_BYCOMMAND | MF_CHECKED);
                    AppendMenu(g_Menu, MF_STRING, ID_Menu_TimeoutFirst, L"├─ 1.超时优先");
                    AppendMenu(g_Menu, MF_STRING, ID_Menu_PlayFirst, L"├─ 2.完成播放优先");
                    AppendMenu(g_Menu, MF_STRING, ID_Menu_PlayThenTimeout, L"└─ 3.先播放再超时");
                    CheckMenuItem(g_Menu, g_TimeoutType, MF_BYCOMMAND | MF_CHECKED);
                }
                else
                {
                    CheckMenuItem(g_Menu, ID_Menu_ScreenOff, MF_BYCOMMAND | MF_UNCHECKED);
                }
                AppendMenu(g_Menu, MF_STRING, ID_Menu_HideTray, L"永久隐藏托盘");
                AppendMenu(g_Menu, MF_STRING, ID_Menu_AutoRun, L"设置开机自启");
                if(g_AutoRun)
                {
                    CheckMenuItem(g_Menu, ID_Menu_AutoRun, MF_BYCOMMAND | MF_CHECKED);
                }
                AppendMenu(g_Menu, MF_STRING, ID_Menu_About, L"关于");
                AppendMenu(g_Menu, MF_STRING, ID_Menu_Quit, L"退出");
                // 获取鼠标位置（屏幕坐标）
                POINT pt;
                GetCursorPos(&pt);
                if(g_Screenoff && !g_hMouseHook)g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
                SetForegroundWindow(hwnd);
                // 显示菜单
                TrackPopupMenu(g_Menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

                DestroyMenu(g_Menu); // 别忘了销毁
                if(g_Screenoff && g_hMouseHook)
                {
                    UnhookWindowsHookEx(g_hMouseHook);
                    g_hMouseHook = NULL;
                    _snwprintf(buf, ARRAYSIZE(buf), L"%d", g_Timeout);
                    WritePrivateProfileString(L"Setting", L"Timeout", buf, g_ConfigPath);
                }
                
            }
            break;
        }
        case WM_COMMAND:
        {
            switch(LOWORD(wParam))
            {
                case ID_Menu_Enable:
                    g_Enable = !g_Enable;
                    WritePrivateProfileString(L"Setting", L"Enable", g_Enable ? L"1" : L"0", g_ConfigPath);
                    break;
                case ID_Menu_LoadPlayList:
                    LoadPlayList();
                    break;
                case ID_Menu_Mute:
                    g_Mute = !g_Mute;
                    WritePrivateProfileString(L"Setting", L"Mute", g_Mute ? L"1" : L"0", g_ConfigPath);
                    break;
                case ID_Menu_AutoScale:
                    g_AutoScale = !g_AutoScale;
                    WritePrivateProfileString(L"Setting", L"AutoScale", g_AutoScale ? L"1" : L"0", g_ConfigPath);
                    break;
                case ID_Menu_ScreenOff:
                    g_Screenoff = !g_Screenoff;
                    WritePrivateProfileString(L"Setting", L"Screenoff", g_Screenoff ? L"1" : L"0", g_ConfigPath);
                    break;
                case ID_Menu_TimeoutFirst:
                case ID_Menu_PlayFirst:
                case ID_Menu_PlayThenTimeout:
                {
                    g_TimeoutType = LOWORD(wParam);
                    wchar_t value[3];
                    _snwprintf(value, ARRAYSIZE(value), L"%d", g_TimeoutType);
                    WritePrivateProfileString(L"Setting", L"TimeoutType", value, g_ConfigPath);
                    break;
                }
                case ID_Menu_HideTray:
                {
                    if(MessageBox(hwnd, L"您真的想隐藏托盘吗？该选项会永久生效直到您主动修改配置文件。\n您可以通过任务管理器或者再次运行本程序来关闭此程序", L"提示", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                    {
                        WritePrivateProfileString(L"Setting", L"HideTray", L"1", g_ConfigPath);
                        Shell_NotifyIcon(NIM_DELETE, &g_nid);
                        g_HideTray = true;
                    }
                    break;
                }
                case ID_Menu_AutoRun:
                {
                    if(g_AutoRun)//开机自启
                    {
                        wchar_t buf[MAX_PATH];
                        wchar_t temp[MAX_PATH];
                        SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, buf);
                        StrNCatW(buf, L"\\LockEngine.lnk", ARRAYSIZE(buf));
                        if(PathFileExists(buf))
                        {
                            DeleteFile(buf);
                        }
                        g_AutoRun = false;
                        WritePrivateProfileString(L"Setting", L"AutoRun", L"0", g_ConfigPath);
                        _snwprintf(temp, ARRAYSIZE(temp), L"开机自启已移除\n路径:\n%ws", buf);
                        MessageBox(NULL, temp, L"LockEngine", MB_OK | MB_ICONINFORMATION);
                    }
                    else
                    {
                        wchar_t buf[MAX_PATH];
                        wchar_t temp[MAX_PATH];
                        SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, buf);
                        StrNCatW(buf, L"\\LockEngine.lnk", ARRAYSIZE(buf));

                        if(CreateShortcut(buf, g_argv[0], NULL, NULL, NULL, 0, SW_NORMAL, L"动态锁屏壁纸", false, NULL))
                        {
                            g_AutoRun = true;
                            WritePrivateProfileString(L"Setting", L"AutoRun", L"1", g_ConfigPath);
                            _snwprintf(temp, ARRAYSIZE(temp), L"开机自启已添加\n路径:\n%ws", buf);
                            MessageBox(NULL, temp, L"LockEngine", MB_OK | MB_ICONINFORMATION);
                        }
                        else
                        {
                            _snwprintf(temp, ARRAYSIZE(temp), L"开机自启添加失败\n路径:\n%ws\n请以管理员权限重试", buf);
                            MessageBox(NULL, temp, L"LockEngine", MB_OK | MB_ICONERROR);
                        }
                    }
                    break;
                }
                case ID_Menu_Battery:
                    g_Battery = !g_Battery;
                    WritePrivateProfileString(L"Setting", L"AllowBattery", g_Battery ? L"1" : L"0", g_ConfigPath);
                    break;
                case ID_Menu_About:
                {
                    g_AboutHWND = CreateWindowEx(
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
                case ID_Menu_Quit:
                {
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;
                }
            }
            break;
        }
        case WM_TIMER:
        {
            if(wParam == 1)//判断锁屏桌面情况的Timer
            {
                if(g_LogonStatus)//在普通桌面?
                {
                    if(IsInputDesktopSecure())//到安全桌面了，立即停止
                    {
                        SetThreadExecutionState(ES_CONTINUOUS);
                        StopMedia();
                        KillTimer(hwnd, 2);//关闭内置时钟
                        g_LogonStatus = false;
                    }
                }
                else//转到安全桌面了?
                {
                    if(!IsInputDesktopSecure())//到普通桌面了，赶紧播放
                    {
                        PlayMedia();
                        g_LogonStatus = true;
                    }
                }
            }
            else//wParam==2，自定义时钟
            {
                GetLastInputInfo(&g_lii);
				if(GetTickCount() - g_LastInputTimeFix - g_lii.dwTime >= (g_Timeout - 2) * 1000)//修正因ShutMonitor2秒差
                {
                    KillTimer(hwnd, 2);//自杀
                    g_TimerRunning = false;
                    SetThreadExecutionState(ES_CONTINUOUS);
                    ShutMonitor();
                    StopMedia();
                }
            }
            break;
        }
        case WM_ResetPower:
        {
            SetThreadExecutionState(ES_CONTINUOUS);
            ShutMonitor();
            break;
        }
        case WM_DESTROY:
        {
            if(!g_HideTray)Shell_NotifyIcon(NIM_DELETE, &g_nid);
            if(mediaPlayer)libvlc_media_player_release(mediaPlayer);
            if(vlcInstance)libvlc_release(vlcInstance);
            WTSUnRegisterSessionNotification(hwnd);
            UnregisterPowerSettingNotification(g_hPowerNotify);
            PostQuitMessage(0);
            break;
        }
        default:
        {
            if(msg == WM_KeepTray)//恢复托盘
            {
                RestoreTray(hwnd);
            }
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
    static HWND LinkHwnd_Github;
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

            LinkHwnd_Github = CreateWindowEx(
                NULL, L"SysLink",
                L"Github开源地址: <a href=\"https://github.com/SiyuanX237/LockEngine\">LockEngine</a>",
                WS_CHILD | WS_VISIBLE,
                FontSize.cx * 2,
                LineAnchorY[5] + FontSize.cy,
                g_ClientSize.cx,
                FontSize.cy * 1.5,
                hwnd,
                (HMENU)ID_LINK,
                NULL,
                NULL
            );
            SendMessage(LinkHwnd_Github, WM_SETFONT, (WPARAM)Font, 0);

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
            DrawText(hdc, TEXT("版本：1.5.0.0                日期：2026.1.10"), -1, &RectDrawText, DT_LEFT | DT_SINGLELINE);
            MoveToEx(hdc, FontSize.cx * 2, LineAnchorY[2], NULL);
            LineTo(hdc, g_ClientSize.cx - FontSize.cx * 2, LineAnchorY[2]);

            RectDrawText.left = FontSize.cx * 2;
            RectDrawText.right = g_ClientSize.cx - FontSize.cx * 2;
            RectDrawText.top = LineAnchorY[2] + FontSize.cy;
            RectDrawText.bottom = RectDrawText.top + FontSize.cy * 2;
            DrawText(hdc, TEXT("LockEngine是个免费软件，基于VLC3.0.19开源库制作"), -1, &RectDrawText, DT_LEFT | DT_SINGLELINE);
            SelectObject(hdc, OldFont);
            EndPaint(hwnd, &ps);
            
            break;
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
            DeleteObject(Font);
            DeleteObject(BoldFont);
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    {
        HWND self;
        if(self = FindWindow(L"LockEngine", L"Wallpaper"))
        {
            if(MessageBox(NULL, TEXT("已有程序实例在运行\n您想关闭已经运行的程序？"), TEXT("提示"), MB_YESNO | MB_ICONINFORMATION) == IDYES)
            {
                SendMessage(self, WM_CLOSE, 0, 0);
            }
            return 0;
        }
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

    g_WindowWidth = GetSystemMetrics(SM_CXSCREEN);
    g_WindowHeight = GetSystemMetrics(SM_CYSCREEN);
    g_MainHwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        CLASS_NAME, L"Wallpaper",
        WS_POPUP,
        0, 0, g_WindowWidth, g_WindowHeight,
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
