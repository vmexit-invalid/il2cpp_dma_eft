#include "il2cpp_dumper.h"
#include "utils/Utils.h"
#include "utils/MemoryController.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

struct FieldDump {
    std::string name;
    std::string type;
    int32_t offset;
};

struct ClassDump {
    uint64_t klassPtr;
    std::string name;
    std::string namesp;
    std::string parentName;
    std::vector<FieldDump> fields;
};

static uint64_t ResolveExportAddress(DMAController& dma, ProcessMap& process, uint64_t moduleBase, uint64_t moduleSize, const char* exportName) {
    uint64_t resolved = 0;
    PVMMDLL_MAP_EAT pEatMap = nullptr;
    if (dma.GetModuleEAT((LPSTR)"GameAssembly.dll", pEatMap)) {
        for (DWORD i = 0; i < pEatMap->cMap; i++) {
            if (strcmp(pEatMap->pMap[i].uszFunction, exportName) == 0) {
                resolved = pEatMap->pMap[i].vaFunction;
                break;
            }
        }
        VMMDLL_MemFree(pEatMap);
    }
    return resolved;
}

static uint64_t FindMetadataViaExportAnchors(DMAController& dma, ProcessMap& process, uint64_t moduleBase, uint64_t moduleSize) {
    struct Anchor { const char* name; uint64_t addr; };
    std::vector<Anchor> anchors = {
        { "il2cpp_class_enum_basetype", 0 },
        { "il2cpp_gchandle_new", 0 },
        { "il2cpp_property_get_flags", 0 },
        { "il2cpp_thread_get_top_frame", 0 }
    };

    for (auto& anchor : anchors) {
        anchor.addr = ResolveExportAddress(dma, process, moduleBase, moduleSize, anchor.name);
    }

    std::vector<BYTE> pattern = {
        0x48, 0xc1, 0xe9, 0x04,
        0xba
    };
    std::string mask = "xxxxx";

    constexpr uint64_t window = 0x100000;
    for (const auto& anchor : anchors) {
        if (anchor.addr == 0)
            continue;

        // targeted pattern scan 
        uint64_t start = (anchor.addr > moduleBase + (window / 2)) ? anchor.addr - (window / 2) : moduleBase;
        start = (start & 0xfffffff00000);
        uint64_t maxRangeEnd = moduleBase + moduleSize;
        uint64_t range = window;
        if (start + window > maxRangeEnd) {
            range = (maxRangeEnd > start) ? (maxRangeEnd - start) : 0;
        }

        uint64_t found = dma.FindPattern(start, range, pattern, mask);
        if (found == 0)
            continue;

        const uint64_t movOpcodeOffset = 14;
        const uint64_t dispOffset = movOpcodeOffset + 3;
        int32_t disp32 = dma.ReadValue<int32_t>(found + dispOffset);
        uint64_t movInstruction = found + movOpcodeOffset;
        uint64_t metadataPointerAddress = (movInstruction + 7) + disp32;
        uint64_t metadataBase = dma.ReadValue<uint64_t>(metadataPointerAddress);
        return metadataBase;
    }

    return 0;
}

struct ClassHeader {
    uint64_t namePtr = 0;
    uint64_t namespacePtr = 0;
    uint64_t parentPtr = 0;
    uint64_t fieldsBase = 0;
    uint16_t fieldCount = 0;
};

static bool ScatterReadClassPointers(DMAController& dma, uint64_t classArrayAddr, int32_t classCount, std::vector<uint64_t>& out) {
    if (classCount <= 0 || (size_t)classCount != out.size()) return false;
    auto hS = dma.CreateScatterHandle();
    if (!hS) return false;

    for (int32_t i = 0; i < classCount; ++i) {
        dma.AddScatterRead(hS, classArrayAddr + (uint64_t)i * sizeof(uint64_t), &out[i], sizeof(uint64_t));
    }

    bool ok = dma.ExecuteScatterRead(hS);
    dma.CloseScatterHandle(hS);
    return ok;
}

