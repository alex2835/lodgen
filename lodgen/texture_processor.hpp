#pragma once
#include "types.hpp"
#include <assimp/scene.h>
#include <assimp/material.h>
#include <vector>
#include <string>

namespace lodgen
{

// All texture slot types recognised by assimp that we process
inline constexpr aiTextureType kTextureTypes[] = {
    aiTextureType_DIFFUSE,
    aiTextureType_SPECULAR,
    aiTextureType_AMBIENT,
    aiTextureType_EMISSIVE,
    aiTextureType_HEIGHT,
    aiTextureType_NORMALS,
    aiTextureType_SHININESS,
    aiTextureType_OPACITY,
    aiTextureType_DISPLACEMENT,
    aiTextureType_LIGHTMAP,
    aiTextureType_REFLECTION,
    aiTextureType_BASE_COLOR,
    aiTextureType_NORMAL_CAMERA,
    aiTextureType_EMISSION_COLOR,
    aiTextureType_METALNESS,
    aiTextureType_DIFFUSE_ROUGHNESS,
    aiTextureType_AMBIENT_OCCLUSION,
    aiTextureType_SHEEN,
    aiTextureType_CLEARCOAT,
    aiTextureType_TRANSMISSION,
};

struct TextureOptions
{
    bool     resizeTextures = true;  // downscale proportional to mesh ratio
    bool     buildAtlas     = false; // pack all textures into one atlas image
    fs::path modelDir;               // source model directory — for resolving external texture paths
    fs::path outputDir;              // LOD output directory — resized external files are written here
};

struct TextureStats
{
    unsigned int inputCount  = 0;
    unsigned int outputCount = 0; // 1 if atlased
    unsigned int atlasWidth  = 0;
    unsigned int atlasHeight = 0;
};

struct DecodedTexture
{
    int width      = 0;
    int height     = 0;
    std::vector<unsigned char> pixels; // RGBA8, row-major
    std::string formatHint;            // "png", "jpg", or ""
};

Result<DecodedTexture> decodeTexture( const aiTexture* tex );
Result<DecodedTexture> resizeTexture( const DecodedTexture& src, int newW, int newH );
Result<std::vector<unsigned char>> encodeTexture( const DecodedTexture& tex, const std::string& hint );
Result<DecodedTexture> loadExternalTexture( const fs::path& path );

// Processes all textures referenced by materials:
//   - Embedded textures (*N): resized in-place, stay embedded, mFilename set for exporters.
//   - External textures (file paths): resized and written to opts.outputDir,
//     material paths updated to the new relative filename (stays external).
Result<TextureStats> processTextures( aiScene* scene, float ratio, const TextureOptions& opts );

} // namespace lodgen
