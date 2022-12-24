#include <windows.h>
#include <WinHvPlatform.h>
#include <WinHvEmulation.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>

#define BUFSIZE 4096

HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;

HANDLE g_hOutputFile = NULL;

void ErrorExit(PTSTR lpszFunction)
{
    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
                                      (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
                    LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                    TEXT("%s failed with error %d: %s"),
                    lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(1);
}

void ReadFromPipe(void)
{
    DWORD dwRead, dwWritten;
    CHAR chBuf[BUFSIZE];
    BOOL bSuccess = FALSE;

    for (;;)
    {
        bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL);
        if (!bSuccess || dwRead == 0)
            break;

        bSuccess = WriteFile(g_hOutputFile, chBuf,
                             dwRead, &dwWritten, NULL);
        if (!bSuccess)
            break;
    }
}

int ProbeWHPX() {
    HRESULT hr;
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    int acc_available = 1;
    HMODULE hWinHvPlatform;

    typedef HRESULT (WINAPI *WHvGetCapability_t) (
        WHV_CAPABILITY_CODE, VOID*, UINT32, UINT32*);

    printf("Checking whether Windows Hypervisor Platform (WHPX) is available.");

    hWinHvPlatform = LoadLibraryW(L"WinHvPlatform.dll");
    if (hWinHvPlatform) {
        printf("WinHvPlatform.dll found. Looking for WHvGetCapability...");
        WHvGetCapability_t f_WHvGetCapability = (
            WHvGetCapability_t)GetProcAddress(hWinHvPlatform, "WHvGetCapability");
        if (f_WHvGetCapability) {
            printf("WHvGetCapability found. Querying WHPX capabilities...");
            hr = f_WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &whpx_cap,
                                    sizeof(whpx_cap), &whpx_cap_size);
            if (FAILED(hr) || !whpx_cap.HypervisorPresent) {
                printf("WHvGetCapability failed. hr=0x%08lx whpx_cap.HypervisorPresent? %d\n",
                         hr, whpx_cap.HypervisorPresent);
                acc_available = 0;
            }
        } else {
            printf("Could not load library function 'WHvGetCapability'.");
            acc_available = 0;
        }
    } else {
        printf("Could not load library WinHvPlatform.dll");
        acc_available = 0;
    }

    if (hWinHvPlatform)
        FreeLibrary(hWinHvPlatform);

    if (!acc_available) {
        printf("WHPX is either not available or not installed.");
    }

    return acc_available;
}

void CreateTrinityProcess()
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = g_hChildStd_OUT_Wr;
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(pi));
    LPSTR arg; 
    if (ProbeWHPX()) {
        arg = ".\\x86_64-softmmu\\qemu-system-x86_64.exe -accel whpx -cpu android64 -m 4096 -smp 4 -machine usb=on -device usb-kbd -device usb-tablet -boot menu=on -soundhw hda -net nic -net user,hostfwd=tcp::5555-:5555 -device direct-express-pci -display sdl -hda hda.img -cdrom android_x86_64.iso";
    } else {
        arg = ".\\x86_64-softmmu\\qemu-system-x86_64.exe -accel hax -cpu android64 -m 4096 -smp 4 -machine usb=on -device usb-kbd -device usb-tablet -boot menu=on -soundhw hda -net nic -net user,hostfwd=tcp::5555-:5555 -device direct-express-pci -display sdl -hda hda.img -cdrom android_x86_64.iso";
    }

    if (CreateProcess(NULL, arg, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        ErrorExit(TEXT("Cannot open Trinity"));
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{

    CreateTrinityProcess();

    return 0;
}
