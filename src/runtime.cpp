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
	bool runtime_log_cleared = false;
	bool standalone_xport_mode = false;

	std::filesystem::path get_launcher_log_path()
	{
		char module_path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, module_path, MAX_PATH);
		auto path = std::filesystem::path(module_path).parent_path() / "qos-xport";
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		return path / "launcher.log";
	}

	void log_runtime_message(const std::string& message)
	{
		const auto line = "[runtime] " + message + "\r\n";
		OutputDebugStringA(line.c_str());

		const auto path = get_launcher_log_path();
		if (!runtime_log_cleared)
		{
			runtime_log_cleared = true;
		}

		const auto handle = CreateFileA(
			path.string().c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr
		);

		if (handle == INVALID_HANDLE_VALUE)
		{
			return;
		}

		const auto close_handle = gsl::finally([&]()
		{
			CloseHandle(handle);
		});

		DWORD bytes_written = 0;
		WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &bytes_written, nullptr);
	}

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
	void set_standalone_xport_mode(const bool enabled)
	{
		std::lock_guard _(runtime_mutex);
		standalone_xport_mode = enabled;
	}

	bool is_standalone_xport_mode()
	{
		std::lock_guard _(runtime_mutex);
		return standalone_xport_mode;
	}

	bool initialize(const bool report_errors)
	{
		std::lock_guard _(runtime_mutex);
		log_runtime_message("runtime::initialize entered");

		if (runtime_initialized)
		{
			log_runtime_message("runtime already initialized");
			return true;
		}

		enable_dpi_awareness();
		log_runtime_message("dpi awareness enabled");

		srand(uint32_t(time(nullptr)));
		log_runtime_message("random seed initialized");

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

			log_runtime_message("calling component_loader::post_start");
			if (!component_loader::post_start())
			{
				log_runtime_message("component_loader::post_start returned false");
				throw std::string("component post start failed");
			}
			log_runtime_message("component_loader::post_start succeeded");

			log_runtime_message("calling component_loader::post_load");
			if (!component_loader::post_load())
			{
				log_runtime_message("component_loader::post_load returned false");
				throw std::string("component post load failed");
			}
			log_runtime_message("component_loader::post_load succeeded");
		}
		catch (const std::string& error)
		{
			log_runtime_message("runtime initialization caught error: " + error);
			component_loader::pre_destroy();
			log_runtime_message("component_loader::pre_destroy completed after init failure");

			if (report_errors)
			{
				MessageBoxA(nullptr, error.data(), "ERROR", MB_ICONERROR);
			}

			return false;
		}

		runtime_initialized = true;
		log_runtime_message("runtime initialized successfully");
		return true;
	}

	void shutdown()
	{
		std::lock_guard _(runtime_mutex);

		if (!runtime_initialized)
		{
			log_runtime_message("runtime::shutdown skipped; not initialized");
			return;
		}

		log_runtime_message("runtime::shutdown pre_destroy begin");
		component_loader::pre_destroy();
		runtime_initialized = false;
		log_runtime_message("runtime::shutdown complete");
	}

	bool is_initialized()
	{
		return runtime_initialized;
	}
}
