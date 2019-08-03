/*
 * @file
 * @brief Implementation for BCSV backend.
 * @remarks
 *	- MR: Implements findJMapInfoElementNoCase
 */

#include "JMapInfo.hpp"

#include "Game/MR/JMap.hpp"

// External symbols

namespace JGadget {
	extern u32 getHashCode(const char*);
}
namespace MR {
	extern bool isEqualStringCase(const char*, const char*);
}

//
// Externally referenced internal data
//


// String literal. However, for our linker reinsertion setup, this is necessary.
// Should look into automated data segment splitting and glue
LOCALREF(_defaultName, const char*, "Undifined", JMapInfo_default_name_anonymous)

#ifdef _MSC_VER
#pragma region Internal Types
#endif

struct JMapDataPtr
{
	inline JMapDataPtr(const void* ptr)
		: _wrapped(static_cast<const JMapData*>(ptr))
	{}

	inline bool valid() const
	{
		return (volatile bool)(volatile long long int)_wrapped;
	}


	const JMapData* _wrapped;
};
#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Constructor/Destructor
#endif
JMapInfo::JMapInfo()
	: mpData(nullptr), mpName(_defaultName)
{}
JMapInfo::~JMapInfo()
{}

#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Setup
#endif
bool JMapInfo::attach(const void* pBin)
{
	if (!pBin)
		return false;

	mpData = static_cast<const JMapData*>(pBin);
	return true;
}

#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Primitive Setters/Getters
#endif

void JMapInfo::setName(const char* pName)
{
	mpName = pName;
}
const char* JMapInfo::getName() const
{
	return mpName;
}

// unproven
u32 JMapInfo::getNumData(bool valid) const
{
	return valid ? mpData->nData : 0;
}

#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Advanced Data Acquisition
#endif

int JMapInfo::searchItemInfo(const char* path) const
{
	JMapDataPtr jmp_ptr(mpData);

	const bool valid = jmp_ptr.valid();
	if (!valid)
		return ERR_KEY_NOT_FOUND;

	// Must be an int
	const int nData = static_cast<int>(getNumData(valid));
	const u32 hash = JGadget::getHashCode(path);

	for (int i = 0; i < nData; i++)
		if (mpData->mItemInfoTable[i].hash == hash)
			return i;

	return ERR_KEY_NOT_FOUND;
}

MW_PRAG_NOINLINE
JMapValueType JMapInfo::getValueType(const char* path) const
{
	const int itemInfoIndex = searchItemInfo(path);

	return itemInfoIndex < 0 ? JMAP_VALUE_TYPE_INVALID : static_cast<JMapValueType>(mpData->mItemInfoTable[itemInfoIndex].value_type);
}
MW_PRAG_END

bool JMapInfo::getValueFast(int a, int b, const char** pOut) const
{
	const char* pData = (char*)mpData + calcDataElementOffset(a) + mpData->mItemInfoTable[b].ofs_data;

	switch (mpData->mItemInfoTable[b].value_type)
	{
	case JMAP_VALUE_TYPE_STRING_REFERENCED:
		*pOut = (const char*)mpData + calcDataElementOffset() + *(u32*)pData;
		break;
	default:
		*pOut = (const char*)pData;
	}

	return true;
}

namespace {
template <typename T>
inline T acquire(const char* ptr)
{
	// TODO: Would this have existed for endianness conversion?

	return *reinterpret_cast<const T*>(ptr);
}
}

bool JMapInfo::getValueFast(int dataIndex, int infoIndex, u32* pOut) const
{
	u32 acquired;
	const char* data_ptr = (char*)mpData + mpData->ofsData + (dataIndex * mpData->mDataStride) + mpData->getInfoTableEntry(infoIndex).ofs_data;
	const JMapData::ItemInfo& info = mpData->getInfoTableEntry(infoIndex);


	switch (info.value_type)
	{
	case JMAP_VALUE_TYPE_UINT32:
	case JMAP_VALUE_TYPE_INT32:
		acquired = acquire<u32>(data_ptr);
		break;
	case JMAP_VALUE_TYPE_INT16:
		acquired = acquire<u16>(data_ptr);
		break;
	case JMAP_VALUE_TYPE_INT8:
		acquired = acquire<u8>(data_ptr);
		break;
	default:
		return false;
	}

	*pOut = (acquired & info.mask) >> info.shift;
	return true;
}

bool JMapInfo::getValueFast(int dataIndex, int infoIndex, s32* pOut) const
{
	if (mpData->getInfoTableEntry(infoIndex).shift == 0)
	{
		const char* data_ptr = (char*)mpData + mpData->ofsData + (dataIndex * mpData->mDataStride) + mpData->getInfoTableEntry(infoIndex).ofs_data;
		const JMapData::ItemInfo& info = mpData->getInfoTableEntry(static_cast<u32>(infoIndex));

		switch (info.value_type)
		{
		// Explicitly unsigned UINT32 values cannot be acquired here.
		case JMAP_VALUE_TYPE_INT32:
			if (info.mask != 0xFFFFFFFF)
				goto failure;

			*pOut = acquire<s32>(data_ptr);
			break;

		case JMAP_VALUE_TYPE_INT16:
			if (info.mask != 0xFFFF)
				goto failure;

			*pOut = acquire<s16>(data_ptr);
			break;

		case JMAP_VALUE_TYPE_INT8:
			if (info.mask != 0xFF)
				goto failure;

			*pOut = acquire<s8>(data_ptr);
			break;

		default:
			goto failure;
		}

		return true;
	}

failure:
	return false;
}
#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef _MSC_VER
#pragma region Searching
#endif

MW_PRAG_OPT_S
JMapInfoIter MR::findJMapInfoElementNoCase(const JMapInfo* pInfo, const char* path, const char* key, int startIndex)
{
	const char* acquired;

	for (int i = startIndex; i < pInfo->getNumData(); ++i)
	{
		pInfo->getValue<const char*>(i, path, &acquired);

		if (MR::isEqualStringCase(acquired, key))
			return JMapInfoIter(pInfo, i);
	}

	return pInfo->end();

}
MW_PRAG_END

extern "C" int strcmp(const char * lhs, const char * rhs);
MW_PRAG_OPT_S
JMapInfoIter JMapInfo::findElementBinary(const char* path, const char* key) const
{
	int i = 0;
	int num_data = (int)getNumData();

	while(i < num_data)
	{
		int idx = (int)(num_data + i) / (int)2; // CWCC:S,!P; CWG:52

		// Acquire a pointer to the name
		const char* acquired;
		getValue<const char*>(idx, path, &acquired);

		// Compare the name against key
		const int comparison_result = strcmp(acquired, key);

		if (!comparison_result)
			return JMapInfoIter(this, idx);

		if (comparison_result < 0)
			i = comparison_result + 1;

		if (comparison_result >= 0)
			i = comparison_result;
	}

	return this->end();
}
MW_PRAG_END

#ifdef _MSC_VER
#pragma endregion
#endif
