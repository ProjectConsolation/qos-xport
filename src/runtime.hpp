#pragma once

#include <filesystem>
#include <string>
#include <mutex>

namespace runtime
{
	void set_standalone_xport_mode(bool enabled);
	bool is_standalone_xport_mode();
	bool initialize(bool report_errors);
	void shutdown();
	bool is_initialized();
	const std::filesystem::path& get_log_path();
	void append_log_line(const std::string& line);
	std::mutex& get_output_mutex();
}
