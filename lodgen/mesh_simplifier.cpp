#include "mesh_simplifier.hpp"
#include <assimp/mesh.h>
#include <meshoptimizer.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace lodgen
{

// ── Interleaved vertex layout ────────────────────────────────────────────────
//
// Used ONLY for the compaction step so that one remap pass handles all
// attributes atomically.  NOT passed to meshopt_simplify* (would exceed
// the 256-byte stride limit with max UV/color channels).

static constexpr unsigned int kMaxUVChannels = AI_MAX_NUMBER_OF_TEXTURECOORDS;
static constexpr unsigned int kMaxColorChannels = AI_MAX_NUMBER_OF_COLOR_SETS;

struct InterleavedVertex
{
    float px, py, pz;                // position
    float nx, ny, nz;                // normal
    float tx, ty, tz;                // tangent
    float bx, by, bz;               // bitangent
    float uv[kMaxUVChannels][3];     // texture coords
    float col[kMaxColorChannels][4]; // vertex colours
};

// ── Mesh layout detection ────────────────────────────────────────────────────

struct MeshLayout
{
    bool hasNormals = false;
    bool hasTangents = false;
    unsigned int uvChannels = 0;
    unsigned int colorChannels = 0;
};

static MeshLayout detectLayout( const aiMesh* mesh )
{
    MeshLayout layout;
    layout.hasNormals = ( mesh->mNormals != nullptr );
    layout.hasTangents = ( mesh->mTangents != nullptr );

    for ( unsigned int ch = 0; ch < kMaxUVChannels; ++ch )
    {
        if ( !mesh->mTextureCoords[ch] ) break;
        ++layout.uvChannels;
    }
    for ( unsigned int ch = 0; ch < kMaxColorChannels; ++ch )
    {
        if ( !mesh->mColors[ch] ) break;
        ++layout.colorChannels;
    }
    return layout;
}

// ── Pack: assimp SoA → interleaved AoS ───────────────────────────────────────

static std::vector<InterleavedVertex> packVertices(
    const aiMesh* mesh, const MeshLayout& layout )
{
    unsigned int N = mesh->mNumVertices;
    std::vector<InterleavedVertex> verts( N );
    std::memset( verts.data(), 0, N * sizeof( InterleavedVertex ) );

    for ( unsigned int i = 0; i < N; ++i )
    {
        auto& v = verts[i];

        v.px = mesh->mVertices[i].x;
        v.py = mesh->mVertices[i].y;
        v.pz = mesh->mVertices[i].z;

        if ( layout.hasNormals )
        {
            v.nx = mesh->mNormals[i].x;
            v.ny = mesh->mNormals[i].y;
            v.nz = mesh->mNormals[i].z;
        }
        if ( layout.hasTangents )
        {
            v.tx = mesh->mTangents[i].x;
            v.ty = mesh->mTangents[i].y;
            v.tz = mesh->mTangents[i].z;
            v.bx = mesh->mBitangents[i].x;
            v.by = mesh->mBitangents[i].y;
            v.bz = mesh->mBitangents[i].z;
        }
        for ( unsigned int ch = 0; ch < layout.uvChannels; ++ch )
        {
            v.uv[ch][0] = mesh->mTextureCoords[ch][i].x;
            v.uv[ch][1] = mesh->mTextureCoords[ch][i].y;
            v.uv[ch][2] = mesh->mTextureCoords[ch][i].z;
        }
        for ( unsigned int ch = 0; ch < layout.colorChannels; ++ch )
        {
            v.col[ch][0] = mesh->mColors[ch][i].r;
            v.col[ch][1] = mesh->mColors[ch][i].g;
            v.col[ch][2] = mesh->mColors[ch][i].b;
            v.col[ch][3] = mesh->mColors[ch][i].a;
        }
    }
    return verts;
}

// ── Unpack: interleaved AoS → assimp SoA (reallocates all arrays) ────────────

static void unpackVertices(
    aiMesh* mesh,
    const std::vector<InterleavedVertex>& verts,
    const MeshLayout& layout )
{
    unsigned int N = static_cast<unsigned int>( verts.size() );

    delete[] mesh->mVertices;   mesh->mVertices = nullptr;
    delete[] mesh->mNormals;    mesh->mNormals = nullptr;
    delete[] mesh->mTangents;   mesh->mTangents = nullptr;
    delete[] mesh->mBitangents; mesh->mBitangents = nullptr;
    for ( unsigned int ch = 0; ch < kMaxUVChannels; ++ch )
    {
        delete[] mesh->mTextureCoords[ch];
        mesh->mTextureCoords[ch] = nullptr;
    }
    for ( unsigned int ch = 0; ch < kMaxColorChannels; ++ch )
    {
        delete[] mesh->mColors[ch];
        mesh->mColors[ch] = nullptr;
    }

    mesh->mNumVertices = N;

    mesh->mVertices = new aiVector3D[N];
    for ( unsigned int i = 0; i < N; ++i )
    {
        mesh->mVertices[i].x = verts[i].px;
        mesh->mVertices[i].y = verts[i].py;
        mesh->mVertices[i].z = verts[i].pz;
    }

    if ( layout.hasNormals )
    {
        mesh->mNormals = new aiVector3D[N];
        for ( unsigned int i = 0; i < N; ++i )
        {
            mesh->mNormals[i].x = verts[i].nx;
            mesh->mNormals[i].y = verts[i].ny;
            mesh->mNormals[i].z = verts[i].nz;
        }
    }

    if ( layout.hasTangents )
    {
        mesh->mTangents = new aiVector3D[N];
        mesh->mBitangents = new aiVector3D[N];
        for ( unsigned int i = 0; i < N; ++i )
        {
            mesh->mTangents[i].x = verts[i].tx;
            mesh->mTangents[i].y = verts[i].ty;
            mesh->mTangents[i].z = verts[i].tz;
            mesh->mBitangents[i].x = verts[i].bx;
            mesh->mBitangents[i].y = verts[i].by;
            mesh->mBitangents[i].z = verts[i].bz;
        }
    }

    for ( unsigned int ch = 0; ch < layout.uvChannels; ++ch )
    {
        mesh->mTextureCoords[ch] = new aiVector3D[N];
        for ( unsigned int i = 0; i < N; ++i )
        {
            mesh->mTextureCoords[ch][i].x = verts[i].uv[ch][0];
            mesh->mTextureCoords[ch][i].y = verts[i].uv[ch][1];
            mesh->mTextureCoords[ch][i].z = verts[i].uv[ch][2];
        }
    }

    for ( unsigned int ch = 0; ch < layout.colorChannels; ++ch )
    {
        mesh->mColors[ch] = new aiColor4D[N];
        for ( unsigned int i = 0; i < N; ++i )
        {
            mesh->mColors[ch][i].r = verts[i].col[ch][0];
            mesh->mColors[ch][i].g = verts[i].col[ch][1];
            mesh->mColors[ch][i].b = verts[i].col[ch][2];
            mesh->mColors[ch][i].a = verts[i].col[ch][3];
        }
    }
}

// ── Compact position array for meshopt calls ─────────────────────────────────
//
// meshopt_simplify* asserts vertex_positions_stride <= 256.
// sizeof(InterleavedVertex) is 272 bytes with max channels, so we extract
// a separate tightly-packed float3 array for positions.  This array is used
// ONLY by meshopt; compaction still operates on the full interleaved buffer.

static std::vector<float> extractPositions(
    const std::vector<InterleavedVertex>& verts )
{
    std::vector<float> pos( verts.size() * 3 );
    for ( size_t i = 0; i < verts.size(); ++i )
    {
        pos[i * 3 + 0] = verts[i].px;
        pos[i * 3 + 1] = verts[i].py;
        pos[i * 3 + 2] = verts[i].pz;
    }
    return pos;
}

// ── Build attribute arrays for meshopt_simplifyWithAttributes ─────────────────
//
// Also subject to stride <= 256 and attribute_count <= kMaxAttributes (16).
// With 8 UV channels * 2 + 3 normals = 19 — exceeds kMaxAttributes.
// So we cap to the first few UV channels that matter most and normals.

static constexpr size_t kMeshoptMaxAttributes = 32; // meshoptimizer hard limit (attribute_count must be <= 32)

struct SimplifyAttributes
{
    std::vector<float> data;
    std::vector<float> weights;
    size_t             stride;     // bytes
    size_t             count;      // components per vertex
};

static SimplifyAttributes buildSimplifyAttributes(
    const std::vector<InterleavedVertex>& verts,
    const MeshLayout& layout )
{
    SimplifyAttributes attrs{};

    // Budget: 2 floats per UV channel + 3 for normals, capped at kMeshoptMaxAttributes
    unsigned int uvChansToUse = layout.uvChannels;
    size_t needed = uvChansToUse * 2 + ( layout.hasNormals ? 3 : 0 );

    // If we exceed the limit, reduce UV channels (normals cost 3 slots)
    while ( needed > kMeshoptMaxAttributes && uvChansToUse > 0 )
    {
        --uvChansToUse;
        needed = uvChansToUse * 2 + ( layout.hasNormals ? 3 : 0 );
    }
    // If still over (unlikely: 0 UVs + 3 normals = 3), drop normals
    bool useNormals = layout.hasNormals && ( needed <= kMeshoptMaxAttributes );

    size_t count = uvChansToUse * 2 + ( useNormals ? 3 : 0 );

    attrs.count = count;
    attrs.stride = count * sizeof( float );

    if ( count == 0 )
        return attrs;

    // Also check stride <= 256
    if ( attrs.stride > 256 )
    {
        // Shouldn't happen with 16 attrs * 4 bytes = 64 bytes, but guard anyway
        attrs = {};
        return attrs;
    }

    size_t N = verts.size();
    attrs.data.resize( N * count );
    attrs.weights.resize( count );

    size_t offset = 0;

    for ( unsigned int ch = 0; ch < uvChansToUse; ++ch )
    {
        for ( size_t i = 0; i < N; ++i )
        {
            attrs.data[i * count + offset + 0] = verts[i].uv[ch][0];
            attrs.data[i * count + offset + 1] = verts[i].uv[ch][1];
        }
        // First UV channel gets highest weight (usually the one that matters)
        attrs.weights[offset + 0] = ( ch == 0 ) ? 1.5f : 0.8f;
        attrs.weights[offset + 1] = ( ch == 0 ) ? 1.5f : 0.8f;
        offset += 2;
    }

    if ( useNormals )
    {
        for ( size_t i = 0; i < N; ++i )
        {
            attrs.data[i * count + offset + 0] = verts[i].nx;
            attrs.data[i * count + offset + 1] = verts[i].ny;
            attrs.data[i * count + offset + 2] = verts[i].nz;
        }
        attrs.weights[offset + 0] = 0.5f;
        attrs.weights[offset + 1] = 0.5f;
        attrs.weights[offset + 2] = 0.5f;
    }

    return attrs;
}

// ── Bone weight remap ─────────────────────────────────────────────────────────
//
// After vertex compaction, mBones[b]->mWeights[j].mVertexId still holds old
// vertex indices.  We must translate them through the remap table and drop any
// weights whose vertex was removed (remap[old] == ~0u).

static void remapBoneWeights( aiMesh* mesh, const std::vector<unsigned int>& remap )
{
    if ( !mesh->mBones )
        return;

    for ( unsigned int b = 0; b < mesh->mNumBones; ++b )
    {
        aiBone* bone = mesh->mBones[b];
        if ( !bone ) continue;

        // Translate and compact in-place, dropping removed vertices.
        unsigned int out = 0;
        for ( unsigned int w = 0; w < bone->mNumWeights; ++w )
        {
            unsigned int oldIdx = bone->mWeights[w].mVertexId;
            if ( oldIdx < remap.size() && remap[oldIdx] != ~0u )
            {
                bone->mWeights[out] = bone->mWeights[w];
                bone->mWeights[out].mVertexId = remap[oldIdx];
                ++out;
            }
        }
        bone->mNumWeights = out;
    }
}

// ── Index extraction / face write-back ───────────────────────────────────────

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
        mesh->mFaces[f].mIndices = new unsigned int[3] {
            indices[f * 3 + 0],
                indices[f * 3 + 1],
                indices[f * 3 + 2]
        };
    }
}

