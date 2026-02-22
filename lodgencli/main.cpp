#include <lodgen/lodgen.hpp>
#include <lodgen/scene_io.hpp>
#include <cxxopts.hpp>
#include <iostream>

static void printError( const lodgen::Error& err )
{
    std::cerr << "Error: " << err.message << "\n";

    if ( err.code == lodgen::ErrorCode::UnsupportedFormat )
    {
        std::cerr << "Supported formats:";
        for ( const auto& fmt : lodgen::supportedFormats() )
            std::cerr << " " << fmt;
        std::cerr << "\n";
    }
}

int main( int argc, char* argv[] )
{
    cxxopts::Options options( "lodgencli", "Mesh LOD generator" );
    options.add_options()
        ( "i,input",    "Input model file",
          cxxopts::value<std::string>() )
        ( "o,output",   "Output directory (default: ./output)",
          cxxopts::value<std::string>()->default_value( "output" ) )
        ( "r,ratios",   "Triangle reduction ratios (default: 0.5 0.25 0.125)",
          cxxopts::value<std::vector<float>>()->default_value( "0.5,0.25,0.125" ) )
        ( "t,textures", "Resize textures proportional to mesh ratio" )
        ( "a,atlas",    "Pack all textures into a single atlas" )
        ( "h,help",     "Print usage" );

    options.parse_positional( { "input" } );
    options.positional_help( "<input_file>" );

    cxxopts::ParseResult args;
    try
    {
        args = options.parse( argc, argv );
    }
    catch ( const cxxopts::exceptions::exception& e )
    {
        std::cerr << "Error: " << e.what() << "\n\n" << options.help() << "\n";
        return 1;
    }

    if ( args.count( "help" ) || !args.count( "input" ) )
    {
        std::cout << options.help() << "\n";
        return args.count( "help" ) ? 0 : 1;
    }

    lodgen::fs::path inputPath( args["input"].as<std::string>() );
    lodgen::fs::path outputDir( args["output"].as<std::string>() );

    auto ratios = args["ratios"].as<std::vector<float>>();
    for ( float r : ratios )
    {
        if ( r <= 0.0f || r >= 1.0f )
        {
            std::cerr << "Error: ratio " << r << " must be in range (0, 1)\n";
            return 1;
        }
    }

    const bool useTextures = args.count( "textures" ) > 0;
    const bool useAtlas    = args.count( "atlas" ) > 0;

    // Load scene
    std::cout << "Loading: " << inputPath.filename().string() << "\n";
    auto sceneResult = lodgen::loadScene( inputPath );
    if ( !sceneResult )
    {
        printError( sceneResult.error() );
        return 1;
    }

    const aiScene* scene = sceneResult->get();
    std::cout << "  " << scene->mNumMeshes << " mesh(es), "
              << scene->mNumTextures << " embedded texture(s)\n";
    for ( unsigned int i = 0; i < scene->mNumMeshes; ++i )
    {
        const aiMesh* mesh = scene->mMeshes[i];
        std::cout << "  [" << i << "] ";
        if ( mesh->mName.length > 0 )
            std::cout << mesh->mName.C_Str() << " ";
        std::cout << mesh->mNumVertices << " verts, " << mesh->mNumFaces << " tris\n";
    }

    // Build texture options if needed
    std::unique_ptr<lodgen::TextureOptions> texOpts;
    if ( useTextures || useAtlas )
    {
        texOpts = std::make_unique<lodgen::TextureOptions>();
        texOpts->resizeTextures = useTextures;
        texOpts->buildAtlas     = useAtlas;
        texOpts->modelDir       = inputPath.parent_path();
    }

    // Generate LODs
    auto lodsResult = lodgen::generateLods( scene, inputPath, outputDir, ratios, texOpts.get() );
    if ( !lodsResult )
    {
        printError( lodsResult.error() );
        return 1;
    }

    // Report results
    for ( size_t i = 0; i < lodsResult->size(); ++i )
    {
        const auto& lod = ( *lodsResult )[i];
        std::cout << "\nLOD " << ( i + 1 )
                  << " (" << static_cast<int>( lod.ratio * 100 ) << "%) -> "
                  << lod.outputPath.string() << "\n";

        for ( size_t m = 0; m < lod.meshResults.size(); ++m )
            std::cout << "  [" << m << "] " << lod.meshResults[m].simplifiedTriangles << " tris\n";

        if ( lod.textureStats )
        {
            const auto& ts = *lod.textureStats;
            std::cout << "  textures: " << ts.inputCount << " -> " << ts.outputCount;
            if ( ts.atlasWidth > 0 )
                std::cout << " (atlas " << ts.atlasWidth << "x" << ts.atlasHeight << ")";
            std::cout << "\n";
        }
    }

    std::cout << "\nDone.\n";
    return 0;
}
