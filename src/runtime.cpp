#include <std_include.hpp>

#include "runtime.hpp"

#include "loader/component_loader.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

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
		const utils::nt::library user32{ "user32.dll" };

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
			const utils::nt::library shcore{ "shcore.dll" };
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
}

namespace runtime
{
	bool initialize(const bool report_errors)
	{
		std::lock_guard _(runtime_mutex);

		if (runtime_initialized)
		{
			return true;
		}

		enable_dpi_awareness();

		srand(uint32_t(time(nullptr)));

		try
		{
			/*
			const auto version_check = utils::hook::get<DWORD>(0x59B69C);
			if (version_check != 0x6C6C6143)
			{
				throw std::string("invalid game files");
			}
			*/
			// utils::hook::set(0x5931B8, exit_hook);

			if (!component_loader::post_start())
			{
				throw std::string("component post start failed");
			}

			if (!component_loader::post_load())
			{
				throw std::string("component post load failed");
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

		runtime_initialized = true;
		return true;
	}

	void shutdown()
	{
		std::lock_guard _(runtime_mutex);

		if (!runtime_initialized)
		{
			return;
		}

		component_loader::pre_destroy();
		runtime_initialized = false;
	}

	bool is_initialized()
	{
		return runtime_initialized;
	}
}
