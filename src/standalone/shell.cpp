#include <std_include.hpp>

#include "shell.hpp"
#include "standalone/runtime.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <optional>
#include <string_view>

namespace standalone::shell
{
	namespace
	{
		constexpr WORD k_color_white = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		constexpr WORD k_color_qos_major = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		constexpr WORD k_color_qos_minor = FOREGROUND_BLUE;
		constexpr WORD k_color_host_major = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		constexpr WORD k_color_host_minor = FOREGROUND_GREEN | FOREGROUND_BLUE;
		constexpr WORD k_color_patch_major = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		constexpr WORD k_color_patch_minor = FOREGROUND_RED | FOREGROUND_BLUE;
		constexpr WORD k_color_component_major = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		constexpr WORD k_color_component_minor = FOREGROUND_GREEN;
		constexpr WORD k_color_runtime_major = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		constexpr WORD k_color_runtime_minor = FOREGROUND_BLUE | FOREGROUND_GREEN;
		constexpr WORD k_color_debug_major = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		constexpr WORD k_color_debug_minor = FOREGROUND_RED | FOREGROUND_GREEN;
		constexpr WORD k_color_engine_major = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		constexpr WORD k_color_engine_minor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		constexpr WORD k_color_error = FOREGROUND_RED | FOREGROUND_INTENSITY;
		constexpr WORD k_color_success = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		constexpr auto k_shell_prompt = "QoS-xport: ";
		constexpr auto k_prompt_name = "QoS-xport";
		constexpr auto k_prompt_suffix = ": ";

		struct tag_palette
		{
			WORD major;
			WORD minor;
			WORD body;
		};

		struct tag_rule
		{
			std::string_view category;
			tag_palette palette;
		};

		struct tag_variant_rule
		{
			std::string_view tag;
			WORD color;
		};

		struct prompt_state
		{
			bool active = false;
			std::string buffer;
			size_t cursor = 0;
			std::int32_t history_index = -1;
			std::deque<std::string> history{};
			SHORT row = -1;
		};

		constexpr std::array k_tag_rules =
		{
			tag_rule{ "qos-xport", { k_color_qos_major, k_color_qos_minor, k_color_white } },
			tag_rule{ "host", { k_color_host_major, k_color_host_minor, k_color_host_major } },
			tag_rule{ "patch", { k_color_patch_major, k_color_patch_minor, k_color_patch_major } },
			tag_rule{ "component_loader", { k_color_component_major, k_color_component_minor, k_color_component_major } },
			tag_rule{ "runtime", { k_color_runtime_major, k_color_runtime_minor, k_color_runtime_major } },
			tag_rule{ "debug", { k_color_debug_major, k_color_debug_minor, k_color_debug_major } },
			tag_rule{ "engine", { k_color_engine_major, k_color_engine_minor, k_color_engine_major } },
		};

		constexpr std::array k_tag_variant_rules =
		{
			tag_variant_rule{ "engine:error", k_color_error },
			tag_variant_rule{ "engine:init", FOREGROUND_GREEN },
			tag_variant_rule{ "engine:warn", k_color_debug_major },
		};

		prompt_state g_prompt_state{};

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

		void clear_console_row(HANDLE handle, const COORD row_start, const WORD attributes)
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!GetConsoleScreenBufferInfo(handle, &info))
			{
				return;
			}

