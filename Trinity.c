#include <windows.h>
#include <WinHvPlatform.h>
#include <WinHvEmulation.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>
#include <stdlib.h>

#define BUFSIZE 4096

HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;

HANDLE g_hOutputFile = NULL;

typedef struct {
    // display
    int screenWidth;
    int screenHeight;
    int windowWidth;
    int windowHeight;
    // CPU
    int cpuCore;
    // memory
    int memorySize;
    // accel
    int accel;
} Config;

Config g_config;

void ErrorExit(PTSTR lpszFunction) {
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
                                      (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(
                                          TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
                    LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                    TEXT("%s failed with error %d: %s"),
                    lpszFunction, dw, lpMsgBuf);
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(1);
}

void ReadFromPipe(void) {
    DWORD dwRead, dwWritten;
    CHAR chBuf[BUFSIZE];
    BOOL bSuccess = FALSE;

    for (;;) {
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

    typedef HRESULT (WINAPI *WHvGetCapability_t)(
        WHV_CAPABILITY_CODE, VOID*, UINT32, UINT32*);

    printf("Checking whether Windows Hypervisor Platform (WHPX) is available\n");

    hWinHvPlatform = LoadLibraryW(L"WinHvPlatform.dll");
    if (hWinHvPlatform) {
        printf("WinHvPlatform.dll found. Looking for WHvGetCapability...\n");
        WHvGetCapability_t f_WHvGetCapability = (
            WHvGetCapability_t)GetProcAddress(hWinHvPlatform, "WHvGetCapability");
        if (f_WHvGetCapability) {
            printf("WHvGetCapability found. Querying WHPX capabilities...\n");
            hr = f_WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &whpx_cap,
                                    sizeof(whpx_cap), &whpx_cap_size);
            if (FAILED(hr) || !whpx_cap.HypervisorPresent) {
                printf("WHvGetCapability failed. hr=0x%08lx whpx_cap.HypervisorPresent? %d\n",
                       hr, whpx_cap.HypervisorPresent);
                acc_available = 0;
            }
        }
        else {
            printf("Could not load library function 'WHvGetCapability'\n");
            acc_available = 0;
        }
    }
    else {
        printf("Could not load library WinHvPlatform.dll\n");
        acc_available = 0;
    }

    if (hWinHvPlatform)
        FreeLibrary(hWinHvPlatform);

    if (!acc_available) {
        printf("WHPX is either not available or not installed\n");
    }

    return acc_available;
}

void CreateTrinityProcess() {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = g_hChildStd_OUT_Wr;
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.dwFlags |= STARTF_USESTDHANDLES;
    ZeroMemory(&pi, sizeof(pi));
    const LPSTR arg = malloc(1024);
    // make config into a string
    sprintf_s(arg, 1024,
              ".\\qemu-system-x86_64.exe -m %d -smp %d "
              "-audiodev dsound,id=hda  -device intel-hda -device hda-output,audiodev=hda "
              "-kernel .\\Android\\kernel -append \"no_timer_check console=ttyS0 RAMDISK=vdb DATA=vdc video=%dx%d quiet\" "
              "-initrd .\\Android\\initrd.img "
              "-drive index=0,if=virtio,id=system,file=./Android/system.sfs,format=raw "
              "-drive index=1,if=virtio,id=ramdisk,file=./Android/ramdisk.img,format=raw "
              "-drive index=2,if=virtio,id=userdata,file=./Android/userdata.qcow2,format=qcow2 "
              "-accel %s -cpu android64 "
              "-device teleport,touchscreen=on,scroll_is_zoom=on,right_click_is_two_finger=on,scroll_ratio=10,keyboard=on,finger_replay=off,"
              "window_width=%d,window_height=%d,keep_window_scale=on,independ_window=off,"
              "gl=on,sync=on,gl_debug=off,gl_log_level=1,gl_log_to_host=on,buffer_log=off,opengl_trace=off,"
              "display_width=%d,display_height=%d,phy_width=%d,phy_height=%d,display_switch=off,"
              "device_input_window=off,battery=on,mic=on,accel=on,gps=on,gyro=on,bridge=on -serial stdio",
              g_config.memorySize, g_config.cpuCore, g_config.screenWidth, g_config.screenHeight, g_config.accel == 1? "whpx" : "hax", g_config.windowWidth,
              g_config.windowHeight, g_config.screenWidth, g_config.screenHeight, g_config.screenWidth, g_config.screenHeight);

    printf("arg is %s\n", arg);

    if (CreateProcess(NULL, arg, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        free(arg);
        ErrorExit(TEXT("Cannot open Trinity"));
    }
    free(arg);
}

size_t getline(char** lineptr, size_t* n, FILE* stream) {
    char* bufptr = NULL;
    char* p = bufptr;
    size_t size;
    int c;

    if (lineptr == NULL) {
        return -1;
    }
    if (stream == NULL) {
        return -1;
    }
    if (n == NULL) {
        return -1;
    }
    bufptr = *lineptr;
    size = *n;

    c = fgetc(stream);
    if (c == EOF) {
        return -1;
    }
    if (bufptr == NULL) {
        bufptr = malloc(128);
        if (bufptr == NULL) {
            return -1;
        }
        size = 128;
    }
    p = bufptr;
    while (c != EOF) {
        if ((p - bufptr) > (size - 1)) {
            size = size + 128;
            bufptr = realloc(bufptr, size);
            if (bufptr == NULL) {
                return -1;
            }
        }
        *p++ = c;
        if (c == '\n') {
            break;
        }
        c = fgetc(stream);
    }

    *p++ = '\0';
    *lineptr = bufptr;
    *n = size;

    return p - bufptr - 1;
}

void ParseConfig() {
    // config file is organized as key=value
    FILE* fp;
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    fp = fopen("config.txt", "r");
    if (fp == NULL) {
        printf("Cannot open config.txt\n");
        ErrorExit(TEXT("Cannot open config file"));
    }

    // default config
    g_config.screenWidth = 1920;
    g_config.screenHeight = 1080;
    g_config.windowWidth = 1920;
    g_config.windowHeight = 1080;
    g_config.cpuCore = 4;
    g_config.memorySize = 4096;

    while ((read = getline(&line, &len, fp)) != -1) {
        char* key = strtok(line, "=");
        char* value = strtok(NULL, "=");
        if (strcmp(key, "screenWidth") == 0) {
            g_config.screenWidth = atoi(value);
        }
        else if (strcmp(key, "screenHeight") == 0) {
            g_config.screenHeight = atoi(value);
        }
        else if (strcmp(key, "windowWidth") == 0) {
            g_config.windowWidth = atoi(value);
        }
        else if (strcmp(key, "windowHeight") == 0) {
            g_config.windowHeight = atoi(value);
        }
        else if (strcmp(key, "cpuCore") == 0) {
            g_config.cpuCore = atoi(value);
        }
        else if (strcmp(key, "memorySize") == 0) {
            g_config.memorySize = atoi(value);
        }
    }

    if (ProbeWHPX())
        g_config.accel = 1;
    else
        g_config.accel = 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ParseConfig();
    CreateTrinityProcess();

    return 0;
}
