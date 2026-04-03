#include <std_include.hpp>
#include <Windows.h>

#include "runtime.hpp"

extern "C" __declspec(dllexport) BOOL qos_xport_init()
{
	return runtime::initialize(true) ? TRUE : FALSE;
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
		runtime::initialize(true);
#endif
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
#if defined(QOS_XPORT_AUTO_INIT)
		runtime::shutdown();
#endif
	}

	return 1;
}

