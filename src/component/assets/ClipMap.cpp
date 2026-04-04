#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "component/assethandler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/map_dumper.hpp"
#include "component/scheduler.hpp"
#include "../../runtime.hpp"

#include "game/game.hpp"
#include "game/structs.IW4.hpp"

#include <utils/memory.hpp>

namespace clipmap
{
	namespace
	{
		game::iw4::SModelAabbNode* build_simple_smodel_nodes(game::iw4::clipMap_t* clipmap, unsigned short* size)
		{
			auto* node = utils::memory::allocate<game::iw4::SModelAabbNode>();
			ZeroMemory(node, sizeof(*node));

			if (!clipmap || !size)
			{
				return node;
			}

			*size = 1;

			if (!clipmap->staticModelList || !clipmap->numStaticModels)
			{
				return node;
			}

			float mins[3];
			float maxs[3];
			std::memcpy(mins, clipmap->staticModelList[0].absBounds.midPoint, sizeof(mins));
			std::memcpy(maxs, clipmap->staticModelList[0].absBounds.midPoint, sizeof(maxs));
			for (auto i = 0; i < 3; ++i)
			{
				mins[i] -= clipmap->staticModelList[0].absBounds.halfSize[i];
				maxs[i] += clipmap->staticModelList[0].absBounds.halfSize[i];
			}

			for (unsigned int i = 1; i < clipmap->numStaticModels; ++i)
			{
				float this_mins[3];
				float this_maxs[3];
				for (auto j = 0; j < 3; ++j)
				{
					this_mins[j] = clipmap->staticModelList[i].absBounds.midPoint[j] - clipmap->staticModelList[i].absBounds.halfSize[j];
					this_maxs[j] = clipmap->staticModelList[i].absBounds.midPoint[j] + clipmap->staticModelList[i].absBounds.halfSize[j];
					mins[j] = std::min(mins[j], this_mins[j]);
					maxs[j] = std::max(maxs[j], this_maxs[j]);
				}
			}

			node->bounds.compute(mins, maxs);
			node->firstChild = 0;
			node->childCount = static_cast<unsigned short>(clipmap->numStaticModels);
			return node;
		}

		void add_triggers(game::iw4::clipMap_t* clipmap)
		{
			if (!clipmap || !clipmap->mapEnts || !clipmap->cmodels || !clipmap->leafbrushNodes || !clipmap->brushes)
			{
				return;
			}

			clipmap->mapEnts->trigger.count = static_cast<unsigned short>(clipmap->numSubModels);
			clipmap->mapEnts->trigger.hullCount = static_cast<unsigned short>(clipmap->numSubModels);
			clipmap->mapEnts->trigger.models = utils::memory::allocate_array<game::iw4::TriggerModel>(clipmap->numSubModels);
			clipmap->mapEnts->trigger.hulls = utils::memory::allocate_array<game::iw4::TriggerHull>(clipmap->numSubModels);

			std::vector<game::iw4::TriggerSlab> slabs;

			for (unsigned short i = 0; i < clipmap->mapEnts->trigger.count; ++i)
			{
				auto& trigger_model = clipmap->mapEnts->trigger.models[i];
				auto& trigger_hull = clipmap->mapEnts->trigger.hulls[i];

				trigger_hull.bounds = clipmap->cmodels[i].bounds;
				trigger_hull.contents = clipmap->cmodels[i].leaf.brushContents | clipmap->cmodels[i].leaf.terrainContents;
				trigger_model.contents = trigger_hull.contents;
				trigger_model.firstHull = i;
				trigger_model.hullCount = 1;

				const auto leaf_brush_node = clipmap->cmodels[i].leaf.leafBrushNode;
				if (leaf_brush_node < 0 || static_cast<unsigned int>(leaf_brush_node) >= clipmap->leafbrushNodesCount)
				{
					continue;
				}

				auto* node = &clipmap->leafbrushNodes[leaf_brush_node];
				if (node->leafBrushCount <= 0 || !node->data.leaf.brushes)
				{
					continue;
				}

				const auto base_slab = slabs.size();

				for (int brush_ref = 0; brush_ref < node->leafBrushCount; ++brush_ref)
				{
					const auto brush_index = node->data.leaf.brushes[brush_ref];
					if (brush_index >= clipmap->numBrushes)
					{
						continue;
					}

					auto* brush = &clipmap->brushes[brush_index];
					for (unsigned int side_index = 0; side_index < brush->numsides; ++side_index)
					{
						auto& slab = slabs.emplace_back();
						slab.dir[0] = brush->sides[side_index].plane->normal[0];
						slab.dir[1] = brush->sides[side_index].plane->normal[1];
						slab.dir[2] = brush->sides[side_index].plane->normal[2];
						slab.midPoint = 0.0f;
						slab.halfSize = brush->sides[side_index].plane->dist;
					}
				}

				trigger_hull.firstSlab = static_cast<unsigned short>(base_slab);
				trigger_hull.slabCount = static_cast<unsigned short>(slabs.size() - base_slab);
			}

			clipmap->mapEnts->trigger.slabCount = static_cast<int>(slabs.size());
			clipmap->mapEnts->trigger.slabs = utils::memory::allocate_array<game::iw4::TriggerSlab>(slabs.size());
			for (size_t i = 0; i < slabs.size(); ++i)
			{
				clipmap->mapEnts->trigger.slabs[i] = slabs[i];
			}
		}

