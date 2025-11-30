#include "Utils.h"
#include <chrono>
#include <sstream>
#include <iomanip>

std::ofstream g_logFile;

static std::wstring pPath() {
    TCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    return std::filesystem::path(path).parent_path().wstring();
}

std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    localtime_s(&buf, &in_time_t);
    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
    return ss.str();
}

std::ofstream& OpenLogFile() {
    if (!g_logFile.is_open()) {
        std::wstring dir = pPath();
        std::string filename = GetTimestamp() + "-" + LOG_VERSION + ".txt";
        std::filesystem::path logPath = std::filesystem::path(dir) / filename;
        g_logFile.open(logPath, std::ios::out | std::ios::app);
    }
    return g_logFile;
}

void LogMessage(const std::string& message) {
    std::ofstream& log = OpenLogFile();
    if (log.is_open()) {
        log << message << std::endl;
    } else {
        std::cerr << message << std::endl;
    }
}

std::wstring GetExecutableDirW() {
    return pPath();
}

std::string GetExecutableDirA() {
    return std::filesystem::path(GetExecutableDirW()).string();
}

LPSTR ConvertWStringToLPSTR(const std::wstring& wstr) {
    int size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    LPSTR strTo = new char[size_needed + 1];
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), strTo, size_needed, NULL, NULL);
    strTo[size_needed] = '\0';
    return strTo;
}

LPCSTR ConvertWStringToLPCSTR(const std::wstring& wstr) {
    int size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    char* buf = new char[size_needed + 1];
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.size(), buf, size_needed, NULL, NULL);
    buf[size_needed] = '\0';
    return buf;
}
