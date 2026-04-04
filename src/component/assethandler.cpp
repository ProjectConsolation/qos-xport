#include <std_include.hpp>

#include "loader/component_loader.hpp"

#include "component/command.hpp"
#include "component/console.hpp"
#include "component/map_dumper.hpp"
#include "component/scheduler.hpp"
#include "../runtime.hpp"

#include "game/game.hpp"
#include "game/structs.IW4.hpp"

#include <utils/memory.hpp>
#include <utils/string.hpp>

#define QOS_TYPE_TO_IW4(type) { game::qos::type, game::iw4::type },

namespace assethandler
{
	namespace
	{
		struct type_name_entry
		{
			game::qos::XAssetType type;
			const char* name;
		};

		constexpr type_name_entry type_names[] =
		{
			{ game::qos::ASSET_TYPE_PHYSPRESET, "physpreset" },
			{ game::qos::ASSET_TYPE_XANIMPARTS, "xanimparts" },
			{ game::qos::ASSET_TYPE_XMODEL, "xmodel" },
			{ game::qos::ASSET_TYPE_MATERIAL, "material" },
			{ game::qos::ASSET_TYPE_TECHNIQUE_SET, "techset" },
			{ game::qos::ASSET_TYPE_IMAGE, "image" },
			{ game::qos::ASSET_TYPE_SOUND, "sound" },
			{ game::qos::ASSET_TYPE_SOUND_CURVE, "soundcurve" },
			{ game::qos::ASSET_TYPE_CLIPMAP_SP, "clipmap_sp" },
			{ game::qos::ASSET_TYPE_CLIPMAP_MP, "clipmap_mp" },
			{ game::qos::ASSET_TYPE_COMWORLD, "comworld" },
			{ game::qos::ASSET_TYPE_gameWORLD_SP, "gameworld_sp" },
			{ game::qos::ASSET_TYPE_gameWORLD_MP, "gameworld_mp" },
			{ game::qos::ASSET_TYPE_MAP_ENTS, "mapents" },
			{ game::qos::ASSET_TYPE_GFXWORLD, "gfxworld" },
			{ game::qos::ASSET_TYPE_LIGHT_DEF, "lightdef" },
			{ game::qos::ASSET_TYPE_UI_MAP, "uimap" },
			{ game::qos::ASSET_TYPE_FONT, "font" },
			{ game::qos::ASSET_TYPE_MENULIST, "menulist" },
			{ game::qos::ASSET_TYPE_MENU, "menu" },
			{ game::qos::ASSET_TYPE_LOCALIZE_ENTRY, "localize" },
			{ game::qos::ASSET_TYPE_WEAPON, "weapon" },
			{ game::qos::ASSET_TYPE_SNDDRIVER_GLOBALS, "snddriver_globals" },
			{ game::qos::ASSET_TYPE_FX, "fx" },
			{ game::qos::ASSET_TYPE_IMPACT_FX, "impactfx" },
			{ game::qos::ASSET_TYPE_AITYPE, "aitype" },
			{ game::qos::ASSET_TYPE_MPTYPE, "mptype" },
			{ game::qos::ASSET_TYPE_CHARACTER, "character" },
			{ game::qos::ASSET_TYPE_XMODELALIAS, "xmodelalias" },
			{ game::qos::ASSET_TYPE_RAWFILE, "rawfile" },
			{ game::qos::ASSET_TYPE_STRINGTABLE, "stringtable" },
		};

		bool has_converter(const game::qos::XAssetType type)
		{
			return std::any_of(component_loader::get_components().begin(), component_loader::get_components().end(),
				[&](const auto& component)
			{
				return component->get_type() == type;
			});
		}
	}

	struct processed_asset
	{
		void* original_ptr;
		game::iw4::XAssetHeader converted_asset;
		game::qos::XAssetType qos_type;
	};

	std::vector<processed_asset> converted_assets;

