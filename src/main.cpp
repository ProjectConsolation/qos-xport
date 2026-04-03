#include <std_include.hpp>
#include <Windows.h>

#include "runtime.hpp"

namespace
{
	DWORD WINAPI async_runtime_init(LPVOID)
	{
		// Give the MP host a moment to finish bootstrapping before we start
		// installing hooks and components from the export runtime.
		Sleep(5000);
		return runtime::initialize(true) ? TRUE : FALSE;
	}
}

extern "C" __declspec(dllexport) BOOL qos_xport_init()
{
	return runtime::initialize(true) ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) DWORD WINAPI qos_xport_remote_init(LPVOID)
{
	return qos_xport_init();
}

extern "C" __declspec(dllexport) void qos_xport_shutdown()
{
	runtime::shutdown();
}

extern "C" __declspec(dllexport) BOOL qos_xport_is_initialized()
{
	return runtime::is_initialized() ? TRUE : FALSE;
}

int WINAPI DllMain(HINSTANCE instance, const DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(instance);

#if defined(QOS_XPORT_AUTO_INIT)
		if (const auto thread = CreateThread(nullptr, 0, async_runtime_init, nullptr, 0, nullptr))
		{
			CloseHandle(thread);
		}
#endif
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		runtime::shutdown();
	}

	return 1;
}

