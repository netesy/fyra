#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/TargetRelocationEvaluator.h"
#include "target/core/TargetInfo.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace target {
namespace artifact {
namespace linker {

namespace {

uint64_t alignUp(uint64_t offset, uint64_t align) {
    if (align <= 1) return offset;
    return (offset + align - 1) & ~(align - 1);
}

// A self-contained Windows image must resolve the C runtime and Win32 API that
// the linked runtime archive depends on without calling an external driver.
// The MinGW import libraries are COFF stubs our linker cannot expand, so the
// unresolved references are classified here against a curated export database
// and turned into real PE import-table entries. Symbols that belong to the
// statically linked C++/GCC runtime (libstdc++, libgcc) are intentionally NOT
// imported; if they are still undefined it means a static archive was not
// contributed, which is reported as an error rather than a bogus DLL import.
struct WindowsImportInfo {
    bool handled = false;
    std::string name;   // bare export name registered in the import table
    std::string dll;
    bool data = false;  // true when referenced through an __imp_ pointer
};

const std::unordered_map<std::string, std::string>& windows_dll_exports() {
    // Symbol -> owning system DLL. UCRT (ucrtbase.dll) is the C runtime this
    // toolchain's objects target (evidenced by __acrt_iob_func/_errno).
    static const std::unordered_map<std::string, std::string> exports = {
        // ---- C runtime (UCRT) ----
        // atexit/_onexit are NOT exported by ucrtbase.dll on this CRT; the
        // loader resolves them from msvcrt.dll (verified via GetProcAddress).
        {"atexit","msvcrt.dll"},{"_onexit","msvcrt.dll"},{"_XcptFilter","msvcrt.dll"},{"_amsg_exit","msvcrt.dll"},{"exit","ucrtbase.dll"},{"_exit","ucrtbase.dll"},
        {"abort","ucrtbase.dll"},{"_cexit","ucrtbase.dll"},{"_crt_atexit","ucrtbase.dll"},
        {"_configure_narrow_argv","ucrtbase.dll"},{"_initialize_onexit_table","ucrtbase.dll"},
        {"_execute_onexit_table","ucrtbase.dll"},{"_register_onexit_function","ucrtbase.dll"},
        {"__acrt_iob_func","ucrtbase.dll"},{"_errno","ucrtbase.dll"},{"errno","ucrtbase.dll"},
        {"malloc","ucrtbase.dll"},{"free","ucrtbase.dll"},{"calloc","ucrtbase.dll"},{"realloc","ucrtbase.dll"},
        {"_msize","ucrtbase.dll"},
        {"memcpy","ucrtbase.dll"},{"memmove","ucrtbase.dll"},{"memset","ucrtbase.dll"},{"memcmp","ucrtbase.dll"},{"memchr","ucrtbase.dll"},
        {"strcpy","ucrtbase.dll"},{"strncpy","ucrtbase.dll"},{"strcat","ucrtbase.dll"},{"strncat","ucrtbase.dll"},
        {"strcmp","ucrtbase.dll"},{"strncmp","ucrtbase.dll"},{"strchr","ucrtbase.dll"},{"strrchr","ucrtbase.dll"},
        {"strstr","ucrtbase.dll"},{"strlen","ucrtbase.dll"},{"strnlen","ucrtbase.dll"},{"strerror","ucrtbase.dll"},
        {"strtok","ucrtbase.dll"},{"strdup","ucrtbase.dll"},{"strcoll","ucrtbase.dll"},{"strxfrm","ucrtbase.dll"},
        {"strcpy_s","ucrtbase.dll"},{"strcat_s","ucrtbase.dll"},{"sprintf_s","ucrtbase.dll"},{"vsprintf_s","ucrtbase.dll"},
        {"fopen","ucrtbase.dll"},{"freopen","ucrtbase.dll"},{"fclose","ucrtbase.dll"},{"fflush","ucrtbase.dll"},
        {"fread","ucrtbase.dll"},{"fwrite","ucrtbase.dll"},{"fseek","ucrtbase.dll"},{"ftell","ucrtbase.dll"},
        {"rewind","ucrtbase.dll"},{"ferror","ucrtbase.dll"},{"feof","ucrtbase.dll"},{"setvbuf","ucrtbase.dll"},
        {"fprintf","ucrtbase.dll"},{"printf","ucrtbase.dll"},{"sprintf","ucrtbase.dll"},{"snprintf","ucrtbase.dll"},
        {"vfprintf","ucrtbase.dll"},{"vprintf","ucrtbase.dll"},{"vsprintf","ucrtbase.dll"},{"vsnprintf","ucrtbase.dll"},
        {"fputs","ucrtbase.dll"},{"fgets","ucrtbase.dll"},{"fputc","ucrtbase.dll"},{"fgetc","ucrtbase.dll"},
        {"puts","ucrtbase.dll"},{"gets","ucrtbase.dll"},{"putchar","ucrtbase.dll"},{"getchar","ucrtbase.dll"},
        {"putc","ucrtbase.dll"},{"getc","ucrtbase.dll"},{"ungetc","ucrtbase.dll"},{"perror","ucrtbase.dll"},
        {"remove","ucrtbase.dll"},{"rename","ucrtbase.dll"},{"tmpfile","ucrtbase.dll"},{"tmpnam","ucrtbase.dll"},
        {"sscanf","ucrtbase.dll"},{"fscanf","ucrtbase.dll"},{"scanf","ucrtbase.dll"},
        {"getenv","ucrtbase.dll"},{"_dupenv_s","ucrtbase.dll"},{"system","ucrtbase.dll"},
        {"qsort","ucrtbase.dll"},{"bsearch","ucrtbase.dll"},{"rand","ucrtbase.dll"},{"srand","ucrtbase.dll"},{"abs","ucrtbase.dll"},
        {"signal","ucrtbase.dll"},{"raise","ucrtbase.dll"},
        {"time","ucrtbase.dll"},{"clock","ucrtbase.dll"},{"difftime","ucrtbase.dll"},{"mktime","ucrtbase.dll"},
        {"localtime","ucrtbase.dll"},{"localtime_s","ucrtbase.dll"},{"gmtime","ucrtbase.dll"},{"gmtime_s","ucrtbase.dll"},
        {"strftime","ucrtbase.dll"},{"asctime","ucrtbase.dll"},{"ctime","ucrtbase.dll"},
        {"strtod","ucrtbase.dll"},{"strtof","ucrtbase.dll"},{"strtold","ucrtbase.dll"},{"strtol","ucrtbase.dll"},
        {"strtoll","ucrtbase.dll"},{"strtoul","ucrtbase.dll"},{"strtoull","ucrtbase.dll"},{"atoi","ucrtbase.dll"},
        {"atol","ucrtbase.dll"},{"atoll","ucrtbase.dll"},{"atof","ucrtbase.dll"},{"itoa","ucrtbase.dll"},
        {"ceil","ucrtbase.dll"},{"ceilf","ucrtbase.dll"},{"floor","ucrtbase.dll"},{"floorf","ucrtbase.dll"},
        {"fabs","ucrtbase.dll"},{"fabsf","ucrtbase.dll"},{"sqrt","ucrtbase.dll"},{"sqrtf","ucrtbase.dll"},
        {"pow","ucrtbase.dll"},{"powf","ucrtbase.dll"},{"exp","ucrtbase.dll"},{"expf","ucrtbase.dll"},
        {"log","ucrtbase.dll"},{"logf","ucrtbase.dll"},{"log10","ucrtbase.dll"},{"log10f","ucrtbase.dll"},{"log2","ucrtbase.dll"},{"log2f","ucrtbase.dll"},
        {"sin","ucrtbase.dll"},{"sinf","ucrtbase.dll"},{"cos","ucrtbase.dll"},{"cosf","ucrtbase.dll"},{"tan","ucrtbase.dll"},{"tanf","ucrtbase.dll"},
        {"asin","ucrtbase.dll"},{"acos","ucrtbase.dll"},{"atan","ucrtbase.dll"},{"atan2","ucrtbase.dll"},{"atan2f","ucrtbase.dll"},
        {"sinh","ucrtbase.dll"},{"cosh","ucrtbase.dll"},{"tanh","ucrtbase.dll"},{"fmod","ucrtbase.dll"},{"fmodf","ucrtbase.dll"},
        {"frexp","ucrtbase.dll"},{"ldexp","ucrtbase.dll"},{"modf","ucrtbase.dll"},{"round","ucrtbase.dll"},{"roundf","ucrtbase.dll"},
        {"trunc","ucrtbase.dll"},{"truncf","ucrtbase.dll"},{"rint","ucrtbase.dll"},{"nearbyint","ucrtbase.dll"},{"hypot","ucrtbase.dll"},{"hypotf","ucrtbase.dll"},
        {"isnan","ucrtbase.dll"},{"isinf","ucrtbase.dll"},{"finite","ucrtbase.dll"},
        // ---- kernel32 ----
        {"ExitProcess","kernel32.dll"},{"GetStdHandle","kernel32.dll"},{"WriteFile","kernel32.dll"},{"ReadFile","kernel32.dll"},
        {"VirtualAlloc","kernel32.dll"},{"VirtualFree","kernel32.dll"},{"VirtualProtect","kernel32.dll"},{"VirtualQuery","kernel32.dll"},
        {"GetLastError","kernel32.dll"},{"SetLastError","kernel32.dll"},{"Sleep","kernel32.dll"},{"FlushFileBuffers","kernel32.dll"},
        {"GetCommandLineA","kernel32.dll"},{"GetCommandLineW","kernel32.dll"},{"GetCurrentProcessId","kernel32.dll"},{"GetCurrentThreadId","kernel32.dll"},
        {"GetCurrentProcess","kernel32.dll"},{"GetCurrentThread","kernel32.dll"},{"GetModuleHandleA","kernel32.dll"},{"GetModuleHandleW","kernel32.dll"},
        {"CreateFileA","kernel32.dll"},{"CreateFileW","kernel32.dll"},{"CloseHandle","kernel32.dll"},{"SetFilePointer","kernel32.dll"},
        {"GetFileSize","kernel32.dll"},{"CreateThread","kernel32.dll"},{"WaitForSingleObject","kernel32.dll"},{"WaitForSingleObjectEx","kernel32.dll"},
        {"WaitForMultipleObjects","kernel32.dll"},{"SwitchToThread","kernel32.dll"},{"GetTickCount","kernel32.dll"},{"GetTickCount64","kernel32.dll"},
        {"QueryPerformanceCounter","kernel32.dll"},{"QueryPerformanceFrequency","kernel32.dll"},{"GetSystemTimeAsFileTime","kernel32.dll"},
        {"LoadLibraryA","kernel32.dll"},{"LoadLibraryW","kernel32.dll"},{"FreeLibrary","kernel32.dll"},{"GetProcAddress","kernel32.dll"},
        {"MapViewOfFile","kernel32.dll"},{"CreateFileMappingA","kernel32.dll"},{"UnmapViewOfFile","kernel32.dll"},
        {"CreateProcessA","kernel32.dll"},{"GetEnvironmentVariableA","kernel32.dll"},{"SetEnvironmentVariableA","kernel32.dll"},
        {"GetEnvironmentStringsW","kernel32.dll"},{"FreeEnvironmentStringsW","kernel32.dll"},{"GetSystemInfo","kernel32.dll"},{"GetNativeSystemInfo","kernel32.dll"},
        {"CreateEventA","kernel32.dll"},{"SetEvent","kernel32.dll"},{"ResetEvent","kernel32.dll"},{"CreateMutexA","kernel32.dll"},
        {"ReleaseMutex","kernel32.dll"},{"CreateSemaphoreA","kernel32.dll"},{"ReleaseSemaphore","kernel32.dll"},
        {"AcquireSRWLockExclusive","kernel32.dll"},{"ReleaseSRWLockExclusive","kernel32.dll"},{"AcquireSRWLockShared","kernel32.dll"},{"ReleaseSRWLockShared","kernel32.dll"},
        {"InitializeSRWLock","kernel32.dll"},{"InitializeCriticalSection","kernel32.dll"},{"InitializeCriticalSectionEx","kernel32.dll"},{"DeleteCriticalSection","kernel32.dll"},
        {"EnterCriticalSection","kernel32.dll"},{"LeaveCriticalSection","kernel32.dll"},{"InterlockedExchangeAdd","kernel32.dll"},{"InterlockedCompareExchange","kernel32.dll"},
        {"GetProcessHeap","kernel32.dll"},{"HeapAlloc","kernel32.dll"},{"HeapFree","kernel32.dll"},{"HeapReAlloc","kernel32.dll"},{"HeapCreate","kernel32.dll"},{"HeapDestroy","kernel32.dll"},
        {"FormatMessageA","kernel32.dll"},{"GetUserDefaultLangID","kernel32.dll"},{"GlobalAlloc","kernel32.dll"},{"GlobalFree","kernel32.dll"},
        // ---- bcrypt / random ----
        {"BCryptGenRandom","bcrypt.dll"},
        // ---- user32 ----
        {"MessageBoxA","user32.dll"},{"MessageBoxW","user32.dll"},{"CreateWindowExA","user32.dll"},{"CreateWindowExW","user32.dll"},
        {"DefWindowProcA","user32.dll"},{"DefWindowProcW","user32.dll"},{"DestroyWindow","user32.dll"},{"ShowWindow","user32.dll"},
        {"UpdateWindow","user32.dll"},{"RegisterClassExA","user32.dll"},{"RegisterClassExW","user32.dll"},{"PostQuitMessage","user32.dll"},
        {"PeekMessageA","user32.dll"},{"PeekMessageW","user32.dll"},{"GetMessageA","user32.dll"},{"TranslateMessage","user32.dll"},{"DispatchMessageA","user32.dll"},{"DispatchMessageW","user32.dll"},
        {"PostMessageA","user32.dll"},{"PostMessageW","user32.dll"},{"SendMessageA","user32.dll"},{"SendMessageW","user32.dll"},
        {"GetDC","user32.dll"},{"ReleaseDC","user32.dll"},{"BeginPaint","user32.dll"},{"EndPaint","user32.dll"},{"InvalidateRect","user32.dll"},
        {"SetWindowPos","user32.dll"},{"GetClientRect","user32.dll"},{"GetWindowRect","user32.dll"},{"AdjustWindowRectEx","user32.dll"},
        {"LoadCursorA","user32.dll"},{"SetCursor","user32.dll"},{"ShowCursor","user32.dll"},{"GetKeyState","user32.dll"},{"MapVirtualKeyA","user32.dll"},
        {"ClientToScreen","user32.dll"},{"ScreenToClient","user32.dll"},{"SetWindowTextW","user32.dll"},{"GetActiveWindow","user32.dll"},
        {"TrackPopupMenu","user32.dll"},{"GetSystemMetrics","user32.dll"},
        // ---- gdi32 ----
        {"CreateWindowEx","user32.dll"},
        {"SwapBuffers","gdi32.dll"},{"ChoosePixelFormat","gdi32.dll"},{"SetPixelFormat","gdi32.dll"},{"GetDeviceCaps","gdi32.dll"},
        {"CreateCompatibleDC","gdi32.dll"},{"CreateCompatibleBitmap","gdi32.dll"},{"SelectObject","gdi32.dll"},{"DeleteObject","gdi32.dll"},{"DeleteDC","gdi32.dll"},
        {"BitBlt","gdi32.dll"},{"StretchBlt","gdi32.dll"},{"GetStockObject","gdi32.dll"},{"CreateDCW","gdi32.dll"},
        // ---- opengl32 ----
        {"wglChoosePixelFormatARB","opengl32.dll"},{"wglCreateContext","opengl32.dll"},{"wglDeleteContext","opengl32.dll"},{"wglMakeCurrent","opengl32.dll"},
        {"wglGetCurrentContext","opengl32.dll"},{"wglGetProcAddress","opengl32.dll"},{"glGetIntegerv","opengl32.dll"},{"glViewport","opengl32.dll"},
        // ---- shell32 ----
        {"ShellExecuteA","shell32.dll"},{"ShellExecuteW","shell32.dll"},{"SHGetKnownFolderPath","shell32.dll"},
        // ---- ole32 ----
        {"CoInitializeEx","ole32.dll"},{"CoUninitialize","ole32.dll"},{"CoCreateInstance","ole32.dll"},{"CoTaskMemFree","ole32.dll"},{"StringFromGUID2","ole32.dll"},
        // ---- advapi32 ----
        {"RegOpenKeyExA","advapi32.dll"},{"RegQueryValueExA","advapi32.dll"},{"RegCloseKey","advapi32.dll"},
        // ---- ws2_32 (winsock) ----
        {"WSAStartup","ws2_32.dll"},{"WSACleanup","ws2_32.dll"},{"WSAGetLastError","ws2_32.dll"},{"__WSAFDIsSet","ws2_32.dll"},
        {"socket","ws2_32.dll"},{"closesocket","ws2_32.dll"},{"connect","ws2_32.dll"},{"bind","ws2_32.dll"},{"listen","ws2_32.dll"},
        {"accept","ws2_32.dll"},{"send","ws2_32.dll"},{"recv","ws2_32.dll"},{"select","ws2_32.dll"},{"htons","ws2_32.dll"},{"inet_pton","ws2_32.dll"},{"getaddrinfo","ws2_32.dll"},{"freeaddrinfo","ws2_32.dll"},
        // ---- additional UCRT entries reached through __imp_ pointers ----
        {"_assert","ucrtbase.dll"},{"freopen_s","ucrtbase.dll"},{"_lock","ucrtbase.dll"},{"_unlock","ucrtbase.dll"},
        // ---- kernel32 (sokol / console) ----
        {"AllocConsole","kernel32.dll"},{"AttachConsole","kernel32.dll"},{"FreeConsole","kernel32.dll"},
        {"GetConsoleOutputCP","kernel32.dll"},{"SetConsoleOutputCP","kernel32.dll"},{"GetConsoleWindow","kernel32.dll"},
        {"GlobalLock","kernel32.dll"},{"GlobalUnlock","kernel32.dll"},{"GlobalSize","kernel32.dll"},
        {"MultiByteToWideChar","kernel32.dll"},{"WideCharToMultiByte","kernel32.dll"},{"GetACP","kernel32.dll"},
        {"GetForegroundWindow","user32.dll"},
        // ---- user32 (sokol window/input/clipboard) ----
        {"PtInRect","user32.dll"},{"GetMonitorInfoA","user32.dll"},{"GetMonitorInfoW","user32.dll"},
        {"MonitorFromPoint","user32.dll"},{"MonitorFromWindow","user32.dll"},{"GetPrimaryMonitorInfo","user32.dll"},
        {"CreateIconIndirect","user32.dll"},{"DestroyIcon","user32.dll"},{"LoadIconA","user32.dll"},{"LoadIconW","user32.dll"},
        {"LoadCursorW","user32.dll"},{"LoadImageW","user32.dll"},{"LoadImageA","user32.dll"},
        {"GetCursorPos","user32.dll"},{"SetCursorPos","user32.dll"},{"ClipCursor","user32.dll"},{"GetCursorInfo","user32.dll"},
        {"TrackMouseEvent","user32.dll"},{"SetCapture","user32.dll"},{"ReleaseCapture","user32.dll"},{"GetCapture","user32.dll"},
        {"GetRawInputData","user32.dll"},{"GetRawInputDeviceInfoA","user32.dll"},{"RegisterRawInputDevices","user32.dll"},
        {"GetWindowLongA","user32.dll"},{"GetWindowLongW","user32.dll"},{"SetWindowLongA","user32.dll"},{"SetWindowLongW","user32.dll"},
        {"SetWindowLongPtrA","user32.dll"},{"SetWindowLongPtrW","user32.dll"},{"GetWindowLongPtrA","user32.dll"},{"GetWindowLongPtrW","user32.dll"},
        {"GetWindow","user32.dll"},{"GetTopWindow","user32.dll"},{"WindowFromPoint","user32.dll"},{"SetForegroundWindow","user32.dll"},
        {"GetWindowTextW","user32.dll"},{"GetWindowTextA","user32.dll"},
        {"IsIconic","user32.dll"},{"IsWindowVisible","user32.dll"},{"IsWindow","user32.dll"},{"Iconify","user32.dll"},
        {"KillTimer","user32.dll"},{"SetTimer","user32.dll"},{"KillTimer","user32.dll"},
        {"RegisterClassA","user32.dll"},{"RegisterClassW","user32.dll"},{"UnregisterClassA","user32.dll"},{"UnregisterClassW","user32.dll"},{"GetClassLongPtrA","user32.dll"},
        {"OpenClipboard","user32.dll"},{"CloseClipboard","user32.dll"},{"EmptyClipboard","user32.dll"},
        {"GetClipboardData","user32.dll"},{"SetClipboardData","user32.dll"},{"EnumClipboardFormats","user32.dll"},
        {"GetAsyncKeyState","user32.dll"},{"GetMouseMovePointsEx","user32.dll"},{"MapVirtualKeyW","user32.dll"},{"GetKeyboardLayout","user32.dll"},
        {"EnableWindow","user32.dll"},{"SetFocus","user32.dll"},{"GetFocus","user32.dll"},
        // ---- gdi32 (sokol DIB surface) ----
        {"CreateDIBSection","gdi32.dll"},{"StretchDIBits","gdi32.dll"},{"CreateBitmap","gdi32.dll"},
        {"CreateDCW","gdi32.dll"},{"CreateRectRgn","gdi32.dll"},{"SelectClipRgn","gdi32.dll"},{"GetLayout","gdi32.dll"},
        // ---- shell32 (drag & drop) ----
        {"DragAcceptFiles","shell32.dll"},{"DragFinish","shell32.dll"},{"DragQueryFileW","shell32.dll"},{"DragQueryFileA","shell32.dll"},{"DragQueryPoint","shell32.dll"},
        // ---- imm32 (IME) ----
        {"ImmGetContext","imm32.dll"},{"ImmReleaseContext","imm32.dll"},{"ImmGetCompositionStringW","imm32.dll"},{"ImmSetCompositionWindow","imm32.dll"},{"ImmAssociateContext","imm32.dll"},
        // ---- runtime-archive externals (winpthread/stdc++/mingwex/gcc) ----
        // Verified against MinGW import libraries; every entry's owning DLL is
        // the one that actually exports the symbol (nm --defined-only on lib*.a).
        {"___mb_cur_max_func","ucrtbase.dll"},{"_aligned_free","ucrtbase.dll"},{"_aligned_malloc","ucrtbase.dll"},
        {"_beginthreadex","ucrtbase.dll"},{"_close","ucrtbase.dll"},{"_endthreadex","ucrtbase.dll"},
        {"_fpreset","ucrtbase.dll"},{"_fstat64","ucrtbase.dll"},{"_get_osfhandle","ucrtbase.dll"},
        {"_read","ucrtbase.dll"},{"_wchdir","ucrtbase.dll"},{"_wchmod","ucrtbase.dll"},
        {"_wfopen","ucrtbase.dll"},{"_wgetcwd","ucrtbase.dll"},{"_wmkdir","ucrtbase.dll"},
        {"_wopen","ucrtbase.dll"},{"_wstat64","ucrtbase.dll"},{"_wutime64","ucrtbase.dll"},
        {"isspace","ucrtbase.dll"},{"longjmp","ucrtbase.dll"},{"rand_s","ucrtbase.dll"},
        {"AreFileApisANSI","kernel32.dll"},{"CreateHardLinkW","kernel32.dll"},{"DeleteFileW","kernel32.dll"},
        {"DuplicateHandle","kernel32.dll"},{"FileTimeToSystemTime","kernel32.dll"},{"FindFirstVolumeW","kernel32.dll"},
        {"FindNextVolumeW","kernel32.dll"},{"FindVolumeClose","kernel32.dll"},{"GetDiskFreeSpaceExW","kernel32.dll"},
        {"GetEnvironmentVariableW","kernel32.dll"},{"GetFileAttributesA","kernel32.dll"},{"GetFileAttributesW","kernel32.dll"},
        {"GetFileInformationByHandle","kernel32.dll"},{"GetFileSizeEx","kernel32.dll"},{"GetFileType","kernel32.dll"},
        {"GetFullPathNameW","kernel32.dll"},{"GetHandleInformation","kernel32.dll"},{"GetProcessAffinityMask","kernel32.dll"},
        {"GetProcessTimes","kernel32.dll"},{"GetSystemTimeAdjustment","kernel32.dll"},{"GetTempPathW","kernel32.dll"},
        {"GetThreadContext","kernel32.dll"},{"GetThreadPriority","kernel32.dll"},{"GetThreadTimes","kernel32.dll"},
        {"GetTimeZoneInformation","kernel32.dll"},{"GetVolumeInformationW","kernel32.dll"},{"IsDBCSLeadByteEx","kernel32.dll"},
        {"IsDebuggerPresent","kernel32.dll"},{"LocalAlloc","kernel32.dll"},{"LocalFree","kernel32.dll"},
        {"MoveFileExW","kernel32.dll"},{"OpenProcess","kernel32.dll"},{"OutputDebugStringA","kernel32.dll"},
        {"RaiseException","kernel32.dll"},{"RemoveDirectoryW","kernel32.dll"},{"ResumeThread","kernel32.dll"},
        {"SetEndOfFile","kernel32.dll"},{"SetProcessAffinityMask","kernel32.dll"},{"SetSystemTime","kernel32.dll"},
        {"SetThreadContext","kernel32.dll"},{"SetThreadPriority","kernel32.dll"},{"SuspendThread","kernel32.dll"},
        {"TerminateProcess","kernel32.dll"},{"TlsAlloc","kernel32.dll"},{"TlsGetValue","kernel32.dll"},
        {"TlsSetValue","kernel32.dll"},{"TryEnterCriticalSection","kernel32.dll"},
        {"GetUserNameA","advapi32.dll"},
        {"RtlCaptureContext","ntdll.dll"},{"RtlLookupFunctionEntry","ntdll.dll"},{"RtlUnwindEx","ntdll.dll"},{"RtlVirtualUnwind","ntdll.dll"},
        // ---- bare direct-call CRT/Win32 refs from pulled archives ----
        // (libmingwex/libmsvcrt/libmingw32/libstdc++ reference these without the
        //  __imp_ indirection). DLL assignment verified against MinGW import libs.
        {"___lc_codepage_func","ucrtbase.dll"},
        {"__argc","msvcrt.dll"},{"__argv","msvcrt.dll"},{"__iob_func","msvcrt.dll"},{"__mb_cur_max","msvcrt.dll"},
        {"__wargv","msvcrt.dll"},{"_acmdln","msvcrt.dll"},{"_commode","msvcrt.dll"},{"_daylight","msvcrt.dll"},
        {"_dstbias","msvcrt.dll"},{"_environ","msvcrt.dll"},{"_filbuf","msvcrt.dll"},{"_flsbuf","msvcrt.dll"},
        {"_fmode","msvcrt.dll"},{"_mbctype","msvcrt.dll"},{"_osplatform","msvcrt.dll"},{"_osver","msvcrt.dll"},
        {"_pctype","msvcrt.dll"},{"_pgmptr","msvcrt.dll"},{"_pwctype","msvcrt.dll"},{"_setjmp","msvcrt.dll"},
        {"_sys_errlist","msvcrt.dll"},{"_sys_nerr","msvcrt.dll"},{"_timezone","msvcrt.dll"},{"_tzname","msvcrt.dll"},
        {"_vcprintf","msvcrt.dll"},{"_vcprintf_l","msvcrt.dll"},{"_vcprintf_s","msvcrt.dll"},{"_vcprintf_s_l","msvcrt.dll"},
        {"_vcwprintf","msvcrt.dll"},{"_vcwprintf_l","msvcrt.dll"},{"_vcwprintf_s","msvcrt.dll"},{"_vcwprintf_s_l","msvcrt.dll"},
        {"_vscprintf","msvcrt.dll"},{"_vscprintf_l","msvcrt.dll"},{"_vscprintf_p_l","msvcrt.dll"},{"_vscwprintf","msvcrt.dll"},{"_vscwprintf_p_l","msvcrt.dll"},{"_vswprintf_p_l","msvcrt.dll"},
        {"_wcmdln","msvcrt.dll"},{"_wenviron","msvcrt.dll"},{"_winmajor","msvcrt.dll"},{"_winminor","msvcrt.dll"},
        {"_winver","msvcrt.dll"},{"_wpgmptr","msvcrt.dll"},{"fwscanf","msvcrt.dll"},{"swscanf","msvcrt.dll"},
        {"__C_specific_handler","kernel32.dll"},{"GetLocalTime","kernel32.dll"},{"GetStartupInfoA","kernel32.dll"},
        {"GetStartupInfoW","kernel32.dll"},{"IsProcessorFeaturePresent","kernel32.dll"},{"TlsFree","kernel32.dll"},
        {"__initenv","ucrtbase.dll"},{"__lconv_init","ucrtbase.dll"},{"__ms_fwprintf","ucrtbase.dll"},
        {"__p__acmdln","ucrtbase.dll"},{"__p__wcmdln","ucrtbase.dll"},{"__setusermatherr","ucrtbase.dll"},{"__winitenv","ucrtbase.dll"},
        {"_access","ucrtbase.dll"},{"_cgets","ucrtbase.dll"},{"_cgetws","ucrtbase.dll"},{"_chsize","ucrtbase.dll"},
        {"_controlfp","ucrtbase.dll"},{"_fgetwc_nolock","ucrtbase.dll"},{"_filelengthi64","ucrtbase.dll"},{"_fileno","ucrtbase.dll"},
        {"_findclose","ucrtbase.dll"},{"_findfirst64","ucrtbase.dll"},{"_findnext64","ucrtbase.dll"},{"_fputwc_nolock","ucrtbase.dll"},
        {"_fullpath","ucrtbase.dll"},{"_get_errno","ucrtbase.dll"},{"_gmtime32","ucrtbase.dll"},{"_gmtime64","ucrtbase.dll"},
        {"_hypot","ucrtbase.dll"},{"_i64toa","ucrtbase.dll"},{"_i64tow","ucrtbase.dll"},{"_isctype","ucrtbase.dll"},
        {"_ismbblead","ucrtbase.dll"},{"_localtime32","ucrtbase.dll"},{"_localtime32_s","ucrtbase.dll"},{"_localtime64","ucrtbase.dll"},
        {"_localtime64_s","ucrtbase.dll"},{"_lock_file","ucrtbase.dll"},{"_lseeki64","ucrtbase.dll"},{"_mbcasemap","ucrtbase.dll"},
        {"_mktemp","ucrtbase.dll"},{"_open","ucrtbase.dll"},{"_register_thread_local_exe_atexit_callback","ucrtbase.dll"},
        {"_set_errno","ucrtbase.dll"},{"_sopen","ucrtbase.dll"},{"_stat64","ucrtbase.dll"},{"_strtod_l","ucrtbase.dll"},
        {"_telli64","ucrtbase.dll"},{"_ui64toa","ucrtbase.dll"},{"_ui64tow","ucrtbase.dll"},{"_umask","ucrtbase.dll"},
        {"_unlock_file","ucrtbase.dll"},{"_vsnprintf","ucrtbase.dll"},{"_vsnwprintf","ucrtbase.dll"},{"_waccess","ucrtbase.dll"},
        {"_wasctime","ucrtbase.dll"},{"_wasctime_s","ucrtbase.dll"},{"_wcstod_l","ucrtbase.dll"},{"_wfindfirst64","ucrtbase.dll"},
        {"_wfindfirst64i32","ucrtbase.dll"},{"_wfindnext64","ucrtbase.dll"},{"_wfindnext64i32","ucrtbase.dll"},{"_wfullpath","ucrtbase.dll"},
        {"_wmktemp","ucrtbase.dll"},{"_wtoi64","ucrtbase.dll"},{"acosf","ucrtbase.dll"},{"asctime_s","ucrtbase.dll"},
        {"btowc","ucrtbase.dll"},{"casin","ucrtbase.dll"},{"casinf","ucrtbase.dll"},{"cexp","ucrtbase.dll"},{"cexpf","ucrtbase.dll"},
        {"clog","ucrtbase.dll"},{"clogf","ucrtbase.dll"},{"csqrt","ucrtbase.dll"},{"csqrtf","ucrtbase.dll"},{"fdopen","ucrtbase.dll"},
        {"fegetround","ucrtbase.dll"},{"fesetenv","ucrtbase.dll"},{"fesetround","ucrtbase.dll"},{"fetestexcept","ucrtbase.dll"},
        {"fgetpos","ucrtbase.dll"},{"fgetwc","ucrtbase.dll"},{"fileno","ucrtbase.dll"},{"fputwc","ucrtbase.dll"},{"fsetpos","ucrtbase.dll"},
        {"getwc","ucrtbase.dll"},{"iswctype","ucrtbase.dll"},{"isxdigit","ucrtbase.dll"},{"localeconv","ucrtbase.dll"},
        {"log1p","ucrtbase.dll"},{"log1pf","ucrtbase.dll"},{"mbrlen","ucrtbase.dll"},{"mbrtowc","ucrtbase.dll"},{"mbstowcs","ucrtbase.dll"},
        {"putwc","ucrtbase.dll"},{"remainder","ucrtbase.dll"},{"setlocale","ucrtbase.dll"},{"tolower","ucrtbase.dll"},
        {"towlower","ucrtbase.dll"},{"towupper","ucrtbase.dll"},{"ungetwc","ucrtbase.dll"},{"wcrtomb","ucrtbase.dll"},{"wcscat","ucrtbase.dll"},
        {"wcscmp","ucrtbase.dll"},{"wcscoll","ucrtbase.dll"},{"wcscpy","ucrtbase.dll"},{"wcscspn","ucrtbase.dll"},{"wcsftime","ucrtbase.dll"},
        {"wcslen","ucrtbase.dll"},{"wcsnlen","ucrtbase.dll"},{"wcsspn","ucrtbase.dll"},{"wcstol","ucrtbase.dll"},{"wcstoll","ucrtbase.dll"},
        {"wcstombs","ucrtbase.dll"},{"wcstoul","ucrtbase.dll"},{"wcstoull","ucrtbase.dll"},{"wcsxfrm","ucrtbase.dll"},{"wctob","ucrtbase.dll"},
        {"wctype","ucrtbase.dll"},{"write","ucrtbase.dll"},
    };
    return exports;
}

// Prefixes that must be satisfied by a statically linked archive; they are
// never Windows system DLL imports.
bool isStaticRuntimeSymbol(const std::string& name) {
    static const std::vector<std::string> prefixes = {
        "_Z", "__cxa_", "__gxx_", "_Unwind_", "__emutls", "___chkstk", "__chkstk",
        "__float", "__floatunsitf", "__mingw_", "__gnu_", "__aeabi_", "__div", "__mul",
    };
    for (const auto& p : prefixes) {
        if (name.size() >= p.size() && name.compare(0, p.size(), p) == 0) return true;
    }
    return false;
}

bool classifyWindowsImport(const std::string& refName, WindowsImportInfo& out) {
    std::string base = refName;
    bool viaPointer = false;
    const std::string impPrefix = "__imp_";
    if (base.size() > impPrefix.size() && base.compare(0, impPrefix.size(), impPrefix) == 0) {
        base = base.substr(impPrefix.size());
        viaPointer = true;
    }
    const auto& exports = windows_dll_exports();
    auto it = exports.find(base);
    if (it == exports.end()) return false;
    out.handled = true;

    // This MinGW-w64 toolchain's active C runtime is msvcrt.dll (verified: the
    // default gcc link imports msvcrt.dll, and printf/fprintf/_vsnprintf/_lock/
    // _unlock all resolve there). The symbol database tags C runtime entries as
    // "ucrtbase.dll", but that module is not this toolchain's CRT, so normalize
    // them to msvcrt.dll -- EXCEPT for a narrow set of UCRT-only symbols the
    // runtime archive references that msvcrt.dll does NOT export (importing
    // those from msvcrt yields STATUS_ENTRYPOINT_NOT_FOUND; they are satisfied
    // by ucrtbase.dll, which is always present on Windows 10/11 and loads by
    // literal name).
    static const std::unordered_set<std::string> kUcrtbaseOnly = {
        "__acrt_iob_func", "_wfindfirst64i32", "_wfindnext64i32", "strtoll",
    };
    out.dll = it->second;
    if (out.dll == "ucrtbase.dll" && kUcrtbaseOnly.find(base) == kUcrtbaseOnly.end())
        out.dll = "msvcrt.dll";

    // MinGW also exposes POSIX-style names (fdopen, fileno, write, read, strdup)
    // that NO CRT DLL exports; the toolchain normally supplies them via static
    // libmingwex wrappers that forward to the underscored MS names of identical
    // semantics. A self-contained image has no mingwex wrappers, so bind those
    // imports straight to the underscored export. The whole import/fixup chain
    // keys off out.name, so the rewrite stays consistent with the __imp_<posix>
    // relocation sites that target it.
    static const std::unordered_map<std::string, std::string> kPosixAlias = {
        {"fdopen", "_fdopen"}, {"fileno", "_fileno"}, {"write", "_write"},
        {"read", "_read"}, {"strdup", "_strdup"},
    };
    out.name = base;
    auto alias = kPosixAlias.find(base);
    if (alias != kPosixAlias.end()) out.name = alias->second;

    out.data = viaPointer;
    return true;
}

} // namespace

bool InternalLinker::extractLazyArchiveMembers(
    std::vector<target::artifact::object::ObjectArtifact>& inOutArtifacts,
    std::vector<std::vector<target::artifact::archive::ArchiveObjectMember>>& archives) {

    bool extractedAny = false;
    bool progress = true;

    while (progress) {
        progress = false;

        std::unordered_set<std::string> definedGlobals;
        for (const auto& art : inOutArtifacts) {
            for (const auto& sym : art.symbols) {
                if (sym.isDefined && sym.binding != target::artifact::object::SymbolBinding::Local) {
                    definedGlobals.insert(sym.name);
                }
            }
        }

        std::unordered_set<std::string> unresolvedGlobals;
        for (const auto& art : inOutArtifacts) {
            for (const auto& reloc : art.relocations) {
                if (!definedGlobals.count(reloc.symbolName)) {
                    unresolvedGlobals.insert(reloc.symbolName);
                }
            }
            for (const auto& sym : art.symbols) {
                if (!sym.isDefined && !definedGlobals.count(sym.name)) {
                    unresolvedGlobals.insert(sym.name);
                }
            }
        }

        if (unresolvedGlobals.empty()) {
            break;
        }

        for (auto& archiveMembers : archives) {
            for (auto& member : archiveMembers) {
                if (member.extracted) continue;

                bool providesNeededSymbol = false;
                for (const auto& sym : member.artifact.symbols) {
                    if (sym.isDefined && sym.binding != target::artifact::object::SymbolBinding::Local && unresolvedGlobals.count(sym.name)) {
                        providesNeededSymbol = true;
                        break;
                    }
                }

                if (providesNeededSymbol) {
                    member.extracted = true;
                    inOutArtifacts.push_back(member.artifact);
                    progress = true;
                    extractedAny = true;
                }
            }
        }
    }

    return extractedAny;
}

bool InternalLinker::link(const std::vector<target::artifact::object::ObjectArtifact>& artifacts,
                           LinkedImage& outImage,
                           LinkOutputKind outputKind,
                           const std::vector<DynamicImport>& dynamicImports) {
    lastError_.clear();
    outImage = LinkedImage{};
    outImage.outputKind = outputKind;

    if (artifacts.empty()) {
        lastError_ = "No input object artifacts provided to linker";
        return false;
    }

    outImage.arch = artifacts[0].arch;
    outImage.os = artifacts[0].os;

    // Check target compatibility across all artifacts
    for (size_t i = 1; i < artifacts.size(); ++i) {
        if (artifacts[i].arch != outImage.arch || artifacts[i].os != outImage.os) {
            lastError_ = "Incompatible object artifact target architectures/OS in link input";
            return false;
        }
    }

    // Base addresses
    uint64_t baseVma = (outImage.os == target::OS::Windows) ? 0x140000000ULL : 0x400000ULL;
    uint64_t pageAlign = 0x1000;

    // Merged sections tracking
    struct SectionPlacement {
        size_t artifactIndex;
        std::string sectionName;
        uint64_t outputOffset;
        uint64_t size;
    };
    std::vector<SectionPlacement> placements;

    std::vector<std::string> ordered_sections = {
        ".text", ".rodata", ".data", ".ctors", ".bss"
    };

    uint64_t currentVma = baseVma + pageAlign;

    // MinGW/COFF objects are emitted with one section per function/data item
    // (.text$..., .rdata$..., .data$..., .bss$...) plus special fragments such
    // as .text.startup/.text.unlikely, COMDAT refptrs/vtables and the
    // .ctors/.init_array static-constructor tables. A self-contained linker
    // must fold every fragment into its canonical output section; merging by
    // exact name alone silently drops inline/template code and ctor tables,
    // producing an image that maps but faults at the first such reference.
    auto canonicalSection = [](const std::string& n) -> std::string {
        auto pfx = [&](const char* p) { return n.compare(0, std::char_traits<char>::length(p), p) == 0; };
        if (n == ".text" || pfx(".text")) return ".text";
        if (n == ".rodata" || n == ".rdata" || pfx(".rodata") || pfx(".rdata")) return ".rodata";
        if (n == ".data" || pfx(".data")) return ".data";
        if (n == ".bss" || pfx(".bss")) return ".bss";
        if (n == ".ctors" || pfx(".ctors") || pfx(".init_array") || pfx(".CRT$")) return ".ctors";
        // Debug/EH-frame fragments are intentionally not folded: they are
        // unreferenced at runtime on x64 Windows (SEH lives in .pdata/.xdata)
        // and their COFF relocations use section/relative types the evaluator
        // does not implement. Leaving them out keeps them skipped rather than
        // aborting the link.
        return std::string();
    };

    for (const auto& secName : ordered_sections) {
        LinkedSection lsec;
        lsec.name = secName;
        lsec.alignment = 16;
        lsec.virtualAddress = currentVma;

        if (secName == ".text") { lsec.isExecutable = true; lsec.isWritable = false; }
        else if (secName == ".rodata") { lsec.isExecutable = false; lsec.isWritable = false; }
        else if (secName == ".data" || secName == ".bss" || secName == ".ctors") { lsec.isExecutable = false; lsec.isWritable = true; }
        else { lsec.isExecutable = false; lsec.isWritable = false; lsec.isReadable = false; }

        uint64_t currentOffset = 0;

        for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
            for (const auto& frag : artifacts[aIdx].sections) {
                if (canonicalSection(frag.first) != secName) continue;
                const auto* sec = &frag.second;
                if (sec->data.empty() && sec->virtualSize == 0) continue;

                uint64_t align = sec->alignment ? sec->alignment : 16;
                currentOffset = alignUp(currentOffset, align);

                SectionPlacement p;
                p.artifactIndex = aIdx;
                p.sectionName = frag.first;  // original fragment name; relocs/symbols key on this
                p.outputOffset = currentOffset;
                p.size = (secName == ".bss") ? sec->virtualSize : sec->data.size();
                placements.push_back(p);

                if (secName != ".bss") {
                    if (currentOffset > lsec.data.size()) {
                        lsec.data.resize(currentOffset, 0);
                    }
                    lsec.data.insert(lsec.data.end(), sec->data.begin(), sec->data.end());
                }

                currentOffset += p.size;
            }
        }

        if (currentOffset > 0) {
            lsec.virtualSize = currentOffset;
            outImage.sections[secName] = lsec;
            if (secName == ".text" || secName == ".rodata" || secName == ".data" || secName == ".ctors" || secName == ".bss") {
                currentVma = alignUp(currentVma + currentOffset, pageAlign);
            }
        }
    }

