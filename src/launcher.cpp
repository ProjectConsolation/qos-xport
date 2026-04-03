#include <std_include.hpp>

#include <TlHelp32.h>

namespace
{
	using nt_create_thread_ex_t = LONG(NTAPI*)(
		PHANDLE thread_handle,
		ACCESS_MASK desired_access,
		LPVOID object_attributes,
		HANDLE process_handle,
		LPTHREAD_START_ROUTINE start_routine,
		LPVOID argument,
		ULONG create_flags,
		SIZE_T zero_bits,
		SIZE_T stack_size,
		SIZE_T maximum_stack_size,
		LPVOID attribute_list
	);

	bool g_no_overwrite = false;

	std::filesystem::path get_launcher_log_path()
	{
		char path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		auto base = std::filesystem::path(path).parent_path() / "qos-xport";
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		return base / "launcher.log";
	}

	void append_launcher_log(const std::string& message)
	{
		const auto line = "[QoS-xport]: " + message + "\r\n";
		OutputDebugStringA(line.c_str());

		const auto path = get_launcher_log_path();
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

	void launcher_print(const std::string& message)
	{
		std::printf("[QoS-xport]: %s\n", message.c_str());
		std::fflush(stdout);
		append_launcher_log(message);
	}

	void launcher_debug(const std::string& message)
	{
		launcher_print(message);
	}

	int fail_and_wait(const char* message)
	{
		std::fprintf(stderr, "[QoS-xport]: %s", message);
		std::fprintf(stderr, "\nPress Enter to close...\n");
		std::fflush(stderr);
		append_launcher_log(message);
		std::getchar();
		return 1;
	}

	int fail_and_wait(const std::string& message)
	{
		return fail_and_wait(message.c_str());
	}

	void wait_before_exit(const char* message = "Press Enter to close...")
	{
		launcher_print(message);
		std::getchar();
	}

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
		std::vector<std::string> host_args{ "-multiplayer" };
		std::string wait_module = "jb_mp_s.dll";
		bool pause_on_success = false;
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

			if (argument == "--pause")
			{
				options.pause_on_success = true;
				continue;
			}

			if (argument == "--no_overwrite")
			{
				g_no_overwrite = true;
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

	uintptr_t inject_dll(const HANDLE process, const std::string& dll_path)
	{
		auto* remote_path = VirtualAllocEx(process, nullptr, dll_path.size() + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (!remote_path)
		{
			return 0;
		}

		const auto free_remote_path = gsl::finally([&]()
		{
			VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
		});

		if (!WriteProcessMemory(process, remote_path, dll_path.data(), dll_path.size() + 1, nullptr))
		{
			return 0;
		}

		auto* kernel32 = GetModuleHandleA("kernel32.dll");
		if (!kernel32)
		{
			return 0;
		}

		auto* load_library = GetProcAddress(kernel32, "LoadLibraryA");
		if (!load_library)
		{
			return 0;
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
			return 0;
		}

		const auto close_thread = gsl::finally([&]()
		{
			CloseHandle(thread);
		});

		if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0)
		{
			return 0;
		}

		DWORD exit_code = 0;
		if (!GetExitCodeThread(thread, &exit_code))
		{
			return 0;
		}

		return static_cast<uintptr_t>(exit_code);
	}

	uintptr_t get_remote_export_address(const std::string& dll_path, const uintptr_t remote_base, const char* export_name)
	{
		const auto local_module = LoadLibraryExA(dll_path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
		if (!local_module)
		{
			launcher_debug("LoadLibraryExA failed for export scan (" + std::to_string(GetLastError()) + ")");
			return 0;
		}

		const auto free_module = gsl::finally([&]()
		{
			FreeLibrary(local_module);
		});

		uintptr_t local_export = reinterpret_cast<uintptr_t>(GetProcAddress(local_module, export_name));
		if (!local_export)
		{
			launcher_debug(std::string("GetProcAddress failed for '") + export_name + "', trying stdcall decoration");
			const auto decorated_name = std::string("_") + export_name + "@4";
			local_export = reinterpret_cast<uintptr_t>(GetProcAddress(local_module, decorated_name.c_str()));
			if (!local_export)
			{
				launcher_debug(std::string("GetProcAddress also failed for '") + decorated_name + "'");
				return 0;
			}
		}

		const auto local_base = reinterpret_cast<uintptr_t>(local_module);
		return remote_base + (local_export - local_base);
	}

	bool call_remote_init(const HANDLE process, const std::string& dll_path, const uintptr_t remote_base)
	{
		const auto remote_init = get_remote_export_address(dll_path, remote_base, "qos_xport_remote_init");
		if (!remote_init)
		{
			launcher_debug("failed to resolve remote export qos_xport_remote_init");
			return false;
		}
		launcher_debug("resolved qos_xport_remote_init at 0x" + std::to_string(remote_init));

		HANDLE thread = CreateRemoteThread(
			process,
			nullptr,
			0,
			reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_init),
			nullptr,
			0,
			nullptr
		);
		if (!thread && GetLastError() == ERROR_ACCESS_DENIED)
		{
			launcher_debug("CreateRemoteThread returned ACCESS_DENIED, trying NtCreateThreadEx");

			const auto ntdll = GetModuleHandleA("ntdll.dll");
			const auto nt_create_thread_ex = ntdll
				? reinterpret_cast<nt_create_thread_ex_t>(GetProcAddress(ntdll, "NtCreateThreadEx"))
				: nullptr;

			if (!nt_create_thread_ex)
			{
				launcher_debug("NtCreateThreadEx is unavailable");
			}
			else
			{
				const auto status = nt_create_thread_ex(
					&thread,
					THREAD_ALL_ACCESS,
					nullptr,
					process,
					reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_init),
					nullptr,
					0,
					0,
					0,
					0,
					nullptr
				);

				if (status < 0 || !thread)
				{
					launcher_debug("NtCreateThreadEx failed with status " + std::to_string(status));
				}
				else
				{
					launcher_debug("NtCreateThreadEx succeeded");
				}
			}
		}

		if (!thread)
		{
			launcher_debug("CreateRemoteThread for runtime init failed (" + std::to_string(GetLastError()) + ")");
			return false;
		}

		const auto close_thread = gsl::finally([&]()
		{
			CloseHandle(thread);
		});

		if (WaitForSingleObject(thread, 15000) != WAIT_OBJECT_0)
		{
			launcher_debug("wait for runtime init thread failed/timed out (" + std::to_string(GetLastError()) + ")");
			return false;
		}

		DWORD exit_code = 0;
		if (!GetExitCodeThread(thread, &exit_code))
		{
			launcher_debug("GetExitCodeThread for runtime init failed (" + std::to_string(GetLastError()) + ")");
			return false;
		}

		launcher_print("remote init thread exited with code " + std::to_string(exit_code));
		return exit_code == TRUE;
	}

	HANDLE create_kill_on_close_job()
	{
		const auto job = CreateJobObjectA(nullptr, nullptr);
		if (!job)
		{
			return nullptr;
		}

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
		info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

		if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info)))
		{
			CloseHandle(job);
			return nullptr;
		}

		return job;
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
	const auto log_path = get_launcher_log_path();
	if (!g_no_overwrite)
	{
		DeleteFileA(log_path.string().c_str());
	}

	append_launcher_log("========== launcher start ==========");
	append_launcher_log("launcher cwd is " + std::filesystem::current_path().string());
	const auto options = parse_command_line();
	const auto base_directory = get_self_directory();
	const auto host_path = std::filesystem::path(options.host_path).is_absolute()
		? options.host_path
		: (std::filesystem::path(base_directory) / options.host_path).string();
	const auto runtime_path = (std::filesystem::path(base_directory) / "xport.dll").string();
	append_launcher_log("host path is " + host_path);
	append_launcher_log("runtime path is " + runtime_path);

	if (!std::filesystem::exists(host_path))
	{
		return fail_and_wait("QoS-xport: host executable not found: " + host_path);
	}

	if (!std::filesystem::exists(runtime_path))
	{
		return fail_and_wait("QoS-xport: runtime DLL not found: " + runtime_path);
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
		return fail_and_wait("QoS-xport: failed to launch host (" + std::to_string(GetLastError()) + ")");
	}

	const auto job = create_kill_on_close_job();
	if (!job)
	{
		TerminateProcess(process_info.hProcess, 1);
		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);
		return fail_and_wait("QoS-xport: failed to create process job");
	}

	if (!AssignProcessToJobObject(job, process_info.hProcess))
	{
		const auto error = GetLastError();
		CloseHandle(job);
		TerminateProcess(process_info.hProcess, 1);
		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);
		return fail_and_wait("QoS-xport: failed to assign host to job (" + std::to_string(error) + ")");
	}

	const auto close_process_handles = gsl::finally([&]()
	{
		CloseHandle(job);
		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);
	});

	launcher_print("launched host PID " + std::to_string(process_info.dwProcessId));
	launcher_print("waiting for " + options.wait_module + "...");

	if (!wait_for_remote_module(process_info.dwProcessId, options.wait_module, 60s))
	{
		return fail_and_wait("QoS-xport: timed out waiting for " + options.wait_module);
	}

	launcher_print("injecting " + runtime_path);
	const auto remote_module = inject_dll(process_info.hProcess, runtime_path);
	if (!remote_module)
	{
		return fail_and_wait("failed to inject runtime DLL");
	}

	launcher_print("runtime DLL loaded at 0x" + std::to_string(remote_module));
	Sleep(1000);
	launcher_print("initializing runtime...");
	if (!call_remote_init(process_info.hProcess, runtime_path, remote_module))
	{
		return fail_and_wait("failed to initialize runtime");
	}

	launcher_print("runtime initialized successfully");
	if (options.pause_on_success)
	{
		wait_before_exit();
		return 0;
	}

	launcher_print("waiting for host process to exit...");
	WaitForSingleObject(process_info.hProcess, INFINITE);

	DWORD exit_code = 0;
	if (GetExitCodeProcess(process_info.hProcess, &exit_code))
	{
		launcher_print("host process exited with code " + std::to_string(exit_code));
	}

	wait_before_exit("host exited. Press Enter to close...");

	return 0;
}
