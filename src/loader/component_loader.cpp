#include <std_include.hpp>
#include "component_loader.hpp"

namespace
{
	std::filesystem::path get_launcher_log_path()
	{
		char module_path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, module_path, MAX_PATH);
		auto path = std::filesystem::path(module_path).parent_path() / "qos-xport";
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		return path / "launcher.log";
	}

	void log_component_message(const std::string& message)
	{
		const auto line = "[runtime] " + message + "\r\n";
		OutputDebugStringA(line.c_str());

		const auto path = get_launcher_log_path();
		const auto handle = CreateFileA(
			path.string().c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr
		);

		if (handle == INVALID_HANDLE_VALUE)
		{
			return;
		}

		const auto close_handle = gsl::finally([&]()
		{
			CloseHandle(handle);
		});

		DWORD bytes_written = 0;
		WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &bytes_written, nullptr);
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
