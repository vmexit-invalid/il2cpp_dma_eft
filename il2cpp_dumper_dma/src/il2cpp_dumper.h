#pragma once

#include <cstdint>
#include "utils/MemoryController.h"

#define IL2CPP_MAGIC 0xFAB11BAF

struct Il2CppGlobalMetadataHeader
{
    uint32_t magicNumber;
    uint32_t version;
    uint32_t stringLiteralOffset;
    int32_t stringLiteralCount;
    uint32_t stringLiteralDataOffset;
    int32_t stringLiteralDataCount;
    uint32_t genericInstsOffset;
    int32_t genericInstsCount;
    uint32_t imagesOffset;
    int32_t imagesCount;
    uint32_t assembliesOffset;
    int32_t assembliesCount;
    uint32_t typeDefsOffset;
    int32_t typeDefsCount;
    uint32_t methodDefsOffset;
    int32_t methodDefsCount;
    uint32_t propertyDefsOffset;
    int32_t propertyDefsCount;
    uint32_t fieldDefsOffset;
    int32_t fieldDefsCount;
    uint32_t fieldDefaultValuesOffset;
    int32_t fieldDefaultValuesCount;
    uint32_t methodDefaultValuesOffset;
    int32_t methodDefaultValuesCount;
    uint32_t parameterDefsOffset;
    int32_t parameterDefsCount;
    uint32_t metadataUsageListsOffset;
    int32_t metadataUsageListsCount;
    uint32_t metadataUsagePairsOffset;
    int32_t metadataUsagePairsCount;
    uint32_t typesOffset;
    int32_t typesCount;
};

struct Il2CppImageDefinition
{
    uint32_t nameIndex;
    uint32_t assemblyIndex;
    uint32_t typeStart;
    uint32_t typeCount;
    uint32_t exportedTypeStart;
    uint32_t exportedTypeCount;
    uint32_t entryPointIndex;
    uint32_t token;
};

struct Il2CppType
{
    uint64_t data;
    uint32_t attrs : 16;
    uint32_t type : 8;          // Il2CppTypeEnum
    uint32_t num_mods : 6;
    uint32_t byref : 1;
    uint32_t pinning : 1;
};
static_assert(sizeof(Il2CppType) == 0x10, "il2CppType size mismatch");

enum Il2CppTypeEnum
{
    IL2CPP_TYPE_END = 0x00,
    IL2CPP_TYPE_VOID = 0x01,
    IL2CPP_TYPE_BOOLEAN = 0x02,
    IL2CPP_TYPE_CHAR = 0x03,
    IL2CPP_TYPE_I1 = 0x04,
    IL2CPP_TYPE_U1 = 0x05,
    IL2CPP_TYPE_I2 = 0x06,
    IL2CPP_TYPE_U2 = 0x07,
    IL2CPP_TYPE_I4 = 0x08,
    IL2CPP_TYPE_U4 = 0x09,
    IL2CPP_TYPE_I8 = 0x0a,
    IL2CPP_TYPE_U8 = 0x0b,
    IL2CPP_TYPE_R4 = 0x0c,
    IL2CPP_TYPE_R8 = 0x0d,
    IL2CPP_TYPE_STRING = 0x0e,
    IL2CPP_TYPE_PTR = 0x0f,
    IL2CPP_TYPE_BYREF = 0x10,
    IL2CPP_TYPE_VALUETYPE = 0x11,
    IL2CPP_TYPE_CLASS = 0x12,
    IL2CPP_TYPE_VAR = 0x13,
    IL2CPP_TYPE_ARRAY = 0x14,
    IL2CPP_TYPE_GENERICINST = 0x15,
    IL2CPP_TYPE_TYPEDBYREF = 0x16,
    IL2CPP_TYPE_I = 0x18,
    IL2CPP_TYPE_U = 0x19,
    IL2CPP_TYPE_FNPTR = 0x1b,
    IL2CPP_TYPE_OBJECT = 0x1c,
    IL2CPP_TYPE_SZARRAY = 0x1d,
    IL2CPP_TYPE_MVAR = 0x1e,
    IL2CPP_TYPE_CMOD_REQD = 0x1f,
    IL2CPP_TYPE_CMOD_OPT = 0x20,
    IL2CPP_TYPE_INTERNAL = 0x21,
    IL2CPP_TYPE_MODIFIER = 0x40,
    IL2CPP_TYPE_SENTINEL = 0x41,
    IL2CPP_TYPE_PINNED = 0x45,
    IL2CPP_TYPE_ENUM = 0x55
};

struct Il2CppTypeDefinition
{
    uint32_t nameIndex;
    uint32_t namespaceIndex;
    uint32_t byvalTypeIndex;
    uint32_t byrefTypeIndex;
    uint32_t declaringTypeIndex;
    uint32_t parentIndex;
    uint32_t genericContainerIndex;
    uint32_t flags;
    uint32_t fieldStart;
    uint32_t methodStart;
    uint32_t eventStart;
    uint32_t propertyStart;
    uint32_t nestedTypesStart;
    uint16_t field_count;
    uint16_t method_count;
    uint16_t event_count;
    uint16_t property_count;
    uint16_t nested_type_count;
    uint32_t token;
};

struct Il2CppFieldDefinition
{
    uint32_t nameIndex;
    uint32_t typeIndex;
    uint32_t token;
};

struct Il2CppMethodDefinition
{
    uint32_t nameIndex;
    uint32_t declaringType;
    uint32_t returnType;
    uint32_t parameterStart;
    uint32_t genericContainerIndex;
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint16_t parameterCount;
};

struct Il2CppPropertyDefinition
{
    uint32_t nameIndex;
    uint32_t get;
    uint32_t set;
    uint32_t attrs;
    uint32_t token;
};

struct Il2CppParameterDefinition
{
    uint32_t nameIndex;
    uint32_t typeIndex;
    uint32_t token;
};

struct Il2CppGenericInst
{
    uint32_t type_argc;
    uint32_t type_argv;
};

struct Il2CppGenericContext
{
    void* class_inst;
    void* method_inst;
};

struct Il2CppMetadataRegistrationLayout
{
    int32_t genericClassesCount;
    uint32_t pad0;
    uint64_t genericClasses;
    int32_t genericInstsCount;
    uint32_t pad1;
    uint64_t genericInsts;
    int32_t genericMethodTableCount;
    uint32_t pad2;
    uint64_t genericMethodTable;
    int32_t typesCount;
    uint32_t pad3;
    uint64_t types;
    int32_t methodSpecsCount;
    uint32_t pad4;
    uint64_t methodSpecs;
    uint64_t methodPointers;            // const Il2CppMethodPointer*
    uint64_t invokerPointers;           // const InvokerMethod*
    uint64_t customAttributeGenerators; // const CustomAttributesCacheGenerator*
    uint64_t unresolvedVirtualCallPointers;
    uint64_t interopData;
    int32_t windowsRuntimeFactoryCount;
    uint32_t pad5;
    uint64_t windowsRuntimeFactoryTable;
    uint64_t genericAdjustorThunks;
    uint64_t handle;
};

class DMAController;

void DumpIl2Cpp();