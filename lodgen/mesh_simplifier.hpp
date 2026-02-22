#pragma once
#include <assimp/mesh.h>

namespace lodgen
{

struct SimplifyResult
{
    unsigned int originalTriangles;
    unsigned int simplifiedTriangles;
    float error;
};

SimplifyResult simplify( aiMesh* mesh, float ratio );

} // namespace lodgen
