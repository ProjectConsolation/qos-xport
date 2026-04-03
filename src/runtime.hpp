#pragma once

namespace runtime
{
	void set_standalone_xport_mode(bool enabled);
	bool is_standalone_xport_mode();
	bool initialize(bool report_errors);
	void shutdown();
	bool is_initialized();
}
