#include <std_include.hpp>

#include "launcher.hpp"
#include "component/command.hpp"
#include "runtime.hpp"
#include "game/game.hpp"

#include <iostream>
#include <eh.h>
#include <cstdlib>
#include <utils/hook.hpp>

namespace
{
	utils::hook::detour g_xlive_patch_a;
	utils::hook::detour g_xlive_patch_b;
	utils::hook::detour g_splash_patch;
	utils::hook::detour g_xnaddr_patch;
	utils::hook::detour g_net_init_patch;
	utils::hook::detour g_d3d_interface_patch;
	utils::hook::detour g_d3d_device_patch;
	utils::hook::detour g_d3d_queries_patch;
	utils::hook::detour g_renderer_choice_patch;
	utils::hook::detour g_renderer_init_patch;
	utils::hook::detour g_renderer_backend_begin_patch;
	utils::hook::detour g_renderer_backend_end_patch;
	utils::hook::detour g_material_smp_patch;
	utils::hook::detour g_sound_init_patch;
	utils::hook::detour g_session_start_patch;
	utils::hook::detour g_client_disconnect_patch;
	utils::hook::detour g_exec_config_patch;
	utils::hook::detour g_localize_lookup_patch;
	utils::hook::detour g_engine_printf_hook;
	utils::hook::detour g_com_error_hook;
	utils::hook::detour g_db_load_xassets_hook;
	void* g_db_load_xassets_original = nullptr;
	std::atomic_bool g_bootstrap_zone_redirected = false;
	std::atomic_bool g_bootstrap_zones_ready = false;
	std::atomic_bool g_bootstrap_zone_load_started = false;
	std::atomic_int g_fs_startup_count = 0;
	bool g_debugbreak_bootstrap = false;
	std::atomic_bool g_window_watch_kill = false;
	std::atomic_bool g_window_hidden_logged = false;
	std::thread g_window_watch_thread;

	void append_log_line(const std::string& line);
	void host_print(const std::string& message);
	void write_console_line(const std::string& line);
	void host_patch_print(const std::string& message);
	bool should_suppress_engine_error(const std::string& message);
	bool should_redirect_zone_load(game::qos::XZoneInfo* zone_info, int zone_count, int sync);
	bool should_suppress_engine_line(const std::string& message);
	std::string lower_copy(std::string value);
	enum zone_trace_source
	{
		zone_trace_original = 0,
		zone_trace_redirect = 1,
		zone_trace_manual = 2,
	};
	void log_zone_load_request(int source, game::qos::XZoneInfo* zone_info, int zone_count, int sync);
	bool should_debugbreak_bootstrap();
	void wait_for_debugger_if_requested();
	std::string describe_zone_name(const char* name, bool trusted);
	std::string hex_address(const std::uintptr_t address)
	{
		char buffer[16]{};
		sprintf_s(buffer, "%08X", static_cast<unsigned int>(address));
		return buffer;
	}
	template <typename Stub>
	void apply_detour(utils::hook::detour& detour, const std::uintptr_t address, Stub stub)
	{
		host_patch_print("[patch - detour] patching 0x" + hex_address(address) + "...");
		detour.create(game::game_offset(static_cast<unsigned int>(address)), stub);
	}

	void apply_jump(const std::uintptr_t from, const std::uintptr_t to)
	{
		host_patch_print("[patch - jump] 0x" + hex_address(from) + " -> 0x" + hex_address(to));
		utils::hook::jump(game::game_offset(static_cast<unsigned int>(from)), game::game_offset(static_cast<unsigned int>(to)));
	}

	void apply_nop(const std::uintptr_t address, const size_t size)
	{
		host_patch_print("[patch - nop] 0x" + hex_address(address) + " (" + std::to_string(size) + " bytes)");
		utils::hook::nop(game::game_offset(static_cast<unsigned int>(address)), size);
	}