static bool ScatterReadClassHeaders(DMAController& dma, const std::vector<uint64_t>& classPtrs, std::vector<ClassHeader>& out) {
    if (classPtrs.size() != out.size()) return false;
    auto hS = dma.CreateScatterHandle();
    if (!hS) return false;

    for (size_t i = 0; i < classPtrs.size(); ++i) {
        uint64_t kp = classPtrs[i];
        if (kp == 0) continue;
        dma.AddScatterRead(hS, kp + 0x10, &out[i].namePtr, sizeof(uint64_t));
        dma.AddScatterRead(hS, kp + 0x18, &out[i].namespacePtr, sizeof(uint64_t));
        dma.AddScatterRead(hS, kp + 0x58, &out[i].parentPtr, sizeof(uint64_t));
        dma.AddScatterRead(hS, kp + 0x80, &out[i].fieldsBase, sizeof(uint64_t));
        dma.AddScatterRead(hS, kp + 0x124, &out[i].fieldCount, sizeof(uint16_t));
    }

    bool ok = dma.ExecuteScatterRead(hS);
    dma.CloseScatterHandle(hS);
    return ok;
}

static constexpr size_t kStrBuf = 256;

static std::string DescribeTypeCached(
    const Il2CppType* type,
    const std::unordered_map<uint64_t, Il2CppType>& typeCache,
    const std::unordered_map<uint64_t, size_t>& klassIndex,
    const std::vector<std::array<char, kStrBuf>>& classNames,
    const std::vector<std::array<char, kStrBuf>>& classNamespaces,
    int depth = 0)
{
    if (!type || depth > 5) return "UnknownType";
    switch (type->type) {
    case IL2CPP_TYPE_VOID: return "System.Void";
    case IL2CPP_TYPE_BOOLEAN: return "System.Boolean";
    case IL2CPP_TYPE_CHAR: return "System.Char";
    case IL2CPP_TYPE_I1: return "System.SByte";
    case IL2CPP_TYPE_U1: return "System.Byte";
    case IL2CPP_TYPE_I2: return "System.Int16";
    case IL2CPP_TYPE_U2: return "System.UInt16";
    case IL2CPP_TYPE_I4: return "System.Int32";
    case IL2CPP_TYPE_U4: return "System.UInt32";
    case IL2CPP_TYPE_I8: return "System.Int64";
    case IL2CPP_TYPE_U8: return "System.UInt64";
    case IL2CPP_TYPE_R4: return "System.Single";
    case IL2CPP_TYPE_R8: return "System.Double";
    case IL2CPP_TYPE_STRING: return "System.String";
    case IL2CPP_TYPE_OBJECT: return "System.Object";
    case IL2CPP_TYPE_CLASS:
    case IL2CPP_TYPE_VALUETYPE: {
        uint64_t klassPtr = type->data & ~0x1FULL;
        auto it = klassIndex.find(klassPtr);
        if (it != klassIndex.end()) {
            size_t idx = it->second;
            std::string n = std::string(classNames[idx].data());
            std::string ns = std::string(classNamespaces[idx].data());
            if (!n.empty()) {
                return ns.empty() ? n : (ns + "::" + n);
            }
        }
        return "System.Object";
    }
    case IL2CPP_TYPE_PTR:
    case IL2CPP_TYPE_BYREF:
    case IL2CPP_TYPE_SZARRAY:
    case IL2CPP_TYPE_ARRAY: {
        uint64_t elemPtr = type->data & ~0x1FULL;
        auto it = typeCache.find(elemPtr);
        if (it != typeCache.end()) {
            std::string elemName = DescribeTypeCached(&it->second, typeCache, klassIndex, classNames, classNamespaces, depth + 1);
            if (type->type == IL2CPP_TYPE_PTR) return elemName + "*";
            if (type->type == IL2CPP_TYPE_BYREF) return elemName + "&";
            return elemName + "[]";
        }
        return "UnknownType";
    }
    case IL2CPP_TYPE_GENERICINST:
        return "GenericInst";
    case IL2CPP_TYPE_VAR: {
        uint32_t idx = static_cast<uint32_t>(type->data & 0xffffffffu);
        std::stringstream ss; ss << "T" << idx; return ss.str();
    }
    case IL2CPP_TYPE_MVAR: {
        uint32_t idx = static_cast<uint32_t>(type->data & 0xffffffffu);
        std::stringstream ss; ss << "MVar" << idx; return ss.str();
    }
    default:
        return "UnknownType";
    }
}