	std::unordered_map<game::qos::XAssetType, game::iw4::XAssetType> type_conversion_table = {
		//QOS_TYPE_TO_IW4(ASSET_TYPE_XMODELPIECES)		// not on IW4?
		QOS_TYPE_TO_IW4(ASSET_TYPE_PHYSPRESET)
		//QOS_TYPE_TO_IW4(ASSET_TYPE_PHYSCONSTRAINTS)	// assets are from T4 and dont exist on IW3/IW4
		//QOS_TYPE_TO_IW4(ASSET_TYPE_DESTRUCTIBLE_DEF)	// ^
		QOS_TYPE_TO_IW4(ASSET_TYPE_XANIMPARTS)
		QOS_TYPE_TO_IW4(ASSET_TYPE_XMODEL)
		QOS_TYPE_TO_IW4(ASSET_TYPE_MATERIAL)
		QOS_TYPE_TO_IW4(ASSET_TYPE_TECHNIQUE_SET)
		QOS_TYPE_TO_IW4(ASSET_TYPE_IMAGE)
		QOS_TYPE_TO_IW4(ASSET_TYPE_SOUND)
		QOS_TYPE_TO_IW4(ASSET_TYPE_SOUND_CURVE)
		//QOS_TYPE_TO_IW4(ASSET_TYPE_LOADED_SOUND)		// no loaded sounds in QoS?
		QOS_TYPE_TO_IW4(ASSET_TYPE_CLIPMAP_SP)
		QOS_TYPE_TO_IW4(ASSET_TYPE_CLIPMAP_MP)
		QOS_TYPE_TO_IW4(ASSET_TYPE_COMWORLD)
		QOS_TYPE_TO_IW4(ASSET_TYPE_gameWORLD_SP)
		QOS_TYPE_TO_IW4(ASSET_TYPE_gameWORLD_MP)
		QOS_TYPE_TO_IW4(ASSET_TYPE_MAP_ENTS)
		QOS_TYPE_TO_IW4(ASSET_TYPE_GFXWORLD)
		QOS_TYPE_TO_IW4(ASSET_TYPE_LIGHT_DEF)
		QOS_TYPE_TO_IW4(ASSET_TYPE_UI_MAP)
		QOS_TYPE_TO_IW4(ASSET_TYPE_FONT)
		QOS_TYPE_TO_IW4(ASSET_TYPE_MENULIST)
		QOS_TYPE_TO_IW4(ASSET_TYPE_MENU)
		QOS_TYPE_TO_IW4(ASSET_TYPE_LOCALIZE_ENTRY)
		QOS_TYPE_TO_IW4(ASSET_TYPE_WEAPON)
		QOS_TYPE_TO_IW4(ASSET_TYPE_SNDDRIVER_GLOBALS)
		QOS_TYPE_TO_IW4(ASSET_TYPE_FX)
		QOS_TYPE_TO_IW4(ASSET_TYPE_IMPACT_FX)
		QOS_TYPE_TO_IW4(ASSET_TYPE_AITYPE)
		QOS_TYPE_TO_IW4(ASSET_TYPE_MPTYPE)
		QOS_TYPE_TO_IW4(ASSET_TYPE_CHARACTER)
		QOS_TYPE_TO_IW4(ASSET_TYPE_XMODELALIAS)
		QOS_TYPE_TO_IW4(ASSET_TYPE_RAWFILE)
		QOS_TYPE_TO_IW4(ASSET_TYPE_STRINGTABLE)

		// these assets only exist in QoS
		//QOS_TYPE_TO_IW4(ASSET_TYPE_XML_TREE)
		//QOS_TYPE_TO_IW4(ASSET_TYPE_SCENE_ANIM_RESOURCE)
		//QOS_TYPE_TO_IW4(ASSET_TYPE_CUTSCENE_RESOURCE)
		//QOS_TYPE_TO_IW4(ASSET_TYPE_CUSTOM_CAMERA_LIST)

		QOS_TYPE_TO_IW4(ASSET_TYPE_COUNT)
		QOS_TYPE_TO_IW4(ASSET_TYPE_STRING)
		QOS_TYPE_TO_IW4(ASSET_TYPE_ASSETLIST)
	};

	utils::memory::allocator local_allocator{};

	bool is_supported_type(const game::qos::XAssetType type)
	{
		return type_conversion_table.contains(type) && has_converter(type);
	}

	const char* type_to_string(const game::qos::XAssetType type)
	{
		const auto entry = std::find_if(std::begin(type_names), std::end(type_names), [&](const type_name_entry& value)
		{
			return value.type == type;
		});

		if (entry != std::end(type_names))
		{
			return entry->name;
		}

		return "unknown";
	}

	std::optional<game::qos::XAssetType> type_from_string(const std::string& type)
	{
		const auto normalized = utils::string::to_lower(type);
		const auto entry = std::find_if(std::begin(type_names), std::end(type_names), [&](const type_name_entry& value)
		{
			return normalized == value.name;
		});

		if (entry != std::end(type_names))
		{
			return entry->type;
		}

		return std::nullopt;
	}

	const char* get_asset_name(const game::qos::XAssetType type, const game::qos::XAssetHeader header)
	{
		switch (type)
		{
		case game::qos::ASSET_TYPE_PHYSPRESET:
			return header.physPreset ? header.physPreset->name : nullptr;
		case game::qos::ASSET_TYPE_XMODEL:
			return header.xmodel ? header.xmodel->name : nullptr;
		case game::qos::ASSET_TYPE_MATERIAL:
			return header.material ? header.material->name : nullptr;
		case game::qos::ASSET_TYPE_IMAGE:
			return header.image ? header.image->name : nullptr;
		case game::qos::ASSET_TYPE_COMWORLD:
			return header.comWorld ? header.comWorld->name : nullptr;
		case game::qos::ASSET_TYPE_gameWORLD_MP:
			return header.gameWorldMp ? header.gameWorldMp->name : nullptr;
		case game::qos::ASSET_TYPE_CLIPMAP_SP:
		case game::qos::ASSET_TYPE_CLIPMAP_MP:
			return header.clipMap ? header.clipMap->name : nullptr;
		case game::qos::ASSET_TYPE_MAP_ENTS:
			return header.data ? reinterpret_cast<game::qos::MapEnts*>(header.data)->name : nullptr;
		case game::qos::ASSET_TYPE_GFXWORLD:
			return header.gfxWorld ? header.gfxWorld->name : nullptr;
		case game::qos::ASSET_TYPE_MENULIST:
			return header.menuList ? header.menuList->name : nullptr;
		case game::qos::ASSET_TYPE_MENU:
			return header.menu ? header.menu->window.name : nullptr;
		case game::qos::ASSET_TYPE_RAWFILE:
			return header.rawfile ? header.rawfile->name : nullptr;
		default:
			return nullptr;
		}
	}

