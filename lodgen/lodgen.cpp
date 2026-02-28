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

    ScenePtr result( copy );

    for ( unsigned int i = 0; i < copy->mNumMeshes; ++i )
        simplify( copy->mMeshes[i], ratio );

    if ( texOpts && texOpts->resizeTextures )
    {
        auto r = processTextures( copy, ratio, *texOpts );
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
        const auto lodPostfix = "lod" + std::to_string( i + 1 );
        fs::path lodDir  = outputDir / lodPostfix;
        fs::path outPath = lodDir / ( inputPath.stem().string() + '_' + lodPostfix + inputPath.extension().string() );

        std::error_code ec;
        fs::create_directories( lodDir, ec );
        if ( ec )
            return std::unexpected( Error{ ErrorCode::ExportFailed,
                "Could not create directory " + lodDir.string() + ": " + ec.message() } );

        aiScene* copy = nullptr;
        aiCopyScene( scene, &copy );
        if ( !copy )
            return std::unexpected( Error{ ErrorCode::SceneCopyFailed, "aiCopyScene failed" } );
        ScenePtr lodScene( copy );

        for ( unsigned int m = 0; m < copy->mNumMeshes; ++m )
            simplify( copy->mMeshes[m], ratios[i] );

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

        auto saveResult = saveScene( lodScene.get(), outPath );
        if ( !saveResult )
            return std::unexpected( saveResult.error() );

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

Result<std::vector<AtlasInfo>> buildLodAtlas(
    const fs::path& modelPath,
    const AtlasOptions& opts )
{
    // Load the saved LOD model as a mutable copy
    auto sceneResult = loadSceneMutable( modelPath );
    if ( !sceneResult )
        return std::unexpected( sceneResult.error() );

    aiScene* scene = sceneResult->get();

    auto atlasResult = buildAtlas( scene, opts );
    if ( !atlasResult )
        return std::unexpected( atlasResult.error() );

    // Re-save model with updated material paths and embedded atlases
    auto saveResult = saveScene( scene, modelPath );
    if ( !saveResult )
        return std::unexpected( saveResult.error() );

    return atlasResult;
}

} // namespace lodgen
