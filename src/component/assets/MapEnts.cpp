#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/assethandler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/map_dumper.hpp"
#include "component/scheduler.hpp"
#include "runtime.hpp"

#include "game/game.hpp"
#include "game/structs.IW4.hpp"

#include <utils/memory.hpp>
#include <utils/string.hpp>
#include <component/entities.hpp>

namespace mapents
{
	namespace
	{
		game::iw4::MapEnts* convert(game::qos::MapEnts* ents)
		{
			if (!ents) return nullptr;

			std::string entString(ents->entityString, ents->numEntityChars - 1);

			Entities mapEnts(entString);

			mapEnts.DeleteOldSchoolPickups();
			mapEnts.AddRemovedSModels(); ///TODO

			entString = mapEnts.Build();
			const auto models = mapEnts.GetModels();

			auto iw4_asset = utils::memory::allocate<game::iw4::MapEnts>();

			iw4_asset->name = ents->name;
			iw4_asset->entityString = utils::memory::duplicate_string(entString);
			iw4_asset->numEntityChars = entString.size() + 1;
			iw4_asset->stages = nullptr;

			/*for (const std::string& model : models)
			{
				command::execute(utils::string::va("dumpxmodel %s", model.data()));
			}*/

			return { iw4_asset };
		}

		void dump_referenced_models(game::iw4::MapEnts* ents)
		{
			if (!ents || !ents->entityString || ents->numEntityChars <= 1)
			{
				return;
			}

			Entities map_ents(ents->entityString, ents->numEntityChars);
			const auto models = map_ents.GetModels();
			for (const auto& model : models)
			{
				command::execute(utils::string::va("dumpxmodel %s", model.data()));
			}
		}

		class component final : public component_interface
		{
		public:
			bool is_supported() override
			{
				return false;
			}

		void post_load() override
		{
			if (runtime::is_standalone_xport_mode())
			{
				return;
			}

			scheduler::once([&]()
			{
						command::add("dumpmapents", [](const command::params& params)
						{
							if (params.size() < 2)
							{
								console::info("USAGE: dumpmapents <name>\n");
								return;
							}

						const auto name = params[1];

						game::DB_SyncXAssets();

						auto header = game::DB_FindXAssetHeader(game::qos::ASSET_TYPE_CLIPMAP_MP, name);
						if (!header.clipMap->mapEnts)
						{
								console::error("dumpmapents failed on '%s'\n", name);
								return;
							}

							auto converted = mapents::convert(header.clipMap->mapEnts);
							map_dumper::api->write(game::iw4::ASSET_TYPE_MAP_ENTS, converted);
							dump_referenced_models(converted);

							console::info("dumped '%s' for IW4\n", name);
						});
					}, scheduler::main);
			}

			game::qos::XAssetType get_type() override
			{
				return game::qos::ASSET_TYPE_MAP_ENTS;
			}

			game::iw4::XAssetHeader convert_asset(game::qos::XAssetHeader header) override
			{
				return { convert(header.mapEnts) };
			}
		};
	}
}

REGISTER_COMPONENT(mapents::component)