    // Per-artifact index of LOCAL (file-scope static) symbol addresses. Names
    // of local symbols are only unique within their own object, so relocation
    // binding must consult the referencing artifact's own copy rather than the
    // global by-name map (which collapses same-named statics across TUs).
    std::vector<std::unordered_map<std::string, uint64_t>> localSymbolAddr(artifacts.size());

    // Symbol resolution and rebasing (both global and local symbols)
    for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
        for (const auto& sym : artifacts[aIdx].symbols) {
            if (!sym.isDefined) continue;

            uint64_t outputOffset = 0;
            for (const auto& p : placements) {
                if (p.artifactIndex == aIdx && p.sectionName == sym.sectionName) {
                    outputOffset = p.outputOffset;
                    break;
                }
            }

            const auto* lsec = outImage.findSection(canonicalSection(sym.sectionName));
            uint64_t secVma = lsec ? lsec->virtualAddress : baseVma;

            LinkedSymbol lsym;
            lsym.name = sym.name;
            lsym.virtualAddress = secVma + outputOffset + sym.value;
            lsym.size = sym.size;
            lsym.isFunction = (sym.type == target::artifact::object::SymbolType::Function);
            const bool isLocal = (sym.binding == target::artifact::object::SymbolBinding::Local);
            const bool isWeak = (sym.binding == target::artifact::object::SymbolBinding::Weak);
            lsym.isGlobal = !isLocal;
            lsym.isWeak = isWeak;
            lsym.sectionName = sym.sectionName;

            if (isLocal) {
                // First definition within a single artifact wins; names are
                // unique per translation unit, so this is deterministic.
                localSymbolAddr[aIdx].emplace(sym.name, lsym.virtualAddress);
            }

            // Resolve symbols with ELF/COFF binding rules instead of aborting on
            // the first repeated name. The runtime is linked from several
            // translation units, and toolchains such as MinGW emit a private
            // copy of the same library helper (for example the libstdc++
            // thread-dependence stub) as a strong symbol in every object. A
            // self-contained linker must collapse those to one definition and
            // still produce an image; later relocation passes bind by name.
            auto existing = outImage.symbols.find(sym.name);
            if (existing == outImage.symbols.end()) {
                outImage.symbols[sym.name] = lsym;
            } else if (isLocal) {
                // Per-object locals never overwrite another object's binding.
            } else if (!existing->second.isGlobal) {
                // A prior local placeholder is superseded by a global symbol.
                outImage.symbols[sym.name] = lsym;
            } else if (existing->second.isWeak && !isWeak) {
                // A strong definition wins over a weak one.
                outImage.symbols[sym.name] = lsym;
            }
            // Weak-after-strong, weak-after-weak, and duplicate-strong all keep
            // the first-resolved symbol and remain non-fatal.
        }
    }

