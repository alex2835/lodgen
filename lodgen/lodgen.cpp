#include "lodgen.hpp"
#include "mesh_simplifier.hpp"
#include "scene_io.hpp"
#include "texture_atlas.hpp"

namespace lodgen
{

Result<ScenePtr> generateLod( const aiScene* scene, float ratio, const TextureOptions* texOpts )
{
    aiScene* copy = nullptr;
    aiCopyScene( scene, &copy );
    if ( !copy )
        return std::unexpected( Error{ ErrorCode::SceneCopyFailed, "aiCopyScene failed" } );

    ScenePtr result( copy ); // RAII cleanup on any error path below

    for ( unsigned int i = 0; i < copy->mNumMeshes; ++i )
        simplify( copy->mMeshes[i], ratio );

    if ( texOpts && texOpts->resizeTextures )
    {
        auto r = processTextures( copy, ratio, *texOpts );
        if ( !r )
            return std::unexpected( r.error() );
    }

    if ( texOpts && texOpts->buildAtlas )
    {
        auto r = buildAtlas( copy );
        if ( !r )
            return std::unexpected( r.error() );
    }

    return result;
}

Result<std::vector<LodInfo>> generateLods(
    const aiScene* scene,
    const fs::path& inputPath,
    const fs::path& outputDir,
    const std::vector<float>& ratios,
    const TextureOptions* texOpts )
{
    std::vector<LodInfo> results;
    results.reserve( ratios.size() );

    for ( size_t i = 0; i < ratios.size(); ++i )
    {
        // Build lod output directory first — processTextures needs it for
        // writing resized external texture files alongside the model
        fs::path lodDir  = outputDir / ( "lod" + std::to_string( i + 1 ) );
        fs::path outPath = lodDir / inputPath.filename();

        std::error_code ec;
        fs::create_directories( lodDir, ec );
        if ( ec )
            return std::unexpected( Error{ ErrorCode::ExportFailed,
                "Could not create directory " + lodDir.string() + ": " + ec.message() } );

        // Copy scene
        aiScene* copy = nullptr;
        aiCopyScene( scene, &copy );
        if ( !copy )
            return std::unexpected( Error{ ErrorCode::SceneCopyFailed, "aiCopyScene failed" } );
        ScenePtr lodScene( copy );

        // Simplify meshes
        for ( unsigned int m = 0; m < copy->mNumMeshes; ++m )
            simplify( copy->mMeshes[m], ratios[i] );

        // Process textures — pass lodDir so external files land next to the model
        std::optional<TextureStats> texStats;
        if ( texOpts && texOpts->resizeTextures )
        {
            TextureOptions lodTexOpts = *texOpts;
            lodTexOpts.outputDir = lodDir;

            auto r = processTextures( copy, ratios[i], lodTexOpts );
            if ( !r )
                return std::unexpected( r.error() );
            texStats = *r;
        }

        if ( texOpts && texOpts->buildAtlas )
        {
            auto r = buildAtlas( copy );
            if ( !r )
                return std::unexpected( r.error() );
            texStats = *r;
        }

        // Save model
        auto saveResult = saveScene( lodScene.get(), outPath );
        if ( !saveResult )
            return std::unexpected( saveResult.error() );

        // Collect per-mesh triangle counts
        LodInfo info;
        info.ratio        = ratios[i];
        info.outputPath   = outPath;
        info.textureStats = texStats;

        for ( unsigned int m = 0; m < copy->mNumMeshes; ++m )
        {
            SimplifyResult meshResult{};
            meshResult.simplifiedTriangles = copy->mMeshes[m]->mNumFaces;
            info.meshResults.push_back( meshResult );
        }

        results.push_back( std::move( info ) );
    }

    return results;
}

} // namespace lodgen
