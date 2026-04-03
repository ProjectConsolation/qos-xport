#include <std_include.hpp>
#include <Windows.h>

#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include <utils/string.hpp>
#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/hook.hpp>

#include <ShellScalingApi.h>

namespace
{
	std::mutex runtime_mutex;
	bool runtime_initialized = false;

	DECLSPEC_NORETURN void WINAPI exit_hook(const int code)
	{
		component_loader::pre_destroy();
		exit(code);
	}

	void enable_dpi_awareness()
	{
		const utils::nt::library user32{"user32.dll"};

		{
			const auto set_dpi = user32
				? user32.get_proc<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
					"SetProcessDpiAwarenessContext")
				: nullptr;
			if (set_dpi)
			{
				set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
				return;
			}
		}

		{
			const utils::nt::library shcore{"shcore.dll"};
			const auto set_dpi = shcore
				? shcore.get_proc<HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS)>(
					"SetProcessDpiAwareness")
				: nullptr;
			if (set_dpi)
			{
				set_dpi(PROCESS_PER_MONITOR_DPI_AWARE);
				return;
			}
		}

		{
			const auto set_dpi = user32
				? user32.get_proc<BOOL(WINAPI*)()>(
					"SetProcessDPIAware")
				: nullptr;
			if (set_dpi)
			{
				set_dpi();
			}
		}
	}

	void unprotect_module(HMODULE module)
	{
		auto header = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
		auto nt_header = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<DWORD>(module) + header->e_lfanew);
		DWORD old_protect;
		VirtualProtect(reinterpret_cast<LPVOID>(module), nt_header->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &old_protect);
	}

	bool initialize_runtime(const bool report_errors)
	{
		std::lock_guard _(runtime_mutex);

		if (runtime_initialized)
		{
			return true;
		}

		ShowWindow(GetConsoleWindow(), SW_HIDE);
		enable_dpi_awareness();

		srand(uint32_t(time(nullptr)));

		{
			try
			{
				/*
				const auto version_check = utils::hook::get<DWORD>(0x59B69C);
				if (version_check != 0x6C6C6143)
				{
					throw "invalid game files";
					return;
				}
				*/
				//utils::hook::set(0x5931B8, exit_hook); // ExitProcess import, might not be good to hook this but iat isn't working

				if (!component_loader::post_start())
				{
					throw "component post start failed";
				}

				if (!component_loader::post_load())
				{
					throw "component post load failed";
				}
			}
			catch (const std::string& error)
			{
				component_loader::pre_destroy();

				if (report_errors)
				{
					MessageBoxA(nullptr, error.data(), "ERROR", MB_ICONERROR);
				}

				return false;
			}
		}

		runtime_initialized = true;
		return true;
	}

	void shutdown_runtime()
	{
		std::lock_guard _(runtime_mutex);

		if (!runtime_initialized)
		{
			return;
		}

		component_loader::pre_destroy();
		runtime_initialized = false;
	}
}

extern "C" __declspec(dllexport) BOOL qos_xport_init()
{
	return initialize_runtime(true) ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void qos_xport_shutdown()
{
	shutdown_runtime();
}

extern "C" __declspec(dllexport) BOOL qos_xport_is_initialized()
{
	return runtime_initialized ? TRUE : FALSE;
}

int WINAPI DllMain(HINSTANCE instance, const DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(instance);

#ifdef QOS_XPORT_PROXY_LOADER
		initialize_runtime(true);
#endif
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
#ifdef QOS_XPORT_PROXY_LOADER
		shutdown_runtime();
#endif
	}

	return 1;
}
