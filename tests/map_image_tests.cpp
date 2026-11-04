#include "map/map_images.h"
#include "map/scenario_form.h"

#include <cstddef>
#include <future>
#include <iostream>
#include <string_view>

namespace
{

using namespace rerevved::tools;

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testGenerateMapTerrain()
{
    MapDocument tiles{};
    tiles.core.fill(std::byte{ 2 });
    tiles.core[3 * 32 + 7] = std::byte{ 6 };
    CreatorWorkspace workspace;
    expect(workspace.StartMapDraft("generate").has_value(), "start a map for generation");
    const auto generated = GenerateMapTerrain(workspace, tiles);
    expect(generated.has_value(), "tiles generate all three images");
    const auto& draft = workspace.Map();
    expect(draft.heights && draft.terrain[0] && draft.terrain[1], "generation records heights, blends and lightmap");
    if (!draft.heights || !draft.terrain[0] || !draft.terrain[1])
    {
        return;
    }
    expect(draft.heights->readBackPassed && draft.terrain[0]->readBackPassed && draft.terrain[1]->readBackPassed, "every generated image passes its DDS read-back");
    expect(draft.heights->generatedFrom == tiles.core && draft.terrain[0]->generatedFrom == tiles.core && draft.terrain[1]->generatedFrom == tiles.core,
           "generated images record the tiles they came from");
    expect(draft.terrain[0]->dds.size() == TerrainDdsByteCount(TerrainTexture::MountainHillBlends) && draft.terrain[1]->dds.size() == TerrainDdsByteCount(TerrainTexture::Lightmap),
           "generated textures carry their DDS");
    ScenarioEditorState state;
    RefreshMapReadiness(workspace, state, nullptr);
    const auto& card = workspace.Map().artifacts[static_cast<std::size_t>(MapArtifact::Heights)];
    expect(card.ready, "generated heights pass source, contract, encoding and read-back");
}

MapDocument sampleTiles()
{
    MapDocument tiles{};
    tiles.core.fill(std::byte{ 2 });
    tiles.core[3 * 32 + 7] = std::byte{ 6 };
    return tiles;
}

void testBuildOnAnotherThread()
{
    const auto tiles = sampleTiles();
    auto       job   = std::async(std::launch::async, [copy = tiles]
                                  {
                              return BuildMapImages(copy);
                                  });
    auto       built = job.get();
    expect(built.has_value(), "a background build succeeds");
    CreatorWorkspace workspace;
    expect(workspace.StartMapDraft("threaded").has_value() && GenerateMapTerrain(workspace, tiles).has_value(), "the synchronous path generates the same map");
    const auto& draft = workspace.Map();
    if (!built || !draft.heights || !draft.terrain[0] || !draft.terrain[1])
    {
        return;
    }
    expect(built->heights.samples == draft.heights->samples && built->blends.dds == draft.terrain[0]->dds && built->lightmap.dds == draft.terrain[1]->dds,
           "a background build gives the same images as GenerateMapTerrain");
}

void testApplyMapImages()
{
    const auto tiles = sampleTiles();
    auto       built = BuildMapImages(tiles);
    expect(built.has_value(), "build the images to apply");
    if (!built)
    {
        return;
    }
    CreatorWorkspace workspace;
    expect(workspace.StartMapDraft("apply").has_value(), "start a map to apply to");
    ApplyMapImages(workspace, std::move(*built));
    const auto& draft = workspace.Map();
    expect(draft.heights && draft.terrain[0] && draft.terrain[1], "apply stores heights, blends and lightmap");
    if (!draft.heights || !draft.terrain[0] || !draft.terrain[1])
    {
        return;
    }
    expect(draft.heights->generatedFrom == tiles.core && draft.terrain[0]->generatedFrom == tiles.core && draft.terrain[1]->generatedFrom == tiles.core,
           "applied images record the tiles they came from");
}

} // namespace

int main()
{
    testGenerateMapTerrain();
    testBuildOnAnotherThread();
    testApplyMapImages();
    if (failures != 0)
    {
        std::cerr << failures << " map-image test(s) failed\n";
    }
    return failures == 0 ? 0 : 1;
}
