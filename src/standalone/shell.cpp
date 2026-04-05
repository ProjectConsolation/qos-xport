#include <std_include.hpp>

#include "shell.hpp"
#include "standalone/runtime.hpp"

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

		void write_console_text(HANDLE handle, const std::string& text)
		{
			if (text.empty())
			{
				return;
			}

			DWORD written = 0;
			WriteConsoleA(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
		}

		void set_console_color(HANDLE handle, WORD color)
		{
			SetConsoleTextAttribute(handle, color);
		}

		WORD get_tag_color(const std::string& tag, const WORD original_attributes)
		{
			if (tag == "QoS-xport")
			{
				return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
			}
			if (tag == "engine:error")
			{
				return FOREGROUND_RED | FOREGROUND_INTENSITY;
			}
			if (tag == "engine:init")
			{
				return FOREGROUND_GREEN;
			}
			if (tag == "debug:wait")
			{
				return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			}
			if (tag.rfind("patch:", 0) == 0)
			{
				return FOREGROUND_BLUE | FOREGROUND_GREEN;
			}
			if (tag.rfind("host:", 0) == 0)
			{
				return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			}
			if (tag.rfind("runtime - ", 0) == 0)
			{
				return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
			}
			if (tag == "engine")
			{
				return FOREGROUND_GREEN | FOREGROUND_BLUE;
			}

			return original_attributes;
		}

		void write_tagged_line(HANDLE handle, const std::string& line, const WORD original_attributes)
		{
			const auto close = line.find(']');
			const auto tag = line.substr(1, close - 1);
			const auto body = close == std::string::npos ? std::string{} : line.substr(close + 1);
			const auto tag_color = get_tag_color(tag, original_attributes);
			const auto lowered = lower_copy(body);
			const auto quote_start = body.find('\'');
			const auto quote_end = quote_start == std::string::npos ? std::string::npos : body.find('\'', quote_start + 1);

			set_console_color(handle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
			write_console_text(handle, "[");
			set_console_color(handle, tag_color);
			write_console_text(handle, tag);
			set_console_color(handle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
			write_console_text(handle, "]");

			if (body.empty())
			{
				return;
			}

			if (tag == "QoS-xport" && body.find("===") != std::string::npos)
			{
				set_console_color(handle, tag_color);
				write_console_text(handle, body);
				return;
			}

			if (quote_start != std::string::npos && quote_end != std::string::npos && quote_end > quote_start)
			{
				WORD quoted_color = original_attributes;
				if (lowered.find("loaded ") != std::string::npos || lowered.find("succeeded") != std::string::npos)
				{
					quoted_color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				}
				else if (lowered.find("loading ") != std::string::npos)
				{
					quoted_color = tag_color;
				}
				else if (lowered.find("failed") != std::string::npos || lowered.find("error") != std::string::npos)
				{
					quoted_color = FOREGROUND_RED | FOREGROUND_INTENSITY;
				}

				set_console_color(handle, original_attributes);
				write_console_text(handle, body.substr(0, quote_start + 1));
				set_console_color(handle, quoted_color);
				write_console_text(handle, body.substr(quote_start + 1, quote_end - quote_start - 1));
				set_console_color(handle, original_attributes);
				write_console_text(handle, body.substr(quote_end));
				return;
			}

			set_console_color(handle, original_attributes);
			write_console_text(handle, body);
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
		append_log_line(format_submitted_input_line(line));
	}

	std::string format_submitted_input_line(const std::string& line)
	{
		return "> '" + line + "'";
	}

	std::string make_host_section_line(const std::string& label, const std::string& message)
	{
		return label + " " + message;
	}

	std::string make_section_banner(const std::string& title)
	{
		return "========== " + title + " ==========";
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

				const auto with_newline = line + "\r\n";
				if (!line.empty() && line.front() == '[' && line.find(']') != std::string::npos)
				{
					write_tagged_line(handle, line, original_attributes);
					write_console_text(handle, "\r\n");
				}
				else
				{
					const auto lowered = lower_copy(line);
					WORD color = original_attributes;
					if (lowered.find("succeeded") != std::string::npos
						|| lowered.find("loaded") != std::string::npos
						|| lowered.find("all patches applied") != std::string::npos)
					{
						color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
					}
					else if (lowered.find("warning") != std::string::npos)
					{
						color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
					}

					set_console_color(handle, color);
					write_console_text(handle, with_newline);
				}
				set_console_color(handle, original_attributes);
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

	void commit_shell_input_line(const std::string& line)
	{
		std::lock_guard _(runtime::get_output_mutex());

		const auto committed = format_submitted_input_line(line);
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
				WriteConsoleA(handle, committed.data(), static_cast<DWORD>(committed.size()), &written, nullptr);
				WriteConsoleA(handle, "\r\n", 2, &written, nullptr);
				return;
			}
		}

		std::fwrite("\r", 1, 1, stdout);
		std::fwrite(committed.data(), 1, committed.size(), stdout);
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



