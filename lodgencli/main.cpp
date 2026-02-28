#include <lodgen/lodgen.hpp>
#include <lodgen/scene_io.hpp>
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main( int argc, char* argv[] )
{
    cxxopts::Options options( "lodgencli", "LOD generator — mesh simplification + optional texture processing" );
    options.add_options()
        ( "input",     "Input model file",
            cxxopts::value<std::string>() )
        ( "o,output",  "Output directory (default: output)",
            cxxopts::value<std::string>()->default_value( "output" ) )
        ( "r,ratios",  "Comma-separated LOD ratios, e.g. 0.5,0.25",
            cxxopts::value<std::string>()->default_value( "0.5,0.25" ) )
        ( "t,textures","Resize textures proportionally to each LOD ratio",
            cxxopts::value<bool>()->default_value( "false" ) )
        ( "a,atlas",   "Build per-type texture atlases after LOD generation",
            cxxopts::value<bool>()->default_value( "false" ) )
        ( "h,help",    "Show help" );

    options.parse_positional( { "input" } );
    options.positional_help( "<model>" );

    cxxopts::ParseResult args;
    try
    {
        args = options.parse( argc, argv );
    }
    catch ( const std::exception& e )
    {
        std::cerr << "Error: " << e.what() << "\n" << options.help() << "\n";
        return 1;
    }

    if ( args.count( "help" ) || !args.count( "input" ) )
    {
        std::cout << options.help() << "\n";
        return args.count( "help" ) ? 0 : 1;
    }

    // ── parse arguments ───────────────────────────────────────────────────────

    fs::path inputPath  = args["input"].as<std::string>();
    fs::path outputDir  = args["output"].as<std::string>();
    bool     doTextures = args["textures"].as<bool>();
    bool     doAtlas    = args["atlas"].as<bool>();

    std::vector<float> ratios;
    {
        std::string ratioStr = args["ratios"].as<std::string>();
        std::string token;
        for ( char c : ratioStr ) {
            if ( c == ',' ) {
                if ( !token.empty() ) ratios.push_back( std::stof( token ) );
                token.clear();
            } else {
                token += c;
            }
        }
        if ( !token.empty() ) ratios.push_back( std::stof( token ) );
    }
    if ( ratios.empty() )
    {
        std::cerr << "Error: no valid ratios specified\n";
        return 1;
    }

    // ── load source scene ─────────────────────────────────────────────────────

    auto sceneResult = lodgen::loadScene( inputPath );
    if ( !sceneResult )
    {
        std::cerr << "Failed to load '" << inputPath.string() << "': "
                  << sceneResult.error().message << "\n";
        return 1;
    }
    const aiScene* scene = sceneResult->get();

    // ── step 1: generate LODs ─────────────────────────────────────────────────

    lodgen::TextureOptions texOpts;
    texOpts.modelDir       = inputPath.parent_path();
    texOpts.resizeTextures = true;

    auto lodsResult = lodgen::generateLods(
        scene, inputPath, outputDir, ratios,
        doTextures ? &texOpts : nullptr );

    if ( !lodsResult )
    {
        std::cerr << "LOD generation failed: " << lodsResult.error().message << "\n";
        return 1;
    }

    for ( const auto& info : *lodsResult )
    {
        std::cout << "lod (ratio=" << info.ratio << "): " << info.outputPath.string() << "\n";
        for ( size_t i = 0; i < info.meshResults.size(); ++i )
            std::cout << "  mesh[" << i << "] "
                      << info.meshResults[i].simplifiedTriangles << " tris\n";
        if ( info.textureStats )
            std::cout << "  textures: " << info.textureStats->outputCount
                      << "/" << info.textureStats->inputCount << " processed\n";
    }

    // ── step 2: build texture atlases (optional) ──────────────────────────────

    if ( doAtlas )
    {
        for ( const auto& info : *lodsResult )
        {
            lodgen::AtlasOptions atlasOpts;
            atlasOpts.modelDir  = inputPath.parent_path();
            atlasOpts.outputDir = info.outputPath.parent_path();

            auto atlasResult = lodgen::buildLodAtlas( info.outputPath, atlasOpts );
            if ( !atlasResult )
            {
                std::cerr << "Atlas failed for '" << info.outputPath.string() << "': "
                          << atlasResult.error().message << "\n";
                return 1;
            }
            for ( const auto& a : *atlasResult )
                std::cout << "  atlas: " << a.filename << " (" << a.inputCount
                          << " textures, " << a.width << "x" << a.height << ")\n";
        }
    }

    return 0;
}
