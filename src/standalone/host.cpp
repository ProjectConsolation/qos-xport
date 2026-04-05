#include <std_include.hpp>

#include "standalone/launcher.hpp"
#include "standalone/build_info.hpp"
#include "component/command.hpp"
#include "standalone/runtime.hpp"
#include "standalone/shell.hpp"
#include "game/game.hpp"

#include <eh.h>
#include <cstdlib>
#include <conio.h>
#include <mmsystem.h>
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
	utils::hook::detour g_profile_config_patch;
	utils::hook::detour g_localize_lookup_patch;
	utils::hook::detour g_engine_printf_hook;
	utils::hook::detour g_com_error_hook;
	utils::hook::detour g_init_popup_hook;
	utils::hook::detour g_fs_startup_patch;
	utils::hook::detour g_fs_readfile_patch;
	std::atomic_bool g_bootstrap_zone_redirected = false;
	std::atomic_bool g_bootstrap_zones_ready = false;
	std::atomic_bool g_bootstrap_zone_load_started = false;
	std::atomic_bool g_debug_wait_consumed = false;
	std::atomic_bool g_profile_config_skip_logged = false;
	std::atomic_bool g_engine_error_seen = false;
	std::atomic_bool g_init_popup_seen = false;
	std::atomic_ulong g_module_load_exception_code = 0;
	std::atomic_int g_fs_startup_count = 0;
	bool g_debugbreak_bootstrap = false;
	std::atomic_bool g_window_watch_kill = false;
	std::atomic_bool g_window_hidden_logged = false;
	std::thread g_window_watch_thread;
	bool g_shell_input_mode_active = false;
	DWORD g_original_shell_input_mode = 0;

	using standalone::shell::append_input_log_line;
	using standalone::shell::append_log_line;
	using standalone::shell::clear_console_display;
	using standalone::shell::commit_shell_input_line;
	using standalone::shell::flush_shell_input_buffer;
	using standalone::shell::host_patch_print;
	using standalone::shell::host_print;
	using standalone::shell::host_section_print;
	using standalone::shell::make_section_banner;
	using standalone::shell::make_host_section_line;
	using standalone::shell::render_shell_prompt;
	using standalone::shell::render_shell_input_line;
	using standalone::shell::settle_shell_io;
	using standalone::shell::write_console_line;
	using standalone::shell::write_shell_line;
	bool should_suppress_engine_error(const std::string& message);
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
	void wait_for_debugger_if_requested(const char* stage);
	std::string describe_zone_name(const char* name, bool trusted);
	void perform_bootstrap_zone_load();
	int __cdecl fs_startup_host_stub();
	int __cdecl fs_readfile_filter_impl(char* name, void** out_buffer, int flags);
	int __cdecl invoke_original_fs_readfile(char* name, void** out_buffer, int flags);
	int __cdecl profile_config_skip_stub(int controller, const char* source);
	void fs_readfile_filter_stub();
	LONG WINAPI host_exception_filter(_EXCEPTION_POINTERS* exception_info);
	void __cdecl init_popup_stub(char* message);
	HMODULE load_jb_mp_module_guarded();
	std::string hex_address(const std::uintptr_t address)
	{
		char buffer[16]{};
		sprintf_s(buffer, "%08X", static_cast<unsigned int>(address));
		return buffer;
	}

	void print_init_complete_banner()
	{
		clear_console_display();
		write_shell_line("");
		write_shell_line("");
		write_shell_line("");
		write_shell_line("[QoS-xport] =========== initialization complete =============");
		write_shell_line("[QoS-xport] type 'help' for a list of commands, or 'quit' to exit");
		append_log_line("[host] =========== qos-xport initialization complete =============");
		append_log_line("[host] type 'help' for a list of commands, or 'quit' to exit");

		flush_shell_input_buffer();
	}

	void activate_shell_input_mode()
	{
		const auto input_handle = GetStdHandle(STD_INPUT_HANDLE);
		if (input_handle == INVALID_HANDLE_VALUE || input_handle == nullptr)
		{
			return;
		}

		DWORD mode = 0;
		if (!GetConsoleMode(input_handle, &mode))
		{
			return;
		}

		if (!g_shell_input_mode_active)
		{
			g_original_shell_input_mode = mode;
			g_shell_input_mode_active = true;
		}

		DWORD shell_mode = mode;
		shell_mode |= ENABLE_EXTENDED_FLAGS;
		shell_mode |= ENABLE_PROCESSED_INPUT;
		shell_mode |= ENABLE_ECHO_INPUT;
		shell_mode |= ENABLE_LINE_INPUT;
		shell_mode &= ~ENABLE_MOUSE_INPUT;
		shell_mode &= ~ENABLE_QUICK_EDIT_MODE;

		SetConsoleMode(input_handle, shell_mode);
		FlushConsoleInputBuffer(input_handle);
	}

	void restore_shell_input_mode()
	{
		if (!g_shell_input_mode_active)
		{
			return;
		}

		const auto input_handle = GetStdHandle(STD_INPUT_HANDLE);
		if (input_handle != INVALID_HANDLE_VALUE && input_handle != nullptr)
		{
			SetConsoleMode(input_handle, g_original_shell_input_mode);
		}

		g_shell_input_mode_active = false;
	}

	bool process_shell_input_line(const std::string& line)
	{
		if (line == "quit" || line == "exit")
		{
			return false;
		}

		if (!line.empty())
		{
			append_input_log_line(line);
			if (runtime::is_standalone_xport_mode())
			{
				if (!command::execute_local(line))
				{
					host_print("unknown standalone command");
				}
			}
			else
			{
				command::execute(line);
			}
		}

		return true;
	}

	bool read_shell_input_line(std::string& line, HANDLE engine_thread, bool& engine_terminated)
	{
		line.clear();
		engine_terminated = false;
		const auto input_handle = GetStdHandle(STD_INPUT_HANDLE);

		settle_shell_io();
		render_shell_prompt();

		while (true)
		{
			if (engine_thread)
			{
				const auto engine_wait = WaitForSingleObject(engine_thread, 0);
				if (engine_wait == WAIT_OBJECT_0)
				{
					engine_terminated = true;
					return false;
				}
			}

			if (input_handle == INVALID_HANDLE_VALUE || input_handle == nullptr)
			{
				Sleep(10);
				continue;
			}

			const auto input_wait = WaitForSingleObject(input_handle, 10);
			if (input_wait != WAIT_OBJECT_0)
			{
				continue;
			}

			wchar_t wide_buffer[512]{};
			DWORD chars_read = 0;
			if (!ReadConsoleW(input_handle, wide_buffer, static_cast<DWORD>(std::size(wide_buffer) - 1), &chars_read, nullptr))
			{
				continue;
			}

			if (chars_read == 0)
			{
				continue;
			}

			std::wstring wide_line(wide_buffer, chars_read);
			while (!wide_line.empty() && (wide_line.back() == L'\r' || wide_line.back() == L'\n'))
			{
				wide_line.pop_back();
			}

			if (!wide_line.empty())
			{
				const auto required = WideCharToMultiByte(CP_UTF8, 0, wide_line.c_str(), static_cast<int>(wide_line.size()), nullptr, 0, nullptr, nullptr);
				if (required > 0)
				{
					line.resize(required);
					WideCharToMultiByte(CP_UTF8, 0, wide_line.c_str(), static_cast<int>(wide_line.size()), line.data(), required, nullptr, nullptr);
				}
			}

			commit_shell_input_line(line);
			return true;
		}
	}

	bool run_shell_loop(HANDLE engine_thread = nullptr)
	{
		activate_shell_input_mode();
		const auto restore_input = gsl::finally([]()
		{
			restore_shell_input_mode();
		});

		std::string line;
		while (true)
		{
			if (engine_thread)
			{
				const auto engine_wait = WaitForSingleObject(engine_thread, 0);
				if (engine_wait == WAIT_OBJECT_0)
				{
					DWORD exit_code = 0;
					if (GetExitCodeThread(engine_thread, &exit_code))
					{
						host_print("engine thread exited with code " + std::to_string(exit_code));
					}
					return false;
				}
			}

			bool engine_terminated = false;
			if (!read_shell_input_line(line, engine_thread, engine_terminated))
			{
				if (engine_terminated)
				{
					DWORD exit_code = 0;
					if (engine_thread && GetExitCodeThread(engine_thread, &exit_code))
					{
						host_print("engine thread exited with code " + std::to_string(exit_code));
					}
					return false;
				}

				host_print("stdin closed unexpectedly");
				return false;
			}

			if (!process_shell_input_line(line))
			{
				return true;
			}

			settle_shell_io();
		}
	}

	int handle_module_load_exception(_EXCEPTION_POINTERS* exception_info)
	{
		g_module_load_exception_code = exception_info && exception_info->ExceptionRecord
			? exception_info->ExceptionRecord->ExceptionCode
			: 0;
		return EXCEPTION_EXECUTE_HANDLER;
	}

	HMODULE load_jb_mp_module_guarded()
	{
		g_module_load_exception_code = 0;
		__try
		{
			return LoadLibraryA("jb_mp_s.dll");
		}
		__except (handle_module_load_exception(GetExceptionInformation()))
		{
			return nullptr;
		}
	}
	template <typename Stub>
	void apply_detour(utils::hook::detour& detour, const std::uintptr_t address, Stub stub)
	{
		host_patch_print("[patch:detour] patching 0x" + hex_address(address) + "...");
		detour.create(game::game_offset(static_cast<unsigned int>(address)), stub);
	}

	void apply_jump(const std::uintptr_t from, const std::uintptr_t to)
	{
		host_patch_print("[patch:jump] 0x" + hex_address(from) + " -> 0x" + hex_address(to));
		utils::hook::jump(game::game_offset(static_cast<unsigned int>(from)), game::game_offset(static_cast<unsigned int>(to)));
	}

	void apply_nop(const std::uintptr_t address, const size_t size)
	{
		host_patch_print("[patch:nop] 0x" + hex_address(address) + " (" + std::to_string(size) + " bytes)");
		utils::hook::nop(game::game_offset(static_cast<unsigned int>(address)), size);
	}

	void apply_gfxconfig_callsite_patch()
	{
		host_patch_print("[patch:callsite] 0x103F9A80");
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
		if (runtime::is_standalone_xport_mode())
		{
			if (!key)
			{
				return "";
			}

			return key;
		}

		return g_localize_lookup_patch.invoke<const char*>(const_cast<char*>(key));
	}

	char __cdecl exec_config_filter_stub(char* name, int a2, int a3, int a4)
	{
		if (runtime::is_standalone_xport_mode())
		{
			if (!name || !*name)
			{
				host_print("skipping empty config exec in xport mode");
				return 0;
			}

			const auto lowered = lower_copy(name);
			if (lowered.find("consolation_mp.cfg") != std::string::npos
				|| lowered.find("config_mp.cfg") != std::string::npos)
			{
				host_print(std::string("skipping config exec in xport mode: ") + name);
				return 0;
			}
		}

		return g_exec_config_patch.invoke<char>(name, a2, a3, a4);
	}

	int __cdecl profile_config_skip_stub(int controller, const char* source)
	{
		(void)controller;
		(void)source;

		if (runtime::is_standalone_xport_mode())
		{
			if (!g_profile_config_skip_logged.exchange(true))
			{
				host_print("skipping config_mp.cfg bootstrap in standalone xport mode");
			}

			return 0;
		}

		return g_profile_config_patch.invoke<int>(controller, source);
	}

	int __cdecl invoke_original_fs_readfile(char* name, void** out_buffer, int flags)
	{
		auto* original = g_fs_readfile_patch.get_original();
		int result = -1;

		__asm
		{
			mov eax, name
			push flags
			push out_buffer
			call original
			add esp, 8
			mov result, eax
		}

		return result;
	}

	int __cdecl fs_readfile_filter_impl(char* name, void** out_buffer, int flags)
	{
		if (runtime::is_standalone_xport_mode() && (!name || !*name))
		{
			if (out_buffer)
			{
				*out_buffer = nullptr;
			}

			host_print("skipping empty FS_ReadFile request in xport mode");
			return -1;
		}

		return invoke_original_fs_readfile(name, out_buffer, flags);
	}

	__declspec(naked) void fs_readfile_filter_stub()
	{
		__asm
		{
			push dword ptr [esp + 8]
			push dword ptr [esp + 8]
			push eax
			call fs_readfile_filter_impl
			add esp, 12
			ret
		}
	}

	std::string lower_copy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});

		return value;
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
			g_engine_error_seen = true;
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

	void __cdecl init_popup_stub(char* message)
	{
		g_init_popup_seen = true;
		g_engine_error_seen = true;

		std::string text = message ? message : "<null>";
		{
			std::lock_guard _(runtime::get_output_mutex());
			write_console_line("[engine:init] " + text);
			append_log_line("[engine:init] " + text);
		}

		g_init_popup_hook.invoke<void>(message);
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
		host_section_print(make_section_banner(std::string("zones: ") + source_name));
		host_section_print(make_host_section_line("[host:zone_count]", "zone_count = " + std::to_string(zone_count)));
		host_section_print(make_host_section_line("[host:zone_sync]", "sync = " + std::to_string(sync ? 1 : 0)));
		for (int i = 0; i < zone_count; ++i)
		{
			host_section_print(make_host_section_line("[host:zones]", describe_zone_name(zone_info[i].name, trusted_names)));
		}
	}

	void log_file_load_refs()
	{
		host_section_print(make_section_banner("host refs"));
		host_section_print(make_host_section_line("[host:refs]", "FS_Startup = 0x10272D80"));
		host_section_print(make_host_section_line("[host:refs]", "ExecConfig = 0x103F5820"));
		host_section_print(make_host_section_line("[host:refs]", "Scr_ReadFile_FastFile = 0x1022DF13"));
		host_section_print(make_host_section_line("[host:refs]", "DB_LoadXAssets = 0x103E1CF0"));
	}

	void reinforce_engine_imports()
	{
		utils::hook::set<void*>(game::game_offset(0x104761C4), reinterpret_cast<void*>(::SwitchToThread));
		utils::hook::set<void*>(game::game_offset(0x10476344), reinterpret_cast<void*>(::timeGetTime));

		host_section_print(make_section_banner("host imports"));
		host_section_print(make_host_section_line("[host:imports]", "SwitchToThread = " + hex_address(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(game::game_offset(0x104761C4))))));
		host_section_print(make_host_section_line("[host:imports]", "timeGetTime = " + hex_address(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void**>(game::game_offset(0x10476344))))));
	}

	void perform_bootstrap_zone_load()
	{
		if (!runtime::is_standalone_xport_mode())
		{
			return;
		}

		if (!g_bootstrap_zone_redirected.exchange(true))
		{
			host_print("loading bootstrap zones");
			log_zone_load_request(zone_trace_manual, g_bootstrap_zones, static_cast<int>(std::size(g_bootstrap_zones)), 0);
			game::DB_LoadXAssets(g_bootstrap_zones, static_cast<int>(std::size(g_bootstrap_zones)), false);
			g_bootstrap_zones_ready = true;
		}
	}

	int __cdecl fs_startup_host_stub()
	{
		const auto result = g_fs_startup_patch.invoke<int>();

		if (runtime::is_standalone_xport_mode() && !g_bootstrap_zone_load_started.exchange(true))
		{
			host_print("FS startup reached; loading bootstrap zones");
			perform_bootstrap_zone_load();
		}

		return result;
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

		if (g_debug_wait_consumed.exchange(true))
		{
			return;
		}

		const auto message = std::string("[debug:wait] attach debugger now, or click enter to continue without debugging (") + stage + ")";
		host_section_print(message);
		while (!IsDebuggerPresent())
		{
			SetConsoleTitleA(build_info::get_window_title().c_str());
			if (_kbhit())
			{
				const auto key = _getch();
				if (key == '\r')
				{
					host_section_print("[debug:wait] continuing without debugger");
					return;
				}
			}
			Sleep(100);
		}

		host_section_print(std::string("[debug:wait] debugger attached (") + stage + ")");
	}

	LONG WINAPI host_exception_filter(_EXCEPTION_POINTERS* exception_info)
	{
		if (exception_info && exception_info->ExceptionRecord)
		{
			const auto code = exception_info->ExceptionRecord->ExceptionCode;
			const auto address = reinterpret_cast<std::uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress);
			append_log_line("[host] unhandled exception code=0x" + hex_address(code) + " address=0x" + hex_address(address));
		}
		else
		{
			append_log_line("[host] unhandled exception with no exception record");
		}

		return EXCEPTION_CONTINUE_SEARCH;
	}

	__declspec(naked) void bootstrap_zone_load_stub()
	{
		__asm
		{
			call should_debugbreak_bootstrap
			test al, al
			jz no_break
			int 3
		no_break:
			pushad
			call perform_bootstrap_zone_load
			popad
			xor eax, eax
			ret
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
		host_print("press Enter to close");
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

	int handle_engine_thread_exception(_EXCEPTION_POINTERS* exception_info)
	{
		if (exception_info && exception_info->ExceptionRecord)
		{
			const auto code = exception_info->ExceptionRecord->ExceptionCode;
			const auto address = reinterpret_cast<std::uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress);
			append_log_line("[host] engine thread exception code=0x" + hex_address(code) + " address=0x" + hex_address(address));

			if (exception_info->ContextRecord)
			{
				const auto* ctx = exception_info->ContextRecord;
				append_log_line(
					"[host] engine thread context "
					"EAX=0x" + hex_address(ctx->Eax) +
					" EBX=0x" + hex_address(ctx->Ebx) +
					" ECX=0x" + hex_address(ctx->Ecx) +
					" EDX=0x" + hex_address(ctx->Edx) +
					" ESI=0x" + hex_address(ctx->Esi) +
					" EDI=0x" + hex_address(ctx->Edi) +
					" EBP=0x" + hex_address(ctx->Ebp) +
					" ESP=0x" + hex_address(ctx->Esp) +
					" EIP=0x" + hex_address(ctx->Eip)
				);

				if (ctx->Esp && !IsBadReadPtr(reinterpret_cast<const void*>(ctx->Esp), sizeof(std::uint32_t)))
				{
					const auto return_address = *reinterpret_cast<const std::uint32_t*>(ctx->Esp);
					append_log_line("[host] engine thread return address = 0x" + hex_address(return_address));
				}
			}
		}
		else
		{
			append_log_line("[host] engine thread exception with no exception record");
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}

	DWORD WINAPI standalone_engine_thread(LPVOID parameter)
	{
		const auto entry = reinterpret_cast<start_main_mp_raw_t>(parameter);
		const auto switch_to_thread = reinterpret_cast<int>(::SwitchToThread);
		__try
		{
			return static_cast<DWORD>(call_start_main_mp(entry, 0, switch_to_thread, GetModuleHandleA(nullptr), 0, 0, 0, 0, 0));
		}
		__except (handle_engine_thread_exception(GetExceptionInformation()))
		{
			return 0xE0000002;
		}
	}

	void hide_xport_windows_loop()
	{
		while (!g_window_watch_kill.load())
		{
			SetConsoleTitleA(build_info::get_window_title().c_str());

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

	HMODULE prepare_standalone_host()
	{
		const auto& log_path = runtime::get_log_path();
		DeleteFileA(log_path.string().c_str());
		set_terminate(host_terminate_handler);
		_set_invalid_parameter_handler(host_invalid_parameter_handler);
		SetUnhandledExceptionFilter(host_exception_filter);
		SetConsoleTitleA(build_info::get_window_title().c_str());

		host_print("========== qos-xport initializing ==========");
		host_print("loading 'jb_mp_s.dll' ...");

		const auto module = load_jb_mp_module_guarded();

		if (!module)
		{
			if (g_module_load_exception_code.load() != 0)
			{
			fail_and_wait("exception while loading 'jb_mp_s.dll' (0x" + hex_address(g_module_load_exception_code.load()) + ")!");
				return nullptr;
			}

			fail_and_wait("failed to load 'jb_mp_s.dll' (" + std::to_string(GetLastError()) + ")!");
			return nullptr;
		}
		game::mp_dll = module;

		host_print("loaded 'jb_mp_s.dll'!");
		runtime::set_standalone_xport_mode(true);
		g_engine_error_seen = false;
		g_init_popup_seen = false;

		try
		{
			host_section_print(make_section_banner("patching detours"));
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
			apply_detour(g_profile_config_patch, 0x103F1A30, profile_config_skip_stub);
			apply_detour(g_fs_readfile_patch, 0x10271E40, fs_readfile_filter_stub);
			apply_detour(g_engine_printf_hook, 0x103F6400, engine_printf_stub);
			apply_detour(g_com_error_hook, 0x103F77B0, com_error_stub);
			apply_detour(g_init_popup_hook, 0x10245050, init_popup_stub);
			apply_detour(g_fs_startup_patch, 0x10272D80, fs_startup_host_stub);

			host_section_print(make_section_banner("patching jumps"));
			apply_jump(0x1024D8E9, 0x1024D909);
			apply_jump(0x103F7156, 0x103F7162);
			apply_jump(0x103F7162, 0x103F71A7);
			apply_jump(0x103F71A7, 0x103F721D);
			apply_jump(0x103F9BC1, 0x103F9BD7);
			apply_jump(0x103F9B5A, 0x103F9B85);

			host_section_print(make_section_banner("patching nops"));
			apply_nop(0x102489A1, 5);
			apply_nop(0x10245B68, 2);

			host_section_print(make_section_banner("patching callsites"));
			apply_gfxconfig_callsite_patch();
			host_patch_print("[patch:nop] 0x103F9A71 (5 bytes)");
			utils::hook::nop(game::game_offset(0x103F9A71), 5);
			host_patch_print("[patch:nop] 0x103F7665 (5 bytes)");
			utils::hook::nop(game::game_offset(0x103F7665), 5);
			host_patch_print("[patch:nop] 0x103209F8 (5 bytes)");
			utils::hook::nop(game::game_offset(0x103209F8), 5);
			reinforce_engine_imports();
			host_patch_print("[host:patch] all patches applied");
			log_file_load_refs();
		}
		catch (const std::exception& error)
		{
			fail_and_wait(std::string("early patch stage failed: ") + error.what());
			return nullptr;
		}
		catch (...)
		{
			fail_and_wait("early patch stage failed with unknown exception");
			return nullptr;
		}

		return module;
	}

	int run_minimal_standalone_mode()
	{
		const auto module = prepare_standalone_host();
		if (!module)
		{
			return 1;
		}

		(void)module;
		wait_for_debugger_if_requested("pre-runtime");
		host_print("using minimal standalone bootstrap");

		if (!runtime::initialize(true))
		{
			return fail_and_wait("runtime initialization failed");
		}

		wait_for_debugger_if_requested("post-runtime");

		print_init_complete_banner();
		run_shell_loop();

		runtime::shutdown();
		return 0;
	}

	int run_legacy_startmp_mode()
	{
		const auto module = prepare_standalone_host();
		if (!module)
		{
			return 1;
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
				host_print("bootstrap zones not confirmed yet; continuing with manual bootstrap state");
				g_bootstrap_zones_ready = true;
				break;
			}
		}

		if (g_engine_error_seen.load() || g_init_popup_seen.load())
		{
			g_window_watch_kill = true;
			if (g_window_watch_thread.joinable())
			{
				g_window_watch_thread.join();
			}
			return fail_and_wait("engine reported initialization error before runtime init");
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

		const auto ready_wait_start = GetTickCount64();
		while ((GetTickCount64() - ready_wait_start) < 5000)
		{
			if (g_engine_error_seen.load() || g_init_popup_seen.load())
			{
				g_window_watch_kill = true;
				if (g_window_watch_thread.joinable())
				{
					g_window_watch_thread.join();
				}
				return fail_and_wait("engine reported initialization error before ready state");
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
					return fail_and_wait("engine thread exited before initialization completed (" + std::to_string(exit_code) + ")");
				}

				g_window_watch_kill = true;
				if (g_window_watch_thread.joinable())
				{
					g_window_watch_thread.join();
				}
				return fail_and_wait("engine thread exited before initialization completed");
			}

		}

		if (g_bootstrap_zones_ready.load() && !g_engine_error_seen.load() && !g_init_popup_seen.load())
		{
			print_init_complete_banner();
		}
		run_shell_loop(thread);

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
	bool use_legacy_startmp = false;
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
			if (arg == L"--legacy-startmp")
			{
				use_legacy_startmp = true;
			}
			if (arg == L"-debug_env")
			{
				g_debugbreak_bootstrap = true;
			}
		}
	}

	if (use_legacy_startmp)
	{
		return run_legacy_startmp_mode();
	}

	return run_minimal_standalone_mode();
}



