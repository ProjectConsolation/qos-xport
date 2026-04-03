#include <std_include.hpp>

#include <TlHelp32.h>

namespace
{
	std::string narrow_string(const std::wstring& value)
	{
		if (value.empty())
		{
			return {};
		}

		const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		std::string result(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
		return result;
	}

	struct launcher_options
	{
		std::string host_path = "JB_LiveEngine_s.exe";
		std::vector<std::string> host_args{};
		std::string wait_module = "jb_mp_s.dll";
	};

	std::string quote_argument(const std::string& value)
	{
		if (value.find_first_of(" \t\"") == std::string::npos)
		{
			return value;
		}

		std::string escaped = "\"";
		for (const auto ch : value)
		{
			if (ch == '"')
			{
				escaped += '\\';
			}

			escaped += ch;
		}

		escaped += '"';
		return escaped;
	}

	std::string get_self_directory()
	{
		char path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		return std::filesystem::path(path).parent_path().string();
	}

	launcher_options parse_command_line()
	{
		launcher_options options;

		int argc = 0;
		auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		if (!argv)
		{
			return options;
		}

		const auto free_argv = gsl::finally([&]()
		{
			LocalFree(argv);
		});

		for (int i = 1; i < argc; ++i)
		{
			const auto argument = narrow_string(argv[i]);

			if (argument == "--host" && (i + 1) < argc)
			{
				options.host_path = narrow_string(argv[++i]);
				continue;
			}

			if (argument == "--wait-module" && (i + 1) < argc)
			{
				options.wait_module = narrow_string(argv[++i]);
				continue;
			}

			options.host_args.push_back(argument);
		}

		return options;
	}

	bool remote_module_loaded(const DWORD process_id, const std::string& module_name)
	{
		const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		const auto close_snapshot = gsl::finally([&]()
		{
			CloseHandle(snapshot);
		});

		MODULEENTRY32 module_entry{};
		module_entry.dwSize = sizeof(module_entry);

		if (!Module32First(snapshot, &module_entry))
		{
			return false;
		}

		do
		{
			if (!_stricmp(module_entry.szModule, module_name.data()))
			{
				return true;
			}
		}
		while (Module32Next(snapshot, &module_entry));

		return false;
	}

	bool wait_for_remote_module(const DWORD process_id, const std::string& module_name, const std::chrono::milliseconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (remote_module_loaded(process_id, module_name))
			{
				return true;
			}

			Sleep(100);
		}

		return false;
	}

	bool inject_dll(const HANDLE process, const std::string& dll_path)
	{
		auto* remote_path = VirtualAllocEx(process, nullptr, dll_path.size() + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!remote_path)
		{
			return false;
		}

		const auto free_remote_path = gsl::finally([&]()
		{
			VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
		});

		if (!WriteProcessMemory(process, remote_path, dll_path.data(), dll_path.size() + 1, nullptr))
		{
			return false;
		}

		auto* kernel32 = GetModuleHandleA("kernel32.dll");
		if (!kernel32)
		{
			return false;
		}

		auto* load_library = GetProcAddress(kernel32, "LoadLibraryA");
		if (!load_library)
		{
			return false;
		}

		const auto thread = CreateRemoteThread(
			process,
			nullptr,
			0,
			reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library),
			remote_path,
			0,
			nullptr
		);
		if (!thread)
		{
			return false;
		}

		const auto close_thread = gsl::finally([&]()
		{
			CloseHandle(thread);
		});

		if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0)
		{
			return false;
		}

		DWORD exit_code = 0;
		if (!GetExitCodeThread(thread, &exit_code))
		{
			return false;
		}

		return exit_code != 0;
	}

	std::string build_host_command_line(const launcher_options& options)
	{
		std::string command_line = quote_argument(options.host_path);
		for (const auto& argument : options.host_args)
		{
			command_line += " ";
			command_line += quote_argument(argument);
		}

		return command_line;
	}
}

int main()
{
	const auto options = parse_command_line();
	const auto base_directory = get_self_directory();
	const auto host_path = std::filesystem::path(options.host_path).is_absolute()
		? options.host_path
		: (std::filesystem::path(base_directory) / options.host_path).string();
	const auto runtime_path = (std::filesystem::path(base_directory) / "qos-xport.dll").string();

	if (!std::filesystem::exists(host_path))
	{
		std::fprintf(stderr, "QoS-xport: host executable not found: %s\n", host_path.c_str());
		return 1;
	}

	if (!std::filesystem::exists(runtime_path))
	{
		std::fprintf(stderr, "QoS-xport: runtime DLL not found: %s\n", runtime_path.c_str());
		return 1;
	}

	auto command_line = build_host_command_line({
		host_path,
		options.host_args,
		options.wait_module
	});

	STARTUPINFOA startup_info{};
	startup_info.cb = sizeof(startup_info);

	PROCESS_INFORMATION process_info{};
	if (!CreateProcessA(
		host_path.c_str(),
		command_line.data(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		base_directory.c_str(),
		&startup_info,
		&process_info))
	{
		std::fprintf(stderr, "QoS-xport: failed to launch host (%lu)\n", GetLastError());
		return 1;
	}

	const auto close_process_handles = gsl::finally([&]()
	{
		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);
	});

	std::printf("QoS-xport: launched host PID %lu\n", process_info.dwProcessId);
	std::printf("QoS-xport: waiting for %s...\n", options.wait_module.c_str());

	if (!wait_for_remote_module(process_info.dwProcessId, options.wait_module, 60s))
	{
		std::fprintf(stderr, "QoS-xport: timed out waiting for %s\n", options.wait_module.c_str());
		return 1;
	}

	std::printf("QoS-xport: injecting %s\n", runtime_path.c_str());
	if (!inject_dll(process_info.hProcess, runtime_path))
	{
		std::fprintf(stderr, "QoS-xport: failed to inject runtime DLL\n");
		return 1;
	}

	std::printf("QoS-xport: runtime injected successfully\n");
	return 0;
}