		void log_clipmap_stats(const game::qos::clipMap_t* clipmap)
		{
			if (!clipmap)
			{
				return;
			}

			console::info(
				"clipmap stats: smodels=%u materials=%u brushsides=%u leafs=%u submodels=%u brushes=%hu\n",
				clipmap->numStaticModels,
				clipmap->numMaterials,
				clipmap->numBrushSides,
				clipmap->numLeafs,
				clipmap->numSubModels,
				clipmap->numBrushes
			);
		}

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

			iw4_asset->numStaticModels = clipmap->numStaticModels;
			if (clipmap->staticModelList && clipmap->numStaticModels)
			{
				iw4_asset->staticModelList = utils::memory::allocate_array<game::iw4::cStaticModel_t>(clipmap->numStaticModels);
				for (unsigned int i = 0; i < clipmap->numStaticModels; ++i)
				{
					auto* source = &clipmap->staticModelList[i];
					auto* target = &iw4_asset->staticModelList[i];

					std::memcpy(target->origin, source->origin, sizeof(target->origin));
					std::memcpy(target->invScaledAxis, source->invScaledAxis, sizeof(target->invScaledAxis));
					target->absBounds.compute(source->absmin, source->absmax);

					if (source->xmodel)
					{
						target->xmodel = assethandler::convert_asset_header(game::qos::ASSET_TYPE_XMODEL, { source->xmodel }).model;
					}
				}
			}

			iw4_asset->numMaterials = clipmap->numMaterials;
			if (clipmap->materials && clipmap->numMaterials)
			{
				iw4_asset->materials = utils::memory::allocate_array<game::iw4::dmaterial_t>(clipmap->numMaterials);
				for (unsigned int i = 0; i < clipmap->numMaterials; ++i)
				{
					auto* source = &clipmap->materials[i];
					auto* target = &iw4_asset->materials[i];
					target->surfaceFlags = source->surfaceFlags;
					target->contentFlags = source->contentFlags;
					target->material = utils::memory::duplicate_string(source->material);
				}
			}

			iw4_asset->numBrushSides = clipmap->numBrushSides;
			if (clipmap->brushsides && clipmap->numBrushSides)
			{
				iw4_asset->brushsides = utils::memory::allocate_array<game::iw4::cbrushside_t>(clipmap->numBrushSides);
				for (unsigned int i = 0; i < clipmap->numBrushSides; ++i)
				{
					auto* source = &clipmap->brushsides[i];
					auto* target = &iw4_asset->brushsides[i];
					target->plane = source->plane;
					target->materialNum = static_cast<unsigned short>(source->materialNum);
					target->firstAdjacentSideOffset = static_cast<char>(source->firstAdjacentSideOffset);
					target->edgeCount = source->edgeCount;
				}
			}

			iw4_asset->numBrushEdges = clipmap->numBrushEdges;
			iw4_asset->brushEdges = clipmap->brushEdges;
			iw4_asset->numNodes = clipmap->numNodes;
			iw4_asset->nodes = reinterpret_cast<game::iw4::cNode_t*>(clipmap->nodes);
			iw4_asset->numSubModels = clipmap->numSubModels;

			iw4_asset->numLeafs = clipmap->numLeafs;
			if (clipmap->leafs && clipmap->numLeafs)
			{
				iw4_asset->leafs = utils::memory::allocate_array<game::iw4::cLeaf_t>(clipmap->numLeafs);
				for (unsigned int i = 0; i < clipmap->numLeafs; ++i)
				{
					iw4_asset->leafs[i].firstCollAabbIndex = clipmap->leafs[i].firstCollAabbIndex;
					iw4_asset->leafs[i].collAabbCount = clipmap->leafs[i].collAabbCount;
					iw4_asset->leafs[i].brushContents = clipmap->leafs[i].brushContents;
					iw4_asset->leafs[i].terrainContents = clipmap->leafs[i].terrainContents;
					iw4_asset->leafs[i].bounds.compute(clipmap->leafs[i].mins, clipmap->leafs[i].maxs);
					iw4_asset->leafs[i].leafBrushNode = clipmap->leafs[i].leafBrushNode;
				}
			}

