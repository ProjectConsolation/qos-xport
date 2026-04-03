#include <std_include.hpp>

#include "launcher.hpp"
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
	std::atomic_bool g_window_watch_kill = false;
	std::atomic_bool g_window_hidden_logged = false;
	std::thread g_window_watch_thread;

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

	std::filesystem::path get_host_log_path()
	{
		char path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		auto base = std::filesystem::path(path).parent_path() / "qos-xport";
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		return base / "launcher.log";
	}

	void append_host_log(const std::string& message)
	{
		const auto line = "[host] " + message + "\r\n";
		OutputDebugStringA(line.c_str());

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
		WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &bytes_written, nullptr);
	}

	void host_print(const std::string& message)
	{
		std::printf("[QoS-xport]: %s\n", message.c_str());
		std::fflush(stdout);
		append_host_log(message);
	}

	void __cdecl host_terminate_handler()
	{
		append_host_log("CRT terminate handler invoked");
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

		append_host_log(message);
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

		host_print("========== standalone host start ==========");

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
			host_print("applying early xlive/profile/render bypass patches");
			host_print("patch detour 0x10240B30");
			g_xlive_patch_a.create(game::game_offset(0x10240B30), xlive_ret_one_stub);
			host_print("patch detour 0x10240B30 done");
			host_print("patch detour 0x10240A30");
			g_xlive_patch_b.create(game::game_offset(0x10240A30), xlive_ret_one_stub);
			host_print("patch detour 0x10240A30 done");
			host_print("patch jump 0x1024D8E9 -> 0x1024D909");
			utils::hook::jump(game::game_offset(0x1024D8E9), game::game_offset(0x1024D909));
			host_print("patch jump 0x1024D8E9 done");
			host_print("patch nop 0x102489A1");
			utils::hook::nop(game::game_offset(0x102489A1), 5);
			host_print("patch nop 0x102489A1 done");
			host_print("patch detour 0x102C3280");
			g_splash_patch.create(game::game_offset(0x102C3280), ret_success_stub);
			host_print("patch detour 0x102C3280 done");
			host_print("patch detour 0x10243EE0");
			g_xnaddr_patch.create(game::game_offset(0x10243EE0), xnaddr_success_stub);
			host_print("patch detour 0x10243EE0 done");
			host_print("patch detour 0x10244890");
			g_net_init_patch.create(game::game_offset(0x10244890), net_init_skip_stub);
			host_print("patch detour 0x10244890 done");
			host_print("early patches applied");
		}
		catch (const std::exception& error)
		{
			return fail_and_wait(std::string("early patch stage failed: ") + error.what());
		}
		catch (...)
		{
			return fail_and_wait("early patch stage failed with unknown exception");
		}

		const auto start_main_mp = GetProcAddress(module, "startMainMP");
		if (!start_main_mp)
		{
			return fail_and_wait("failed to resolve startMainMP export");
		}

		host_print("resolved startMainMP export");

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
		Sleep(5000);

		host_print("initializing runtime in-process");
		if (!runtime::initialize(true))
		{
			return fail_and_wait("runtime initialization failed");
		}

		host_print("runtime initialized successfully");
		host_print("enter commands, or type 'quit' to exit");

		std::string line;
		while (std::getline(std::cin, line))
		{
			if (line == "quit" || line == "exit")
			{
				break;
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

		DWORD exit_code = 0;
		if (GetExitCodeThread(thread, &exit_code))
		{
			host_print("engine thread exited with code " + std::to_string(exit_code));
		}

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
		}
	}

	return run_standalone_mode();
}
