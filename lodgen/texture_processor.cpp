#include "texture_processor.hpp"
#include <assimp/material.h>
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>
#include <cstring>
#include <fstream>
#include <map>

namespace lodgen
{

Result<DecodedTexture> decodeTexture( const aiTexture* tex )
{
    DecodedTexture out;
    out.formatHint = tex->achFormatHint;

    if ( tex->mHeight == 0 )
    {
        // Compressed blob: mWidth = byte size, pcData = raw file bytes
        int channels  = 0;
        unsigned char* pixels = stbi_load_from_memory(
            reinterpret_cast<const unsigned char*>( tex->pcData ),
            static_cast<int>( tex->mWidth ),
            &out.width, &out.height, &channels, 4 );

        if ( !pixels )
            return std::unexpected( Error{ ErrorCode::TextureDecodeFailed,
                std::string( "stbi_load_from_memory: " ) + stbi_failure_reason() } );

        out.pixels.assign( pixels, pixels + out.width * out.height * 4 );
        stbi_image_free( pixels );
    }
    else
    {
        // Uncompressed ARGB8888 aiTexel array — convert to RGBA8
        out.width  = static_cast<int>( tex->mWidth );
        out.height = static_cast<int>( tex->mHeight );
        out.pixels.resize( static_cast<size_t>( out.width * out.height ) * 4 );
        for ( int i = 0; i < out.width * out.height; ++i )
        {
            const aiTexel& t      = tex->pcData[i];
            out.pixels[i * 4 + 0] = t.r;
            out.pixels[i * 4 + 1] = t.g;
            out.pixels[i * 4 + 2] = t.b;
            out.pixels[i * 4 + 3] = t.a;
        }
    }
    return out;
}

Result<DecodedTexture> resizeTexture( const DecodedTexture& src, int newW, int newH )
{
    if ( newW <= 0 || newH <= 0 )
        return std::unexpected( Error{ ErrorCode::TextureResizeFailed,
            "Invalid resize target dimensions" } );

    DecodedTexture out;
    out.width      = newW;
    out.height     = newH;
    out.formatHint = src.formatHint;
    out.pixels.resize( static_cast<size_t>( newW * newH ) * 4 );

    stbir_resize_uint8_linear(
        src.pixels.data(), src.width,  src.height,  0,
        out.pixels.data(), out.width, out.height, 0,
        STBIR_RGBA );

    return out;
}

Result<std::vector<unsigned char>> encodeTexture( const DecodedTexture& tex, const std::string& hint )
{
    std::vector<unsigned char> out;

    auto writeFunc = []( void* ctx, void* data, int size ) {
        auto* buf = static_cast<std::vector<unsigned char>*>( ctx );
        auto* ptr = static_cast<unsigned char*>( data );
        buf->insert( buf->end(), ptr, ptr + size );
    };

    int ok = 0;
    if ( hint == "jpg" || hint == "jpeg" )
        ok = stbi_write_jpg_to_func( writeFunc, &out, tex.width, tex.height, 4, tex.pixels.data(), 85 );
    else
        ok = stbi_write_png_to_func( writeFunc, &out, tex.width, tex.height, 4, tex.pixels.data(), tex.width * 4 );

    if ( !ok || out.empty() )
        return std::unexpected( Error{ ErrorCode::TextureEncodeFailed,
            "stbi_write failed for hint: " + hint } );

    return out;
}

Result<DecodedTexture> loadExternalTexture( const fs::path& path )
{
    if ( !fs::exists( path ) )
        return std::unexpected( Error{ ErrorCode::TextureLoadFailed,
            "Texture file not found: " + path.string() } );

    DecodedTexture out;
    int channels  = 0;
    unsigned char* pixels = stbi_load( path.string().c_str(), &out.width, &out.height, &channels, 4 );

    if ( !pixels )
        return std::unexpected( Error{ ErrorCode::TextureLoadFailed,
            std::string( "stbi_load: " ) + stbi_failure_reason() } );

    out.pixels.assign( pixels, pixels + out.width * out.height * 4 );
    stbi_image_free( pixels );

    std::string ext = path.extension().string();
    if ( !ext.empty() && ext[0] == '.' )
        ext = ext.substr( 1 );
    out.formatHint = ext;

    return out;
}

// ── helpers ──────────────────────────────────────────────────────────────────

// Replace the pixel data of an embedded aiTexture with a freshly encoded blob.
// mHeight stays 0 (compressed convention), mWidth = new byte count.
static VoidResult replaceEmbeddedBlob(
    aiTexture* tex, const DecodedTexture& resized, const std::string& hint )
{
    auto encoded = encodeTexture( resized, hint );
    if ( !encoded )
        return std::unexpected( encoded.error() );

    // Free old blob — aiTexture allocates pcData with operator new[]
    delete[] reinterpret_cast<unsigned char*>( tex->pcData );

    tex->mWidth  = static_cast<unsigned int>( encoded->size() );
    tex->mHeight = 0;
    tex->pcData  = reinterpret_cast<aiTexel*>( new unsigned char[encoded->size()] );
    std::memcpy( tex->pcData, encoded->data(), encoded->size() );

    // Keep / update format hint
    std::strncpy( tex->achFormatHint, hint.c_str(), HINTMAXTEXTURELEN - 1 );
    tex->achFormatHint[HINTMAXTEXTURELEN - 1] = '\0';

    return {};
}

// Write encoded bytes to disk and return the filename (leaf only, for material paths)
static Result<std::string> writeExternalFile(
    const std::vector<unsigned char>& bytes,
    const fs::path& destPath )
{
    std::ofstream f( destPath, std::ios::binary );
    if ( !f )
        return std::unexpected( Error{ ErrorCode::TextureEncodeFailed,
            "Cannot open for writing: " + destPath.string() } );
    f.write( reinterpret_cast<const char*>( bytes.data() ),
             static_cast<std::streamsize>( bytes.size() ) );
    if ( !f )
        return std::unexpected( Error{ ErrorCode::TextureEncodeFailed,
            "Write failed: " + destPath.string() } );
    return destPath.filename().string();
}

// ── main entry point ──────────────────────────────────────────────────────────

Result<TextureStats> processTextures( aiScene* scene, float ratio, const TextureOptions& opts )
{
    TextureStats stats;

    // ── 1. Embedded textures (*N) ─────────────────────────────────────────────
    // Resize in-place. Material paths stay "*N" — no remapping needed.
    // Set mFilename so exporters can write the file with a sensible name.
    for ( unsigned int i = 0; i < scene->mNumTextures; ++i )
    {
        aiTexture* tex = scene->mTextures[i];

        ++stats.inputCount;

        auto decoded = decodeTexture( tex );
        if ( !decoded )
            return std::unexpected( decoded.error() );

        int newW = std::max( 1, static_cast<int>( decoded->width  * ratio ) );
        int newH = std::max( 1, static_cast<int>( decoded->height * ratio ) );
        auto resized = resizeTexture( *decoded, newW, newH );
        if ( !resized )
            return std::unexpected( resized.error() );

        std::string hint = decoded->formatHint.empty() ? "png" : decoded->formatHint;

        auto r = replaceEmbeddedBlob( tex, *resized, hint );
        if ( !r )
            return std::unexpected( r.error() );

        // Give the embedded texture a filename so exporters (e.g. glTF) can
        // name the file; use the existing name if already set.
        if ( tex->mFilename.length == 0 )
        {
            std::string name = "texture_" + std::to_string( i ) + "." + hint;
            tex->mFilename   = aiString( name );
        }

        ++stats.outputCount;
    }

    // ── 2. External textures (file path references) ───────────────────────────
    // Load → resize → write to outputDir → update material path to new filename.
    // Deduplication: same source path produces one output file.
    if ( opts.outputDir.empty() )
        return stats; // no output dir — skip external textures

    // Maps source path string -> leaf filename written in outputDir
    std::map<std::string, std::string> pathToOutputName;

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
                std::string rawPath = aiPath.C_Str();

                // Skip embedded references — handled above
                if ( scene->GetEmbeddedTexture( rawPath.c_str() ) )
                    continue;

                auto it = pathToOutputName.find( rawPath );
                if ( it == pathToOutputName.end() )
                {
                    ++stats.inputCount;

                    fs::path srcFile = opts.modelDir / rawPath;
                    auto decoded = loadExternalTexture( srcFile );
                    if ( !decoded )
                        return std::unexpected( decoded.error() );

                    int newW = std::max( 1, static_cast<int>( decoded->width  * ratio ) );
                    int newH = std::max( 1, static_cast<int>( decoded->height * ratio ) );
                    auto resized = resizeTexture( *decoded, newW, newH );
                    if ( !resized )
                        return std::unexpected( resized.error() );

                    std::string hint = decoded->formatHint.empty() ? "png" : decoded->formatHint;
                    auto encoded = encodeTexture( *resized, hint );
                    if ( !encoded )
                        return std::unexpected( encoded.error() );

                    // Keep original filename, write into outputDir
                    fs::path destPath = opts.outputDir / fs::path( rawPath ).filename();
                    auto nameResult = writeExternalFile( *encoded, destPath );
                    if ( !nameResult )
                        return std::unexpected( nameResult.error() );

                    pathToOutputName[rawPath] = *nameResult;
                    ++stats.outputCount;
                    it = pathToOutputName.find( rawPath );
                }

                // Update material to point at the new file (leaf name — relative to model)
                aiString newAiPath( it->second );
                mat->AddProperty( &newAiPath, AI_MATKEY_TEXTURE( type, slot ) );
            }
        }
    }

    return stats;
}

} // namespace lodgen