static std::vector<uint64_t> PrefetchClassPtrs(DMAController& dma, uint64_t base, int count) {
    std::vector<uint64_t> classPtrs(count, 0);
    if (!ScatterReadClassPointers(dma, base, count, classPtrs)) {
        LogMessage("scatter read failed. fatal");
        return {};
    }
    return classPtrs;
}

static bool PrefetchClassHeaders(DMAController& dma, const std::vector<uint64_t>& classPtrs, std::vector<ClassHeader>& headers) {
    headers.resize(classPtrs.size());
    if (!ScatterReadClassHeaders(dma, classPtrs, headers)) {
        LogMessage("scatter read failed. fatal");
        return false;
    }
    return true;
}

static void PrefetchNames(DMAController& dma, const std::vector<ClassHeader>& headers, std::vector<std::array<char, kStrBuf>>& names, std::vector<std::array<char, kStrBuf>>& namespaces) {
    if (auto hNames = dma.CreateScatterHandle()) {
        for (size_t i = 0; i < headers.size(); ++i) {
            const ClassHeader& hdr = headers[i];
            if (hdr.namePtr) dma.AddScatterRead(hNames, hdr.namePtr, names[i].data(), kStrBuf);
            if (hdr.namespacePtr) dma.AddScatterRead(hNames, hdr.namespacePtr, namespaces[i].data(), kStrBuf);
        }
        dma.ExecuteScatterRead(hNames);
        dma.CloseScatterHandle(hNames);
    }
}

static void PrefetchFieldBlocks(DMAController& dma, const std::vector<ClassHeader>& headers, std::vector<std::vector<uint8_t>>& fieldBlocks) {
    if (auto hFieldsAll = dma.CreateScatterHandle()) {
        for (size_t i = 0; i < headers.size(); ++i) {
            const ClassHeader& hdr = headers[i];
            if (hdr.fieldsBase && hdr.fieldCount > 0 && hdr.fieldCount < 3000) {
                fieldBlocks[i].resize((size_t)hdr.fieldCount * 0x20);
                dma.AddScatterRead(hFieldsAll, hdr.fieldsBase, fieldBlocks[i].data(), fieldBlocks[i].size());
            }
        }
        dma.ExecuteScatterRead(hFieldsAll);
        dma.CloseScatterHandle(hFieldsAll);
    }
}

static void CollectFieldMeta(const std::vector<ClassHeader>& headers, const std::vector<std::vector<uint8_t>>& fieldBlocks, std::vector<std::vector<std::array<char, kStrBuf>>>& fieldNameBufAll, std::unordered_map<uint64_t, size_t>& typePtrToIdx, std::vector<uint64_t>& typePtrList) {
    for (size_t i = 0; i < headers.size(); ++i) {
        const ClassHeader& hdr = headers[i];
        if (hdr.fieldCount == 0 || hdr.fieldCount >= 3000 || fieldBlocks[i].empty()) continue;
        fieldNameBufAll[i].resize(hdr.fieldCount);
        const uint8_t* basePtr = fieldBlocks[i].data();
        for (int f = 0; f < hdr.fieldCount; ++f) {
            const uint8_t* entry = basePtr + (size_t)f * 0x20;
            uint64_t typePtr = *(const uint64_t*)(entry + 0x8);
            if (typePtr && typePtrToIdx.find(typePtr) == typePtrToIdx.end()) {
                size_t idx = typePtrList.size();
                typePtrToIdx[typePtr] = idx;
                typePtrList.push_back(typePtr);
            }
        }
    }
}

