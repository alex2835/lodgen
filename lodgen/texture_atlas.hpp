#pragma once
#include "types.hpp"
#include "texture_processor.hpp"
#include <assimp/scene.h>

namespace lodgen
{

// Packs all embedded textures in the scene into a single atlas.
// Precondition: all textures must already be embedded (call processTextures first).
// Postcondition:
//   - scene->mTextures[0] holds the PNG-encoded atlas
//   - scene->mNumTextures == 1
//   - all material texture paths set to "*0"
//   - all mesh UV coordinates remapped into atlas space
//   - all material wrap modes set to clamp
Result<TextureStats> buildAtlas( aiScene* scene );

} // namespace lodgen
