#include "scene_io.hpp"
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

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

VoidResult saveScene( const aiScene* scene, const fs::path& path )
{
    auto fmtResult = findExportFormatId( path.extension().string() );
    if ( !fmtResult )
        return std::unexpected( fmtResult.error() );

    Assimp::Exporter exporter;
    if ( exporter.Export( scene, fmtResult.value(), path.string() ) != aiReturn_SUCCESS )
        return std::unexpected( Error{ ErrorCode::ExportFailed,
                                       exporter.GetErrorString() } );

    return {};
}

} // namespace lodgen