    // Materialize MinGW C-runtime internal *data* flags that are normally
    // provided by the CRT startup objects (crt2.o / dllcrt2.o). A self-contained
    // image synthesizes its own _start and never links those startup objects,
    // yet the folded runtime archive still references these flags. For an EXE
    // (never a DLL) they are constant and zero, so back them with
    // zero-initialized storage. This mirrors the existing base provisioning
    // model and deliberately leaves the duplicate-abort and DLL-classifier
    // behavior intact for every other symbol.
    {
        struct CrtDataFlag { const char* name; uint64_t size; };
        static const std::vector<CrtDataFlag> crtDataFlags = {
            {"__mingw_module_is_dll", 8},
        };

        std::vector<const CrtDataFlag*> needed;
        for (const auto& flag : crtDataFlags) {
            if (outImage.symbols.count(flag.name)) continue;
            bool referenced = false;
            for (const auto& art : artifacts) {
                for (const auto& reloc : art.relocations) {
                    if (reloc.symbolName == flag.name) { referenced = true; break; }
                }
                if (referenced) break;
            }
            if (referenced) needed.push_back(&flag);
        }

        if (!needed.empty()) {
            auto* bss = outImage.findSection(".bss");
            if (!bss) {
                LinkedSection nbss;
                nbss.name = ".bss";
                nbss.alignment = 16;
                nbss.virtualAddress = currentVma;
                nbss.isExecutable = false;
                nbss.isWritable = true;
                outImage.sections[".bss"] = nbss;
                bss = outImage.findSection(".bss");
            }
            for (const auto* flag : needed) {
                uint64_t off = alignUp(bss->virtualSize, 8);
                LinkedSymbol fsym;
                fsym.name = flag->name;
                fsym.virtualAddress = bss->virtualAddress + off;
                fsym.size = flag->size;
                fsym.isFunction = false;
                fsym.isGlobal = true;
                fsym.isWeak = false;
                fsym.sectionName = ".bss";
                outImage.symbols[flag->name] = fsym;
                bss->virtualSize = off + flag->size;
            }
            // .bss is the last folded section, but advance currentVma so any
            // later placement derived from it stays clear of the new storage.
            currentVma = alignUp(bss->virtualAddress + bss->virtualSize, pageAlign);
        }
    }

