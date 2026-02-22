#pragma once
#include "types.hpp"
#include <string>
#include <vector>

namespace lodgen
{

Result<std::string> findExportFormatId( const std::string& extension );
std::vector<std::string> supportedFormats();

Result<ScenePtr> loadScene( const fs::path& path );
VoidResult saveScene( const aiScene* scene, const fs::path& path );

} // namespace lodgen
