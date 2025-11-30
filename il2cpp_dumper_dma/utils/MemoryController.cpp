#include "MemoryController.h"
#include <filesystem>
#include <sstream>
#include <iomanip>

bool MmapExists(const std::wstring& name) {
    struct _stat buffer;
    return (_wstat(name.c_str(), &buffer) == 0);
}

// =====================================
// = DMA
// =====================================

DMAController::DMAController() {
}

DMAController::~DMAController() {
    Close();
}

bool DMAController::Initialize() {
    if (vmmdll) return true;

    std::wstring basePath = GetExecutableDirW();
    std::wstring suffix = L"\\mmap.txt";
    std::wstring filePath = basePath + suffix;
    bool mmap_exists_result = MmapExists(filePath);

    if (mmap_exists_result) {
        std::unique_ptr<char[]> memmap_path(const_cast<char*>(ConvertWStringToLPCSTR(filePath)));
        LPCSTR args[] = { "", "-device", "fpga", "-memmap", memmap_path.get() };
        DWORD argc = 5;
        vmmdll = VMMDLL_Initialize(argc, args);
        return (vmmdll != 0);
    }

    LPCSTR args[] = { "", "-device", "fpga", "-v" };
    DWORD argc = 4;

    vmmdll = VMMDLL_Initialize(argc, args);
    if (vmmdll) {
        PVMMDLL_MAP_PHYSMEM pMPhys = NULL;
        auto res = VMMDLL_Map_GetPhysMem(vmmdll, &pMPhys);
        if (res) {
            if (pMPhys->dwVersion != VMMDLL_MAP_PHYSMEM_VERSION) {
                VMMDLL_MemFree(pMPhys);
                Close();
                return false;
            }
            std::stringstream sb;
            for (DWORD i = 0; i < pMPhys->cMap; i++) {
                sb << std::setfill('0') << std::setw(4) << i << "  " << std::hex << pMPhys->pMap[i].pa << "  -  " << (pMPhys->pMap[i].pa + pMPhys->pMap[i].cb - 1) << "  ->  " << pMPhys->pMap[i].pa << std::endl;
            }
            std::ofstream nFile(filePath);
            nFile << sb.str();
            nFile.close();
            VMMDLL_MemFree(pMPhys);
            Close(); 
            return false;
        } else {
            Close();
            return false;
        }
    }
    return false;
}

bool DMAController::GetProcessInfo(LPSTR processName, ProcessMap& Process) {
    if (!vmmdll) return false;
    DWORD tmpPid = 0;
    if (!VMMDLL_PidGetFromName(vmmdll, processName, &tmpPid)) {
        return false;
    }
    pid = tmpPid;

    VMMDLL_PROCESS_INFORMATION pProcInfo;
    SIZE_T procInfoSz = sizeof(pProcInfo);
    pProcInfo.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
    pProcInfo.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
    if (VMMDLL_ProcessGetInformation(vmmdll, pid, &pProcInfo, &procInfoSz)) {
        Process.pid = pProcInfo.dwPID;
    } else {
        return false;
    }

    PVMMDLL_MAP_MODULEENTRY pModuleEntryExplorer;
    if (VMMDLL_Map_GetModuleFromNameU(vmmdll, pid, processName, &pModuleEntryExplorer, NULL)) {
        Process.baseAddress = pModuleEntryExplorer->vaBase;
        Process.sz = pModuleEntryExplorer->cbImageSize;
        Process.endAddress = pModuleEntryExplorer->vaBase + pModuleEntryExplorer->cbImageSize;
        return true;
    }
    return false;
}

bool DMAController::GetModuleInfo(LPSTR moduleName, ProcessMap& Process, uint64_t& OutputAddress, uint64_t& OutputSize) {
    if (!vmmdll) return false;
    PVMMDLL_MAP_MODULEENTRY pModuleEntryExplorer;
    if (VMMDLL_Map_GetModuleFromNameU(vmmdll, Process.pid, moduleName, &pModuleEntryExplorer, NULL)) {
        OutputAddress = pModuleEntryExplorer->vaBase;
        OutputSize = pModuleEntryExplorer->cbImageSize;
        return true;
    }
    return false;
}

bool DMAController::GetModulePath(LPSTR moduleName, ProcessMap& Process, std::wstring& outPath) {
    if (!vmmdll) return false;
    PVMMDLL_MAP_MODULEENTRY pModuleEntryExplorer;
    if (!VMMDLL_Map_GetModuleFromNameU(vmmdll, Process.pid, moduleName, &pModuleEntryExplorer, NULL)) {
        return false;
    }
    if (pModuleEntryExplorer->wszFullName) {
        outPath = std::wstring(pModuleEntryExplorer->wszFullName);
    } else if (pModuleEntryExplorer->uszFullName) {
        outPath = std::wstring(pModuleEntryExplorer->uszFullName, pModuleEntryExplorer->uszFullName + strlen(pModuleEntryExplorer->uszFullName));
    } else {
        return false;
    }
    return true;
}

