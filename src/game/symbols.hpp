#pragma once

#define WEAK __declspec(selectany)

namespace game
{
	WEAK symbol<void(int channel, const char* fmt, ...)> Com_Printf{ 0x103F6400 };

	WEAK symbol<qos::XAssetHeader(qos::XAssetType type, const char* name)> DB_FindXAssetHeader{ 0x103E2260 };
	WEAK symbol<qos::XAssetHeader(qos::XAssetType type, const char* name, int create_default)> DB_FindXAssetHeader_Internal{ 0x103E1EE0 };

	WEAK symbol<int(const char* name)> Scr_LoadScript_{ 0x1022E7C0 };

	// Variables
	WEAK symbol<DWORD> command_id{ 0x10752C70 };
	WEAK symbol<DWORD> cmd_argc{ 0x10752CB4 };
	WEAK symbol<char**> cmd_argv{ 0x10752CD4 };
	WEAK symbol<qos::cmd_function_s*> cmd_functions{ 0x10752CF8 };
	WEAK symbol<qos::scrMemTreePub_t> scrMemTreePub{ 0x116357CC };

	WEAK symbol<unsigned short> db_hashTable{ 0x1082ED60 };
	WEAK symbol<qos::XAssetEntryPoolEntry> g_assetEntryPool{ 0x108CB5C0 };

	WEAK symbol<unsigned int> scr_numParams{ 0x117384A4 };

	WEAK symbol<qos::scrVmPub_t> scrVmPub{ 0x11738488 };
}