    // Add architecture-specific _start entry stub if main is present and _start is missing (Executable output kind)
    if (outImage.outputKind == LinkOutputKind::Executable && 
        (outImage.os == target::OS::Linux || outImage.os == target::OS::BareMetal)) {
        if (!outImage.symbols.count("_start") && (outImage.symbols.count("main") || outImage.symbols.count("$main"))) {
            std::string mainName = outImage.symbols.count("main") ? "main" : "$main";
            uint64_t mainAddr = outImage.symbols[mainName].virtualAddress;

            auto* textSec = outImage.findSection(".text");
            if (textSec) {
                uint64_t startOffset = textSec->data.size();
                uint64_t startVma = textSec->virtualAddress + startOffset;

                std::vector<uint8_t> startBytes;
                
                if (outImage.arch == target::Arch::X64) {
                    // x86-64 _start stub (System V ABI stack aligned)
                    // 31 ed                xor %ebp, %ebp
                    // 48 8b 3c 24          mov (%rsp), %rdi       (argc)
                    // 48 8d 74 24 08       lea 8(%rsp), %rsi      (argv)
                    // 48 83 e4 f0          and $-16, %rsp         (align stack to 16 bytes)
                    // e8 [rel32]           call main
                    // 48 89 c7             mov %rax, %rdi
                    // b8 3c 00 00 00       mov $60, %eax
                    // 0f 05                syscall
                    startBytes = {
                        0x31, 0xED,
                        0x48, 0x8B, 0x3C, 0x24,
                        0x48, 0x8D, 0x74, 0x24, 0x08,
                        0x48, 0x83, 0xE4, 0xF0,
                        0xE8, 0x00, 0x00, 0x00, 0x00,
                        0x48, 0x89, 0xC7,
                        0xB8, 0x3C, 0x00, 0x00, 0x00,
                        0x0F, 0x05
                    };
                    
                    int64_t callRel = static_cast<int64_t>(mainAddr) - static_cast<int64_t>(startVma + 20);
                    int32_t callRel32 = static_cast<int32_t>(callRel);
                    std::memcpy(startBytes.data() + 16, &callRel32, 4);
                    
                } else if (outImage.arch == target::Arch::AArch64) {
                    // AArch64 _start stub (AArch64 Linux ABI)
                    // mov x0, sp           (argc at [sp])
                    // add x1, sp, #8       (argv at [sp+8])
                    // and sp, sp, #-16     (align stack to 16 bytes)
                    // bl main
                    // mov x8, #93          (sys_exit)
                    // mov x0, x0           (return value)
                    // svc #0
                    startBytes = {
                        0x9F, 0x00, 0x00, 0x10,  // mov x0, sp
                        0x91, 0x04, 0x01, 0x38,  // add x1, sp, #8
                        0x9F, 0x00, 0x00, 0x13,  // and sp, sp, #-16
                        0x00, 0x00, 0x00, 0x94,  // bl main (patched below)
                        0xD0, 0x01, 0x00, 0xD2,  // mov x8, #93 (sys_exit)
                        0xAA, 0x00, 0x00, 0x1E,  // mov x0, x0
                        0x00, 0x00, 0x00, 0xD4   // svc #0
                    };
                    
                    // Patch BL instruction for AArch64 (signed 26-bit immediate)
                    int64_t offset = static_cast<int64_t>(mainAddr) - static_cast<int64_t>(startVma + 12);
                    int64_t wordOffset = offset / 4;
                    uint32_t imm26 = static_cast<uint32_t>(wordOffset) & 0x03FFFFFF;
                    uint32_t blInstr = 0x94000000 | imm26;
                    std::memcpy(startBytes.data() + 12, &blInstr, 4);
                    
                } else if (outImage.arch == target::Arch::RISCV64) {
                    // RISC-V _start stub (RISC-V Linux ABI)
                    // mv a0, sp            (argc at [sp])
                    // addi a1, sp, 8       (argv at [sp+8])
                    // andi sp, sp, -16     (align stack to 16 bytes)
                    // jal ra, main
                    // li a7, 93            (sys_exit)
                    // mv a0, a0            (return value)
                    // ecall
                    startBytes = {
                        0x85, 0x13, 0x02, 0x10,  // mv a0, sp
                        0x85, 0x93, 0x41, 0x10,  // addi a1, sp, 8
                        0x1F, 0x13, 0xF0, 0xFE,  // andi sp, sp, -16
                        0x6F, 0x00, 0x00, 0x00,  // jal ra, main (patched below)
                        0x93, 0x08, 0xD0, 0x05,  // li a7, 93 (sys_exit)
                        0x85, 0x13, 0x05, 0x00,  // mv a0, a0
                        0x73, 0x00, 0x00, 0x00   // ecall
                    };
                    
                    // Patch JAL instruction for RISC-V (signed 20-bit immediate in J-type format)
                    int64_t offset = static_cast<int64_t>(mainAddr) - static_cast<int64_t>(startVma + 12);
                    uint32_t imm = static_cast<uint32_t>(offset);
                    uint32_t j_imm = ((imm & 0x100000) << 11) | ((imm & 0x7FE) << 20) |
                                    ((imm & 0x800) << 9) | (imm & 0xFF000);
                    uint32_t jalInstr = 0x0000006F | j_imm;
                    std::memcpy(startBytes.data() + 12, &jalInstr, 4);

                } else if (outImage.arch == target::Arch::LoongArch64) {
                    // LoongArch64 _start stub (LoongArch64 Linux ABI)
                    // move $r4, $r3         (argc at [$r3])
                    // addi.d $r5, $r3, 8   (argv at [$r3+8])
                    // andi $r3, $r3, -16   (align stack to 16 bytes)
                    // bl main
                    // li.w $r11, 93        (sys_exit)
                    // syscall 0
                    startBytes = {
                        0x00, 0x00, 0x00, 0x01,  // move $r4, $r3
                        0x0C, 0x40, 0x02, 0x02,  // addi.d $r5, $r3, 8
                        0x00, 0x00, 0xF0, 0x03,  // andi $r3, $r3, -16
                        0x00, 0x00, 0x00, 0x48,  // bl main (patched below)
                        0x5C, 0x00, 0x00, 0x14,  // li.w $r11, 93 (sys_exit)
                        0x00, 0x00, 0x00, 0x00   // syscall 0
                    };

                    // Patch BL instruction for LoongArch64 (signed 26-bit immediate)
                    int64_t offset = static_cast<int64_t>(mainAddr) - static_cast<int64_t>(startVma + 12);
                    uint32_t imm = static_cast<uint32_t>(offset >> 2);
                    uint32_t blInstr = 0x48000000 | (imm & 0x03FFFFFF);
                    std::memcpy(startBytes.data() + 12, &blInstr, 4);
                }

                if (!startBytes.empty()) {
                    textSec->data.insert(textSec->data.end(), startBytes.begin(), startBytes.end());
                    textSec->virtualSize = textSec->data.size();

                    LinkedSymbol startSym;
                    startSym.name = "_start";
                    startSym.virtualAddress = startVma;
                    startSym.size = startBytes.size();
                    startSym.isFunction = true;
                    startSym.isGlobal = true;
                    startSym.sectionName = ".text";

                    outImage.symbols["_start"] = startSym;
                }
            }
        }
    }