			iw4_asset->leafbrushNodesCount = clipmap->leafbrushNodesCount;
			iw4_asset->leafbrushNodes = reinterpret_cast<game::iw4::cLeafBrushNode_s*>(clipmap->leafbrushNodes);
			iw4_asset->numLeafBrushes = clipmap->numLeafBrushes;
			iw4_asset->leafbrushes = clipmap->leafbrushes;
			iw4_asset->numLeafSurfaces = clipmap->numLeafSurfaces;
			iw4_asset->leafsurfaces = clipmap->leafsurfaces;
			iw4_asset->vertCount = clipmap->vertCount;
			iw4_asset->verts = clipmap->verts;
			iw4_asset->triCount = clipmap->triCount;
			iw4_asset->triIndices = clipmap->triIndices;
			iw4_asset->triEdgeIsWalkable = clipmap->triEdgeIsWalkable;
			iw4_asset->borderCount = clipmap->borderCount;
			iw4_asset->borders = reinterpret_cast<game::iw4::CollisionBorder*>(clipmap->borders);
			iw4_asset->partitionCount = clipmap->partitionCount;
			iw4_asset->partitions = reinterpret_cast<game::iw4::CollisionPartition*>(clipmap->partitions);
			iw4_asset->aabbTreeCount = clipmap->aabbTreeCount;
			if (clipmap->aabbTrees && clipmap->aabbTreeCount)
			{
				iw4_asset->aabbTrees = utils::memory::allocate_array<game::iw4::CollisionAabbTree>(clipmap->aabbTreeCount);
				for (int i = 0; i < clipmap->aabbTreeCount; ++i)
				{
					std::memcpy(iw4_asset->aabbTrees[i].midPoint, clipmap->aabbTrees[i].origin, sizeof(iw4_asset->aabbTrees[i].midPoint));
					std::memcpy(iw4_asset->aabbTrees[i].halfSize, clipmap->aabbTrees[i].halfSize, sizeof(iw4_asset->aabbTrees[i].halfSize));
					iw4_asset->aabbTrees[i].materialIndex = clipmap->aabbTrees[i].materialIndex;
					iw4_asset->aabbTrees[i].childCount = clipmap->aabbTrees[i].childCount;
					iw4_asset->aabbTrees[i].u.firstChildIndex = clipmap->aabbTrees[i].u.firstChildIndex;
				}
			}

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

			iw4_asset->numBrushes = clipmap->numBrushes;
			if (clipmap->brushes && clipmap->numBrushes)
			{
				iw4_asset->brushes = utils::memory::allocate_array<game::iw4::cbrush_t>(clipmap->numBrushes);
				iw4_asset->brushBounds = utils::memory::allocate_array<game::iw4::Bounds>(clipmap->numBrushes);
				iw4_asset->brushContents = utils::memory::allocate_array<int>(clipmap->numBrushes);

				for (unsigned int i = 0; i < clipmap->numBrushes; ++i)
				{
					auto* source = &clipmap->brushes[i];
					auto* target = &iw4_asset->brushes[i];

					target->numsides = static_cast<unsigned short>(source->numsides);
					target->glassPieceIndex = 0;
					target->baseAdjacentSide = source->baseAdjacentSide;
					std::memcpy(target->axialMaterialNum, source->axialMaterialNum, sizeof(target->axialMaterialNum));
					for (auto x = 0; x < 2; ++x)
					{
						for (auto y = 0; y < 3; ++y)
						{
							target->firstAdjacentSideOffsets[x][y] = static_cast<char>(source->firstAdjacentSideOffsets[x][y]);
							target->edgeCount[x][y] = source->edgeCount[x][y];
						}
					}

					target->sides = nullptr;
					if (source->sides && clipmap->brushsides)
					{
						const auto side_index = static_cast<unsigned int>(source->sides - clipmap->brushsides);
						if (side_index < clipmap->numBrushSides)
						{
							target->sides = &iw4_asset->brushsides[side_index];
						}
					}

					iw4_asset->brushBounds[i].compute(source->mins, source->maxs);
					iw4_asset->brushContents[i] = source->contents;
				}
			}

			if (clipmap->mapEnts)
			{
				iw4_asset->mapEnts = assethandler::convert_asset_header(game::qos::ASSET_TYPE_MAP_ENTS, { clipmap->mapEnts }).mapEnts;
				iw4_asset->smodelNodes = build_simple_smodel_nodes(iw4_asset, &iw4_asset->smodelNodeCount);
				add_stage(iw4_asset->mapEnts);
				add_triggers(iw4_asset);
			}

			return iw4_asset;
		}
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

					log_clipmap_stats(header.clipMap);

					auto converted = clipmap::convert(header.clipMap);
					map_dumper::api->write(game::iw4::ASSET_TYPE_CLIPMAP_MP, converted);

					console::info("dumpclipmap exported clipmap data for '%s'\n", name);
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