static void PrefetchFieldNames(DMAController& dma, const std::vector<ClassHeader>& headers, const std::vector<std::vector<uint8_t>>& fieldBlocks, std::vector<std::vector<std::array<char, kStrBuf>>>& fieldNameBufAll) {
    if (auto hFieldNames = dma.CreateScatterHandle()) {
        for (size_t i = 0; i < headers.size(); ++i) {
            const ClassHeader& hdr = headers[i];
            if (hdr.fieldCount == 0 || hdr.fieldCount >= 3000 || fieldBlocks[i].empty()) continue;
            const uint8_t* basePtr = fieldBlocks[i].data();
            for (int f = 0; f < hdr.fieldCount; ++f) {
                const uint8_t* entry = basePtr + (size_t)f * 0x20;
                uint64_t namePtr = *(const uint64_t*)(entry + 0x0);
                if (namePtr) dma.AddScatterRead(hFieldNames, namePtr, fieldNameBufAll[i][f].data(), kStrBuf);
            }
        }
        dma.ExecuteScatterRead(hFieldNames);
        dma.CloseScatterHandle(hFieldNames);
    }
}

static std::unordered_map<uint64_t, Il2CppType> PrefetchTypes(DMAController& dma, const std::vector<uint64_t>& typePtrList) {
    std::unordered_map<uint64_t, Il2CppType> typeCache;
    std::vector<Il2CppType> typeBuf(typePtrList.size());
    if (!typePtrList.empty()) {
        if (auto hTypes = dma.CreateScatterHandle()) {
            for (size_t t = 0; t < typePtrList.size(); ++t) dma.AddScatterRead(hTypes, typePtrList[t], &typeBuf[t], sizeof(Il2CppType));
            dma.ExecuteScatterRead(hTypes);
            dma.CloseScatterHandle(hTypes);
        }
    }
    typeCache.reserve(typeBuf.size() * 2);
    for (size_t t = 0; t < typePtrList.size(); ++t) typeCache[typePtrList[t]] = typeBuf[t];
    return typeCache;
}

static void ShowProgress(int idx, int total, std::chrono::steady_clock::time_point& last) {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    std::cout << "\rprogress " << idx << "/" << total << " +" << std::fixed << std::setprecision(2) << dt << "s" << std::flush;
}

static std::vector<FieldDump> BuildFields(int clsIdx, uint64_t klassPtr, const ClassHeader& hdr, const std::vector<std::vector<uint8_t>>& fieldBlocksAll, const std::vector<std::vector<std::array<char, kStrBuf>>>& fieldNameBufAll, const std::unordered_map<uint64_t, Il2CppType>& typeCache, const std::unordered_map<uint64_t, size_t>& klassIndex, const std::vector<std::array<char, kStrBuf>>& nameBufAll, const std::vector<std::array<char, kStrBuf>>& nsBufAll) {
    std::vector<FieldDump> fields;
    if (fieldBlocksAll[clsIdx].empty()) return fields;

    struct PendingField { FieldDump dump; uint64_t typePtr = 0; };
    std::vector<PendingField> pending;
    pending.reserve(hdr.fieldCount);

    const uint8_t* basePtr = fieldBlocksAll[clsIdx].data();
    for (int f = 0; f < hdr.fieldCount; ++f) {
        const uint8_t* entry = basePtr + (size_t)f * 0x20;
        uint64_t namePtr = *(const uint64_t*)(entry + 0x0);
        if (namePtr == 0) break;
        std::string fname = fieldNameBufAll[clsIdx][f].data();
        if (fname.empty()) break;
        uint64_t fieldParent = *(const uint64_t*)(entry + 0x10);
        if (fieldParent != klassPtr) break;
        uint64_t typePtr = *(const uint64_t*)(entry + 0x8);
        uint64_t offTok = *(const uint64_t*)(entry + 0x18);
        uint32_t fOff = static_cast<uint32_t>(offTok & 0xFFFFFFFFu);

        PendingField pf;
        pf.dump.name = fname;
        pf.dump.offset = (int32_t)fOff;
        pf.typePtr = typePtr;
        pending.push_back(std::move(pf));
    }

    for (auto& pf : pending) {
        Il2CppType fType{};
        auto itType = typeCache.find(pf.typePtr);
        if (itType != typeCache.end()) fType = itType->second;
        std::string fTypeName = DescribeTypeCached(itType != typeCache.end() ? &itType->second : nullptr, typeCache, klassIndex, nameBufAll, nsBufAll);
        if (fType.type == IL2CPP_TYPE_VAR || fType.type == IL2CPP_TYPE_MVAR) {
            auto pos = pf.dump.name.find("i__Field");
            if (pos != std::string::npos) fTypeName = pf.dump.name.substr(0, pos) + "j__TPar";
        }
        pf.dump.type = fTypeName;
        fields.push_back(std::move(pf.dump));
    }

    return fields;
}

