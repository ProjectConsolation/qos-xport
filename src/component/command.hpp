#pragma once

namespace command
{
	struct help_entry
	{
		std::string name;
		std::string description;
		std::string example;
	};

	class params
	{
	public:
		params();

		int size() const;
		const char* get(int index) const;
		std::string join(int index) const;
		std::vector<std::string> get_all() const;

		const char* operator[](const int index) const
		{
			return this->get(index); //
		}

	private:
		DWORD nesting_;
	};

	void add_raw(const char* name, void (*callback)());
	void add(const char* name, const std::function<void(const params&)>& callback);
	void add(const char* name, const std::function<void()>& callback);
	void set_help(const char* name, const std::string& description, const std::string& example);
	void execute(std::string command);
	bool execute_local(const std::string& command_line);
	std::vector<help_entry> get_help_entries();
}