			DWORD written = 0;
			FillConsoleOutputCharacterA(handle, ' ', info.dwSize.X, row_start, &written);
			FillConsoleOutputAttribute(handle, attributes, info.dwSize.X, row_start, &written);
			SetConsoleCursorPosition(handle, row_start);
		}

		void draw_prompt_locked(HANDLE handle, const WORD original_attributes)
		{
			DWORD written = 0;
			SetConsoleTextAttribute(handle, FOREGROUND_RED);
			WriteConsoleA(handle, k_prompt_name, static_cast<DWORD>(std::strlen(k_prompt_name)), &written, nullptr);
			SetConsoleTextAttribute(handle, k_color_white);
			WriteConsoleA(handle, k_prompt_suffix, static_cast<DWORD>(std::strlen(k_prompt_suffix)), &written, nullptr);
			if (!g_prompt_state.buffer.empty())
			{
				WriteConsoleA(handle, g_prompt_state.buffer.data(), static_cast<DWORD>(g_prompt_state.buffer.size()), &written, nullptr);
			}
			SetConsoleTextAttribute(handle, original_attributes);
		}

		size_t get_max_input_length(HANDLE handle)
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!GetConsoleScreenBufferInfo(handle, &info))
			{
				return 510;
			}

			const auto columns = static_cast<size_t>(info.dwSize.X);
			const auto prompt_length = std::strlen(k_prompt_name) + std::strlen(k_prompt_suffix);
			if (columns <= prompt_length + 1)
			{
				return 1;
			}

			return std::min<size_t>(510, columns - prompt_length - 1);
		}

		void redraw_prompt_locked(HANDLE handle, const WORD original_attributes)
		{
			if (!g_prompt_state.active)
			{
				return;
			}

			CONSOLE_SCREEN_BUFFER_INFO info{};
			if (!GetConsoleScreenBufferInfo(handle, &info))
			{
				return;
			}

			const auto row = g_prompt_state.row >= 0 ? g_prompt_state.row : info.dwCursorPosition.Y;
			const COORD row_start{ 0, row };
			clear_console_row(handle, row_start, original_attributes);
			g_prompt_state.row = row_start.Y;
			draw_prompt_locked(handle, original_attributes);
			const COORD cursor
			{
				static_cast<SHORT>(std::strlen(k_prompt_name) + std::strlen(k_prompt_suffix) + g_prompt_state.cursor),
				row_start.Y
			};
			SetConsoleCursorPosition(handle, cursor);
		}

		std::optional<tag_palette> get_family_palette(const std::string& family)
		{
			for (const auto& rule : k_tag_rules)
			{
				if (rule.category == family)
				{
					return rule.palette;
				}
			}

			return std::nullopt;
		}

		WORD get_tag_color(const std::string& tag, const WORD original_attributes)
		{
			const auto lowered_tag = lower_copy(tag);
			for (const auto& rule : k_tag_variant_rules)
			{
				if (rule.tag == lowered_tag)
				{
					return rule.color;
				}
			}

			const auto separator = lowered_tag.find(':');
			const auto family = separator == std::string::npos ? lowered_tag : lowered_tag.substr(0, separator);
			if (const auto palette = get_family_palette(family); palette.has_value())
			{
				return separator == std::string::npos ? palette->major : palette->minor;
			}

			if (lowered_tag.rfind("runtime - ", 0) == 0)
			{
				return k_color_runtime_minor;
			}

			return original_attributes;
		}

		WORD get_banner_color(const std::string& line, const WORD original_attributes)
		{
			const auto lowered = lower_copy(line);

			if (lowered.find("initialization complete") != std::string::npos
				|| lowered.find("patches applied") != std::string::npos)
			{
				return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
			}

			if (lowered.find("patch") != std::string::npos)
			{
				return k_color_patch_major;
			}

			if (lowered.find("host ") != std::string::npos
				|| lowered.find("zones:") != std::string::npos
				|| lowered.find("imports") != std::string::npos
				|| lowered.find("refs") != std::string::npos)
			{
				return k_color_host_major;
			}

			if (lowered.find("component_loader") != std::string::npos)
			{
				return k_color_component_major;
			}

			if (lowered.find("runtime") != std::string::npos)
			{
				return k_color_runtime_major;
			}

			if (lowered.find("debug") != std::string::npos)
			{
				return k_color_debug_major;
			}

			return original_attributes;
		}

		void write_tagged_line(HANDLE handle, const std::string& line, const WORD original_attributes)
		{
			const auto close = line.find(']');
			const auto tag = close == std::string::npos ? std::string{} : line.substr(1, close - 1);
			const auto body = close == std::string::npos ? std::string{} : line.substr(close + 1);
			const auto tag_color = get_tag_color(tag, original_attributes);
			const auto lowered_tag = lower_copy(tag);
			const auto separator = lowered_tag.find(':');
			const auto family = separator == std::string::npos ? lowered_tag : lowered_tag.substr(0, separator);
			const auto lowered_body = lower_copy(body);
			const auto quote_start = body.find('\'');
			const auto quote_end = quote_start == std::string::npos ? std::string::npos : body.find('\'', quote_start + 1);
			WORD body_color = k_color_white;

			if (const auto palette = get_family_palette(family); palette.has_value())
			{
				body_color = palette->body;
			}

			if (lowered_body.find("all patches applied") != std::string::npos
				|| lowered_body.find(" succeeded") != std::string::npos
				|| lowered_body.find("successfully") != std::string::npos)
			{
				body_color = k_color_success;
			}
			else if (lowered_body.find("warning") != std::string::npos)
			{
				body_color = k_color_debug_major;
			}
			else if (lowered_body.find("error") != std::string::npos
				|| lowered_body.find("failed") != std::string::npos)
			{
				body_color = k_color_error;
			}

			const auto bracket_color = family == "qos-xport" ? k_color_white : tag_color;
			set_console_color(handle, bracket_color);
			write_console_text(handle, "[");
			set_console_color(handle, tag_color);
			write_console_text(handle, tag);
			set_console_color(handle, bracket_color);
			write_console_text(handle, "]");

			if (body.empty())
			{
				return;
			}

			if (lower_copy(tag) == "qos-xport" && body.find("===") != std::string::npos)
			{
				set_console_color(handle, tag_color);
				write_console_text(handle, body);
				return;
			}

			if (quote_start != std::string::npos && quote_end != std::string::npos && quote_end > quote_start)
			{
				WORD quoted_color = tag_color;
				if (lowered_body.find("loaded ") != std::string::npos
					|| lowered_body.find("succeeded") != std::string::npos)
				{
					quoted_color = k_color_success;
				}
				else if (lowered_body.find("failed") != std::string::npos
					|| lowered_body.find("error") != std::string::npos)
				{
					quoted_color = k_color_error;
				}

				set_console_color(handle, body_color);
				write_console_text(handle, body.substr(0, quote_start + 1));
				set_console_color(handle, quoted_color);
				write_console_text(handle, body.substr(quote_start + 1, quote_end - quote_start - 1));
				set_console_color(handle, body_color);
				write_console_text(handle, body.substr(quote_end));
				return;
			}

			set_console_color(handle, body_color);
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
				WORD original_attributes = k_color_white;
				if (GetConsoleScreenBufferInfo(handle, &info))
				{
					original_attributes = info.wAttributes;
				}

				const auto had_prompt = g_prompt_state.active;
				if (had_prompt)
				{
					const auto row = g_prompt_state.row >= 0 ? g_prompt_state.row : info.dwCursorPosition.Y;
					clear_console_row(handle, COORD{ 0, row }, original_attributes);
					SetConsoleCursorPosition(handle, COORD{ 0, row });
				}

				if (!line.empty() && line.front() == '[' && line.find(']') != std::string::npos)
				{
					write_tagged_line(handle, line, original_attributes);
					write_console_text(handle, "\r\n");
				}
				else
				{
					const auto lowered = lower_copy(line);
					WORD color = original_attributes;
					if (line.rfind("==========", 0) == 0)
					{
						color = get_banner_color(line, original_attributes);
					}
					else if (lowered.find("succeeded") != std::string::npos
						|| lowered.find("loaded") != std::string::npos
						|| lowered.find("all patches applied") != std::string::npos)
					{
						color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
					}
					else if (lowered.find("warning") != std::string::npos)
					{
						color = k_color_debug_major;
					}

					set_console_color(handle, color);
					write_console_text(handle, line + "\r\n");
				}

				if (had_prompt)
				{
					CONSOLE_SCREEN_BUFFER_INFO after_info{};
					if (GetConsoleScreenBufferInfo(handle, &after_info))
					{
						g_prompt_state.row = after_info.dwCursorPosition.Y;
					}
					redraw_prompt_locked(handle, original_attributes);
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
		write_console_line(line);
	}

	void render_shell_prompt()
	{
		std::lock_guard _(runtime::get_output_mutex());
		g_prompt_state.active = true;
		g_prompt_state.buffer.clear();
		g_prompt_state.cursor = 0;
		g_prompt_state.history_index = -1;
		g_prompt_state.row = -1;

		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				CONSOLE_SCREEN_BUFFER_INFO info{};
				WORD original_attributes = k_color_white;
				if (GetConsoleScreenBufferInfo(handle, &info))
				{
					original_attributes = info.wAttributes;
					g_prompt_state.row = info.dwCursorPosition.Y;
				}

				draw_prompt_locked(handle, original_attributes);
				return;
			}
		}

		std::fwrite(k_shell_prompt, 1, std::strlen(k_shell_prompt), stdout);
		std::fflush(stdout);
	}

	void commit_shell_input_line(const std::string& line)
	{
		std::lock_guard _(runtime::get_output_mutex());
		g_prompt_state.active = false;
		g_prompt_state.buffer.clear();
		g_prompt_state.cursor = 0;
		g_prompt_state.history_index = -1;
		g_prompt_state.row = -1;

		const auto committed = format_submitted_input_line(line);
		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				CONSOLE_SCREEN_BUFFER_INFO info{};
				WORD original_attributes = k_color_white;
				if (GetConsoleScreenBufferInfo(handle, &info))
				{
					original_attributes = info.wAttributes;
				}

				auto line_y = info.dwCursorPosition.Y;
				if (info.dwCursorPosition.X == 0 && line_y > 0)
				{
					--line_y;
				}

				const COORD line_start{ 0, line_y };
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
		g_prompt_state.active = true;
		g_prompt_state.buffer = line;
		g_prompt_state.cursor = line.size();
		(void)previous_length;

		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
		{
			DWORD mode = 0;
			if (GetConsoleMode(handle, &mode))
			{
				CONSOLE_SCREEN_BUFFER_INFO info{};
				WORD original_attributes = k_color_white;
				if (GetConsoleScreenBufferInfo(handle, &info))
				{
					original_attributes = info.wAttributes;
					g_prompt_state.row = info.dwCursorPosition.Y;
				}

				const COORD line_start{ 0, info.dwCursorPosition.Y };
				DWORD written = 0;
				FillConsoleOutputCharacterA(handle, ' ', info.dwSize.X, line_start, &written);
				FillConsoleOutputAttribute(handle, original_attributes, info.dwSize.X, line_start, &written);
				SetConsoleCursorPosition(handle, line_start);

				SetConsoleTextAttribute(handle, FOREGROUND_RED);
				WriteConsoleA(handle, k_prompt_name, static_cast<DWORD>(std::strlen(k_prompt_name)), &written, nullptr);

				SetConsoleTextAttribute(handle, k_color_white);
				WriteConsoleA(handle, k_prompt_suffix, static_cast<DWORD>(std::strlen(k_prompt_suffix)), &written, nullptr);

				SetConsoleTextAttribute(handle, k_color_white);
				if (!line.empty())
				{
					WriteConsoleA(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
				}

				const COORD cursor{
					static_cast<SHORT>(std::strlen(k_prompt_name) + std::strlen(k_prompt_suffix) + g_prompt_state.cursor),
					line_start.Y
				};
				SetConsoleCursorPosition(handle, cursor);
				return;
			}
		}

		std::fwrite("\r", 1, 1, stdout);
		std::fwrite(k_shell_prompt, 1, std::strlen(k_shell_prompt), stdout);
		std::fwrite(line.data(), 1, line.size(), stdout);
		std::fflush(stdout);
	}

	void begin_shell_input()
	{
		render_shell_prompt();
	}

	bool handle_shell_input_record(const INPUT_RECORD& record, std::string& completed_line)
	{
		completed_line.clear();

		if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
		{
			render_shell_input_line(g_prompt_state.buffer, g_prompt_state.buffer.size());
			return false;
		}

		if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
		{
			return false;
		}

		const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
		switch (record.Event.KeyEvent.wVirtualKeyCode)
		{
		case VK_UP:
			if (!g_prompt_state.history.empty())
			{
				if (++g_prompt_state.history_index >= static_cast<std::int32_t>(g_prompt_state.history.size()))
				{
					g_prompt_state.history_index = static_cast<std::int32_t>(g_prompt_state.history.size()) - 1;
				}

				g_prompt_state.buffer = g_prompt_state.history.at(g_prompt_state.history_index);
				g_prompt_state.cursor = g_prompt_state.buffer.size();
				render_shell_input_line(g_prompt_state.buffer, g_prompt_state.buffer.size());
			}
			return false;

		case VK_DOWN:
			if (!g_prompt_state.history.empty())
			{
				if (--g_prompt_state.history_index < -1)
				{
					g_prompt_state.history_index = -1;
				}

				if (g_prompt_state.history_index == -1)
				{
					g_prompt_state.buffer.clear();
				}
				else
				{
					g_prompt_state.buffer = g_prompt_state.history.at(g_prompt_state.history_index);
				}

				g_prompt_state.cursor = g_prompt_state.buffer.size();
				render_shell_input_line(g_prompt_state.buffer, g_prompt_state.buffer.size());
			}
			return false;

		case VK_LEFT:
			if (g_prompt_state.cursor > 0)
			{
				--g_prompt_state.cursor;
				if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
				{
					CONSOLE_SCREEN_BUFFER_INFO info{};
					if (GetConsoleScreenBufferInfo(handle, &info))
					{
						const COORD cursor{
							static_cast<SHORT>(std::strlen(k_prompt_name) + std::strlen(k_prompt_suffix) + g_prompt_state.cursor),
							g_prompt_state.row >= 0 ? g_prompt_state.row : info.dwCursorPosition.Y
						};
						SetConsoleCursorPosition(handle, cursor);
					}
				}
			}
			return false;

		case VK_RIGHT:
			if (g_prompt_state.cursor < g_prompt_state.buffer.size())
			{
				++g_prompt_state.cursor;
				if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
				{
					CONSOLE_SCREEN_BUFFER_INFO info{};
					if (GetConsoleScreenBufferInfo(handle, &info))
					{
						const COORD cursor{
							static_cast<SHORT>(std::strlen(k_prompt_name) + std::strlen(k_prompt_suffix) + g_prompt_state.cursor),
							g_prompt_state.row >= 0 ? g_prompt_state.row : info.dwCursorPosition.Y
						};
						SetConsoleCursorPosition(handle, cursor);
					}
				}
			}
			return false;

		case VK_RETURN:
			completed_line = g_prompt_state.buffer;
			if (!g_prompt_state.buffer.empty())
			{
				if (g_prompt_state.history_index >= 0
					&& g_prompt_state.history_index < static_cast<std::int32_t>(g_prompt_state.history.size()))
				{
					const auto itr = g_prompt_state.history.begin() + g_prompt_state.history_index;
					if (*itr == g_prompt_state.buffer)
					{
						g_prompt_state.history.erase(itr);
					}
				}

				g_prompt_state.history.push_front(g_prompt_state.buffer);
				if (g_prompt_state.history.size() > 10)
				{
					g_prompt_state.history.erase(g_prompt_state.history.begin() + 10, g_prompt_state.history.end());
				}
			}

			commit_shell_input_line(completed_line);
			return true;

		case VK_BACK:
			if (g_prompt_state.cursor > 0 && !g_prompt_state.buffer.empty())
			{
				g_prompt_state.buffer.erase(g_prompt_state.cursor - 1, 1);
				--g_prompt_state.cursor;
				g_prompt_state.history_index = -1;
				render_shell_input_line(g_prompt_state.buffer, g_prompt_state.buffer.size());
			}
			return false;

		case VK_ESCAPE:
			if (!g_prompt_state.buffer.empty())
			{
				g_prompt_state.buffer.clear();
				g_prompt_state.cursor = 0;
				g_prompt_state.history_index = -1;
				render_shell_input_line(g_prompt_state.buffer, g_prompt_state.buffer.size());
			}
			return false;

		default:
		{
			const auto c = record.Event.KeyEvent.uChar.AsciiChar;
			if (!c || static_cast<unsigned char>(c) < 32)
			{
				return false;
			}

			if (g_prompt_state.buffer.size() >= get_max_input_length(handle))
			{
				return false;
			}

			g_prompt_state.buffer.insert(g_prompt_state.cursor, 1, c);
			++g_prompt_state.cursor;
			g_prompt_state.history_index = -1;
			render_shell_input_line(g_prompt_state.buffer, g_prompt_state.buffer.size());
			return false;
		}
		}
	}

	void clear_console_display()
	{
		std::lock_guard _(runtime::get_output_mutex());
		g_prompt_state.active = false;
		g_prompt_state.buffer.clear();
		g_prompt_state.cursor = 0;
		g_prompt_state.history_index = -1;
		g_prompt_state.row = -1;

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
