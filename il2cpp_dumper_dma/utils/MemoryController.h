#pragma once
#include "Utils.h"
#include "Lib/vmmdll.h"
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

class DMAController {
private:
    VMM_HANDLE vmmdll = 0;
    DWORD pid = 0;

public:
    DMAController();
    ~DMAController();
    bool Initialize();
    void Close();
    bool GetProcessInfo(LPSTR ProcessName, ProcessMap& Process);
    bool GetModuleInfo(LPSTR moduleName, ProcessMap& Process, uint64_t& OutputAddress, uint64_t& OutputSize);
    bool GetModulePath(LPSTR moduleName, ProcessMap& Process, std::wstring& outPath);
    bool GetModuleEAT(LPSTR moduleName, PVMMDLL_MAP_EAT& pEatMap);
    uint64_t FindPattern(uint64_t baseAddress, uint64_t size, const std::vector<BYTE>& pattern, const std::string& mask);
    bool ReadMemoryRange(uint64_t addr, void* buffer, DWORD size, DWORD& bytesRead);
    BYTE* ReadMemory(uint64_t address, DWORD count);
    void ReadMemoryVoid(uint64_t addr, void* buffer, DWORD size);
    uintptr_t ReadPtr(uint64_t ptr, uint64_t offset);
    uintptr_t ReadPtrChain(uint64_t ptr, std::vector<uint64_t>& offsets);

    template <typename T>
    T ReadValue(uint64_t address) {
        DWORD bytesRead = 0;
        T value{};
        if (vmmdll) {
             VMMDLL_MemReadEx(vmmdll, pid, address, (PBYTE)&value, sizeof(T), &bytesRead, 0);
        }
        return value;
    }

    VMMDLL_SCATTER_HANDLE CreateScatterHandle();
    void AddScatterRead(VMMDLL_SCATTER_HANDLE hS, uint64_t address, void* buffer, size_t size);
    bool ExecuteScatterRead(VMMDLL_SCATTER_HANDLE hS);
    void CloseScatterHandle(VMMDLL_SCATTER_HANDLE VMMDLL_Scatter_Handle);
};