	void apply_gfxconfig_callsite_patch()
	{
		host_patch_print("[patch - callsite] 0x103F9A80");
		utils::hook::set<std::uint8_t>(game::game_offset(0x103F9A80), 0xB0);
		utils::hook::set<std::uint8_t>(game::game_offset(0x103F9A81), 0x01);
		utils::hook::nop(game::game_offset(0x103F9A82), 15);
	}

	constexpr std::array<const char*, 5> g_bootstrap_zone_names =
	{
		"code_pre_gfx_mp",
		"localized_code_pre_gfx_mp",
		"code_post_gfx_mp",
		"localized_code_post_gfx_mp",
		"common_mp"
	};

	game::qos::XZoneInfo g_bootstrap_zones[] =
	{
		{ g_bootstrap_zone_names[0], 0, 0 },
		{ g_bootstrap_zone_names[1], 0, 0 },
		{ g_bootstrap_zone_names[2], 0, 0 },
		{ g_bootstrap_zone_names[3], 0, 0 },
		{ g_bootstrap_zone_names[4], 0, 0 },
	};

	__declspec(naked) void xlive_ret_one_stub()
	{
		__asm
		{
			mov eax, 1
			ret
		}
	}

	__declspec(naked) void ret_success_stub()
	{
		__asm
		{
			mov eax, 1
			ret
		}
	}

	void* __cdecl xnaddr_success_stub()
	{
		return reinterpret_cast<void*>(game::game_offset(0x115FA0D4));
	}

	void __cdecl net_init_skip_stub()
	{
	}

	char __cdecl d3d_interface_skip_stub()
	{
		return 1;
	}

	int __cdecl d3d_device_skip_stub(int, int, int)
	{
		return 0;
	}

	char __cdecl d3d_queries_skip_stub()
	{
		return 1;
	}

	void __fastcall renderer_choice_skip_stub(void*)
	{
	}

	HWND __cdecl renderer_init_skip_stub()
	{
		return nullptr;
	}

	int __cdecl renderer_backend_skip_stub()
	{
		return 0;
	}

	int __fastcall material_smp_skip_stub(char)
	{
		return 0;
	}

	char __cdecl sound_init_skip_stub()
	{
		return 1;
	}

	int __fastcall session_start_skip_stub(int, char)
	{
		return 1;
	}

	void __cdecl client_disconnect_skip_stub()
	{
	}

	const char* __fastcall localize_lookup_filter_stub(const char* key)
	{
		if (runtime::is_standalone_xport_mode() && key)
		{
			return key;
		}

		return g_localize_lookup_patch.invoke<const char*>(const_cast<char*>(key));
	}

	char __cdecl exec_config_filter_stub(char* name, int a2, int a3, int a4)
	{
		if (runtime::is_standalone_xport_mode() && name)
		{
			const auto lowered = lower_copy(name);
			if (lowered.find("consolation_mp.cfg") != std::string::npos
				|| lowered.find("config_mp.cfg") != std::string::npos)
			{
				host_print(std::string("skipping config exec in xport mode: ") + name);
				return 1;
			}
		}

		return g_exec_config_patch.invoke<char>(name, a2, a3, a4);
	}

