#include <std_include.hpp>
#include "component_loader.hpp"
#include "standalone/runtime.hpp"
#include "standalone/shell.hpp"

namespace
{
	void write_console_line(const std::string& line)
	{
		std::lock_guard _(runtime::get_output_mutex());
		standalone::shell::write_console_line(line);
	}

	void log_component_message(const std::string& message)
	{
		runtime::append_log_line("[component_loader] " + message);
	}

	void print_component_console_progress(const char* phase, const std::string& component_name)
	{
		write_console_line(std::string("[component_loader:") + phase + "] " + component_name);
	}
}

void component_loader::register_component(std::unique_ptr<component_interface>&& component_)
{
	get_components().push_back(std::move(component_));
}

bool component_loader::post_start()
{
	static auto handled = false;
	if (handled) return true;
	handled = true;

	clean();

	try
	{
		for (const auto& component_ : get_components())
		{
			log_component_message(std::string("component post_start begin: ") + typeid(*component_).name());
			print_component_console_progress("post_start", typeid(*component_).name());
			component_->post_start();
			log_component_message(std::string("component post_start end: ") + typeid(*component_).name());
		}
	}
	catch (premature_shutdown_trigger&)
	{
		return false;
	}

	return true;
}

bool component_loader::post_load()
{
	static auto handled = false;
	if (handled) return true;
	handled = true;

	clean();

	try
	{
		for (const auto& component_ : get_components())
		{
			log_component_message(std::string("component post_load begin: ") + typeid(*component_).name());
			component_->post_load();
			log_component_message(std::string("component post_load end: ") + typeid(*component_).name());
		}
	}
	catch (premature_shutdown_trigger&)
	{
		return false;
	}

	return true;
}

void component_loader::pre_destroy()
{
	static auto handled = false;
	if (handled) return;
	handled = true;

	for (const auto& component_ : get_components())
	{
		log_component_message(std::string("component pre_destroy begin: ") + typeid(*component_).name());
		component_->pre_destroy();
		log_component_message(std::string("component pre_destroy end: ") + typeid(*component_).name());
	}
}

void component_loader::clean()
{
	auto& components = get_components();
	for (auto i = components.begin(); i != components.end();)
	{
		if (!(*i)->is_supported())
		{
			(*i)->pre_destroy();
			i = components.erase(i);
		}
		else
		{
			++i;
		}
	}
}

void* component_loader::load_import(const std::string& library, const std::string& function)
{
	void* function_ptr = nullptr;

	for (const auto& component_ : get_components())
	{
		auto* const component_function_ptr = component_->load_import(library, function);
		if (component_function_ptr)
		{
			function_ptr = component_function_ptr;
		}
	}

	return function_ptr;
}

void component_loader::trigger_premature_shutdown()
{
	throw premature_shutdown_trigger();
}

std::vector<std::unique_ptr<component_interface>>& component_loader::get_components()
{
	using component_vector = std::vector<std::unique_ptr<component_interface>>;
	using component_vector_container = std::unique_ptr<component_vector, std::function<void(component_vector*)>>;

	static component_vector_container components(new component_vector, [](component_vector* component_vector)
	{
		pre_destroy();
		delete component_vector;
	});

	return *components;
}



