#pragma once
#include "types.hpp"
#include "mesh_simplifier.hpp"
#include "texture_processor.hpp"
#include <optional>
#include <vector>

namespace lodgen
{

struct LodInfo
{
    float ratio;
    fs::path outputPath;
    std::vector<SimplifyResult> meshResults;
    std::optional<TextureStats> textureStats;
};

// Generate a single LOD from a scene at the given triangle ratio
Result<ScenePtr> generateLod(
    const aiScene* scene,
    float ratio,
    const TextureOptions* texOpts = nullptr );

// Generate multiple LODs and save to disk.
// Each LOD is saved to: outputDir/lod{1..n}/{filename}{ext}
// Returns per-LOD info for logging/reporting.
Result<std::vector<LodInfo>> generateLods(
    const aiScene* scene,
    const fs::path& inputPath,
    const fs::path& outputDir,
    const std::vector<float>& ratios,
    const TextureOptions* texOpts = nullptr );

} // namespace lodgen