	std::string lower_copy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});

		return value;
	}

	std::filesystem::path get_host_log_path()
	{
		char path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		auto base = std::filesystem::path(path).parent_path() / "qos-xport";
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		return base / "qos-xport.log";
	}

	void append_log_line(const std::string& line)
	{
		const auto full_line = line + "\r\n";
		OutputDebugStringA(full_line.c_str());

		const auto path = get_host_log_path();
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
		WriteFile(handle, full_line.data(), static_cast<DWORD>(full_line.size()), &bytes_written, nullptr);
	}

	std::string format_message(va_list* ap, const char* message)
	{
		thread_local char buffer[0x2000]{};
		const auto count = vsnprintf_s(buffer, _TRUNCATE, message, *ap);
		if (count < 0)
		{
			return {};
		}

		return {buffer, static_cast<size_t>(count)};
	}

	void host_print(const std::string& message)
	{
		std::lock_guard _(runtime::get_output_mutex());
		write_console_line("[QoS-xport]: " + message);
		append_log_line("[host] " + message);
	}

	void host_patch_print(const std::string& message)
	{
		std::lock_guard _(runtime::get_output_mutex());
		write_console_line(message);
		append_log_line(message);
	}

	void write_console_line(const std::string& line)
	{
		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				const auto with_newline = line + "\r\n";
				DWORD written = 0;
				WriteConsoleA(handle, with_newline.data(), static_cast<DWORD>(with_newline.size()), &written, nullptr);
				return;
			}
		}

		std::fwrite(line.data(), 1, line.size(), stdout);
		std::fwrite("\n", 1, 1, stdout);
		std::fflush(stdout);
	}

	void engine_print(const std::string& message)
	{
		std::lock_guard _(runtime::get_output_mutex());
		std::string normalized = message;
		normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());

		size_t start = 0;
		while (start <= normalized.size())
		{
			const auto end = normalized.find('\n', start);
			auto line = normalized.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (!line.empty() && !should_suppress_engine_line(line))
			{
				if (line == "----- FS_Startup -----")
				{
					++g_fs_startup_count;
				}

				write_console_line("[engine] " + line);
				append_log_line("[engine] " + line);
			}

			if (end == std::string::npos)
			{
				break;
			}

			start = end + 1;
		}
	}

	void engine_printf_stub(int channel, const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		const auto message = format_message(&ap, fmt);
		va_end(ap);

		if (!runtime::is_standalone_xport_mode())
		{
			g_engine_printf_hook.invoke<void>(channel, "%s", message.c_str());
		}

		engine_print(message);
	}

	bool should_suppress_engine_error(const std::string& message)
	{
		const auto lowered = lower_copy(message);

		static const std::array<const char*, 4> suppressed_patterns =
		{
			"exe_disconnected_from_server",
			"failed to create xbox live notification listener",
			"unable to find an xnaddr",
			"fs_readfile"
		};

		for (const auto* pattern : suppressed_patterns)
		{
			if (lowered.find(pattern) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	bool should_suppress_engine_line(const std::string& message)
	{
		const auto lowered = lower_copy(message);

		static const std::array<const char*, 4> suppressed_prefixes =
		{
			"adding channel:",
			"hiding channel:",
			"no channels added or hidden",
			"cmd_addcommand:"
		};

		for (const auto* prefix : suppressed_prefixes)
		{
			if (lowered.rfind(prefix, 0) == 0)
			{
				return true;
			}
		}

		return false;
	}

	void com_error_stub(int a1, int a2, int a3, char* format, ...)
	{
		va_list ap;
		va_start(ap, format);
		const auto message = format_message(&ap, format);
		va_end(ap);

		if (!message.empty())
		{
			std::lock_guard _(runtime::get_output_mutex());
			write_console_line("[engine:error] " + message);
			append_log_line("[engine:error] " + message);
		}

		if (runtime::is_standalone_xport_mode() && should_suppress_engine_error(message))
		{
			std::lock_guard _(runtime::get_output_mutex());
			write_console_line("[QoS-xport] suppressed non-fatal engine error in xport mode");
			append_log_line("[host] suppressed non-fatal engine error in xport mode");
			return;
		}

		g_com_error_hook.invoke<void>(a1, a2, a3, const_cast<char*>("%s"), message.c_str());
	}

	bool should_redirect_zone_load(game::qos::XZoneInfo* zone_info, int zone_count, int)
	{
		if (!runtime::is_standalone_xport_mode() || !zone_info || zone_count <= 0)
		{
			return false;
		}

		for (int i = 0; i < zone_count; ++i)
		{
			const auto* name = zone_info[i].name;
			if (!name)
			{
				return false;
			}

			const auto zone_name = lower_copy(name);
			if (zone_name != "configuration_mp" && zone_name != "localized_configuration_mp")
			{
				return false;
			}
		}

		if (!g_bootstrap_zone_redirected.exchange(true))
		{
			host_print("redirecting configuration_mp zone bootstrap to ZoneTool-style default zones");
		}

	g_bootstrap_zones_ready = true;
		return true;
	}

	std::string describe_zone_name(const char* name, const bool trusted)
	{
		if (!name)
		{
			return "<null>";
		}

		if (trusted)
		{
			return name;
		}

		return "ptr=0x" + hex_address(reinterpret_cast<std::uintptr_t>(name));
	}

	void log_zone_load_request(int source, game::qos::XZoneInfo* zone_info, int zone_count, int sync)
	{
		if (!runtime::is_standalone_xport_mode() || !zone_info || zone_count <= 0)
		{
			return;
		}

		const char* source_name = "unknown";
		switch (source)
		{
		case zone_trace_original:
			source_name = "original";
			break;
		case zone_trace_redirect:
			source_name = "redirect";
			break;
		case zone_trace_manual:
			source_name = "manual";
			break;
		default:
			break;
		}

		const bool trusted_names = (source == zone_trace_manual);
		std::string message = std::string(source_name) + " zone_count=" + std::to_string(zone_count) + " sync=" + std::to_string(sync ? 1 : 0) + " zones=[";
		for (int i = 0; i < zone_count; ++i)
		{
			if (i > 0)
			{
				message += ", ";
			}

			message += describe_zone_name(zone_info[i].name, trusted_names);
		}
		message += "]";

		host_print(message);
	}

	bool should_debugbreak_bootstrap()
	{
		return g_debugbreak_bootstrap && IsDebuggerPresent();
	}

	void wait_for_debugger_if_requested(const char* stage)
	{
		if (!g_debugbreak_bootstrap)
		{
			return;
		}

		write_console_line(std::string("[debug - wait] attach a debugger now (") + stage + ")");
		append_log_line(std::string("[host] debug env waiting for debugger attach (") + stage + ")");
		while (!IsDebuggerPresent())
		{
			Sleep(100);
		}

		append_log_line(std::string("[host] debug env attached (") + stage + ")");
	}

	__declspec(naked) void db_load_xassets_stub()
	{
		__asm
		{
			push ebp
			mov ebp, esp
			mov edx, eax
			pushad
			push dword ptr [ebp + 12]
			push dword ptr [ebp + 8]
			push edx
			push zone_trace_original
			call log_zone_load_request
			add esp, 16
			push dword ptr [ebp + 12]
			push dword ptr [ebp + 8]
			push edx
			call should_redirect_zone_load
			add esp, 12
			mov bl, al
			popad
			test bl, bl
			jz continue_original
			call should_debugbreak_bootstrap
			test al, al
			jz no_break
			int 3
		no_break:
			pushad
			push 0
			push 5
			push offset g_bootstrap_zones
			push zone_trace_redirect
			call log_zone_load_request
			add esp, 16
			popad
			mov eax, offset g_bootstrap_zones
			push 0
			push 5
			call dword ptr [g_db_load_xassets_original]
			add esp, 8
			mov esp, ebp
			pop ebp
			ret
		continue_original:
			mov esp, ebp
			pop ebp
			jmp dword ptr [g_db_load_xassets_original]
		}
	}

	void __cdecl host_terminate_handler()
	{
		append_log_line("[host] CRT terminate handler invoked");
		ExitProcess(0xE0000001);
	}

	void __cdecl host_invalid_parameter_handler(
		const wchar_t* expression,
		const wchar_t* function,
		const wchar_t* file,
		unsigned int line,
		uintptr_t)
	{
		std::string message = "CRT invalid parameter";
		if (function)
		{
			char buffer[512]{};
			WideCharToMultiByte(CP_UTF8, 0, function, -1, buffer, sizeof(buffer), nullptr, nullptr);
			message += " in ";
			message += buffer;
		}

		if (file)
		{
			char buffer[512]{};
			WideCharToMultiByte(CP_UTF8, 0, file, -1, buffer, sizeof(buffer), nullptr, nullptr);
			message += " at ";
			message += buffer;
			message += ":";
			message += std::to_string(line);
		}

		append_log_line("[host] " + message);
	}

	int fail_and_wait(const std::string& message)
	{
		host_print(message);
		std::printf("Press Enter to close...\n");
		std::fflush(stdout);
		std::getchar();
		return 1;
	}

	using start_main_mp_raw_t = int(*)();

	int call_start_main_mp(
		start_main_mp_raw_t entry,
		int a1,
		int a2,
		HINSTANCE hinstance,
		int a4,
		int a5,
		int a6,
		int a7,
		int a8)
	{
		int result = 0;

		__asm
		{
			mov edi, a1
			mov esi, a2
			push a8
			push a7
			push a6
			push a5
			push a4
			push hinstance
			call entry
			add esp, 24
			mov result, eax
		}

		return result;
	}

	DWORD WINAPI standalone_engine_thread(LPVOID parameter)
	{
		const auto entry = reinterpret_cast<start_main_mp_raw_t>(parameter);
		return static_cast<DWORD>(call_start_main_mp(entry, 0, 0, GetModuleHandleA(nullptr), 0, 0, 0, 0, 0));
	}

	void hide_xport_windows_loop()
	{
		while (!g_window_watch_kill.load())
		{
			if (const auto window = FindWindowA("JB_MP", nullptr))
			{
				ShowWindow(window, SW_HIDE);
				SetWindowPos(window, HWND_BOTTOM, -32000, -32000, 1, 1, SWP_NOACTIVATE | SWP_NOOWNERZORDER);

				if (!g_window_hidden_logged.exchange(true))
				{
					host_print("hid JB_MP window for standalone xport mode");
				}
			}

			if (const auto splash = FindWindowA("007 Splash Screen", nullptr))
			{
				ShowWindow(splash, SW_HIDE);
				SetWindowPos(splash, HWND_BOTTOM, -32000, -32000, 1, 1, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
			}

			Sleep(250);
		}
	}

	int run_standalone_mode()
	{
		const auto log_path = get_host_log_path();
		DeleteFileA(log_path.string().c_str());
		set_terminate(host_terminate_handler);
		_set_invalid_parameter_handler(host_invalid_parameter_handler);

		host_print("========== qos-xport initializing ==========");

		const auto module = LoadLibraryA("jb_mp_s.dll");
		if (!module)
		{
			return fail_and_wait("failed to load jb_mp_s.dll (" + std::to_string(GetLastError()) + ")");
		}
		game::mp_dll = module;

		host_print("loaded jb_mp_s.dll");
		runtime::set_standalone_xport_mode(true);

		try
		{
			host_print("patching detours...");
			apply_detour(g_xlive_patch_a, 0x10240B30, xlive_ret_one_stub);
			apply_detour(g_xlive_patch_b, 0x10240A30, xlive_ret_one_stub);
			apply_detour(g_splash_patch, 0x102C3280, ret_success_stub);
			apply_detour(g_xnaddr_patch, 0x10243EE0, xnaddr_success_stub);
			apply_detour(g_net_init_patch, 0x10244890, net_init_skip_stub);
			apply_detour(g_d3d_interface_patch, 0x103BEC80, d3d_interface_skip_stub);
			apply_detour(g_d3d_device_patch, 0x103BD850, d3d_device_skip_stub);
			apply_detour(g_d3d_queries_patch, 0x103BDB50, d3d_queries_skip_stub);
			apply_detour(g_renderer_choice_patch, 0x103BE750, renderer_choice_skip_stub);
			apply_detour(g_renderer_init_patch, 0x103BEFD0, renderer_init_skip_stub);
			apply_detour(g_renderer_backend_begin_patch, 0x103BFF00, renderer_backend_skip_stub);
			apply_detour(g_renderer_backend_end_patch, 0x103BF5C0, renderer_backend_skip_stub);
			apply_detour(g_material_smp_patch, 0x103BF110, material_smp_skip_stub);
			apply_detour(g_sound_init_patch, 0x104035F0, sound_init_skip_stub);
			apply_detour(g_session_start_patch, 0x10247160, session_start_skip_stub);
			apply_detour(g_client_disconnect_patch, 0x1031E840, client_disconnect_skip_stub);
			apply_detour(g_localize_lookup_patch, 0x102DA9B0, localize_lookup_filter_stub);
			apply_detour(g_exec_config_patch, 0x103F5820, exec_config_filter_stub);
			apply_detour(g_engine_printf_hook, 0x103F6400, engine_printf_stub);
			apply_detour(g_com_error_hook, 0x103F77B0, com_error_stub);
			apply_detour(g_db_load_xassets_hook, 0x103E1CF0, db_load_xassets_stub);
			g_db_load_xassets_original = g_db_load_xassets_hook.get_original();

			host_print("patching jumps...");
			apply_jump(0x1024D8E9, 0x1024D909);
			apply_jump(0x103F7156, 0x103F7162);
			apply_jump(0x103F71B8, 0x103F721D);
			apply_jump(0x103F9BC1, 0x103F9BD7);
			apply_jump(0x103F9B5A, 0x103F9B85);

			host_print("patching nops...");
			apply_nop(0x102489A1, 5);

			host_print("patching callsites...");
			apply_gfxconfig_callsite_patch();
			host_patch_print("[host - patch] all patches applied");
			host_print("file load refs: FS_Startup=0x10272D80 ExecConfig=0x103F5820 Scr_ReadFile_FastFile=0x1022DF13 DB_LoadXAssets=0x103E1CF0");
		}
		catch (const std::exception& error)
		{
			return fail_and_wait(std::string("early patch stage failed: ") + error.what());
		}
		catch (...)
		{
			return fail_and_wait("early patch stage failed with unknown exception");
		}

		wait_for_debugger_if_requested("pre-engine");

		const auto start_main_mp = GetProcAddress(module, "startMainMP");
		if (!start_main_mp)
		{
			return fail_and_wait("failed to resolve startMainMP export");
		}

		host_print("resolved startMainMP export");

		g_bootstrap_zone_redirected = false;
		g_bootstrap_zones_ready = false;
		g_bootstrap_zone_load_started = false;
		g_fs_startup_count = 0;
		g_window_watch_kill = false;
		g_window_hidden_logged = false;
		g_window_watch_thread = std::thread(hide_xport_windows_loop);

		const auto thread = CreateThread(nullptr, 0, standalone_engine_thread, start_main_mp, 0, nullptr);
		if (!thread)
		{
			g_window_watch_kill = true;
			if (g_window_watch_thread.joinable())
			{
				g_window_watch_thread.join();
			}
			return fail_and_wait("failed to create standalone engine thread (" + std::to_string(GetLastError()) + ")");
		}

		const auto close_thread = gsl::finally([&]()
		{
			CloseHandle(thread);
		});

		host_print("started standalone engine thread");

		const auto early_wait_result = WaitForSingleObject(thread, 10);
		if (early_wait_result == WAIT_OBJECT_0)
		{
			DWORD exit_code = 0;
			GetExitCodeThread(thread, &exit_code);
			g_window_watch_kill = true;
			if (g_window_watch_thread.joinable())
			{
				g_window_watch_thread.join();
			}
			return fail_and_wait("engine thread exited before runtime initialization (" + std::to_string(exit_code) + ")");
		}

		host_print("waiting for bootstrap zones before runtime init");
		auto bootstrap_wait_start = GetTickCount64();
		while (!g_bootstrap_zones_ready.load())
		{
			if (!g_bootstrap_zone_load_started.exchange(true) && g_fs_startup_count.load() > 0)
			{
				host_print("FS startup reached; loading ZoneTool-style bootstrap zones");
				log_zone_load_request(zone_trace_manual, g_bootstrap_zones, static_cast<int>(std::size(g_bootstrap_zones)), 0);
				game::DB_LoadXAssets(g_bootstrap_zones, static_cast<int>(std::size(g_bootstrap_zones)), false);
				g_bootstrap_zones_ready = true;
				break;
			}

			const auto engine_wait = WaitForSingleObject(thread, 50);
			if (engine_wait == WAIT_OBJECT_0)
			{
				DWORD exit_code = 0;
				if (GetExitCodeThread(thread, &exit_code))
				{
					g_window_watch_kill = true;
					if (g_window_watch_thread.joinable())
					{
						g_window_watch_thread.join();
					}
					return fail_and_wait("engine thread exited before bootstrap zones completed (" + std::to_string(exit_code) + ")");
				}

				g_window_watch_kill = true;
				if (g_window_watch_thread.joinable())
				{
					g_window_watch_thread.join();
				}
				return fail_and_wait("engine thread exited before bootstrap zones completed");
			}

			if ((GetTickCount64() - bootstrap_wait_start) > 10000)
			{
				g_window_watch_kill = true;
				if (g_window_watch_thread.joinable())
				{
					g_window_watch_thread.join();
				}
				return fail_and_wait("timed out waiting for bootstrap zones to load");
			}
		}

		host_print("bootstrap zones loaded, initializing runtime in-process");
		if (!runtime::initialize(true))
		{
			g_window_watch_kill = true;
			if (g_window_watch_thread.joinable())
			{
				g_window_watch_thread.join();
			}
			return fail_and_wait("runtime initialization failed");
		}

		wait_for_debugger_if_requested("post-runtime");

		if (g_bootstrap_zones_ready.load())
		{
			write_console_line("");
			write_console_line("");
			write_console_line("");
			write_console_line("[QoS-xport: init] =========== initialization complete =============");
			write_console_line("[QoS-xport] type 'help' for a list of commands, or 'quit' to exit");
			append_log_line("[host] initialization complete");
			append_log_line("[host] type 'help' for a list of commands, or 'quit' to exit");
		}

		std::string line;
		while (true)
		{
			const auto engine_wait = WaitForSingleObject(thread, 0);
			if (engine_wait == WAIT_OBJECT_0)
			{
				DWORD exit_code = 0;
				if (GetExitCodeThread(thread, &exit_code))
				{
					host_print("engine thread exited with code " + std::to_string(exit_code));
				}
				break;
			}

			if (!std::getline(std::cin, line))
			{
				if (std::cin.eof() || std::cin.fail())
				{
					std::cin.clear();
					Sleep(50);
					continue;
				}

				host_print("stdin closed unexpectedly");
				break;
			}

			if (line == "quit" || line == "exit")
			{
				break;
			}

			if (!line.empty() && command::execute_local(line))
			{
				continue;
			}

			if (!line.empty())
			{
				game::Cbuf_AddText(0, (line + "\n").c_str());
			}
		}

		runtime::shutdown();
		g_window_watch_kill = true;
		if (g_window_watch_thread.joinable())
		{
			g_window_watch_thread.join();
		}
		WaitForSingleObject(thread, 1000);

		return 0;
	}
}

int main()
{
	int argc = 0;
	auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv)
	{
		const auto free_argv = gsl::finally([&]()
		{
			LocalFree(argv);
		});

		for (int i = 1; i < argc; ++i)
		{
			const std::wstring arg = argv[i];
			if (arg == L"--inject")
			{
				return run_launcher_mode();
			}
			if (arg == L"-debug_env")
			{
				g_debugbreak_bootstrap = true;
			}
		}
	}

	return run_standalone_mode();
}