    // Windows x64 entry stub: run the folded .ctors (static-constructor) array,
    // call main(0,0), then ExitProcess(main's return code). Without this the
    // image entry is main() directly: the C++ static constructors that populate
    // the runtime/FFI dispatch tables never run, so the very first dispatch
    // calls a null table slot and faults with STATUS_ACCESS_VIOLATION at 0x0.
    if (outImage.outputKind == LinkOutputKind::Executable &&
        outImage.os == target::OS::Windows &&
        outImage.arch == target::Arch::X64 &&
        !outImage.symbols.count("_start") &&
        (outImage.symbols.count("main") || outImage.symbols.count("$main"))) {
        std::string winMainName = outImage.symbols.count("main") ? "main" : "$main";
        uint64_t winMainAddr = outImage.symbols[winMainName].virtualAddress;
        auto* textSec = outImage.findSection(".text");
        if (textSec) {
            // Ensure a kernel32!ExitProcess import thunk exists so the stub can
            // terminate cleanly. Keyed by the bare export name; DynamicLinkPlan
            // folds it into the PE import table and PeImage patches the jmp slot.
            const std::string epThunkName = "__imp_thunk_ExitProcess";
            uint64_t epThunkVma = 0;
            if (outImage.symbols.count(epThunkName)) {
                epThunkVma = outImage.symbols[epThunkName].virtualAddress;
            } else {
                uint64_t epOff = textSec->data.size();
                epThunkVma = textSec->virtualAddress + epOff;
                std::vector<uint8_t> epBytes = TargetRelocationEvaluator::getImportThunkBytes(outImage.arch, outImage.os);
                textSec->data.insert(textSec->data.end(), epBytes.begin(), epBytes.end());
                textSec->virtualSize = textSec->data.size();
                LinkedSymbol epSym;
                epSym.name = epThunkName;
                epSym.virtualAddress = epThunkVma;
                epSym.size = epBytes.size();
                epSym.isFunction = true;
                epSym.isGlobal = false;
                epSym.sectionName = ".text";
                outImage.symbols[epThunkName] = epSym;
                outImage.importLibraryNames["ExitProcess"] = "kernel32.dll";
                outImage.importThunkVmas["ExitProcess"] = epThunkVma;
            }

            uint64_t ctorsStart = 0, ctorsEnd = 0;
            if (const auto* cs = outImage.findSection(".ctors")) {
                ctorsStart = cs->virtualAddress;
                ctorsEnd = cs->virtualAddress + cs->virtualSize;
            }

            uint64_t startOffset = textSec->data.size();
            uint64_t startVma = textSec->virtualAddress + startOffset;

            std::vector<uint8_t> sb;
            auto push = [&](std::initializer_list<int> bs) { for (int b : bs) sb.push_back(static_cast<uint8_t>(b)); };
            auto patch8 = [&](size_t at, size_t targetOff) { int32_t rel = static_cast<int32_t>(targetOff - (at + 1)); sb[at] = static_cast<uint8_t>(static_cast<int8_t>(rel)); };

            push({0x48, 0x83, 0xE4, 0xF0});                       // and rsp,-16
            push({0x48, 0xBE}); size_t relocCtorsStart = sb.size(); for (int i = 0; i < 8; ++i) push({0}); // movabs rsi, ctorsStart
            push({0x48, 0xBF}); size_t relocCtorsEnd = sb.size();   for (int i = 0; i < 8; ++i) push({0}); // movabs rdi, ctorsEnd
            size_t labelA = sb.size();                              // .loop
            push({0x48, 0x39, 0xFE});                              // cmp rsi,rdi
            push({0x73, 0x00}); size_t jaeAt = sb.size() - 1;      // jae .done
            push({0x48, 0x8B, 0x06});                              // mov rax,[rsi]
            push({0x48, 0x85, 0xC0});                              // test rax,rax
            push({0x74, 0x00}); size_t jzAt = sb.size() - 1;       // jz .skip
            push({0x48, 0x83, 0xEC, 0x20});                        // sub rsp,32
            push({0xFF, 0xD0});                                    // call rax
            push({0x48, 0x83, 0xC4, 0x20});                        // add rsp,32
            size_t labelB = sb.size();                             // .skip
            push({0x48, 0x83, 0xC6, 0x08});                        // add rsi,8
            push({0xEB, 0x00}); size_t jmpAt = sb.size() - 1;      // jmp .loop
            size_t labelC = sb.size();                             // .done
            patch8(jaeAt, labelC);
            patch8(jzAt, labelB);
            patch8(jmpAt, labelA);
            push({0x31, 0xC9});                                    // xor ecx,ecx (argc=0)
            push({0x31, 0xD2});                                    // xor edx,edx (argv=0)
            push({0x48, 0x83, 0xEC, 0x20});                        // sub rsp,32
            push({0xE8, 0, 0, 0, 0}); size_t callMainRel = sb.size() - 4; // call main
            push({0x48, 0x83, 0xC4, 0x20});                        // add rsp,32
            push({0x89, 0xC3});                                    // mov ebx,eax
            push({0x8B, 0xCB});                                    // mov ecx,ebx
            push({0xE8, 0, 0, 0, 0}); size_t callExitRel = sb.size() - 4; // call ExitProcess thunk
            push({0xC3});                                          // ret (unreachable)

            int64_t mainRel = static_cast<int64_t>(winMainAddr) - static_cast<int64_t>(startVma + callMainRel + 4);
            int64_t exitRel = static_cast<int64_t>(epThunkVma) - static_cast<int64_t>(startVma + callExitRel + 4);
            int32_t mainRel32 = static_cast<int32_t>(mainRel);
            int32_t exitRel32 = static_cast<int32_t>(exitRel);
            std::memcpy(sb.data() + callMainRel, &mainRel32, 4);
            std::memcpy(sb.data() + callExitRel, &exitRel32, 4);
            std::memcpy(sb.data() + relocCtorsStart, &ctorsStart, 8);
            std::memcpy(sb.data() + relocCtorsEnd, &ctorsEnd, 8);

            // Register the two absolute ctor bounds for PE base relocation so the
            // stub stays correct under ASLR. Only when a real ctor table exists.
            if (ctorsStart != 0 && ctorsEnd != 0) {
                outImage.relocationFixupVmas.push_back(startVma + relocCtorsStart);
                outImage.relocationFixupVmas.push_back(startVma + relocCtorsEnd);
            }

            textSec->data.insert(textSec->data.end(), sb.begin(), sb.end());
            textSec->virtualSize = textSec->data.size();

            LinkedSymbol startSym;
            startSym.name = "_start";
            startSym.virtualAddress = startVma;
            startSym.size = sb.size();
            startSym.isFunction = true;
            startSym.isGlobal = true;
            startSym.sectionName = ".text";
            outImage.symbols["_start"] = startSym;
        }
    }

