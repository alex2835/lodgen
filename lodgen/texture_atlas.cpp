#include "texture_atlas.hpp"
#include <assimp/material.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <vector>

namespace lodgen
{

// ── helpers ───────────────────────────────────────────────────────────────────

static unsigned int nextPow2( unsigned int v )
{
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return ++v;
}

struct AtlasRegion { int x, y, w, h; };

static std::vector<AtlasRegion> shelfPack(
    const std::vector<DecodedTexture>& textures, int atlasW, int& atlasH )
{
    std::vector<unsigned int> order( textures.size() );
    for ( unsigned int i = 0; i < order.size(); ++i ) order[i] = i;
    std::sort( order.begin(), order.end(), [&]( unsigned int a, unsigned int b ){
        return textures[a].height > textures[b].height;
    } );

    std::vector<AtlasRegion> regions( textures.size() );
    int curX = 0, curY = 0, shelfH = 0;
    for ( unsigned int idx : order )
    {
        int w = textures[idx].width, h = textures[idx].height;
        if ( curX + w > atlasW ) { curY += shelfH; curX = 0; shelfH = 0; }
        regions[idx] = { curX, curY, w, h };
        curX += w; shelfH = std::max( shelfH, h );
    }
    atlasH = static_cast<int>( nextPow2( static_cast<unsigned int>( curY + shelfH ) ) );
    return regions;
}

// Human-readable suffix for an aiTextureType used in the filename
static const char* typeSuffix( aiTextureType type )
{
    switch ( type )
    {
    case aiTextureType_DIFFUSE:           return "diffuse";
    case aiTextureType_SPECULAR:          return "specular";
    case aiTextureType_AMBIENT:           return "ambient";
    case aiTextureType_EMISSIVE:          return "emissive";
    case aiTextureType_HEIGHT:            return "height";
    case aiTextureType_NORMALS:           return "normal";
    case aiTextureType_SHININESS:         return "shininess";
    case aiTextureType_OPACITY:           return "opacity";
    case aiTextureType_DISPLACEMENT:      return "displacement";
    case aiTextureType_LIGHTMAP:          return "lightmap";
    case aiTextureType_REFLECTION:        return "reflection";
    case aiTextureType_BASE_COLOR:        return "basecolor";
    case aiTextureType_NORMAL_CAMERA:     return "normal_camera";
    case aiTextureType_EMISSION_COLOR:    return "emission";
    case aiTextureType_METALNESS:         return "metalness";
    case aiTextureType_DIFFUSE_ROUGHNESS: return "roughness";
    case aiTextureType_AMBIENT_OCCLUSION: return "ao";
    case aiTextureType_SHEEN:             return "sheen";
    case aiTextureType_CLEARCOAT:         return "clearcoat";
    case aiTextureType_TRANSMISSION:      return "transmission";
    default:                              return "unknown";
    }
}

// Encode RGBA pixels as PNG into a byte vector
static Result<std::vector<unsigned char>> encodePng(
    const std::vector<unsigned char>& pixels, int w, int h )
{
    std::vector<unsigned char> out;
    auto writeFunc = []( void* ctx, void* data, int size ){
        auto* buf = static_cast<std::vector<unsigned char>*>( ctx );
        buf->insert( buf->end(),
                     static_cast<unsigned char*>( data ),
                     static_cast<unsigned char*>( data ) + size );
    };
    if ( !stbi_write_png_to_func( writeFunc, &out, w, h, 4, pixels.data(), w * 4 ) )
        return std::unexpected( Error{ ErrorCode::AtlasBuildFailed,
            "Failed to encode atlas PNG" } );
    return out;
}

// Write bytes to file
static VoidResult writeFile( const fs::path& path,
                              const std::vector<unsigned char>& bytes )
{
    std::ofstream f( path, std::ios::binary );
    if ( !f )
        return std::unexpected( Error{ ErrorCode::AtlasBuildFailed,
            "Cannot open for writing: " + path.string() } );
    f.write( reinterpret_cast<const char*>( bytes.data() ),
             static_cast<std::streamsize>( bytes.size() ) );
    if ( !f )
        return std::unexpected( Error{ ErrorCode::AtlasBuildFailed,
            "Write failed: " + path.string() } );
    return {};
}

// ── main entry point ──────────────────────────────────────────────────────────

Result<std::vector<AtlasInfo>> buildAtlas( aiScene* scene, const AtlasOptions& opts )
{
    // ── Step 1: collect ALL unique source textures across all types/materials ──
    //
    // We maintain a global dedup map (key -> sourceIndex) so that if the same
    // texture file is used for multiple types it is decoded only once.
    // The ordering of sources is determined by the order they are first seen
    // while iterating kTextureTypes — DIFFUSE is first, so sources[0..] follow
    // diffuse material order, which drives UV remapping later.

    struct Source
    {
        DecodedTexture decoded;
        fs::path       externalPath; // non-empty if loaded from disk (for cleanup)
    };

    std::map<std::string, unsigned int> keyToSource; // raw path -> sources[] index
    std::vector<Source>                 sources;

    // Per material+type+slot: which source index
    struct SlotRef
    {
        unsigned int mat;
        aiTextureType type;
        unsigned int  slot;
        unsigned int  srcIdx;
    };
    std::vector<SlotRef> slotRefs;

    // Which types actually have any textures (to skip building empty atlases)
    std::set<aiTextureType> activeTypes;

    for ( unsigned int m = 0; m < scene->mNumMaterials; ++m )
    {
        aiMaterial* mat = scene->mMaterials[m];
        for ( aiTextureType type : kTextureTypes )
        {
            unsigned int count = mat->GetTextureCount( type );
            for ( unsigned int slot = 0; slot < count; ++slot )
            {
                aiString aiPath;
                mat->GetTexture( type, slot, &aiPath );
                std::string key = aiPath.C_Str();

                auto it = keyToSource.find( key );
                if ( it == keyToSource.end() )
                {
                    Source src;
                    const aiTexture* embedded = scene->GetEmbeddedTexture( key.c_str() );
                    if ( embedded )
                    {
                        auto dec = decodeTexture( embedded );
                        if ( !dec ) return std::unexpected( dec.error() );
                        src.decoded = std::move( *dec );
                    }
                    else
                    {
                        // Prefer resized copy in outputDir, fall back to original in modelDir
                        fs::path fromOutput = opts.outputDir / fs::path( key ).filename();
                        fs::path fromModel  = opts.modelDir  / fs::path( key ).filename();
                        fs::path filePath   = fs::exists( fromOutput ) ? fromOutput : fromModel;
                        auto dec = loadExternalTexture( filePath );
                        if ( !dec ) return std::unexpected( dec.error() );
                        src.decoded      = std::move( *dec );
                        src.externalPath = filePath;
                    }
                    unsigned int idx = static_cast<unsigned int>( sources.size() );
                    keyToSource[key] = idx;
                    sources.push_back( std::move( src ) );
                    it = keyToSource.find( key );
                }

                slotRefs.push_back( { m, type, slot, it->second } );
                activeTypes.insert( type );
            }
        }
    }

    if ( sources.empty() )
        return std::vector<AtlasInfo>{};

    // ── Step 2: determine material→source mapping for UV remap ───────────────
    //
    // UV remap is done once using DIFFUSE (first in kTextureTypes).
    // All per-type atlases are built with the SAME per-material source ordering,
    // so the same remapped UVs address all type atlases correctly.

    // matToDiffuseSrc[m] = sources[] index of the diffuse texture for material m
    // (-1 if material has no diffuse)
    std::vector<int> matToDiffuseSrc( scene->mNumMaterials, -1 );
    for ( const auto& ref : slotRefs )
        if ( ref.type == aiTextureType_DIFFUSE && matToDiffuseSrc[ref.mat] == -1 )
            matToDiffuseSrc[ref.mat] = static_cast<int>( ref.srcIdx );

    // If no diffuse at all, fall back to first texture of any type per material
    for ( const auto& ref : slotRefs )
        if ( matToDiffuseSrc[ref.mat] == -1 )
            matToDiffuseSrc[ref.mat] = static_cast<int>( ref.srcIdx );

    // ── Step 3: build one atlas per active texture type ───────────────────────
    //
    // For each type we pack only the unique sources used by that type.
    // We keep a per-type map: srcIdx -> region in that type's atlas.
    // The atlas dimensions are computed from each type's own source set.

    // Free all existing embedded textures first — we'll replace them all
    for ( unsigned int i = 0; i < scene->mNumTextures; ++i )
        delete scene->mTextures[i];
    delete[] scene->mTextures;
    scene->mTextures    = nullptr;
    scene->mNumTextures = 0;

    std::vector<AtlasInfo>    result;
    std::vector<aiTexture*>   newEmbedded; // scene->mTextures replacement

    // For UV remap: per source, what region in the diffuse atlas?
    // (indexed by srcIdx)
    std::vector<AtlasRegion> diffuseRegions( sources.size(), { 0, 0, 0, 0 } );
    int diffuseAtlasW = 1, diffuseAtlasH = 1;
    bool diffuseBuilt = false;

    for ( aiTextureType type : kTextureTypes )
    {
        if ( activeTypes.find( type ) == activeTypes.end() )
            continue;

        // Collect unique sources for this type, preserving first-seen order
        std::map<unsigned int, unsigned int> srcIdxToTypeSlot; // srcIdx -> index in typeTextures
        std::vector<DecodedTexture>          typeTextures;

        for ( const auto& ref : slotRefs )
        {
            if ( ref.type != type )
                continue;
            if ( srcIdxToTypeSlot.find( ref.srcIdx ) == srcIdxToTypeSlot.end() )
            {
                unsigned int typeSlot = static_cast<unsigned int>( typeTextures.size() );
                srcIdxToTypeSlot[ref.srcIdx] = typeSlot;
                typeTextures.push_back( sources[ref.srcIdx].decoded );
            }
        }

        if ( typeTextures.empty() )
            continue;

        // Pack
        int maxW = 0;
        for ( const auto& t : typeTextures ) maxW = std::max( maxW, t.width );
        int n      = static_cast<int>( typeTextures.size() );
        int atlasW = static_cast<int>( nextPow2(
            static_cast<unsigned int>( maxW * static_cast<int>( std::ceil( std::sqrt( n ) ) ) ) ) );
        atlasW = std::min( atlasW, 8192 );
        int atlasH = 0;
        auto regions = shelfPack( typeTextures, atlasW, atlasH );
        if ( atlasH > 8192 )
            return std::unexpected( Error{ ErrorCode::AtlasBuildFailed,
                std::string( "Atlas height exceeds 8192px for type: " ) + typeSuffix( type ) } );

        // Blit
        std::vector<unsigned char> pixels( static_cast<size_t>( atlasW * atlasH ) * 4, 0 );
        for ( unsigned int i = 0; i < static_cast<unsigned int>( typeTextures.size() ); ++i )
        {
            const auto& src = typeTextures[i];
            const auto& reg = regions[i];
            for ( int row = 0; row < reg.h; ++row )
                std::memcpy(
                    &pixels[static_cast<size_t>( ( reg.y + row ) * atlasW + reg.x ) * 4],
                    &src.pixels[static_cast<size_t>( row * reg.w ) * 4],
                    static_cast<size_t>( reg.w ) * 4 );
        }

        // Encode
        auto encoded = encodePng( pixels, atlasW, atlasH );
        if ( !encoded ) return std::unexpected( encoded.error() );

        // Write file: atlas_diffuse.png, atlas_normal.png, etc.
        std::string filename = std::string( "atlas_" ) + typeSuffix( type ) + ".png";
        auto wr = writeFile( opts.outputDir / filename, *encoded );
        if ( !wr ) return std::unexpected( wr.error() );

        // Add embedded texture to scene (used by GLB/FBX which match by mFilename)
        aiTexture* tex = new aiTexture();
        tex->mHeight   = 0;
        tex->mWidth    = static_cast<unsigned int>( encoded->size() );
        tex->pcData    = reinterpret_cast<aiTexel*>( new unsigned char[encoded->size()] );
        std::memcpy( tex->pcData, encoded->data(), encoded->size() );
        std::strncpy( tex->achFormatHint, "png", HINTMAXTEXTURELEN - 1 );
        tex->mFilename = aiString( filename );
        newEmbedded.push_back( tex );

        // Update material slots of this type.
        // Use the plain filename (e.g. "atlas_diffuse.png") as the texture path:
        //   - OBJ/MTL exporters write it verbatim → correct external file reference
        //   - GLB/FBX exporters match it against mTextures[N].mFilename to locate
        //     the embedded blob → also correct
        // Do NOT use "*N" — that is meaningful only inside assimp's runtime and
        // produces literal "*0" / "*1" strings in text-based formats like MTL.
        aiString atlasFilename( filename );
        int clampMode = aiTextureMapMode_Clamp;
        for ( const auto& ref : slotRefs )
        {
            if ( ref.type != type ) continue;
            aiMaterial* mat = scene->mMaterials[ref.mat];

            mat->AddProperty( &atlasFilename, AI_MATKEY_TEXTURE( type, ref.slot ) );
            mat->AddProperty( &clampMode, 1, AI_MATKEY_MAPPINGMODE_U( type, ref.slot ) );
            mat->AddProperty( &clampMode, 1, AI_MATKEY_MAPPINGMODE_V( type, ref.slot ) );
        }

        // Record diffuse atlas dimensions for UV remap
        if ( type == aiTextureType_DIFFUSE && !diffuseBuilt )
        {
            diffuseAtlasW = atlasW;
            diffuseAtlasH = atlasH;
            // Map srcIdx -> region in the diffuse atlas
            for ( auto& [srcIdx, typeSlot] : srcIdxToTypeSlot )
                diffuseRegions[srcIdx] = regions[typeSlot];
            diffuseBuilt = true;
        }

        AtlasInfo info;
        info.filename   = filename;
        info.type       = type;
        info.inputCount = static_cast<unsigned int>( typeTextures.size() );
        info.width      = static_cast<unsigned int>( atlasW );
        info.height     = static_cast<unsigned int>( atlasH );
        result.push_back( info );
    }

    // ── Step 4: install new embedded textures into scene ─────────────────────

    scene->mNumTextures = static_cast<unsigned int>( newEmbedded.size() );
    if ( !newEmbedded.empty() )
    {
        scene->mTextures = new aiTexture*[newEmbedded.size()];
        for ( size_t i = 0; i < newEmbedded.size(); ++i )
            scene->mTextures[i] = newEmbedded[i];
    }

    // ── Step 5: remap UV coordinates using the diffuse atlas layout ───────────
    //
    // If there is no diffuse atlas, UV remap is skipped (no reliable reference).
    // All per-type atlases were built with the same per-material source ordering,
    // so the same UV transform is valid for every type atlas.

    if ( diffuseBuilt )
    {
        for ( unsigned int mi = 0; mi < scene->mNumMeshes; ++mi )
        {
            aiMesh* mesh = scene->mMeshes[mi];
            if ( mesh->mMaterialIndex >= scene->mNumMaterials ) continue;
            int srcIdx = matToDiffuseSrc[mesh->mMaterialIndex];
            if ( srcIdx < 0 ) continue;

            const AtlasRegion& reg = diffuseRegions[static_cast<unsigned int>( srcIdx )];
            if ( reg.w == 0 || reg.h == 0 ) continue;

            float u0     = static_cast<float>( reg.x ) / diffuseAtlasW;
            float v0     = static_cast<float>( reg.y ) / diffuseAtlasH;
            float uScale = static_cast<float>( reg.w ) / diffuseAtlasW;
            float vScale = static_cast<float>( reg.h ) / diffuseAtlasH;

            for ( unsigned int ch = 0; ch < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++ch )
            {
                if ( !mesh->mTextureCoords[ch] ) break;
                for ( unsigned int v = 0; v < mesh->mNumVertices; ++v )
                {
                    aiVector3D& uv = mesh->mTextureCoords[ch][v];
                    uv.x = u0 + uv.x * uScale;
                    uv.y = v0 + uv.y * vScale;
                }
            }
        }
    }

    // ── Step 6: remove external files that are now baked into atlases ─────────

    for ( const auto& src : sources )
    {
        if ( src.externalPath.empty() ) continue;
        std::error_code ec;
        fs::remove( src.externalPath, ec ); // best-effort
    }

    return result;
}

} // namespace lodgen
