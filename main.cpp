// main.cpp
// Effect Generator - Main application

#include "effect_generator.h"
#include <iostream>
#include <string>
#include <cstdlib>

void printUsage(const char* prog) {
    std::cout << "Effect Generator - Video Effects Tool\n\n";
    std::cout << "Usage: " << prog << " [options]\n\n";
    std::cout << "General Options:\n";
    std::cout << "  --help                    Show this help\n";
    std::cout << "  --list-effects            List all available effects\n";
    std::cout << "  --effect <name>           Select effect to use (required)\n";
    std::cout << "  --help-<effectname>       Show help for specific effect\n\n";
    std::cout << "Video Options:\n";
    std::cout << "  --width <int>             Video width (default: 1920)\n";
    std::cout << "  --height <int>            Video height (default: 1080)\n";
    std::cout << "  --fps <int>               Frames per second (default: 30)\n";
    std::cout << "  --duration <int>          Duration in seconds (default: 5)\n";
    std::cout << "  --fade <float>            Fade in/out duration in seconds (default: 0.0)\n";
    std::cout << "  --background-image <path> Background image (JPG/PNG)\n";
    std::cout << "  --background-video <path> Background video (MP4/MOV/etc)\n";
    std::cout << "  --output <string>         Output filename (default: output.mp4)\n\n";
    std::cout << "Environment Variables:\n";
    std::cout << "  FFMPEG_PATH               Path to ffmpeg executable\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " --list-effects\n";
    std::cout << "  " << prog << " --help-snowflake\n";
    std::cout << "  " << prog << " --effect snowflake --flakes 200 --duration 10\n";
    std::cout << "  " << prog << " --effect snowflake --background-video input.mp4 --output snowy.mp4\n";
}

void listEffects() {
    std::cout << "Available Effects:\n";
    std::cout << "==================\n\n";
    
    auto& factory = EffectFactory::instance();
    auto names = factory.getEffectNames();
    
    if (names.empty()) {
        std::cout << "No effects registered.\n";
        return;
    }
    
    for (const auto& name : names) {
        std::cout << "  " << name << "\n";
        std::cout << "    " << factory.getDescription(name) << "\n\n";
    }
}

int main(int argc, char** argv) {
    // Check for help or list
    if (argc == 1) {
        printUsage(argv[0]);
        return 0;
    }
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--list-effects") {
            listEffects();
            return 0;
        } else if (arg.rfind("--help-", 0) == 0) {
            std::string effectName = arg.substr(7);
            auto effect = EffectFactory::instance().create(effectName);
            if (effect) {
                std::cout << "Effect: " << effect->getName() << "\n";
                std::cout << effect->getDescription() << "\n\n";
                effect->printHelp();
            } else {
                std::cerr << "Unknown effect: " << effectName << "\n";
                std::cerr << "Use --list-effects to see available effects.\n";
                return 1;
            }
            return 0;
        }
    }
    
    // Parse common arguments
    int width = 1920, height = 1080, fps = 30, duration = -1; // -1 means auto-detect
    int crf = 23;
    float fadeDuration = 0.0f;
    std::string output = "output.mp4";
    std::string backgroundImage;
    std::string backgroundVideo;
    std::string effectName;
    std::string ffmpegPath;
    
    std::unique_ptr<Effect> effect;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--width" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::atoi(argv[++i]);
        } else if (arg == "--fade" && i + 1 < argc) {
            fadeDuration = std::atof(argv[++i]);
        } else if (arg == "--crf" && i + 1 < argc) {
            crf = std::atoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--background-image" && i + 1 < argc) {
            backgroundImage = argv[++i];
        } else if (arg == "--background-video" && i + 1 < argc) {
            backgroundVideo = argv[++i];
        } else if (arg == "--effect" && i + 1 < argc) {
            effectName = argv[++i];
            effect = EffectFactory::instance().create(effectName);
            if (!effect) {
                std::cerr << "Unknown effect: " << effectName << "\n";
                std::cerr << "Use --list-effects to see available effects.\n";
                return 1;
            }
        } else if (effect && effect->parseArgs(argc, argv, i)) {
            // Effect parsed its own argument
            continue;
        } else {
            std::cerr << "Unknown or invalid argument: " << arg << "\n";
            return 1;
        }
    }
    
    if (!effect) {
        std::cerr << "Error: No effect specified. Use --effect <name>\n";
        std::cerr << "Use --list-effects to see available effects.\n";
        return 1;
    }
    
    if (!backgroundImage.empty() && !backgroundVideo.empty()) {
        std::cerr << "Error: Cannot specify both --background-image and --background-video\n";
        return 1;
    }
    
    // Create video generator
    VideoGenerator generator(width, height, fps, fadeDuration);
    
    // Set background if specified
    if (!backgroundImage.empty()) {
        if (!generator.setBackgroundImage(backgroundImage.c_str())) {
            std::cerr << "Error: Could not load background image\n";
            return 1;
        }
    }
    
    if (!backgroundVideo.empty()) {
        if (!generator.setBackgroundVideo(backgroundVideo.c_str())) {
            std::cerr << "Error: Could not load background video\n";
            return 1;
        }
        
        // If duration not specified, keep it as -1 for auto-detect
        // Don't set to 5 here!
    }
    
    // Use default duration only if no video background
    if (duration == -1 && backgroundVideo.empty()) {
        duration = 5;
    }
    
    // Print configuration
    std::cout << "Effect Generator\n";
    std::cout << "================\n";
    std::cout << "Effect: " << effect->getName() << "\n";
    std::cout << "Resolution: " << width << "x" << height << "\n";
    std::cout << "FPS: " << fps << "\n";
    if (duration == -1) {
        std::cout << "Duration: auto-detect from video\n";
    } else {
        std::cout << "Duration: " << duration << "s\n";
    }
    std::cout << "Fade duration: " << fadeDuration << "s\n";
    if (!backgroundImage.empty()) {
        std::cout << "Background image: " << backgroundImage << "\n";
    }
    if (!backgroundVideo.empty()) {
        std::cout << "Background video: " << backgroundVideo << "\n";
    }
    std::cout << "Output: " << output << "\n\n";
    
    // Generate video
    if (!generator.generate(effect.get(), duration, output.c_str())) {
        std::cerr << "Error: Video generation failed\n";
        return 1;
    }
    
    return 0;
}