    // Set entry point
    if (outImage.symbols.count("_start")) {
        outImage.entryAddress = outImage.symbols["_start"].virtualAddress;
        outImage.entrySymbolName = "_start";
    } else if (outImage.symbols.count("main")) {
        outImage.entryAddress = outImage.symbols["main"].virtualAddress;
        outImage.entrySymbolName = "main";
    }

    // Evaluate relocations
    for (size_t aIdx = 0; aIdx < artifacts.size(); ++aIdx) {
        for (const auto& reloc : artifacts[aIdx].relocations) {
            uint64_t outputOffset = 0;
            for (const auto& p : placements) {
                if (p.artifactIndex == aIdx && p.sectionName == reloc.sectionName) {
                    outputOffset = p.outputOffset;
                    break;
                }
            }

            auto* lsec = outImage.findSection(canonicalSection(reloc.sectionName));
            if (!lsec) {
                continue;
            }

            uint64_t targetSymAddr = 0;
            const DynamicImport* matchedImp = nullptr;
            DynamicImport discovered;
            for (const auto& imp : dynamicImports) {
                if (imp.symbol == reloc.symbolName) {
                    matchedImp = &imp;
                    break;
                }
            }

            // On Windows an `__imp_X` reference for a system DLL export must bind
            // through the PE import table (our IAT), even when a MinGW import-stub
            // object locally "defines" `__imp_X`. That stub pointer targets an
            // import section we never emit or relocate, so resolving it locally
            // yields the image base and every import call jumps into the PE header
            // (ACCESS_VIOLATION). Prefer the authoritative system-import DB for
            // `__imp_` references and let the Data-fixup path bind them to a real
            // IAT slot.
            bool forceWindowsImp = false;
            if (matchedImp == nullptr && outImage.os == target::OS::Windows &&
                reloc.symbolName.rfind("__imp_", 0) == 0) {
                WindowsImportInfo stubInfo;
                if (classifyWindowsImport(reloc.symbolName, stubInfo)) {
                    discovered.symbol = stubInfo.name;
                    discovered.dependencyLibrary = stubInfo.dll;
                    discovered.kind = stubInfo.data ? DynamicImportKind::Data : DynamicImportKind::Function;
                    matchedImp = &discovered;
                    forceWindowsImp = true;
                }
            }

            // Bind LOCAL (file-scope static) references against the referencing
            // artifact's own placement. Same-named statics in different objects
            // are distinct variables; the global by-name map collapses them and
            // aliases independent state (observed as a garbage std::map node
            // pointer dereferenced in _Rb_tree_increment).
            const auto localIt = localSymbolAddr[aIdx].find(reloc.symbolName);
            if (!forceWindowsImp && localIt != localSymbolAddr[aIdx].end()) {
                targetSymAddr = localIt->second;
            } else if (!forceWindowsImp && outImage.symbols.count(reloc.symbolName)) {
                targetSymAddr = outImage.symbols[reloc.symbolName].virtualAddress;
            } else if (const auto* sec = outImage.findSection(canonicalSection(reloc.symbolName))) {
                // Relocation targets a section symbol directly (e.g. a .ctors
                // entry referencing .text.startup). Base the address on this
                // artifact's fragment placement inside the folded output section,
                // not the section head; the addend supplies the in-fragment offset.
                uint64_t fragOff = 0;
                for (const auto& p : placements) {
                    if (p.artifactIndex == aIdx && p.sectionName == reloc.symbolName) { fragOff = p.outputOffset; break; }
                }
                targetSymAddr = sec->virtualAddress + fragOff;
            } else {
                // Not defined locally and not pre-declared as an import. On
                // Windows, classify the reference against the system import
                // database so a self-contained image binds it through the PE
                // import table instead of aborting.
                if (matchedImp == nullptr && outImage.os == target::OS::Windows) {
                    WindowsImportInfo info;
                    if (classifyWindowsImport(reloc.symbolName, info)) {
                        discovered.symbol = info.name;
                        discovered.dependencyLibrary = info.dll;
                        discovered.kind = info.data ? DynamicImportKind::Data : DynamicImportKind::Function;
                        matchedImp = &discovered;
                    }
                }
            }

            if (matchedImp != nullptr) {
                // Bind by the canonical export name so the import table, the
                // synthesized thunk, and any data fixup all key consistently
                // (a MinGW __imp_X reference imports the bare export X).
                const std::string importKey = matchedImp->symbol;
                outImage.importLibraryNames[importKey] = matchedImp->dependencyLibrary;
                if (matchedImp->kind == DynamicImportKind::Data) {
                    // Record data import fixup to be resolved against the IAT slot VMA in PeImageWriter.
                    // sectionDataOffset is the offset inside the *folded* output section, so the
                    // recorded section name must be the folded name (lsec->name, e.g. ".text"),
                    // not the original COFF fragment name (e.g. ".text$startup") - PeImageWriter
                    // looks the fixup up by PE section name and would otherwise silently skip it,
                    // leaving object-provided `jmp qword [__imp_X]` import thunks unpatched.
                    uint64_t placeAddress = lsec->virtualAddress + outputOffset + reloc.offset;
                    uint64_t sectionDataOffset = outputOffset + reloc.offset;
                    outImage.dataImportFixups.push_back({
                        lsec->name,
                        sectionDataOffset,
                        placeAddress,
                        importKey,
                        reloc.addend,
                        reloc.type
                    });
                    // An absolute (8-byte) pointer to an IAT slot must be rebased when ASLR
                    // moves the image, exactly like any other Absolute relocation site.
                    if (reloc.type == "R_X86_64_64") {
                        outImage.relocationFixupVmas.push_back(placeAddress);
                    }
                    continue;
                } else {
                    // Synthesize or retrieve import thunk in .text for function imports
                    std::string thunkSymName = "__imp_thunk_" + importKey;
                    if (!outImage.symbols.count(thunkSymName)) {
                        auto* textSec = outImage.findSection(".text");
                        if (!textSec) {
                            lastError_ = "Cannot synthesize import thunk for '" + importKey + "': .text section missing";
                            return false;
                        }
                        uint64_t thunkOffset = textSec->data.size();
                        uint64_t thunkVma = textSec->virtualAddress + thunkOffset;

                        std::vector<uint8_t> thunkBytes = TargetRelocationEvaluator::getImportThunkBytes(outImage.arch, outImage.os);
                        if (thunkBytes.empty()) {
                            lastError_ = "Target import thunk generation not supported for architecture/OS";
                            return false;
                        }
                        textSec->data.insert(textSec->data.end(), thunkBytes.begin(), thunkBytes.end());
                        textSec->virtualSize = textSec->data.size();

                        LinkedSymbol thunkSym;
                        thunkSym.name = thunkSymName;
                        thunkSym.virtualAddress = thunkVma;
                        thunkSym.size = thunkBytes.size();
                        thunkSym.isFunction = true;
                        thunkSym.isGlobal = false;
                        thunkSym.sectionName = ".text";
                        outImage.symbols[thunkSymName] = thunkSym;
                        outImage.importThunkVmas[importKey] = thunkVma;
                    }
                    targetSymAddr = outImage.symbols[thunkSymName].virtualAddress;
                }
            } else if (!outImage.symbols.count(reloc.symbolName) && !outImage.findSection(canonicalSection(reloc.symbolName))) {
                std::string msg = "Unresolved undefined symbol reference: '" + reloc.symbolName + "'";
                if (isStaticRuntimeSymbol(reloc.symbolName))
                    msg += " (C++/GCC runtime symbol; a static archive such as libstdc++/libgcc was not contributed)";
                lastError_ = msg;
                return false;
            }

            uint64_t placeAddress = lsec->virtualAddress + outputOffset + reloc.offset;
            uint64_t sectionDataOffset = outputOffset + reloc.offset;

            RelocationKind kind = TargetRelocationEvaluator::normalizeType(reloc.type);
            if (kind == RelocationKind::Unknown) {
                lastError_ = "Relocation evaluation failed for symbol '" + reloc.symbolName + "' in section '" + reloc.sectionName + "': unknown/unsupported relocation type '" + reloc.type + "'";
                return false;
            }
            std::string evalError;
            if (!TargetRelocationEvaluator::evaluate(kind, targetSymAddr, placeAddress, reloc.addend, lsec->data, sectionDataOffset, evalError, outImage.arch)) {
                lastError_ = "Relocation evaluation failed for '" + reloc.symbolName + "': " + evalError;
                return false;
            }

            if (kind == RelocationKind::Absolute) {
                outImage.relocationFixupVmas.push_back(placeAddress);
            }
        }
    }

    return true;
}

} // namespace linker
} // namespace artifact
} // namespace target
