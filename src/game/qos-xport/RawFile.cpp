#include <std_include.hpp>
#include "component/component_loader.hpp"

#include "component/assethandler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/map_dumper.hpp"
#include "component/scheduler.hpp"
#include "standalone/runtime.hpp"

#include "game/game.hpp"
#include "game/structs.IW4.hpp"

#include <utils/memory.hpp>

namespace rawfile
{
	namespace
	{
		game::iw4::RawFile* convert(game::qos::RawFile* rawfile)
		{
			if (!rawfile || !rawfile->buffer) return nullptr;

			auto iw4_asset = utils::memory::allocate<game::iw4::RawFile>();
			iw4_asset->name = rawfile->name;
			iw4_asset->compressedLen = 0;
			iw4_asset->len = rawfile->len;
			iw4_asset->buffer = rawfile->buffer;
			return iw4_asset;
		}
	}

	class component final : public component_interface
	{
	public:
		bool is_supported() override
		{
			return true;
		}

		void post_load() override
		{
			scheduler::once([&]()
			{
				command::add("dumprawfile", [](const command::params& params)
				{
					if (params.size() < 2)
					{
						command::print_line("USAGE: dumprawfile <name>");
						command::print_line("USAGE: dumprawfile *");
						return;
					}

					const auto name = params[1];

					if (std::string{name} == "*")
					{
						const auto dumped = assethandler::dump_all_assets_of_type(game::qos::ASSET_TYPE_RAWFILE);
						command::print_line(std::string("dumped ") + std::to_string(dumped) + " rawfile asset(s)");
						return;
					}

					if (runtime::is_standalone_xport_mode())
					{
						const auto header = game::DB_FindXAssetHeader_Internal(game::qos::ASSET_TYPE_RAWFILE, name, false);
						if (!header.data || game::DB_IsXAssetDefault(game::qos::ASSET_TYPE_RAWFILE, name))
						{
							command::print_line(std::string("dumprawfile failed on '") + name + "'");
							return;
						}

						if (!assethandler::dump_asset(game::qos::ASSET_TYPE_RAWFILE, header))
						{
							command::print_line(std::string("dumprawfile failed on '") + name + "'");
							return;
						}

						command::print_line(std::string("dumped '") + name + "' for IW4");
						return;
					}

					if (!assethandler::dump_asset_by_name(game::qos::ASSET_TYPE_RAWFILE, name))
					{
						command::print_line(std::string("dumprawfile failed on '") + name + "'");
						return;
					}

					command::print_line(std::string("dumped '") + name + "' for IW4");
				});
				command::set_help("dumprawfile", "Dump one loaded rawfile asset, or '*' to dump all loaded rawfiles.", "dumprawfile maps/_art.gsc");
			}, scheduler::main);
		}

		game::qos::XAssetType get_type() override
		{
			return game::qos::ASSET_TYPE_RAWFILE;
		}

		game::iw4::XAssetHeader convert_asset(game::qos::XAssetHeader header) override
		{
			return { convert(header.rawfile) };
		}
	};
}

REGISTER_COMPONENT(rawfile::component)