	game::iw4::XAssetHeader convert_asset_header(game::qos::XAssetType type, game::qos::XAssetHeader header)
	{
		const auto& existing = std::find_if(converted_assets.begin(), converted_assets.end(), [&](processed_asset& asset)
		{
			return (asset.qos_type == type && asset.original_ptr == header.data);
		});

		if (existing != converted_assets.end())
		{
			return { existing._Ptr->converted_asset.data };
		}

		// TODO: review this, this is probably veryyy slow to do lol
		for (const auto& component_ : component_loader::get_components())
		{
			if (component_->get_type() != game::qos::ASSET_TYPE_INVALID &&
				component_->get_type() == type)
			{
				const auto& result = component_->convert_asset(header);
				converted_assets.push_back({ header.data, result, type });
				return result;
			}
		}

		return { nullptr };
	}

	bool dump_asset(const game::qos::XAssetType type, const game::qos::XAssetHeader asset)
	{
		if (!is_supported_type(type))
		{
			return false;
		}

		const auto converted = convert_asset_header(type, asset);
		if (converted.data)
		{
			map_dumper::api->write(type_conversion_table[type], converted.data);
			return true;
		}

		return false;
	}

	bool dump_asset_by_name(const game::qos::XAssetType type, const std::string& name)
	{
		game::DB_SyncXAssets();

		const auto header = game::DB_FindXAssetHeader(type, name.data());
		if (!header.data || game::DB_IsXAssetDefault(type, name.data()))
		{
			return false;
		}

		return dump_asset(type, header);
	}

	std::size_t dump_all_assets_of_type(const game::qos::XAssetType type, const std::string& filter)
	{
		game::DB_SyncXAssets();

		std::size_t dumped = 0;
		const auto lowered_filter = utils::string::to_lower(filter);

		game::DB_EnumXAssetEntries(type, [&](game::qos::XAssetEntryPoolEntry* asset)
		{
			if (!asset || !asset->entry.inuse)
			{
				return;
			}

			const auto name = get_asset_name(type, asset->entry.asset.header);
			if (!name || !name[0])
			{
				return;
			}

			if (!lowered_filter.empty())
			{
				const auto lowered_name = utils::string::to_lower(name);
				if (lowered_name.find(lowered_filter) == std::string::npos)
				{
					return;
				}
			}

			if (game::DB_IsXAssetDefault(type, name))
			{
				return;
			}

			if (dump_asset(type, asset->entry.asset.header))
			{
				++dumped;
			}
		}, false);

		return dumped;
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			if (runtime::is_standalone_xport_mode())
			{
				return;
			}

			scheduler::once([&]()
			{
				command::add("listassettypes", []()
				{
					console::info("supported export types:\n");
					for (const auto& entry : type_names)
					{
						if (is_supported_type(entry.type))
						{
							console::info(" - %s\n", entry.name);
						}
					}
				});

				command::add("dumpasset", [](const command::params& params)
				{
					if (params.size() < 3)
					{
						console::info("USAGE: dumpasset <type> <name>\n");
						return;
					}

					const auto type = type_from_string(params[1]);
					if (!type.has_value())
					{
						console::error("unknown asset type '%s'\n", params[1]);
						return;
					}

					if (!is_supported_type(*type))
					{
						console::error("asset type '%s' is not exportable yet\n", params[1]);
						return;
					}

					const auto name = params.join(2);
					if (!dump_asset_by_name(*type, name))
					{
						console::error("failed to dump %s '%s'\n", type_to_string(*type), name.data());
						return;
					}

					console::info("dumped %s '%s'\n", type_to_string(*type), name.data());
				});

				command::add("dumpallassets", [](const command::params& params)
				{
					if (params.size() < 2)
					{
						console::info("USAGE: dumpallassets <type> [name_filter]\n");
						return;
					}

					const auto type = type_from_string(params[1]);
					if (!type.has_value())
					{
						console::error("unknown asset type '%s'\n", params[1]);
						return;
					}

					if (!is_supported_type(*type))
					{
						console::error("asset type '%s' is not exportable yet\n", params[1]);
						return;
					}

					const auto filter = params.size() >= 3 ? params.join(2) : "";
					const auto dumped = dump_all_assets_of_type(*type, filter);
					console::info("dumped %zu %s asset(s)\n", dumped, type_to_string(*type));
				});
			}, scheduler::main);
		}

		void pre_destroy() override
		{
			converted_assets.clear();
		}
	};
}

REGISTER_COMPONENT(assethandler::component)
