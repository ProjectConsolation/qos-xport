#include <std_include.hpp>

#include "shell.hpp"
#include "../runtime.hpp"

#include <algorithm>
#include <cstring>

namespace standalone::shell
{
	namespace
	{
		constexpr auto k_shell_prompt = "QoS-xport: ";
		constexpr auto k_prompt_name = "QoS-xport";
		constexpr auto k_prompt_suffix = ": ";

		std::string lower_copy(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});

			return value;
		}
	}

	const char* prompt_text()
	{
		return k_shell_prompt;
	}

	void append_log_line(const std::string& line)
	{
		runtime::append_log_line(line);
	}

	void append_input_log_line(const std::string& line)
	{
		append_log_line(std::string(k_shell_prompt) + line);
	}

	std::string make_host_section_line(const std::string& label, const std::string& message)
	{
		constexpr size_t target_prefix_width = 19;
		auto prefix = label;
		if (prefix.size() < target_prefix_width)
		{
			prefix.append(target_prefix_width - prefix.size(), ' ');
		}

		return prefix + message;
	}

	void write_console_line(const std::string& line)
	{
		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				CONSOLE_SCREEN_BUFFER_INFO info{};
				WORD original_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
				if (GetConsoleScreenBufferInfo(handle, &info))
				{
					original_attributes = info.wAttributes;
				}

				WORD color = original_attributes;
				const auto lowered = lower_copy(line);

				if (line.rfind("[engine:error]", 0) == 0)
				{
					color = FOREGROUND_RED | FOREGROUND_INTENSITY;
				}
				else if (line.rfind("[engine:init]", 0) == 0)
				{
					color = FOREGROUND_GREEN;
				}
				else if (line.rfind("[debug:wait]", 0) == 0 || lowered.find("warning") != std::string::npos)
				{
					color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				}
				else if (line.rfind("[QoS-xport] =========== initialization complete =============", 0) == 0)
				{
					color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				}
				else if (line.rfind("[host:patch]", 0) == 0 || line.rfind("[patch:", 0) == 0)
				{
					color = FOREGROUND_BLUE | FOREGROUND_GREEN;
				}
				else if (line.rfind("[host:", 0) == 0)
				{
					color = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				}
				else if (line.rfind("[QoS-xport]", 0) == 0)
				{
					color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
				}

				if (lowered.find("succeeded") != std::string::npos
					|| lowered.find("loaded") != std::string::npos
					|| lowered.find("all patches applied") != std::string::npos)
				{
					color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				}

				const auto with_newline = line + "\r\n";
				DWORD written = 0;
				SetConsoleTextAttribute(handle, color);
				WriteConsoleA(handle, with_newline.data(), static_cast<DWORD>(with_newline.size()), &written, nullptr);
				SetConsoleTextAttribute(handle, original_attributes);
				return;
			}
		}

		std::fwrite(line.data(), 1, line.size(), stdout);
		std::fwrite("\n", 1, 1, stdout);
		std::fflush(stdout);
	}

	void host_print(const std::string& message)
	{
		std::lock_guard _(runtime::get_output_mutex());
		write_console_line("[QoS-xport] " + message);
		append_log_line("[host] " + message);
	}

	void host_patch_print(const std::string& message)
	{
		std::lock_guard _(runtime::get_output_mutex());
		write_console_line(message);
		append_log_line(message);
	}

	void host_section_print(const std::string& message)
	{
		std::lock_guard _(runtime::get_output_mutex());
		write_console_line(message);
		append_log_line(message);
	}

	void write_shell_line(const std::string& line)
	{
		std::lock_guard _(runtime::get_output_mutex());

		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				const auto with_newline = line + "\r\n";
				DWORD written = 0;
				WriteConsoleA(handle, with_newline.data(), static_cast<DWORD>(with_newline.size()), &written, nullptr);
				return;
			}
		}

		std::fwrite(line.data(), 1, line.size(), stdout);
		std::fwrite("\n", 1, 1, stdout);
		std::fflush(stdout);
	}

	void render_shell_input_line(const std::string& line, size_t previous_length)
	{
		std::lock_guard _(runtime::get_output_mutex());

		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				CONSOLE_SCREEN_BUFFER_INFO info{};
				WORD original_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
				if (GetConsoleScreenBufferInfo(handle, &info))
				{
					original_attributes = info.wAttributes;
				}

				const COORD line_start{ 0, info.dwCursorPosition.Y };
				DWORD written = 0;
				FillConsoleOutputCharacterA(handle, ' ', info.dwSize.X, line_start, &written);
				FillConsoleOutputAttribute(handle, original_attributes, info.dwSize.X, line_start, &written);
				SetConsoleCursorPosition(handle, line_start);

				SetConsoleTextAttribute(handle, FOREGROUND_RED);
				WriteConsoleA(handle, k_prompt_name, static_cast<DWORD>(std::strlen(k_prompt_name)), &written, nullptr);

				SetConsoleTextAttribute(handle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
				WriteConsoleA(handle, k_prompt_suffix, static_cast<DWORD>(std::strlen(k_prompt_suffix)), &written, nullptr);

				SetConsoleTextAttribute(handle, original_attributes);
				if (!line.empty())
				{
					WriteConsoleA(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
				}

				if (previous_length > line.size())
				{
					const std::string padding(previous_length - line.size(), ' ');
					WriteConsoleA(handle, padding.data(), static_cast<DWORD>(padding.size()), &written, nullptr);
				}

				const COORD cursor{
					static_cast<SHORT>(std::strlen(k_prompt_name) + std::strlen(k_prompt_suffix) + line.size()),
					line_start.Y
				};
				SetConsoleCursorPosition(handle, cursor);
				return;
			}
		}

		std::fwrite("\r", 1, 1, stdout);
		std::fwrite(k_shell_prompt, 1, std::strlen(k_shell_prompt), stdout);
		std::fwrite(line.data(), 1, line.size(), stdout);
		if (previous_length > line.size())
		{
			const std::string padding(previous_length - line.size(), ' ');
			std::fwrite(padding.data(), 1, padding.size(), stdout);
		}
		std::fflush(stdout);
	}

	void clear_console_display()
	{
		std::lock_guard _(runtime::get_output_mutex());

		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
		{
			return;
		}

		CONSOLE_SCREEN_BUFFER_INFO info{};
		if (!GetConsoleScreenBufferInfo(handle, &info))
		{
			return;
		}

		const COORD home{ 0, 0 };
		const auto cell_count = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
		DWORD written = 0;
		FillConsoleOutputCharacterA(handle, ' ', cell_count, home, &written);
		FillConsoleOutputAttribute(handle, info.wAttributes, cell_count, home, &written);
		SetConsoleCursorPosition(handle, home);
	}

	void settle_shell_io()
	{
		Sleep(35);
	}

	void flush_shell_input_buffer()
	{
		if (const auto input_handle = GetStdHandle(STD_INPUT_HANDLE);
			input_handle != INVALID_HANDLE_VALUE && input_handle != nullptr)
		{
			FlushConsoleInputBuffer(input_handle);
		}
	}
}
