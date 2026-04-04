#pragma once

#include "structs.hpp"

namespace game
{
	extern HMODULE mp_dll;

	uintptr_t game_offset(uintptr_t ida_address);

	template <typename T>
	class symbol
	{
	public:
		symbol(const size_t ida_address)
			: ida_address_(ida_address)
		{
		}

		T* get() const
		{
			return reinterpret_cast<T*>(game_offset(static_cast<uintptr_t>(ida_address_)));
		}

		operator T* () const
		{
			return this->get();
		}

		T* operator->() const
		{
			return this->get();
		}

	private:
		size_t ida_address_;
	};

	void Cbuf_AddText(int controller, const char* text);
	void Cmd_AddCommandInternal(const char* name, void(__cdecl* function)(), qos::cmd_function_s* cmd);
	bool DB_IsXAssetDefault(qos::XAssetType type, const char* name);
	void DB_EnumXAssetEntries(qos::XAssetType type, std::function<void(qos::XAssetEntryPoolEntry*)> callback, bool overrides);
	void DB_LoadXAssets(qos::XZoneInfo* zone_info, int zone_count, bool sync);
	void DB_SyncXAssets();

	namespace iw4
	{
		void VectorSubtract(const qos::vec3_t& a, const qos::vec3_t& b, qos::vec3_t& out);
	}

	unsigned int Scr_GetFunctionHandle(const char* filename, const char* funcHandle);
	void RemoveRefToObject(unsigned int obj);
	__int16 Scr_ExecThread(int handle);

	int Scr_LoadScript(const char* name);
	char* Scr_ReadFile_FastFile(const char* file_name, int code_pos);

	qos::dvar_s* Dvar_RegisterBool(const char* name, bool value, int flags, const char* desc);

	unsigned int Scr_GetNumParam();
}

#include "symbols.hpp"