// ── Main entry point ─────────────────────────────────────────────────────────

SimplifyResult simplify( aiMesh* mesh, float ratio )
{
    SimplifyResult result{};
    result.originalTriangles = mesh->mNumFaces;

    // Only simplify pure triangle meshes.
    // aiProcess_SortByPType can produce separate point/line meshes in the same
    // scene; passing those to meshopt would violate the index_count % 3 == 0
    // assert inside meshopt_optimizeVertexFetchRemap.
    if ( mesh->mPrimitiveTypes != aiPrimitiveType_TRIANGLE )
        return result;

    auto indices = extractIndices( mesh );
    if ( indices.empty() )
        return result;

    // ── 1. Pack all vertex data into interleaved buffer ───────────────────────

    MeshLayout layout = detectLayout( mesh );
    auto verts = packVertices( mesh, layout );

    // ── 2. Extract compact position array for meshopt (stride = 12 bytes) ────
    //
    // meshopt asserts stride <= 256; InterleavedVertex is 272 bytes.
    // Positions are extracted separately; the interleaved buffer is used
    // only for the compaction remap.

    auto positions = extractPositions( verts );
    constexpr size_t kPosStride = 3 * sizeof( float ); // 12 bytes

    // ── 3. Simplify (attribute-aware) ─────────────────────────────────────────

    size_t targetIndexCount = ( static_cast<size_t>( indices.size() * ratio ) / 3 ) * 3;
    targetIndexCount = std::max( targetIndexCount, size_t( 3 ) );

    auto attrs = buildSimplifyAttributes( verts, layout );

    std::vector<unsigned int> simplified( indices.size() );
    size_t newIndexCount;

    if ( attrs.count > 0 )
    {
        newIndexCount = meshopt_simplifyWithAttributes(
            simplified.data(),
            indices.data(),
            indices.size(),
            positions.data(),               // compact float3 array
            verts.size(),
            kPosStride,                     // 12 bytes — well within 256 limit
            attrs.data.data(),
            attrs.stride,
            attrs.weights.data(),
            attrs.count,
            nullptr,                        // no locked vertices
            targetIndexCount,
            0.01f,
            0,
            &result.error );
    }
    else
    {
        newIndexCount = meshopt_simplify(
            simplified.data(),
            indices.data(),
            indices.size(),
            positions.data(),
            verts.size(),
            kPosStride,
            targetIndexCount,
            0.01f,
            0,
            &result.error );
    }

    simplified.resize( newIndexCount );

    // ── 4. Post-simplification cache + overdraw optimisation ──────────────────

    meshopt_optimizeVertexCache(
        simplified.data(), simplified.data(),
        simplified.size(), verts.size() );

    meshopt_optimizeOverdraw(
        simplified.data(), simplified.data(), simplified.size(),
        positions.data(),
        verts.size(),
        kPosStride,
        1.05f );

    // ── 5. Compact: remap the single interleaved buffer ──────────────────────
    //
    // One remap pass handles ALL vertex attributes atomically.

    std::vector<unsigned int> remap( verts.size() );
    size_t newVertCount = meshopt_optimizeVertexFetchRemap(
        remap.data(),
        simplified.data(),
        simplified.size(),
        verts.size() );

    meshopt_remapIndexBuffer(
        simplified.data(), simplified.data(),
        simplified.size(), remap.data() );

    std::vector<InterleavedVertex> compacted( newVertCount );
    for ( size_t i = 0; i < verts.size(); ++i )
        if ( remap[i] != ~0u )
            compacted[remap[i]] = verts[i];

    // ── 6. Remap bone weights, unpack back into assimp SoA + write faces ──────

    remapBoneWeights( mesh, remap );
    unpackVertices( mesh, compacted, layout );
    writeBackFaces( mesh, simplified );

    result.simplifiedTriangles = mesh->mNumFaces;
    return result;
}

} // namespace lodgen