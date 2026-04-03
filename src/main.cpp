#include <std_include.hpp>
#include <Windows.h>

#include "runtime.hpp"

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
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		runtime::shutdown();
	}

	return 1;
}

