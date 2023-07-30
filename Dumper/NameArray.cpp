#include "NameArray.h"

/* DEBUG */
#include "ObjectArray.h"

uint8* NameArray::GNames = nullptr;

FNameEntry::FNameEntry(void* Ptr)
	: Address((uint8*)Ptr)
{
}

std::string FNameEntry::GetString()
{
	if (!Address)
		return "";

	return GetStr(Address);
}

void* FNameEntry::GetAddress()
{
	return Address;
}

void FNameEntry::Init(uint8_t* FirstChunkPtr, int64 NameEntryStringOffset)
{
	if (Settings::Internal::bUseNamePool)
	{
		constexpr int64 NoneStrLen = 0x4;
		constexpr int64 BytePropertyStrLen = 0xC;

		Off::FNameEntry::NamePool::StringOffset = NameEntryStringOffset;
		Off::FNameEntry::NamePool::HeaderOffset = NameEntryStringOffset == 6 ? 4 : 0;

		uint16 BytePropertyHeader = *reinterpret_cast<uint16*>(*reinterpret_cast<uint8**>(FirstChunkPtr) + NameEntryStringOffset + NoneStrLen);

		while (BytePropertyHeader != BytePropertyStrLen)
		{			
			FNameEntryLengthShiftCount++;
			BytePropertyHeader >>= 1;
		}

		GetStr = [](uint8* NameEntry) -> std::string
		{
			const uint16 HeaderWithoutNumber = *reinterpret_cast<uint16*>(NameEntry + Off::FNameEntry::NamePool::HeaderOffset);
			const int32 NameLen = HeaderWithoutNumber >> FNameEntry::FNameEntryLengthShiftCount;

			if (NameLen == 0)
			{
				const int32 EntryIdOffset = Off::FNameEntry::NamePool::StringOffset + ((Off::FNameEntry::NamePool::StringOffset == 6) * 2);

				return NameArray::GetNameEntry(*reinterpret_cast<int32*>(NameEntry + EntryIdOffset)).GetString();
			}

			if (HeaderWithoutNumber & NameWideMask)
			{
				std::wstring WString(reinterpret_cast<const wchar_t*>(NameEntry + Off::FNameEntry::NamePool::StringOffset), NameLen);
				return std::string(WString.begin(), WString.end());
			}

			return std::string(reinterpret_cast<const char*>(NameEntry + Off::FNameEntry::NamePool::StringOffset), NameLen);
		};
	}
	else
	{
		uint8_t* FNameEntryNone = (uint8_t*)NameArray::GetNameEntry(0x0).GetAddress();
		uint8_t* FNameEntryIdxThree = (uint8_t*)NameArray::GetNameEntry(0x3).GetAddress();
		uint8_t* FNameEntryIdxEight = (uint8_t*)NameArray::GetNameEntry(0x8).GetAddress();

		for (int i = 0; i < 0x20; i++)
		{
			if (*reinterpret_cast<uint32*>(FNameEntryNone + i) == 0x656e6f4e /*"None" in little-endian*/)
			{
				Off::FNameEntry::NameArray::StringOffset = i;
				break;
			}
		}

		for (int i = 0; i < 0x20; i++)
		{
			// lowest bit is bIsWide mask, shift right by 1 to get the index
			if ((*reinterpret_cast<uint32*>(FNameEntryIdxThree + i) >> 1) == 0x3 &&
				(*reinterpret_cast<uint32*>(FNameEntryIdxEight + i) >> 1) == 0x8)
			{
				Off::FNameEntry::NameArray::IndexOffset = i;
				break;
			}
		}

		GetStr = [](uint8* NameEntry) -> std::string
		{
			const int32 NameIdx = *reinterpret_cast<int32*>(NameEntry + Off::FNameEntry::NameArray::IndexOffset);
			const void* NameString = reinterpret_cast<void*>(NameEntry + Off::FNameEntry::NameArray::StringOffset);

			if (NameIdx & NameWideMask)
			{
				std::wstring WString(reinterpret_cast<const wchar_t*>(NameString));
				return std::string(WString.begin(), WString.end());
			}

			return reinterpret_cast<const char*>(NameString);
		};
	}
}

