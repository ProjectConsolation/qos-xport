#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "command.hpp"
#include "../game/symbols.hpp"
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
		: nesting_(*game::command_id)
	{
	}

	int params::size() const
	{
		return game::cmd_argc[this->nesting_];
	}

	const char* params::get(const int index) const
	{
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

		if (handlers.find(command) == handlers.end())
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
					game::Com_Printf(0, "available commands:\n");

					for (const auto& entry : entries)
					{
						game::Com_Printf(0, " - %s\n", entry.name.c_str());
						if (!entry.description.empty())
						{
							game::Com_Printf(0, "   %s\n", entry.description.c_str());
						}
						if (!entry.example.empty())
						{
							game::Com_Printf(0, "   example: %s\n", entry.example.c_str());
						}
					}
				});
				set_help("help", "List available standalone xport commands.", "help");

				add("hello", []()
				{
					game::Com_Printf(0, "hello from qos-xport!\n");
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
