#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <Windows.h>
#include <filesystem>

// Global constants
const std::string LOG_VERSION = "v1.0";

struct ProcessMap {
    uint64_t baseAddress = 0;
    uint64_t endAddress = 0;
    DWORD sz = 0;
    DWORD pid = 0;
};

// Logging and File Utils
std::ofstream& OpenLogFile();
void LogMessage(const std::string& message);
std::string GetTimestamp();
std::wstring GetExecutableDirW();
std::string GetExecutableDirA();

// String Utils
LPSTR ConvertWStringToLPSTR(const std::wstring& wstr);
LPCSTR ConvertWStringToLPCSTR(const std::wstring& wstr);
