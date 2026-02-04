#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cexport.h>
#include <meshoptimizer.h>

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>

static_assert( sizeof( ai_real ) == sizeof( float ),
               "lodgen requires assimp built with single-precision floats (no ASSIMP_DOUBLE_PRECISION)" );

namespace fs = std::filesystem;

static std::string findExportFormatId( const std::string& ext )
{
    Assimp::Exporter exporter;
    for ( size_t i = 0; i < exporter.GetExportFormatCount(); ++i )
    {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription( i );
        if ( desc && ext == desc->fileExtension )
            return desc->id;
    }
    return "";
}

static std::vector<unsigned int> extractIndices( const aiMesh* mesh )
{
    std::vector<unsigned int> indices;
    indices.reserve( mesh->mNumFaces * 3 );
    for ( unsigned int f = 0; f < mesh->mNumFaces; ++f )
    {
        const aiFace& face = mesh->mFaces[f];
        for ( unsigned int j = 0; j < face.mNumIndices; ++j )
            indices.push_back( face.mIndices[j] );
    }
    return indices;
}

static void writeBackFaces( aiMesh* mesh, const std::vector<unsigned int>& indices )
{
    unsigned int newFaceCount = static_cast<unsigned int>( indices.size() / 3 );

    delete[] mesh->mFaces;

    mesh->mNumFaces = newFaceCount;
    mesh->mFaces = new aiFace[newFaceCount];

    for ( unsigned int f = 0; f < newFaceCount; ++f )
    {
        mesh->mFaces[f].mNumIndices = 3;
        mesh->mFaces[f].mIndices = new unsigned int[3];
        mesh->mFaces[f].mIndices[0] = indices[f * 3 + 0];
        mesh->mFaces[f].mIndices[1] = indices[f * 3 + 1];
        mesh->mFaces[f].mIndices[2] = indices[f * 3 + 2];
    }
}

static float simplifyMesh( aiMesh* mesh, float ratio )
{
    auto indices = extractIndices( mesh );
    if ( indices.empty() )
    {
        return 0.0f;
    }

    size_t targetIndexCount = static_cast<size_t>( indices.size() * ratio );
    targetIndexCount = ( targetIndexCount / 3 ) * 3;
    if ( targetIndexCount < 3 )
    {
        targetIndexCount = 3;
    }

    std::vector<unsigned int> dst( indices.size() );
    float resultError = 0.0f;

    size_t newCount = meshopt_simplify(
        dst.data(),
        indices.data(),
        indices.size(),
        reinterpret_cast<const float*>( mesh->mVertices ),
        mesh->mNumVertices,
        sizeof( aiVector3D ),
        targetIndexCount,
        0.01f,
        0,
        &resultError );

    dst.resize( newCount );

    meshopt_optimizeVertexCache(
        dst.data(), dst.data(), dst.size(), mesh->mNumVertices );

    meshopt_optimizeOverdraw(
        dst.data(), dst.data(), dst.size(),
        reinterpret_cast<const float*>( mesh->mVertices ),
        mesh->mNumVertices, sizeof( aiVector3D ), 1.05f );

    writeBackFaces( mesh, dst );
    return resultError;
}

int main( int argc, char* argv[] )
{
    if ( argc != 2 )
    {
        std::cerr << "Usage: lodgen <input_file>\n";
        return 1;
    }

    fs::path inputPath( argv[1] );

    if ( !fs::exists( inputPath ) )
    {
        std::cerr << "Error: file not found: " << inputPath << "\n";
        return 1;
    }

    std::string ext = inputPath.extension().string();
    if ( !ext.empty() && ext[0] == '.' )
    {
        ext = ext.substr( 1 );
    }

    std::string formatId = findExportFormatId( ext );
    if ( formatId.empty() )
    {
        std::cerr << "Error: no export format found for extension '." << ext << "'\n";
        std::cerr << "Supported export formats:\n";
        Assimp::Exporter exporter;
        for ( size_t i = 0; i < exporter.GetExportFormatCount(); ++i )
        {
            const aiExportFormatDesc* desc = exporter.GetExportFormatDescription( i );
            if ( desc )
                std::cerr << "  ." << desc->fileExtension << " (" << desc->description << ")\n";
        }
        return 1;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile( inputPath.string(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType );

    if ( !scene || !scene->mRootNode )
    {
        std::cerr << "Error: failed to load " << inputPath << "\n";
        std::cerr << importer.GetErrorString() << "\n";
        return 1;
    }

    std::cout << "Loaded: " << inputPath.filename().string() << "\n";
    for ( unsigned int i = 0; i < scene->mNumMeshes; ++i )
    {
        const aiMesh* mesh = scene->mMeshes[i];
        std::cout << "  Mesh " << i;
        if ( mesh->mName.length > 0 )
            std::cout << " \"" << mesh->mName.C_Str() << "\"";
        std::cout << ": " << mesh->mNumVertices << " vertices, " << mesh->mNumFaces << " triangles\n";
    }

    const float ratios[] = { 0.5f, 0.25f, 0.125f };
    const int numLods = 3;

    fs::path dir = inputPath.parent_path();
    std::string stem = inputPath.stem().string();
    std::string dotExt = inputPath.extension().string();

    for ( int lod = 0; lod < numLods; ++lod )
    {
        float ratio = ratios[lod];
        int percent = static_cast<int>( ratio * 100 );
        std::cout << "\nGenerating LOD " << ( lod + 1 ) << " (" << percent << "%):\n";

        aiScene* lodScene = nullptr;
        aiCopyScene( scene, &lodScene );
        if ( !lodScene )
        {
            std::cerr << "  Error: failed to copy scene for LOD " << ( lod + 1 ) << "\n";
            continue;
        }

        for ( unsigned int i = 0; i < lodScene->mNumMeshes; ++i )
        {
            aiMesh* mesh = lodScene->mMeshes[i];
            unsigned int origFaces = mesh->mNumFaces;

            float error = simplifyMesh( mesh, ratio );

            std::cout << "  Mesh " << i;
            if ( mesh->mName.length > 0 )
                std::cout << " \"" << mesh->mName.C_Str() << "\"";
            
            std::cout << ": " << origFaces << " -> " << mesh->mNumFaces
                      << " triangles (error: " << error << ")\n"; 
        }

        fs::path outPath = dir / ( stem + "_lod" + std::to_string( lod + 1 ) + dotExt );

        Assimp::Exporter exporter;
        aiReturn result = exporter.Export( lodScene, formatId, outPath.string() );

        if ( result == aiReturn_SUCCESS )
        {
            std::cout << "  Exported: " << outPath.filename().string() << "\n";
        }
        else
        {
            std::cerr << "  Error exporting LOD " << ( lod + 1 ) << ": "
                      << exporter.GetErrorString() << "\n";
        }

        aiFreeScene( lodScene );
    }

    return 0;
}
