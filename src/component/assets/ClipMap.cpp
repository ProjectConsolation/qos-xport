#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/assethandler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/map_dumper.hpp"
#include "component/scheduler.hpp"

#include "game/game.hpp"
#include "game/structs.IW4.hpp"

#include <utils/memory.hpp>

namespace clipmap
{
	namespace
	{
		void add_stage(game::iw4::MapEnts* map_ents)
		{
			if (!map_ents)
			{
				return;
			}

			map_ents->stageCount = 1;
			map_ents->stages = utils::memory::allocate<game::iw4::Stage>();
			map_ents->stages[0].name = "stage 0";
			map_ents->stages[0].origin[0] = 0.0f;
			map_ents->stages[0].origin[1] = 0.0f;
			map_ents->stages[0].origin[2] = 0.0f;
			map_ents->stages[0].triggerIndex = 0x400;
			map_ents->stages[0].sunPrimaryLightIndex = 1;
		}

		game::iw4::clipMap_t* convert(game::qos::clipMap_t* clipmap)
		{
			if (!clipmap) return nullptr;

			auto iw4_asset = utils::memory::allocate<game::iw4::clipMap_t>();
			ZeroMemory(iw4_asset, sizeof(*iw4_asset));

			iw4_asset->name = clipmap->name;
			iw4_asset->isInUse = clipmap->isInUse;
			iw4_asset->planeCount = clipmap->planeCount;
			iw4_asset->planes = clipmap->planes;

			iw4_asset->numSubModels = clipmap->numSubModels;
			if (clipmap->cmodels && clipmap->numSubModels)
			{
				iw4_asset->cmodels = utils::memory::allocate_array<game::iw4::cmodel_t>(clipmap->numSubModels);
				for (unsigned int i = 0; i < clipmap->numSubModels; ++i)
				{
					iw4_asset->cmodels[i].bounds.compute(clipmap->cmodels[i].mins, clipmap->cmodels[i].maxs);
					iw4_asset->cmodels[i].radius = clipmap->cmodels[i].radius;
					iw4_asset->cmodels[i].leaf.firstCollAabbIndex = clipmap->cmodels[i].leaf.firstCollAabbIndex;
					iw4_asset->cmodels[i].leaf.collAabbCount = clipmap->cmodels[i].leaf.collAabbCount;
					iw4_asset->cmodels[i].leaf.brushContents = clipmap->cmodels[i].leaf.brushContents;
					iw4_asset->cmodels[i].leaf.terrainContents = clipmap->cmodels[i].leaf.terrainContents;
					iw4_asset->cmodels[i].leaf.bounds.compute(clipmap->cmodels[i].leaf.mins, clipmap->cmodels[i].leaf.maxs);
					iw4_asset->cmodels[i].leaf.leafBrushNode = clipmap->cmodels[i].leaf.leafBrushNode;
				}
			}

			if (clipmap->mapEnts)
			{
				iw4_asset->mapEnts = assethandler::convert_asset_header(game::qos::ASSET_TYPE_MAP_ENTS, { clipmap->mapEnts }).mapEnts;
				add_stage(iw4_asset->mapEnts);
			}

			return iw4_asset;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			scheduler::once([&]()
			{
				const auto dump_command = [](const command::params& params)
				{
					if (params.size() < 2)
					{
						console::info("USAGE: dumpclipmap <name>\n");
						return;
					}

					const auto name = params[1];

					game::DB_SyncXAssets();

					auto header = game::DB_FindXAssetHeader(game::qos::ASSET_TYPE_CLIPMAP_MP, name);
					if (!header.clipMap)
					{
						console::error("dumpclipmap failed on '%s'\n", name);
						return;
					}

					auto converted = clipmap::convert(header.clipMap);
					map_dumper::api->write(game::iw4::ASSET_TYPE_CLIPMAP_MP, converted);

					console::warn("dumpclipmap exported a minimal clipmap for '%s'\n", name);
				};

				command::add("dumpclipmap", dump_command);
				command::add("dumpclipmap_t", dump_command);
			}, scheduler::main);
		}

		game::qos::XAssetType get_type() override
		{
			return game::qos::ASSET_TYPE_CLIPMAP_MP;
		}

		game::iw4::XAssetHeader convert_asset(game::qos::XAssetHeader header) override
		{
			return { convert(header.clipMap) };
		}
	};
}

REGISTER_COMPONENT(clipmap::component)
