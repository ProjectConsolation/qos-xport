#pragma once

namespace runtime
{
	bool initialize(bool report_errors);
	void shutdown();
	bool is_initialized();
}
