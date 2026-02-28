#pragma once
#include "types.hpp"
#include "texture_processor.hpp"
#include <assimp/scene.h>
#include <string>

namespace lodgen
{

struct AtlasOptions
{
    fs::path modelDir;  // source model directory — to resolve external texture paths
    fs::path outputDir; // where atlas_<type>.png files are written
};

struct AtlasInfo
{
    std::string    filename;    // e.g. "atlas_diffuse.png"
    aiTextureType  type;        // texture type this atlas covers
    unsigned int   inputCount;  // number of unique textures packed
    unsigned int   width;
    unsigned int   height;
};

// Builds one PNG atlas per texture type (diffuse, specular, normal, etc.)
// that has at least one texture referenced across all materials.
//
// Each atlas is:
//   - written to outputDir as atlas_<typename>.png
//   - embedded in the scene as a new mTextures[N] entry (mFilename = filename)
//   - referenced by material slots of that type via "*N"
//
// UV remapping:
//   Mesh UVs are remapped once using the DIFFUSE atlas layout. All texture
//   types for the same material are packed in the same relative position within
//   their respective atlases, so the same remapped UVs address correctly in
//   every per-type atlas.
//
// External texture files that were baked into atlases are removed from outputDir.
//
// Precondition: processTextures may have already run (external files in outputDir)
//               or not (original files in modelDir are used directly).
Result<std::vector<AtlasInfo>> buildAtlas( aiScene* scene, const AtlasOptions& opts );

} // namespace lodgen
