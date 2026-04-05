#pragma once

#include <optional>

namespace assethandler
{
	extern std::unordered_map<game::qos::XAssetType, game::iw4::XAssetType> type_conversion_table;

	bool is_supported_type(game::qos::XAssetType type);
	const char* type_to_string(game::qos::XAssetType type);
	std::optional<game::qos::XAssetType> type_from_string(const std::string& type);
	const char* get_asset_name(game::qos::XAssetType type, game::qos::XAssetHeader header);

	game::iw4::XAssetHeader convert_asset_header(game::qos::XAssetType type, game::qos::XAssetHeader header);
	bool dump_asset(game::qos::XAssetType type, game::qos::XAssetHeader asset);
	bool dump_asset_by_name(game::qos::XAssetType type, const std::string& name);
	std::size_t dump_all_assets_of_type(game::qos::XAssetType type, const std::string& filter = {});
}

