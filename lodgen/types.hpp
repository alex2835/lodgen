#pragma once
#include <assimp/scene.h>
#include <assimp/cexport.h>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace lodgen
{

namespace fs = std::filesystem;

struct AiSceneDeleter
{
    void operator()( const aiScene* scene ) const { aiFreeScene( scene ); }
};

using ScenePtr = std::unique_ptr<const aiScene, AiSceneDeleter>;

enum class ErrorCode
{
    FileNotFound,
    UnsupportedFormat,
    ImportFailed,
    ExportFailed,
    SceneCopyFailed,
    TextureDecodeFailed,
    TextureResizeFailed,
    TextureEncodeFailed,
    TextureLoadFailed,
    AtlasBuildFailed,
};

struct Error
{
    ErrorCode code;
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

} // namespace lodgen