bool NameArray::InitializeNameArray(uint8_t* NameArray)
{
	int32 ValidPtrCount = 0x0;
	int32 ZeroQWordCount = 0x0;

	int32 PerChunk = 0x0;

	if (!NameArray)
		return false;

	for (int i = 0; i < 0x800; i += 0x8)
	{
		uint8_t* SomePtr = *reinterpret_cast<uint8_t**>(NameArray + i);

		if (SomePtr == 0)
		{
			ZeroQWordCount++;
		}
		else if (ZeroQWordCount == 0x0 && SomePtr != nullptr)
		{
			ValidPtrCount++;
		}
		else if (ZeroQWordCount > 0 && SomePtr != 0)
		{
			int32 NumElements = *reinterpret_cast<int32_t*>(NameArray + i);
			int32 NumChunks = *reinterpret_cast<int32_t*>(NameArray + i + 4);

			if (NumChunks == ValidPtrCount)
			{
				Off::NameArray::NumElements = i;
				Off::NameArray::ChunkCount = i + 4;

				ByIndex = [](void* NamesArray, int32 ComparisonIndex) -> void*
				{
					const int32 ChunkIdx = ComparisonIndex / 0x4000;
					const int32 InChunk = ComparisonIndex % 0x4000;

					return reinterpret_cast<void***>(NamesArray)[ChunkIdx][InChunk];
				};

				return true;
			}
		}
	}

	return false;
}

bool NameArray::InitializeNamePool(uint8_t* NamePool)
{
	constexpr uint64 CoreUObjAsUint64 = 0x726F432F74706972; // little endian "ript/Cor" ["/Script/CoreUObject"]
	constexpr uint32 NoneAsUint32 = 0x656E6F4E; // little endian "None"

	constexpr int64 CoreUObjectStringLength = sizeof("/S");

	uint8_t** ChunkPtr = reinterpret_cast<uint8_t**>(NamePool + 0x10);

	// "/Script/CoreUObject"
	uint8_t* CoreUObjectFNameEntry = nullptr;
	int64 FNameEntryHeaderSize = 0x0;

	for (int i = 0; i < 0x1000; i++)
	{
		if (*reinterpret_cast<uint32*>(*ChunkPtr + i) == NoneAsUint32)
		{
			FNameEntryHeaderSize = i;
		}
		else if (*reinterpret_cast<uint64*>(*ChunkPtr + i) == CoreUObjAsUint64)
		{
			CoreUObjectFNameEntry = *ChunkPtr + static_cast<uint64>(i) + (CoreUObjectStringLength + FNameEntryHeaderSize);
			break;
		}
	}

	NameEntryStride = FNameEntryHeaderSize == 2 ? 2 : 4;

	ByIndex = [](void* NamesArray, int32 ComparisonIndex) -> void*
	{
		const int32 ChunkIdx = ComparisonIndex >> FNameBlockOffsetBits;
		const int32 InChunk = ComparisonIndex & (FNameBlockMaxOffset - 1);

		uint8_t* ChunkPtr = reinterpret_cast<uint8_t*>(NamesArray) + 0x10;

		return reinterpret_cast<uint8_t**>(ChunkPtr)[ChunkIdx] + NameEntryStride * InChunk;
	};

	Settings::Internal::bUseNamePool = true;
	FNameEntry::Init(reinterpret_cast<uint8*>(ChunkPtr), FNameEntryHeaderSize);

	return true;
}

void NameArray::Init()
{
	uintptr_t ImageBase = GetImageBase();

	std::cout << "Searching for GNames...\n\n";

	uint8_t** GNamesAddress = reinterpret_cast<uint8_t**>(FindPattern("48 89 3D ? ? ? ? 8B 87 ? ? ? ? 05 ? ? ? ? 99 81 E2 ? ? ? ?", 3, true));

	if (!GNamesAddress)
		GNamesAddress = reinterpret_cast<uint8_t**>(FindPattern("48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B C0 C6 05", 3, true));

	if (NameArray::InitializeNameArray(*GNamesAddress))
	{
		GNames = *GNamesAddress;
		Settings::Internal::bUseNamePool = false;
		FNameEntry::Init();
		std::cout << "Found NameArray at offset: 0x" << std::hex << (reinterpret_cast<uint8_t*>(GNamesAddress) - ImageBase) << "\n" << std::endl;
		return;
	}
	else if (NameArray::InitializeNamePool(reinterpret_cast<uint8_t*>(GNamesAddress)))
	{
		GNames = reinterpret_cast<uint8_t*>(GNamesAddress);
		Settings::Internal::bUseNamePool = true;
		std::cout << "Found NamePool at offset: 0x" << std::hex << (reinterpret_cast<uint8_t*>(GNamesAddress) - ImageBase) << "\n" << std::endl;
		/* FNameEntry::Init() was moved into NameArray::InitializeNamePool to avoid duplicated logic */
		return;
	}

	std::cout << "\nGNames couldn't be found!\n\n\n";
}

int32 NameArray::GetNumElements()
{
	return !Settings::Internal::bUseNamePool ? *reinterpret_cast<int32*>(GNames + Off::NameArray::NumElements) : 0;
}

int32 NameArray::GetNumChunks()
{
	return *reinterpret_cast<int32*>(GNames + Off::NameArray::ChunkCount);
}

FNameEntry NameArray::GetNameEntry(void* Name)
{
	return ByIndex(GNames, FName(Name).GetCompIdx());
}

FNameEntry NameArray::GetNameEntry(int32 Idx)
{
	return ByIndex(GNames, Idx);
}

