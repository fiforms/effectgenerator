// effect_generator.h
// Main header for the effect generator framework

#ifndef EFFECT_GENERATOR_H
#define EFFECT_GENERATOR_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdio>
#include <cstdint>

// Program version. Can be overridden at compile time with -DEFFECTGENERATOR_VERSION="\"x.y.z\""
#ifndef EFFECTGENERATOR_VERSION
#define EFFECTGENERATOR_VERSION "0.1.1git"
#endif

inline const char* getEffectGeneratorVersion() {
    return EFFECTGENERATOR_VERSION;
}

// Cross-platform compatibility
#ifdef _WIN32
    #define popen _popen
    #define pclose _pclose
    #define PATH_SEPARATOR "\\"
#else
    #define PATH_SEPARATOR "/"
#endif

// Base class for all effects
class Effect {
public:
    virtual ~Effect() = default;
    
    // Effect metadata
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;

    // Describe the effect's options in a machine-readable form. Each
    // EffectOption contains the option name (e.g. "--size"), type
    // ("int","float","string","boolean"), an optional numeric
    // range (low/high) and a short description. Default implementation
    // returns an empty list for backwards compatibility.
    struct EffectOption {
        std::string name;
        std::string type; // "int", "float", "string", "boolean"
        double rangeLow;
        double rangeHigh;
        bool hasRange;
        std::string description;
        std::string defaultValue; // textual default value (empty if none)
    };

    virtual std::vector<EffectOption> getOptions() const {
        return {};
    }
    
    // Parse effect-specific arguments
    virtual bool parseArgs(int argc, char** argv, int& currentArg) = 0;
    
    // Initialize the effect (called once before generation starts)
    virtual bool initialize(int width, int height, int fps) = 0;
    
    // Generate a single frame
    // frame is pre-allocated as width * height * 3 (RGB24)
    // If hasBackground is true, frame already contains the background
    // fadeMultiplier is 0.0-1.0 for fade in/out effects
    virtual void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) = 0;
    
    // Called after each frame is rendered (for animation updates)
    virtual void update() = 0;
    
    // Optional: for effects that need post-processing with frame index knowledge.
    // The `dropFrame` parameter may be set to `true` by the effect to indicate
    // that the current frame should be dropped (not written to the output).
    virtual void postProcess(std::vector<uint8_t>& frame, int frameIndex, int totalFrames, bool& dropFrame) {
        // Default: do nothing, do not drop frame
        (void)frame; (void)frameIndex; (void)totalFrames;
        dropFrame = false;
    }

    // Optional hook: informs the effect what the total frame count will be
    // (useful for effects that need to align behavior to the overall length).
    virtual void setTotalFrames(int /*totalFrames*/) {
        // Default: do nothing
    }
};

// Effect factory
class EffectFactory {
public:
    using EffectCreator = std::unique_ptr<Effect>(*)();
    
    static EffectFactory& instance() {
        static EffectFactory factory;
        return factory;
    }
    
    void registerEffect(const std::string& name, EffectCreator creator, const std::string& description) {
        creators_[name] = creator;
        descriptions_[name] = description;
    }
    
    std::unique_ptr<Effect> create(const std::string& name) {
        auto it = creators_.find(name);
        if (it != creators_.end()) {
            return it->second();
        }
        return nullptr;
    }
    
    std::vector<std::string> getEffectNames() const {
        std::vector<std::string> names;
        for (const auto& pair : creators_) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    std::string getDescription(const std::string& name) const {
        auto it = descriptions_.find(name);
        return it != descriptions_.end() ? it->second : "";
    }
    
private:
    std::map<std::string, EffectCreator> creators_;
    std::map<std::string, std::string> descriptions_;
};

// Helper macro to register effects
#define REGISTER_EFFECT(EffectClass, name, description) \
    namespace { \
        struct EffectClass##Registrar { \
            EffectClass##Registrar() { \
                EffectFactory::instance().registerEffect(name, \
                    []() -> std::unique_ptr<Effect> { return std::make_unique<EffectClass>(); }, \
                    description); \
            } \
        }; \
        static EffectClass##Registrar global_##EffectClass##Registrar; \
    }

// Video generator class
class VideoGenerator {
private:
    int width_, height_, fps_;
    float fadeDuration_;
    float maxFadeRatio_;
    std::string backgroundImage_;
    std::string backgroundVideo_;
    std::string ffmpegPath_;
    int crf_;
    std::string audioCodec_;
    std::string audioBitrate_;
    
    std::vector<uint8_t> frame_;
    std::vector<uint8_t> backgroundBuffer_;
    bool hasBackground_;
    bool isVideo_;
    FILE* videoInput_;
    FILE* ffmpegOutput_;
    
    std::string findFFmpeg(std::string binaryName);
    bool loadBackgroundImage(const char* filename);
    bool startBackgroundVideo(const char* filename);
    bool readVideoFrame();
    bool startFFmpegOutput(const char* filename);
    // Probe the duration (in seconds) of a video file using ffprobe if available.
    double probeVideoDuration(const char* filename);
    float getFadeMultiplier(int frameNumber, int totalFrames);
    
public:
    VideoGenerator(int width, int height, int fps, float fadeDuration, float maxFadeRatio, int crf = 23, std::string audioCodec = "", std::string audioBitrate = "");
    ~VideoGenerator();
    
    void setFFmpegPath(const std::string& path) { ffmpegPath_ = path; }
    void setCRF(int crf) { crf_ = crf; }
    bool setBackgroundImage(const char* filename);
    bool setBackgroundVideo(const char* filename);
    
    bool generate(Effect* effect, int durationSec, const char* outputFile);
};

#endif // EFFECT_GENERATOR_H
