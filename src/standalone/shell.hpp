#pragma once

#include <cstddef>
#include <string>

namespace standalone::shell
{
	const char* prompt_text();

	void append_log_line(const std::string& line);
	void append_input_log_line(const std::string& line);
	std::string format_submitted_input_line(const std::string& line);

	void host_print(const std::string& message);
	void host_patch_print(const std::string& message);
	void host_section_print(const std::string& message);

	std::string make_section_banner(const std::string& title);
	void write_console_line(const std::string& line);
	void write_shell_line(const std::string& line);
	void render_shell_prompt();
	void commit_shell_input_line(const std::string& line);
	void render_shell_input_line(const std::string& line, size_t previous_length);
	void begin_shell_input();
	bool handle_shell_input_record(const INPUT_RECORD& record, std::string& completed_line);
	void clear_console_display();
	void settle_shell_io();
	void flush_shell_input_buffer();

	std::string make_host_section_line(const std::string& label, const std::string& message);
}



