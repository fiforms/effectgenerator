// main.cpp
// Effect Generator - Main application

#include "effect_generator.h"
#include "json_util.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <unordered_map>

template <typename Options>
void printHelp(const Options& opts) {
    // Generate textual help formatted from structured options
    auto printOptionLine = [](const auto& o) {
        std::cout << "  " << o.name;
        if (o.type == "int") std::cout << " <int>";
        else if (o.type == "float") std::cout << " <float>";
        else if (o.type == "string") std::cout << " <string>";
        else if (o.type == "string.color") std::cout << " <#RRGGBB|string>";
        if (o.hasRange) {
            std::cout << " [" << o.rangeLow << " to " << o.rangeHigh << "]";
        }
        std::cout << "\t" << o.description;
        std::cout << " (default: " << (o.defaultValue.empty() ? "none" : o.defaultValue) << ")";
        if (!o.choices.empty()) {
            std::cout << "\n      choices: [";
            for (size_t i = 0; i < o.choices.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << o.choices[i];
            }
            std::cout << "]";
        }
        std::cout << "\n";
    };

    std::cout << "Effect Options:\n";
    bool hasBasic = false;
    bool hasAdvanced = false;
    for (const auto& o : opts) {
        if (o.advanced) hasAdvanced = true;
        else hasBasic = true;
    }

    if (hasBasic) {
        std::cout << "Basic:\n";
        for (const auto& o : opts) {
            if (!o.advanced) printOptionLine(o);
        }
    }

    if (hasAdvanced) {
        if (hasBasic) std::cout << "\n";
        std::cout << "Advanced:\n";
        for (const auto& o : opts) {
            if (o.advanced) printOptionLine(o);
        }
    }
}

struct EffectInvocation {
    std::string name;
    std::unique_ptr<Effect> effect;
};

using EffectOptionMap = std::unordered_map<std::string, Effect::EffectOption>;

EffectOptionMap makeOptionMap(const Effect& effect) {
    EffectOptionMap out;
    for (const auto& opt : effect.getOptions()) {
        out[opt.name] = opt;
    }
    return out;
}

bool applyEffectOption(Effect& effect, const std::string& option, const std::string* value) {
    std::vector<std::string> storage;
    storage.push_back("effectgenerator");
    storage.push_back(option);
    if (value) storage.push_back(*value);

    std::vector<char*> argvPtrs;
    argvPtrs.reserve(storage.size());
    for (auto& token : storage) argvPtrs.push_back(token.data());

    int idx = 1;
    bool ok = effect.parseArgs((int)argvPtrs.size(), argvPtrs.data(), idx);
    if (!ok) return false;
    if (value && idx != 2) return false;
    if (!value && idx != 1) return false;
    return true;
}

