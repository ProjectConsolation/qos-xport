#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "console.hpp"
#include "standalone/shell.hpp"
#include "../runtime.hpp"
#include "scheduler.hpp"

#include <utils/memory.hpp>
#include <utils/string.hpp>

namespace command
{
	namespace
	{
		static utils::memory::allocator cmd_allocator;

		std::unordered_map<std::string, std::function<void(params&)>> handlers;
		std::unordered_map<std::string, help_entry> help_entries;
		thread_local bool local_dispatch = false;
		thread_local std::vector<std::string> local_args;

		void print_command_line(const std::string& line)
		{
			if (!runtime::is_standalone_xport_mode())
			{
				console::info("%s\n", line.c_str());
				return;
			}

			standalone::shell::write_shell_line(line);
			standalone::shell::append_log_line(line);
		}

		void main_handler()
		{
			params params = {};

			const auto command = utils::string::to_lower(params[0]);
			if (handlers.find(command) != handlers.end())
			{
				handlers[command](params);
			}
		}
	}

	params::params()
		: nesting_(local_dispatch ? std::numeric_limits<DWORD>::max() : *game::command_id)
	{
	}

	int params::size() const
	{
		if (this->nesting_ == std::numeric_limits<DWORD>::max())
		{
			return static_cast<int>(local_args.size());
		}

		return game::cmd_argc[this->nesting_];
	}

	const char* params::get(const int index) const
	{
		if (this->nesting_ == std::numeric_limits<DWORD>::max())
		{
			if (index >= this->size())
			{
				return "";
			}

			return local_args[static_cast<size_t>(index)].c_str();
		}

		if (index >= this->size())
		{
			return "";
		}

		return game::cmd_argv[this->nesting_][index];
	}

	std::string params::join(const int index) const
	{
		std::string result = {};

		for (auto i = index; i < this->size(); i++)
		{
			if (i > index) result.append(" ");
			result.append(this->get(i));
		}
		return result;
	}

	std::vector<std::string> params::get_all() const
	{
		std::vector<std::string> params_;
		for (auto i = 0; i < this->size(); i++)
		{
			params_.push_back(this->get(i));
		}
		return params_;
	}

	void add_raw(const char* name, void (*callback)())
	{
		game::Cmd_AddCommandInternal(name, callback, cmd_allocator.allocate<game::qos::cmd_function_s>());
	}

	void add(const char* name, const std::function<void(const params&)>& callback)
	{
		const auto command = utils::string::to_lower(name);

		if (handlers.find(command) == handlers.end() && !runtime::is_standalone_xport_mode())
			add_raw(name, main_handler);

		handlers[command] = callback;
	}

	void add(const char* name, const std::function<void()>& callback)
	{
		add(name, [callback](const params&)
		{
			callback();
		});
	}

	void set_help(const char* name, const std::string& description, const std::string& example)
	{
		const auto command = utils::string::to_lower(name);
		help_entries[command] = help_entry
		{
			name,
			description,
			example
		};
	}

	void execute(std::string command)
	{
		command += "\n";
		game::Cbuf_AddText(0, command.data());
	}

	bool execute_local(const std::string& command_line)
	{
		local_args.clear();

		std::string current;
		bool in_quotes = false;

		for (const char c : command_line)
		{
			if (c == '"')
			{
				in_quotes = !in_quotes;
				continue;
			}

			if (!in_quotes && std::isspace(static_cast<unsigned char>(c)))
			{
				if (!current.empty())
				{
					local_args.push_back(std::move(current));
					current.clear();
				}
				continue;
			}

			current.push_back(c);
		}

		if (!current.empty())
		{
			local_args.push_back(std::move(current));
		}

		if (local_args.empty())
		{
			return false;
		}

		const auto command = utils::string::to_lower(local_args[0]);
		const auto handler = handlers.find(command);
		if (handler == handlers.end())
		{
			return false;
		}

		local_dispatch = true;
		const auto dispatch_guard = gsl::finally([]()
		{
			local_dispatch = false;
			local_args.clear();
		});

		params params{};
		handler->second(params);
		return true;
	}

	std::vector<help_entry> get_help_entries()
	{
		std::vector<help_entry> entries;
		entries.reserve(help_entries.size());

		for (const auto& [_, entry] : help_entries)
		{
			entries.push_back(entry);
		}

		std::sort(entries.begin(), entries.end(), [](const help_entry& a, const help_entry& b)
		{
			return utils::string::to_lower(a.name) < utils::string::to_lower(b.name);
		});

		return entries;
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			scheduler::once([&]()
			{
				add("help", []()
				{
					const auto entries = get_help_entries();
					print_command_line("available commands:");

					for (const auto& entry : entries)
					{
						print_command_line(" - " + entry.name);
						if (!entry.description.empty())
						{
							print_command_line("   " + entry.description);
						}
						if (!entry.example.empty())
						{
							print_command_line("   example: " + entry.example);
						}
					}
				});
				set_help("help", "List available standalone xport commands.", "help");

				add("hello", []()
				{
					print_command_line("hello from qos-xport!");
				});
				set_help("hello", "Test the standalone command shell.", "hello");
			}, scheduler::main);
		}

		void pre_destroy() override
		{
			cmd_allocator.clear();
		}
	};
}

REGISTER_COMPONENT(command::component)
