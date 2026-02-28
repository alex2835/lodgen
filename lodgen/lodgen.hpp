#pragma once
#include "types.hpp"
#include "mesh_simplifier.hpp"
#include "texture_processor.hpp"
#include "texture_atlas.hpp"
#include <optional>
#include <vector>

namespace lodgen
{

struct LodInfo
{
    float ratio;
    fs::path outputPath;
    std::vector<SimplifyResult>  meshResults;
    std::optional<TextureStats>  textureStats; // set if processTextures ran
    std::vector<AtlasInfo>       atlasInfos;   // set if buildLodAtlas ran
};

// Generate a single LOD scene in memory (no disk I/O).
Result<ScenePtr> generateLod(
    const aiScene* scene,
    float ratio,
    const TextureOptions* texOpts = nullptr );

// Generate multiple LODs, save each to outputDir/lod{1..n}/{stem}lod{n}{ext}.
// Mesh simplification and optional texture resize only — atlas is a separate step.
Result<std::vector<LodInfo>> generateLods(
    const aiScene* scene,
    const fs::path& inputPath,
    const fs::path& outputDir,
    const std::vector<float>& ratios,
    const TextureOptions* texOpts = nullptr );

// Build per-type PNG atlases for a single saved LOD model.
// Call after generateLods — modelPath is the saved .glb/.fbx/etc. file.
// Reads textures from modelDir (originals) or outputDir (resized copies),
// builds atlas_<type>.png per type, updates model materials, re-saves.
Result<std::vector<AtlasInfo>> buildLodAtlas(
    const fs::path& modelPath,
    const AtlasOptions& opts );

} // namespace lodgen