void printUsage(const char* prog) {
    std::cout << "Effect Generator " << getEffectGeneratorVersion() << " - Video Effects Tool\n";
    std::cout << "Find the latest version at https://github.com/fiforms/effectgenerator\n";
    std::cout << "============================\n\n";
    std::cout << "Usage: " << prog << " [global-options] --effect <effect> [effect-options] [--effect <effect> [effect-options] ...] --output [outputfile]\n\n";
    std::cout << "General Options:\n";
    std::cout << "  --help                    Show this help\n";
    std::cout << "  --list-effects            List all available effects\n";
    std::cout << "      --json                When combined with --list-effects or --help-<effectname>, output JSON\n";
    std::cout << "  --effect <name>           Add an effect stage (required; repeatable, order-sensitive)\n";
    std::cout << "  --help-<effectname>       Show help for specific effect\n";
    std::cout << "  --version                 Show program version\n\n";
    std::cout << "  --show                    Print resolved effect configuration and exit\n\n";
    std::cout << "Video Options:\n";
    std::cout << "  --width <int>             Video width (default: 1920)\n";
    std::cout << "  --height <int>            Video height (default: 1080)\n";
    std::cout << "  --fps <int>               Frames per second (default: 30)\n";
    std::cout << "  --duration <int>          Duration in seconds (default: 5)\n";
    std::cout << "  --warmup <float>          Pre-run simulation time in seconds before first output frame (default: 0.0)\n";
    std::cout << "  --fade <float>            Fade in/out duration in seconds (default: 0.0)\n";
    std::cout << "  --max-fade <float>        Maximum opacity (0.0-1.0) of effect (default: 1.0)\n";
    std::cout << "  --background-image <path> Background image (JPG/PNG)\n";
    std::cout << "  --background-video <path> Background video (MP4/MOV/etc), or '-' for stdin rawvideo\n";
    std::cout << "  --video-background <path> Alias for --background-video\n";
    std::cout << "  --crf <int>               Output video quality (default: 23, lower is better)\n\n";
    std::cout << "Audio Options:\n";
    std::cout << "  --audio-codec <string>    Output Audio Codec (passed to ffmpeg, default none)\n";
    std::cout << "  --audio-bitrate <int>     Audio Bitrate in kbps (default: 192)\n";
    std::cout << "Output Options:\n";
    std::cout << "  --output <string>         Output filename (required), or '-' for stdout rawvideo\n";
    std::cout << "  --overwrite               Overwrite output file if it exists\n\n";
    std::cout << "Pipe Format:\n";
    std::cout << "  --background-video -      stdin must be rawvideo rgb24 at --width x --height and --fps\n";
    std::cout << "  --output -                stdout is rawvideo rgb24 at --width x --height and --fps\n\n";
    std::cout << "Environment Variables:\n";
    std::cout << "  FFMPEG_PATH               Path to ffmpeg executable\n";
    std::cout << "  FFPROBE_PATH              Path to ffprobe executable\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " --list-effects\n";
    std::cout << "  " << prog << " --help-snowflake\n";
    std::cout << "  " << prog << " --effect snowflake --flakes 200 --duration 10 --output snow.mp4\n";
    std::cout << "  " << prog << " --fade 2 --background-video input.mp4 --effect laser --rays 10 --effect sparkle --output layered.mp4\n";
    std::cout << "  ffmpeg -i input.mp4 -vf \"scale=1920:1080,fps=30,format=rgb24\" -f rawvideo -pix_fmt rgb24 - | "
              << prog << " --effect flame --preset candle --background-video - --width 1920 --height 1080 --fps 30 --output out.mp4\n";
    std::cout << "  " << prog << " --effect flame --preset candle --width 1920 --height 1080 --fps 30 --duration 10 --output - | "
              << "ffmpeg -f rawvideo -pix_fmt rgb24 -s 1920x1080 -r 30 -i - -c:v libx264 final.mp4\n";
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

void listEffectsJson() {
    auto& factory = EffectFactory::instance();
    auto names = factory.getEffectNames();

    json_util::JsonValue root = json_util::JsonValue::object();
    json_util::JsonValue arr = json_util::JsonValue::array();
    for (const auto& name : names) {
        json_util::JsonValue e = json_util::JsonValue::object();
        e.set("name", json_util::JsonValue(name));
        e.set("description", json_util::JsonValue(factory.getDescription(name)));
        // Include structured options if available
        auto effect = factory.create(name);
        if (effect) {
            auto opts = effect->getOptions();
            json_util::JsonValue optArr = json_util::JsonValue::array();
            for (const auto &o : opts) {
                json_util::JsonValue jo = json_util::JsonValue::object();
                jo.set("name", json_util::JsonValue(o.name));
                jo.set("type", json_util::JsonValue(o.type));
                if (o.hasRange) {
                    json_util::JsonValue range = json_util::JsonValue::object();
                    range.set("low", json_util::JsonValue(o.rangeLow));
                    range.set("high", json_util::JsonValue(o.rangeHigh));
                    jo.set("range", range);
                }
                jo.set("description", json_util::JsonValue(o.description));
                if (!o.defaultValue.empty()) {
                    jo.set("default", json_util::JsonValue(o.defaultValue));
                }
                if (o.advanced) {
                    jo.set("advanced", json_util::JsonValue(true));
                }
                if (!o.choices.empty()) {
                    json_util::JsonValue choices = json_util::JsonValue::array();
                    for (const auto& choice : o.choices) {
                        choices.push_back(json_util::JsonValue(choice));
                    }
                    jo.set("choices", choices);
                }
                optArr.push_back(jo);
            }
            e.set("options", optArr);
        }
        arr.push_back(e);
    }
    root.set("effects", arr);
    std::cout << root.toString() << std::endl;
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
        } else if (arg == "--version") {
            std::cout << "Effect Generator: version " << getEffectGeneratorVersion() << "\n";
            return 0;
        } else if (arg == "--list-effects") {
            bool jsonOut = false;
            if (i + 1 < argc) {
                std::string next = argv[i+1];
                if (next == "--json") {
                    jsonOut = true;
                }
            }
            if (jsonOut) listEffectsJson(); else listEffects();
            return 0;
        } else if (arg.rfind("--help-", 0) == 0) {
            std::string effectName = arg.substr(7);
            bool jsonOut = false;
            if (i + 1 < argc) {
                std::string next = argv[i+1];
                if (next == "--json") jsonOut = true;
            }
            auto effect = EffectFactory::instance().create(effectName);
            if (effect) {
                if (jsonOut) {
                    json_util::JsonValue out = json_util::JsonValue::object();
                    out.set("name", json_util::JsonValue(effect->getName()));
                    out.set("description", json_util::JsonValue(effect->getDescription()));
                    // If effect provides structured options, include them in JSON
                    auto opts = effect->getOptions();
                    json_util::JsonValue optArr = json_util::JsonValue::array();
                    for (const auto &o : opts) {
                        json_util::JsonValue jo = json_util::JsonValue::object();
                        jo.set("name", json_util::JsonValue(o.name));
                        jo.set("type", json_util::JsonValue(o.type));
                        if (o.hasRange) {
                            json_util::JsonValue range = json_util::JsonValue::object();
                            range.set("low", json_util::JsonValue(o.rangeLow));
                            range.set("high", json_util::JsonValue(o.rangeHigh));
                            jo.set("range", range);
                        }
                        jo.set("description", json_util::JsonValue(o.description));
                        if (!o.defaultValue.empty()) {
                            jo.set("default", json_util::JsonValue(o.defaultValue));
                        }
                        if (o.advanced) {
                            jo.set("advanced", json_util::JsonValue(true));
                        }
                        if (!o.choices.empty()) {
                            json_util::JsonValue choices = json_util::JsonValue::array();
                            for (const auto& choice : o.choices) {
                                choices.push_back(json_util::JsonValue(choice));
                            }
                            jo.set("choices", choices);
                        }
                        optArr.push_back(jo);
                    }
                    out.set("options", optArr);
                    // Also capture textual help as a convenience (fallback)
                    std::ostringstream helposs;
                    auto oldbuf = std::cout.rdbuf(helposs.rdbuf());
                    std::cout.rdbuf(oldbuf);
                    out.set("help", json_util::JsonValue(helposs.str()));
                    std::cout << out.toString() << std::endl;
                } else {
                    std::cout << "Effect: " << effect->getName() << "\n";
                    std::cout << effect->getDescription() << "\n\n";
                    auto effectOpts = effect->getOptions();
                    printHelp(effectOpts);
                }
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
    float warmupDuration = 0.0f;
    float fadeDuration = 0.0f;
    float maxFadeRatio = 1.0f;
    std::string output = "";
    bool showConfig = false;
    bool overwriteOutput = false;
    std::string backgroundImage;
    std::string backgroundVideo;
    std::string audioCodec;
    std::string audioBitrate = "";

    std::vector<EffectInvocation> stages;
    std::vector<EffectOptionMap> stageOptionMaps;
    int currentStage = -1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--effect" && i + 1 < argc) {
            std::string effectName = argv[++i];
            auto effect = EffectFactory::instance().create(effectName);
            if (!effect) {
                std::cerr << "Unknown effect: " << effectName << "\n";
                std::cerr << "Use --list-effects to see available effects.\n";
                return 1;
            }

            currentStage = (int)stages.size();
            stageOptionMaps.push_back(makeOptionMap(*effect));
            stages.push_back({effectName, std::move(effect)});
            continue;
        }

        if (currentStage >= 0) {
            auto optIt = stageOptionMaps[currentStage].find(arg);
            if (optIt != stageOptionMaps[currentStage].end()) {
                const auto& opt = optIt->second;
                std::string value;
                const std::string* valuePtr = nullptr;
                if (opt.type == "boolean") {
                    // Allow optional explicit boolean values, e.g.:
                    // --heart-spin true / --heart-spin false
                    if (i + 1 < argc) {
                        std::string next = argv[i + 1];
                        if (!next.empty() && next.rfind("--", 0) != 0) {
                            value = next;
                            valuePtr = &value;
                            ++i;
                        }
                    }
                } else {
                    if (i + 1 >= argc) {
                        std::cerr << "Missing value for option " << arg
                                  << " in effect stage " << (currentStage + 1)
                                  << " (" << stages[currentStage].name << ")\n";
                        return 1;
                    }
                    value = argv[++i];
                    valuePtr = &value;
                }
                if (!applyEffectOption(*stages[currentStage].effect, arg, valuePtr)) {
                    std::cerr << "Failed to parse option " << arg
                              << " in effect stage " << (currentStage + 1)
                              << " (" << stages[currentStage].name << ")\n";
                    return 1;
                }
                continue;
            }
        }

        if (arg == "--width" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::atoi(argv[++i]);
        } else if (arg == "--warmup" && i + 1 < argc) {
            warmupDuration = std::atof(argv[++i]);
        } else if (arg == "--fade" && i + 1 < argc) {
            fadeDuration = std::atof(argv[++i]);
        } else if (arg == "--max-fade" && i + 1 < argc) {
            maxFadeRatio = std::atof(argv[++i]);
        } else if (arg == "--crf" && i + 1 < argc) {
            crf = std::atoi(argv[++i]);
        } else if (arg == "--audio-codec" && i + 1 < argc) {
            audioCodec = argv[++i];
        } else if (arg == "--audio-bitrate" && i + 1 < argc) {
            audioBitrate = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--show") {
            showConfig = true;
        } else if (arg == "--overwrite") {
            overwriteOutput = true;
        } else if (arg == "--background-image" && i + 1 < argc) {
            backgroundImage = argv[++i];
        } else if ((arg == "--background-video" || arg == "--video-background") && i + 1 < argc) {
            backgroundVideo = argv[++i];
        } else {
            std::cerr << "Unknown or invalid argument: " << arg << "\n";
            if (currentStage >= 0) {
                std::cerr << "Note: option did not match declared options for effect stage "
                          << (currentStage + 1) << " (" << stages[currentStage].name << ")\n";
            }
            return 1;
        }
    }

    if (stages.empty()) {
        std::cerr << "Error: No effect specified. Use --effect <name>\n";
        std::cerr << "Use --list-effects to see available effects.\n";
        return 1;
    }

    if (showConfig) {
        std::cout << "Effect pipeline configuration (resolved):\n";
        std::cout << "Resolution: " << width << "x" << height << "\n";
        std::cout << "FPS: " << fps << "\n";
        for (size_t i = 0; i < stages.size(); ++i) {
            auto& stage = stages[i];
            if (!stage.effect->initialize(width, height, fps)) {
                std::cerr << "Error: Failed to initialize effect stage " << (i + 1)
                          << " (" << stage.effect->getName() << ") for --show\n";
                return 1;
            }
            std::cout << "\nStage " << (i + 1) << ": " << stage.effect->getName() << "\n";
            stage.effect->printConfig(std::cout);
        }
        return 0;
    }

    if (output.empty()) {
        std::cerr << "Error: Output filename is required. Use --output [filename]\n";
        return 1;
    }

    // Check if output file exists
    if (!overwriteOutput && output != "-") {
      if(FILE* file = std::fopen(output.c_str(), "rb")) {
        std::fclose(file);
        std::cerr << "Error: Output file '" << output << "' already exists. Please choose a different name or pass --overwrite.\n";
        return 1;
      }
    }
    
    if (!backgroundImage.empty() && !backgroundVideo.empty()) {
        std::cerr << "Error: Cannot specify both --background-image and --background-video\n";
        return 1;
    }
    
    // Create video generator (pass CLI CRF through)
    VideoGenerator generator(width, height, fps, fadeDuration, maxFadeRatio, crf, audioCodec, audioBitrate);
    generator.setWarmupSeconds(warmupDuration);
    
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

    const bool outputToStdoutRaw = (output == "-");
    std::ostream& infoOut = outputToStdoutRaw ? std::cerr : std::cout;
    
    // Print configuration
    infoOut << "Effect Generator\n";
    infoOut << "================\n";
    infoOut << "Effects: ";
    for (size_t i = 0; i < stages.size(); ++i) {
        if (i) infoOut << " -> ";
        infoOut << stages[i].effect->getName();
    }
    infoOut << "\n";
    infoOut << "Resolution: " << width << "x" << height << "\n";
    infoOut << "FPS: " << fps << "\n";
    if (duration == -1) {
        infoOut << "Duration: auto-detect from video\n";
    } else {
        infoOut << "Duration: " << duration << "s\n";
    }
    infoOut << "Fade duration: " << fadeDuration << "s\n";
    if (warmupDuration > 0.0f) {
        infoOut << "Warmup duration: " << warmupDuration << "s\n";
    }
    infoOut << "Max Fade Ratio: " << maxFadeRatio << "\n";
    if (!backgroundImage.empty()) {
        infoOut << "Background image: " << backgroundImage << "\n";
    }
    if (!backgroundVideo.empty()) {
        infoOut << "Background video: " << backgroundVideo << "\n";
    }
    infoOut << "Output: " << output << "\n\n";
    
    std::vector<Effect*> pipeline;
    pipeline.reserve(stages.size());
    for (auto& stage : stages) {
        pipeline.push_back(stage.effect.get());
    }

    // Generate video
    if (!generator.generate(pipeline, duration, output.c_str())) {
        std::cerr << "Error: Video generation failed\n";
        return 1;
    }
    
    return 0;
}
