#include "scene_io.hpp"
#include "types.hpp"
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <algorithm>
#include <vector>

namespace lodgen
{

static_assert( sizeof( ai_real ) == sizeof( float ),
               "lodgen requires assimp built with single-precision floats" );

Result<std::string> findExportFormatId( const std::string& extension )
{
    std::string ext = extension;
    if ( !ext.empty() && ext[0] == '.' )
        ext = ext.substr( 1 );

    Assimp::Exporter exporter;
    for ( size_t i = 0; i < exporter.GetExportFormatCount(); ++i )
    {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription( i );
        if ( desc && ext == desc->fileExtension )
            return std::string( desc->id );
    }
    return std::unexpected( Error{ ErrorCode::UnsupportedFormat,
                                   "No export format for extension: " + extension } );
}

std::vector<std::string> supportedFormats()
{
    std::vector<std::string> result;
    Assimp::Exporter exporter;
    for ( size_t i = 0; i < exporter.GetExportFormatCount(); ++i )
    {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription( i );
        if ( desc )
            result.push_back( std::string( "." ) + desc->fileExtension );
    }
    return result;
}

Result<ScenePtr> loadScene( const fs::path& path )
{
    if ( !fs::exists( path ) )
        return std::unexpected( Error{ ErrorCode::FileNotFound,
                                       "File not found: " + path.string() } );

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType );

    if ( !scene || !scene->mRootNode )
        return std::unexpected( Error{ ErrorCode::ImportFailed,
                                       importer.GetErrorString() } );

    aiScene* copy = nullptr;
    aiCopyScene( scene, &copy );

    if ( !copy )
        return std::unexpected( Error{ ErrorCode::SceneCopyFailed,
                                       "aiCopyScene failed" } );

    return ScenePtr( copy );
}

Result<MutableScenePtr> loadSceneMutable( const fs::path& path )
{
    if ( !fs::exists( path ) )
        return std::unexpected( Error{ ErrorCode::FileNotFound,
                                       "File not found: " + path.string() } );

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType );

    if ( !scene || !scene->mRootNode )
        return std::unexpected( Error{ ErrorCode::ImportFailed,
                                       importer.GetErrorString() } );

    aiScene* copy = nullptr;
    aiCopyScene( scene, &copy );

    if ( !copy )
        return std::unexpected( Error{ ErrorCode::SceneCopyFailed,
                                       "aiCopyScene failed" } );

    return MutableScenePtr( copy );
}

// Remove materials from `sc` that are not referenced by any mesh.
// Assimp's OBJ exporter always prepends a "DefaultMaterial"; stripping unused
// materials before export keeps the MTL clean and matching the source.
static void removeUnusedMaterials( aiScene* sc )
{
    if ( sc->mNumMaterials == 0 )
        return;

    // Count how many meshes reference each material index.
    std::vector<unsigned int> refCount( sc->mNumMaterials, 0 );
    for ( unsigned int m = 0; m < sc->mNumMeshes; ++m )
        if ( sc->mMeshes[m]->mMaterialIndex < sc->mNumMaterials )
            ++refCount[sc->mMeshes[m]->mMaterialIndex];

    // Build a compacted material list and a remap from old → new index.
    std::vector<unsigned int> remap( sc->mNumMaterials, ~0u );
    std::vector<aiMaterial*>  kept;
    for ( unsigned int i = 0; i < sc->mNumMaterials; ++i )
    {
        if ( refCount[i] > 0 )
        {
            remap[i] = static_cast<unsigned int>( kept.size() );
            kept.push_back( sc->mMaterials[i] );
        }
        else
        {
            delete sc->mMaterials[i];
        }
    }

    if ( kept.size() == sc->mNumMaterials )
        return; // nothing removed

    // Install the compacted array.
    delete[] sc->mMaterials;
    sc->mNumMaterials = static_cast<unsigned int>( kept.size() );
    sc->mMaterials    = new aiMaterial*[kept.size()];
    for ( size_t i = 0; i < kept.size(); ++i )
        sc->mMaterials[i] = kept[i];

    // Fix mesh material indices.
    for ( unsigned int m = 0; m < sc->mNumMeshes; ++m )
    {
        unsigned int old = sc->mMeshes[m]->mMaterialIndex;
        if ( old < remap.size() && remap[old] != ~0u )
            sc->mMeshes[m]->mMaterialIndex = remap[old];
    }
}

VoidResult saveScene( const aiScene* scene, const fs::path& path )
{
    auto fmtResult = findExportFormatId( path.extension().string() );
    if ( !fmtResult )
        return std::unexpected( fmtResult.error() );

    // Some assimp exporters cast away const and modify the scene in-place
    // (e.g. applying node-hierarchy transforms to vertex data for OBJ/FBX).
    // Always export from a private copy so the caller's scene is never touched.
    aiScene* copy = nullptr;
    aiCopyScene( scene, &copy );
    if ( !copy )
        return std::unexpected( Error{ ErrorCode::SceneCopyFailed,
            "aiCopyScene failed inside saveScene" } );
    MutableScenePtr guard( copy );

    // Strip unreferenced materials (e.g. assimp's auto-added DefaultMaterial).
    removeUnusedMaterials( copy );

    Assimp::Exporter exporter;
    if ( exporter.Export( copy, fmtResult.value(), path.string() ) != aiReturn_SUCCESS )
        return std::unexpected( Error{ ErrorCode::ExportFailed,
                                       exporter.GetErrorString() } );

    return {};
}

} // namespace lodgen
