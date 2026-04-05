#include <std_include.hpp>
#include "component/component_loader.hpp"

#include "component/assethandler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/entities.hpp"
#include "component/map_dumper.hpp"
#include "component/scheduler.hpp"
#include "../runtime.hpp"

#include "game/game.hpp"
#include "game/structs.IW4.hpp"

#include <utils/io.hpp>
#include <utils/string.hpp>

namespace map_dumper
{
	iw4of::api* api = nullptr;

	namespace
	{
		bool dump_asset_by_name(const game::qos::XAssetType type, const std::string& name, const char* label)
		{
			console::info("exporting %s...\n", label);
			if (!assethandler::dump_asset_by_name(type, name))
			{
				console::warn("failed to export %s '%s'\n", label, name.data());
				return false;
			}

			return true;
		}

		bool dump_map_ents(const std::string& bsp_name)
		{
			console::info("exporting Entities...\n");

			const auto header = game::DB_FindXAssetHeader(game::qos::ASSET_TYPE_CLIPMAP_MP, bsp_name.data());
			if (!header.clipMap || !header.clipMap->mapEnts)
			{
				console::warn("failed to export entities for '%s'\n", bsp_name.data());
				return false;
			}

			const auto dumped = assethandler::dump_asset(game::qos::ASSET_TYPE_MAP_ENTS, { header.clipMap->mapEnts });
			if (!dumped)
			{
				return false;
			}

			Entities map_ents(header.clipMap->mapEnts->entityString, header.clipMap->mapEnts->numEntityChars);
			const auto models = map_ents.GetModels();
			for (const auto& model : models)
			{
				command::execute(utils::string::va("dumpxmodel %s", model.data()));
			}

			return true;
		}

		std::string api_read_file(const std::string& name)
		{
			/*
			if (name.ends_with(".iwi"))
			{
				return IGfxImage::ConvertIWIOnTheFly(filename);
			}
			*/

			std::string data;
			if (utils::io::read_file(name, &data))
			{
				return data;
			}

			return "";
		}

		iw4of::params_t get_params()
		{
			auto params = iw4of::params_t();
			params.write_only_once = true;
			params.fs_read_file = api_read_file;
			params.get_from_string_table = [](unsigned int index)
			{
				const char* result = 0;
				if (index)
				{
					result = &game::scrMemTreePub->mt_buffer[8 * index + 4];
				}
				return result;
			};

			params.find_other_asset = [](int type, const std::string& name)
			{
				for (const auto& kv : assethandler::type_conversion_table)
				{
					if (kv.second == type)
					{
						auto qos_type = kv.first;
						std::string name_to_find = name;

						/*
						if (qos_type == game::qos::ASSET_TYPE_WEAPON)
						{
							// Fix weapon name
							name_to_find = name.substr(4); // Remove iw3_ prefix while seeking
						}
						*/

						auto header = game::DB_FindXAssetHeader(qos_type, name_to_find.data());
						if (header.data && !game::DB_IsXAssetDefault(qos_type, name_to_find.data()))
						{
							return assethandler::convert_asset_header(qos_type, header).data;
						}

						return static_cast<void*>(nullptr);
					}
				}

				return static_cast<void*>(nullptr);
			};

			params.print = [](int level, const std::string& message)
			{
				if (level)
				{
					console::error("%s", message.data());
					assert(false);
				}
				else
				{
					console::debug("%s", message.data());
				}
			};

			params.work_directory = "qosxport_out/default";

			return params;
		}

		void dump_map(std::string name)
		{
			// QoS server init loads the optional "<map>_load" fastfile through DB_LoadXAssets
			// with alloc/free flags 4/12 before asset resolution continues.
			constexpr int xzone_alloc_map_load = 4;
			constexpr int xzone_free_map_load = 12;

			bool is_singleplayer = !name.starts_with("mp_");
			std::string bsp_name = utils::string::va("maps/%s%s.d3dbsp", is_singleplayer ? "" : "mp/", name.data());
			std::string load_fastfile = utils::string::va("%s_load", name.data());

			console::info("loading map '%s'...\n", name.data());
			command::execute(utils::string::va("%s %s", is_singleplayer ? "loadzone" : "map", name.data()));

			game::qos::XZoneInfo zone_info
			{
				load_fastfile.data(),
				xzone_alloc_map_load,
				xzone_free_map_load
			};
			game::DB_LoadXAssets(&zone_info, 1, false);
			game::DB_SyncXAssets();

			// TODO: export sounds (Louve seems to have some big function for this, i'll do it later lol)
			console::info("exporting all sounds...\n");

			dump_asset_by_name(game::qos::ASSET_TYPE_COMWORLD, bsp_name, "ComWorld");

			dump_asset_by_name(game::qos::ASSET_TYPE_gameWORLD_MP, bsp_name, "GameWorld");

			dump_asset_by_name(game::qos::ASSET_TYPE_GFXWORLD, bsp_name, "GfxWorld");

			dump_asset_by_name(game::qos::ASSET_TYPE_CLIPMAP_MP, bsp_name, "ClipMap");

			// This is redundant with clipmap in iw3x-port too, but still helps discover more referenced models.
			dump_map_ents(bsp_name);

			dump_asset_by_name(game::qos::ASSET_TYPE_RAWFILE, utils::string::va("vision/%s.vision", name.data()), "vision");
			dump_asset_by_name(game::qos::ASSET_TYPE_RAWFILE, utils::string::va("sun/%s.sun", name.data()), "sun");
			dump_asset_by_name(game::qos::ASSET_TYPE_RAWFILE, utils::string::va("maps/%s%s.gsc", is_singleplayer ? "" : "mp/", name.data()), "map gsc");
			dump_asset_by_name(game::qos::ASSET_TYPE_RAWFILE, utils::string::va("maps/%s%s_fx.gsc", is_singleplayer ? "" : "mp/", name.data()), "map fx gsc");
			dump_asset_by_name(game::qos::ASSET_TYPE_RAWFILE, utils::string::va("maps/createfx/%s_fx.gsc", name.data()), "createfx gsc");

			dump_asset_by_name(game::qos::ASSET_TYPE_MATERIAL, utils::string::va("compass_map_%s", name.data()), "compass material");
			dump_asset_by_name(game::qos::ASSET_TYPE_MATERIAL, "$levelbriefing", "loadscreen material");
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			api = new iw4of::api(get_params());

			if (runtime::is_standalone_xport_mode())
			{
				return;
			}

			scheduler::once([&]()
			{
				command::add("dumpmap", [](const command::params& params)
				{
#ifndef DEBUG
					console::error("dumpmap is not supported yet!\n");
					return;
#else
					if (params.size() < 2)
					{
						console::info("USAGE: dumpmap <name>\n");
						return;
					}

					console::warn("dumpmap is not fully supported yet!\n");

					std::string name = params[1];

					api->set_work_path("qos-exp/dump");

					dump_map(name);
					console::info("map '%s' successfully exported.\n", name.data());
				});
#endif
				command::set_help("dumpmap", "Load a map and export its supported world assets and rawfiles.", "dumpmap mp_backlot");
			}, scheduler::main);
		}
	};
}

REGISTER_COMPONENT(map_dumper::component)

