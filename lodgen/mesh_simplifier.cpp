#include "mesh_simplifier.hpp"
#include <assimp/mesh.h>
#include <meshoptimizer.h>
#include <vector>

namespace lodgen
{

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
    unsigned int faceCount = static_cast<unsigned int>( indices.size() / 3 );

    delete[] mesh->mFaces;
    mesh->mNumFaces = faceCount;
    mesh->mFaces = new aiFace[faceCount];

    for ( unsigned int f = 0; f < faceCount; ++f )
    {
        mesh->mFaces[f].mNumIndices = 3;
        mesh->mFaces[f].mIndices = new unsigned int[3]{
            indices[f * 3 + 0],
            indices[f * 3 + 1],
            indices[f * 3 + 2]
        };
    }
}

SimplifyResult simplify( aiMesh* mesh, float ratio )
{
    SimplifyResult result{};
    result.originalTriangles = mesh->mNumFaces;

    auto indices = extractIndices( mesh );
    if ( indices.empty() )
        return result;

    size_t targetIndexCount = ( static_cast<size_t>( indices.size() * ratio ) / 3 ) * 3;
    targetIndexCount = std::max( targetIndexCount, size_t( 3 ) );

    std::vector<unsigned int> simplified( indices.size() );
    size_t newCount = meshopt_simplify(
        simplified.data(),
        indices.data(),
        indices.size(),
        reinterpret_cast<const float*>( mesh->mVertices ),
        mesh->mNumVertices,
        sizeof( aiVector3D ),
        targetIndexCount,
        0.01f,
        0,
        &result.error );
    simplified.resize( newCount );

    meshopt_optimizeVertexCache(
        simplified.data(), simplified.data(), simplified.size(), mesh->mNumVertices );

    meshopt_optimizeOverdraw(
        simplified.data(), simplified.data(), simplified.size(),
        reinterpret_cast<const float*>( mesh->mVertices ),
        mesh->mNumVertices, sizeof( aiVector3D ), 1.05f );

    writeBackFaces( mesh, simplified );

    result.simplifiedTriangles = mesh->mNumFaces;
    return result;
}

} // namespace lodgen