static void DumpRuntimeClasses(DMAController& dma, uint64_t metadata_registry_struct, uint64_t gameAssemblyBase, uint64_t gameAssemblyModuleSize)
{
    OpenLogFile();

    int32_t rawCount = dma.ReadValue<int32_t>(metadata_registry_struct - 0x10);
    int32_t classCount = rawCount / 8;

    std::filesystem::path dumpPath = std::filesystem::path(GetExecutableDirA()) / "dump.cs";
    std::ofstream out(dumpPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        LogMessage("failed to open dump.cs");
        return;
    }

    {
        std::stringstream ss;
        ss << "dumping classes from metadata registry @ 0x" << std::hex << metadata_registry_struct << " count=" << std::dec << classCount;
        LogMessage(ss.str());
    }

    const int dumpLimit = classCount;
    int consecutiveFailures = 0;
    const int failureThreshold = 10;
    auto lastProgress = std::chrono::steady_clock::now();

    std::vector<uint64_t> classPtrs = PrefetchClassPtrs(dma, metadata_registry_struct, dumpLimit);
    if (classPtrs.empty()) return;

    std::vector<ClassHeader> prefetchedHeaders;
    if (!PrefetchClassHeaders(dma, classPtrs, prefetchedHeaders)) return;

    constexpr int CHUNK = 512;

    std::vector<std::array<char, kStrBuf>> nameBufAll(dumpLimit);
    std::vector<std::array<char, kStrBuf>> nsBufAll(dumpLimit);
    PrefetchNames(dma, prefetchedHeaders, nameBufAll, nsBufAll);

    std::vector<std::vector<uint8_t>> fieldBlocksAll(dumpLimit);
    PrefetchFieldBlocks(dma, prefetchedHeaders, fieldBlocksAll);

    std::unordered_map<uint64_t, size_t> klassIndex;
    klassIndex.reserve(dumpLimit * 2);
    for (int i = 0; i < dumpLimit; ++i) if (classPtrs[i]) klassIndex[classPtrs[i]] = static_cast<size_t>(i);

    std::vector<std::vector<std::array<char, kStrBuf>>> fieldNameBufAll(dumpLimit);
    std::unordered_map<uint64_t, size_t> typePtrToIdx;
    std::vector<uint64_t> typePtrList;
    CollectFieldMeta(prefetchedHeaders, fieldBlocksAll, fieldNameBufAll, typePtrToIdx, typePtrList);
    PrefetchFieldNames(dma, prefetchedHeaders, fieldBlocksAll, fieldNameBufAll);

    std::unordered_map<uint64_t, Il2CppType> typeCache = PrefetchTypes(dma, typePtrList);

    for (int base = 0; base < dumpLimit; base += CHUNK) {
        int count = (std::min)(CHUNK, dumpLimit - base);
        std::vector<struct ClassHeader> hdrChunk;
        hdrChunk.resize(count);

        std::vector<uint64_t> chunkPtrs(count, 0);
        for (int j = 0; j < count; ++j) {
            uint64_t kp = classPtrs[base + j];
            chunkPtrs[j] = kp;
            hdrChunk[j] = prefetchedHeaders[base + j];
        }

        // Parse chunk
        for (int j = 0; j < count; ++j) {
            int i = base + j;
            uint64_t klassPtr = chunkPtrs[j];
            if (klassPtr == 0) {
                if (++consecutiveFailures >= failureThreshold) {
                    LogMessage("too many consecutive read failures at class index " + std::to_string(i));
                    goto finish;
                }
                continue;
            }
            consecutiveFailures = 0;

            const ClassHeader& hdr = hdrChunk[j];
            ClassDump cd;
            cd.klassPtr = klassPtr;
            auto bufToString = [](const std::array<char, kStrBuf>& b) { return std::string(b.data()); };
            cd.name = hdr.namePtr ? bufToString(nameBufAll[i]) : "";
            if (cd.name.empty()) {
                if (++consecutiveFailures >= failureThreshold) {
                    LogMessage("too many consecutive read failures (empty class name) at index " + std::to_string(i));
                    goto finish;
                }
                continue;
            }
            cd.namesp = hdr.namespacePtr ? bufToString(nsBufAll[i]) : "";
            if (hdr.parentPtr) {
                auto pit = klassIndex.find(hdr.parentPtr);
                if (pit != klassIndex.end()) {
                    size_t pidx = pit->second;
                    cd.parentName = bufToString(nameBufAll[pidx]);
                    std::string pns = bufToString(nsBufAll[pidx]);
                    if (!pns.empty() && !cd.parentName.empty()) cd.parentName = pns + "::" + cd.parentName;
                }
            }

            if ((i % 200) == 0) ShowProgress(i, dumpLimit, lastProgress);

            cd.fields = BuildFields(i, klassPtr, hdr, fieldBlocksAll, fieldNameBufAll, typeCache, klassIndex, nameBufAll, nsBufAll);

            out << "namespace: " << cd.namesp << ", class: " << cd.name;
            if (!cd.parentName.empty()) out << " : " << cd.parentName;
            out << "\n{\n";
            for (const auto& f : cd.fields) out << "    " << f.type << " " << f.name << "; // 0x" << std::hex << f.offset << std::dec << "\n";
            out << "}\n\n";
        }
    }

    std::cout << "\rprogress " << dumpLimit << "/" << dumpLimit << std::endl;

finish:
    out.close();
    LogMessage("finished dumping classes");
}