bool DMAController::GetModuleEAT(LPSTR moduleName, PVMMDLL_MAP_EAT& pEatMap) {
    if (!vmmdll) return false;
    if (!VMMDLL_Map_GetEATU(vmmdll, pid, moduleName, &pEatMap)) {
        return false;
    }
    return true;
}

void DMAController::Close() {
    if (vmmdll) {
        VMMDLL_Close(vmmdll);
        vmmdll = 0;
    }
}

uint64_t DMAController::FindPattern(uint64_t baseAddress, uint64_t size, const std::vector<BYTE>& pattern, const std::string& mask) {
    if (!vmmdll) return 0;
    size_t patternSize = pattern.size();
    if (patternSize == 0 || patternSize != mask.length()) return 0;
    
    const size_t chunkSize = 4096;
    std::vector<BYTE> buffer(chunkSize);

    for (uint64_t currentAddress = baseAddress; currentAddress < baseAddress + size; currentAddress += chunkSize) {
        DWORD bytesRead = 0;
        if (!VMMDLL_MemReadEx(vmmdll, pid, currentAddress, buffer.data(), chunkSize, &bytesRead, 0)) {
            continue;
        }

        for (size_t i = 0; i < bytesRead; ++i) {
            if (i + patternSize > bytesRead) break;

            bool match = true;
            for (size_t j = 0; j < patternSize; ++j) {
                if (mask[j] == 'x' && buffer[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return currentAddress + i;
        }
    }
    return 0;
}

BYTE* DMAController::ReadMemory(uint64_t address, DWORD count) {
    if (!vmmdll) return nullptr;
    DWORD cbRead = 0;
    BYTE* pb = new BYTE[count];
    bool read_success = false;

    for (int retry = 0; retry < 5; ++retry) {
        if (VMMDLL_MemReadEx(vmmdll, pid, address, pb, count, &cbRead, 0) && cbRead == count) {
            read_success = true;
            break;
        }
        Sleep(10);
    }

    if (!read_success) {
        delete[] pb;
        return nullptr;
    }
    return pb;
}

void DMAController::ReadMemoryVoid(uint64_t addr, void* buffer, DWORD size) {
    if (!vmmdll) return;
    DWORD bytesRead = 0;
    for (int retry = 0; retry < 5; ++retry) {
        if (VMMDLL_MemReadEx(vmmdll, pid, addr, (PBYTE)buffer, size, &bytesRead, 0) && bytesRead == size) {
            break;
        }
        Sleep(10);
    }
}

bool DMAController::ReadMemoryRange(uint64_t addr, void* buffer, DWORD size, DWORD& bytesRead) {
    if (!vmmdll) return false;
    bytesRead = 0;
    for (int retry = 0; retry < 5; ++retry) {
        if (VMMDLL_MemReadEx(vmmdll, pid, addr, (PBYTE)buffer, size, &bytesRead, 0)) {
            if (bytesRead > 0) return true;
        }
        Sleep(5);
    }
    return false;
}
        
uintptr_t DMAController::ReadPtr(uint64_t ptr, uint64_t offset) {
    return ReadValue<uintptr_t>(ptr + offset);
}

uintptr_t DMAController::ReadPtrChain(uint64_t ptr, std::vector<uint64_t>& offsets) {
    uintptr_t address = ReadValue<uintptr_t>(ptr + offsets[0]);
    if (address == 0) return 0;
    for (size_t i = 1; i < offsets.size(); i++) {
        address = ReadPtr(address, offsets[i]);
        if (address == 0) return 0;
    }
    return address;
}

// =====================================
// = SCATTER FUNCTIONS                 = 
// =====================================

VMMDLL_SCATTER_HANDLE DMAController::CreateScatterHandle() {
    if (!vmmdll) return 0;
    return VMMDLL_Scatter_Initialize(vmmdll, pid, VMMDLL_FLAG_NOCACHE);
}

void DMAController::AddScatterRead(VMMDLL_SCATTER_HANDLE hS, uint64_t address, void* buffer, size_t size ) {
    if (hS) VMMDLL_Scatter_PrepareEx(hS, address, size, (PBYTE)buffer, NULL); 
}

bool DMAController::ExecuteScatterRead(VMMDLL_SCATTER_HANDLE hS) {
    if (!vmmdll || !hS) return false;
    bool result = VMMDLL_Scatter_ExecuteRead(hS);
    if (!result) return false;
    result = VMMDLL_Scatter_Clear(hS, pid, VMMDLL_FLAG_NOCACHE);
    return result;
}

void DMAController::CloseScatterHandle(VMMDLL_SCATTER_HANDLE VMMDLL_Scatter_Handle) {
    VMMDLL_Scatter_CloseHandle(VMMDLL_Scatter_Handle);
}