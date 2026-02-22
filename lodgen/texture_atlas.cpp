#include "texture_atlas.hpp"
#include <assimp/material.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lodgen
{

static unsigned int nextPow2( unsigned int v )
{
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return ++v;
}

struct AtlasRegion
{
    unsigned int texIndex;
    int x, y, w, h;
};

static std::vector<AtlasRegion> shelfPack(
    const std::vector<DecodedTexture>& textures,
    int atlasW, int& atlasH )
{
    // Sort indices by height descending for better packing
    std::vector<unsigned int> order( textures.size() );
    for ( unsigned int i = 0; i < order.size(); ++i )
        order[i] = i;
    std::sort( order.begin(), order.end(), [&]( unsigned int a, unsigned int b ) {
        return textures[a].height > textures[b].height;
    } );

    std::vector<AtlasRegion> regions( textures.size() );
    int curX = 0, curY = 0, shelfH = 0;

    for ( unsigned int idx : order )
    {
        int w = textures[idx].width;
        int h = textures[idx].height;

        if ( curX + w > atlasW )
        {
            curY   += shelfH;
            curX    = 0;
            shelfH  = 0;
        }

        regions[idx] = { idx, curX, curY, w, h };
        curX   += w;
        shelfH  = std::max( shelfH, h );
    }

    atlasH = static_cast<int>( nextPow2( static_cast<unsigned int>( curY + shelfH ) ) );
    return regions;
}

Result<TextureStats> buildAtlas( aiScene* scene )
{
    if ( scene->mNumTextures == 0 )
        return TextureStats{};

    // Decode all embedded textures
    std::vector<DecodedTexture> decoded;
    decoded.reserve( scene->mNumTextures );
    for ( unsigned int i = 0; i < scene->mNumTextures; ++i )
    {
        auto result = decodeTexture( scene->mTextures[i] );
        if ( !result )
            return std::unexpected( result.error() );
        decoded.push_back( std::move( *result ) );
    }

    // Compute atlas width
    int maxW = 0;
    for ( const auto& t : decoded )
        maxW = std::max( maxW, t.width );
    int n      = static_cast<int>( decoded.size() );
    int atlasW = static_cast<int>( nextPow2(
        static_cast<unsigned int>( maxW * static_cast<int>( std::ceil( std::sqrt( n ) ) ) ) ) );
    atlasW = std::min( atlasW, 8192 );

    int atlasH = 0;
    auto regions = shelfPack( decoded, atlasW, atlasH );

    if ( atlasH > 8192 )
        return std::unexpected( Error{ ErrorCode::AtlasBuildFailed,
            "Atlas height exceeds 8192px — too many or too large textures" } );

    // Blit all textures into atlas buffer
    std::vector<unsigned char> atlasPixels( static_cast<size_t>( atlasW * atlasH ) * 4, 0 );
    for ( const auto& reg : regions )
    {
        const auto& src = decoded[reg.texIndex];
        for ( int row = 0; row < reg.h; ++row )
        {
            std::memcpy(
                &atlasPixels[static_cast<size_t>( ( reg.y + row ) * atlasW + reg.x ) * 4],
                &src.pixels[static_cast<size_t>( row * reg.w ) * 4],
                static_cast<size_t>( reg.w ) * 4 );
        }
    }

    // Build per-material region lookup (first embedded texture of any type wins)
    std::vector<const AtlasRegion*> matRegion( scene->mNumMaterials, nullptr );
    for ( unsigned int m = 0; m < scene->mNumMaterials; ++m )
    {
        aiMaterial* mat = scene->mMaterials[m];
        for ( aiTextureType type : kTextureTypes )
        {
            if ( mat->GetTextureCount( type ) == 0 )
                continue;
            aiString aiPath;
            mat->GetTexture( type, 0, &aiPath );
            const aiTexture* embedded = scene->GetEmbeddedTexture( aiPath.C_Str() );
            if ( embedded )
            {
                std::string p = aiPath.C_Str();
                if ( p.size() > 1 && p[0] == '*' )
                {
                    unsigned int idx = static_cast<unsigned int>( std::stoul( p.substr( 1 ) ) );
                    if ( idx < regions.size() )
                    {
                        matRegion[m] = &regions[idx];
                        break;
                    }
                }
            }
        }
    }

    // Remap UV coordinates for each mesh
    for ( unsigned int mi = 0; mi < scene->mNumMeshes; ++mi )
    {
        aiMesh* mesh = scene->mMeshes[mi];
        if ( mesh->mMaterialIndex >= scene->mNumMaterials )
            continue;
        const AtlasRegion* reg = matRegion[mesh->mMaterialIndex];
        if ( !reg )
            continue;

        float u0     = static_cast<float>( reg->x ) / atlasW;
        float v0     = static_cast<float>( reg->y ) / atlasH;
        float uScale = static_cast<float>( reg->w ) / atlasW;
        float vScale = static_cast<float>( reg->h ) / atlasH;

        for ( unsigned int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch )
        {
            if ( !mesh->mTextureCoords[ch] )
                break;
            for ( unsigned int v = 0; v < mesh->mNumVertices; ++v )
            {
                aiVector3D& uv = mesh->mTextureCoords[ch][v];
                uv.x = u0 + uv.x * uScale;
                uv.y = v0 + uv.y * vScale;
            }
        }
    }

    // Update all material texture paths to "*0" and set wrap mode to clamp
    aiString atlasPath( "*0" );
    int clampMode = aiTextureMapMode_Clamp;
    for ( unsigned int m = 0; m < scene->mNumMaterials; ++m )
    {
        aiMaterial* mat = scene->mMaterials[m];
        for ( aiTextureType type : kTextureTypes )
        {
            unsigned int count = mat->GetTextureCount( type );
            for ( unsigned int slot = 0; slot < count; ++slot )
            {
                mat->AddProperty( &atlasPath, AI_MATKEY_TEXTURE( type, slot ) );
                mat->AddProperty( &clampMode, 1, AI_MATKEY_MAPPINGMODE_U( type, slot ) );
                mat->AddProperty( &clampMode, 1, AI_MATKEY_MAPPINGMODE_V( type, slot ) );
            }
        }
    }

    // Encode atlas as PNG
    std::vector<unsigned char> encoded;
    auto writeFunc = []( void* ctx, void* data, int size ) {
        auto* buf = static_cast<std::vector<unsigned char>*>( ctx );
        auto* ptr = static_cast<unsigned char*>( data );
        buf->insert( buf->end(), ptr, ptr + size );
    };
    if ( !stbi_write_png_to_func( writeFunc, &encoded, atlasW, atlasH, 4, atlasPixels.data(), atlasW * 4 ) )
        return std::unexpected( Error{ ErrorCode::AtlasBuildFailed, "Failed to encode atlas PNG" } );

    // Replace scene textures with single atlas
    for ( unsigned int i = 0; i < scene->mNumTextures; ++i )
        delete scene->mTextures[i];
    delete[] scene->mTextures;

    aiTexture* atlas = new aiTexture();
    atlas->mHeight   = 0;
    atlas->mWidth    = static_cast<unsigned int>( encoded.size() );
    atlas->pcData    = reinterpret_cast<aiTexel*>( new unsigned char[encoded.size()] );
    std::memcpy( atlas->pcData, encoded.data(), encoded.size() );
    std::strncpy( atlas->achFormatHint, "png", HINTMAXTEXTURELEN - 1 );
    atlas->mFilename = aiString( "atlas.png" );

    scene->mTextures    = new aiTexture*[1]{ atlas };
    scene->mNumTextures = 1;

    TextureStats stats;
    stats.outputCount = 1;
    stats.atlasWidth  = static_cast<unsigned int>( atlasW );
    stats.atlasHeight = static_cast<unsigned int>( atlasH );
    return stats;
}

} // namespace lodgen