void DumpIl2Cpp()
{
    
    DMAController DMA;
    if (!DMA.Initialize()) {
        LogMessage("failed to initialize DMA");
        return;
    }

    ProcessMap Tarkov{};
    uint64_t GameAssembly = 0;

    OpenLogFile();
    LogMessage("starting dma il2cpp dump");

    if (!DMA.GetProcessInfo((LPSTR)"EscapeFromTarkov.exe", Tarkov)) {
        LogMessage("failed to get process");
        return;
    }
    
    uint64_t gameAssemblySize = 0;
    DMA.GetModuleInfo((LPSTR)"GameAssembly.dll", Tarkov, GameAssembly, gameAssemblySize);
    if (GameAssembly == 0) {
        LogMessage("target not found");
        return;
    }

    uint64_t metadata_registry_struct = FindMetadataViaExportAnchors(DMA, Tarkov, GameAssembly, gameAssemblySize);
    if (metadata_registry_struct == 0)
    {
        LogMessage("failed to locate metadata registry via export anchors");
        return;
    }
    
    {
        std::stringstream ss;
        ss << "metadata registry @ 0x" << std::hex << metadata_registry_struct;
        LogMessage(ss.str());
    }
    DumpRuntimeClasses(DMA, metadata_registry_struct, GameAssembly, gameAssemblySize);
}