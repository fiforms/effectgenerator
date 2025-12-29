// wave_effect.cpp
// Water wave ripple effect with interference and directional lighting

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

struct WaveSource {
    float x, y;           // Position
    float phase;          // Current phase
    float frequency;      // Wave frequency
    float amplitude;      // Base wave amplitude
    float currentStrength;// Current strength multiplier (0.0-1.0)
    float targetStrength; // Target strength to ramp to
    float speed;          // Propagation speed
    float decay;          // Amplitude decay with distance
    bool active;          // Whether this source is emitting
    int startFrame;       // When this source starts
    int endFrame;         // When this source stops (-1 = infinite)
    int rampUpFrames;     // Frames to ramp up to full strength
    int rampDownFrames;   // Frames to ramp down to zero
};

// Recorded spawn specification used for warmup recording/replay
struct SpawnSpec {
    float x, y;
    int duration; // frames
    float frequency;
    float amplitude;
    float targetStrength;
    float speed;
    float decay;
    int rampUpFrames;
    int rampDownFrames;
};

class WaveEffect : public Effect {
private:
    int width_, height_, fps_;
    
    // Wave parameters
    int numSources_;
    float baseAmplitude_;
    float baseFrequency_;
    float baseSpeed_;
    float baseDecay_;
    float lightAngle_;         // Angle from top-left for lighting (radians)
    float lightIntensity_;     // Strength of directional lighting effect
    float waveInterference_;   // How much waves interfere (0.0-1.0)
    float displacementScale_;  // Pixel displacement multiplier
    bool useDisplacement_;     // Whether to use displacement or brightness modulation
    std::string waveDirection_; // Direction for directional waves (empty = omnidirectional)
    
    // Random generation
    float sourceSpawnProb_;    // Probability of spawning new source each frame
    float offscreenProb_;      // Probability source is offscreen
    float minLifetime_;        // Minimum wave source lifetime (seconds)
    float maxLifetime_;        // Maximum wave source lifetime (seconds)
    // Warmup (pre-simulate) to stabilize the field before first output
    float warmupSeconds_;
    int warmupFrames_;
    // Recorded spawns per warmup-frame index
    std::vector<std::vector<SpawnSpec>> warmupSpawns_;
    // If >0, when setTotalFrames() is called, we will replay warmupSpawns_
    // at the end of the video (to make the video loop seamlessly)
    int targetTotalFrames_;
    
    std::vector<WaveSource> sources_;
    std::mt19937 rng_;
    int frameCount_;
    
    void getSpawnRegionForDirection(const std::string& dir, float& minX, float& maxX, float& minY, float& maxY) {
        // Offscreen margin
        float margin = std::max(width_, height_) * 0.15f;
        
        if (dir == "up") {
            minX = -margin;
            maxX = width_ + margin;
            minY = height_ + margin;
            maxY = height_ + margin * 2;
        } else if (dir == "down") {
            minX = -margin;
            maxX = width_ + margin;
            minY = -margin * 2;
            maxY = -margin;
        } else if (dir == "left") {
            minX = width_ + margin;
            maxX = width_ + margin * 2;
            minY = -margin;
            maxY = height_ + margin;
        } else if (dir == "right") {
            minX = -margin * 2;
            maxX = -margin;
            minY = -margin;
            maxY = height_ + margin;
        } else if (dir == "upleft") {
            minX = width_ + margin;
            maxX = width_ + margin * 2;
            minY = height_ + margin;
            maxY = height_ + margin * 2;
        } else if (dir == "upright") {
            minX = -margin * 2;
            maxX = -margin;
            minY = height_ + margin;
            maxY = height_ + margin * 2;
        } else if (dir == "downleft") {
            minX = width_ + margin;
            maxX = width_ + margin * 2;
            minY = -margin * 2;
            maxY = -margin;
        } else if (dir == "downright") {
            minX = -margin * 2;
            maxX = -margin;
            minY = -margin * 2;
            maxY = -margin;
        } else {
            // Shouldn't reach here, but default to off-screen
            minX = -margin;
            maxX = width_ + margin;
            minY = -margin;
            maxY = height_ + margin;
        }
    }
    
    void createSource(float x, float y, int startFrame, int duration) {
        std::uniform_real_distribution<float> distFreq(baseFrequency_ * 0.5f, baseFrequency_ * 2.0f);
        std::normal_distribution<float> distSpeed(baseSpeed_, baseSpeed_ * 0.15f);
        std::uniform_real_distribution<float> distDecay(baseDecay_ * 0.8f, baseDecay_ * 1.2f);
        std::uniform_real_distribution<float> distStrength(0.5f, 1.0f);
        
        // Double amplitude for directional waves to account for decay over distance
        float amplitudeMultiplier = waveDirection_.empty() ? 1.0f : 2.0f;
        std::normal_distribution<float> distAmp(baseAmplitude_ * amplitudeMultiplier, baseAmplitude_ * amplitudeMultiplier * 0.3f);
        
        WaveSource ws;
        ws.x = x;
        ws.y = y;
        ws.phase = 0.0f;
        ws.frequency = std::max(0.01f, distFreq(rng_));
        ws.amplitude = std::max(0.01f, distAmp(rng_));
        ws.currentStrength = 0.0f;  // Start at zero
        ws.targetStrength = distStrength(rng_);  // Random target strength
        ws.speed = std::max(10.0f, distSpeed(rng_));
        ws.decay = distDecay(rng_);
        ws.active = true;
        ws.startFrame = startFrame;
        ws.endFrame = (duration > 0) ? (startFrame + duration) : -1;
        
        // Ramp times as fraction of lifetime
        if (duration > 0) {
            ws.rampUpFrames = (int)(duration * 0.2f);    // 20% of lifetime to ramp up
            ws.rampDownFrames = (int)(duration * 0.25f); // 25% of lifetime to ramp down
        } else {
            ws.rampUpFrames = fps_ * 2;  // 2 seconds
            ws.rampDownFrames = fps_ * 2; // 2 seconds
        }
        
        sources_.push_back(ws);
    }

    // Create a source from a recorded SpawnSpec. startFrame overrides the stored
    // start so the recorded spawn can be replayed at a new time.
    void createSourceFromSpec(const SpawnSpec& spec, int startFrame) {
        WaveSource ws;
        ws.x = spec.x;
        ws.y = spec.y;
        ws.phase = 0.0f;
        ws.frequency = spec.frequency;
        ws.amplitude = spec.amplitude;
        ws.currentStrength = 0.0f;
        ws.targetStrength = spec.targetStrength;
        ws.speed = spec.speed;
        ws.decay = spec.decay;
        ws.active = true;
        ws.startFrame = startFrame;
        ws.endFrame = (spec.duration > 0) ? (startFrame + spec.duration) : -1;
        ws.rampUpFrames = spec.rampUpFrames;
        ws.rampDownFrames = spec.rampDownFrames;

        sources_.push_back(ws);
    }
    
    void spawnRandomSource() {
        // If we've been given a target total frame count and warmup recordings,
        // replay the recorded spawns mapped to the end of the video so the
        // sequence matches the warmup (for seamless looping).
        if (targetTotalFrames_ > 0 && warmupFrames_ > 0) {
            int replayStart = targetTotalFrames_ - warmupFrames_;
            if (frameCount_ >= replayStart && frameCount_ < targetTotalFrames_) {
                int idx = frameCount_ - replayStart;
                if (idx >= 0 && idx < (int)warmupSpawns_.size()) {
                    for (const auto& spec : warmupSpawns_[idx]) {
                        createSourceFromSpec(spec, frameCount_);
                    }
                }
                return;
            }
        }

        std::uniform_real_distribution<float> prob(0.0f, 1.0f);
        std::uniform_real_distribution<float> distLife(minLifetime_, maxLifetime_);
        
        if (prob(rng_) < sourceSpawnProb_) {
            float x, y;
            
            if (!waveDirection_.empty()) {
                // Directional mode - spawn in specific region
                float minX, maxX, minY, maxY;
                getSpawnRegionForDirection(waveDirection_, minX, maxX, minY, maxY);
                std::uniform_real_distribution<float> distX(minX, maxX);
                std::uniform_real_distribution<float> distY(minY, maxY);
                x = distX(rng_);
                y = distY(rng_);
            } else {
                // Omnidirectional mode - use original logic
                std::uniform_real_distribution<float> distX(-width_ * 0.2f, width_ * 1.2f);
                std::uniform_real_distribution<float> distY(-height_ * 0.2f, height_ * 1.2f);
                
                // Decide if offscreen
                if (prob(rng_) < offscreenProb_) {
                    // Place offscreen
                    int edge = (int)(prob(rng_) * 4); // 0=top, 1=right, 2=bottom, 3=left
                    switch (edge) {
                        case 0: x = distX(rng_); y = -height_ * 0.1f; break;
                        case 1: x = width_ * 1.1f; y = distY(rng_); break;
                        case 2: x = distX(rng_); y = height_ * 1.1f; break;
                        default: x = -width_ * 0.1f; y = distY(rng_); break;
                    }
                } else {
                    // Place onscreen
                    std::uniform_real_distribution<float> distOnX(0.0f, width_);
                    std::uniform_real_distribution<float> distOnY(0.0f, height_);
                    x = distOnX(rng_);
                    y = distOnY(rng_);
                }
            }
            
            int lifetime = (int)(distLife(rng_) * fps_);

            // Build the same random parameter set that createSource() would
            std::uniform_real_distribution<float> distFreq(baseFrequency_ * 0.5f, baseFrequency_ * 2.0f);
            std::normal_distribution<float> distSpeed(baseSpeed_, baseSpeed_ * 0.15f);
            std::uniform_real_distribution<float> distDecay(baseDecay_ * 0.8f, baseDecay_ * 1.2f);
            std::uniform_real_distribution<float> distStrength(0.5f, 1.0f);
            float amplitudeMultiplier = waveDirection_.empty() ? 1.0f : 2.0f;
            std::normal_distribution<float> distAmp(baseAmplitude_ * amplitudeMultiplier, baseAmplitude_ * amplitudeMultiplier * 0.3f);

            SpawnSpec spec;
            spec.x = x;
            spec.y = y;
            spec.duration = lifetime;
            spec.frequency = std::max(0.01f, distFreq(rng_));
            spec.amplitude = std::max(0.01f, distAmp(rng_));
            spec.targetStrength = distStrength(rng_);
            spec.speed = std::max(10.0f, distSpeed(rng_));
            spec.decay = distDecay(rng_);
            // Ramp times as fraction of lifetime
            if (lifetime > 0) {
                spec.rampUpFrames = (int)(lifetime * 0.2f);
                spec.rampDownFrames = (int)(lifetime * 0.25f);
            } else {
                spec.rampUpFrames = fps_ * 2;
                spec.rampDownFrames = fps_ * 2;
            }

            // Create and (if in warmup) record the parameters
            createSourceFromSpec(spec, frameCount_);
            if (warmupFrames_ > 0 && frameCount_ < warmupFrames_) {
                warmupSpawns_[frameCount_].push_back(spec);
            }
        }
    }
    
    float calculateWaveHeight(float x, float y) {
        float totalHeight = 0.0f;
        
        for (const auto& ws : sources_) {
            if (!ws.active) continue;
            
            float dx = x - ws.x;
            float dy = y - ws.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            
            // Wave equation: A * strength * sin(k*r - ωt) / (1 + decay*r)
            float waveNumber = ws.frequency;
            float r = dist;
            float phase = waveNumber * r - ws.phase;
            
            // Amplitude decreases with distance
            float distFactor = 1.0f / (1.0f + ws.decay * r);
            
            // Apply current strength for smooth ramping
            float waveValue = ws.amplitude * ws.currentStrength * std::sin(phase) * distFactor;
            
            totalHeight += waveValue;
        }
        
        return totalHeight;
    }
    
    float calculateDirectionalLight(float x, float y, float waveHeight) {
        // Calculate normal vector from wave height gradient (simplified)
        // In reality we'd compute gradient, but for performance we'll use the wave height directly
        // to create a directional lighting effect
        
        // Light direction from top-left corner
        float lightX = std::cos(lightAngle_);
        float lightY = std::sin(lightAngle_);
        
        // Use wave height as a proxy for surface tilt
        // Positive wave height = surface tilted toward light = brighter
        // Negative wave height = surface tilted away = darker
        
        float lightEffect = waveHeight * lightIntensity_;
        
        // Add distance-based falloff for more realism
        float dx = x;
        float dy = y;
        float distFromCorner = std::sqrt(dx * dx + dy * dy);
        float maxDist = std::sqrt(width_ * width_ + height_ * height_);
        float distFactor = 1.0f - (distFromCorner / maxDist) * 0.3f; // Slight falloff
        
        return lightEffect * distFactor;
    }
    
public:
        WaveEffect()
        : numSources_(3), baseAmplitude_(0.3f), baseFrequency_(0.02f),
          baseSpeed_(2.0f), baseDecay_(0.001f),
          lightAngle_(-kPi / 4.0f), lightIntensity_(0.3f), waveInterference_(1.0f),
                    displacementScale_(10.0f), useDisplacement_(true), waveDirection_(""),
          sourceSpawnProb_(0.06f), offscreenProb_(0.5f),
                    minLifetime_(2.0f), maxLifetime_(8.0f),
                    rng_(std::random_device{}()), frameCount_(0), warmupSeconds_(0.0f), warmupFrames_(0), targetTotalFrames_(-1) {}
    
    std::string getName() const override {
        return "waves";
    }
    
    std::string getDescription() const override {
        return "Water wave ripples with interference and directional lighting";
    }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--sources", "int", 1, 100000, true, "Initial number of wave sources", "3"});
        opts.push_back({"--amplitude", "float", 0.0, 10000.0, true, "Base wave amplitude", "0.3"});
        opts.push_back({"--frequency", "float", 0.0, 10.0, true, "Base wave frequency", "0.02"});
        opts.push_back({"--speed", "float", 0.0, 10000.0, true, "Wave propagation speed", "2.0"});
        opts.push_back({"--decay", "float", 0.0, 1000.0, true, "Wave decay with distance", "0.001"});
        opts.push_back({"--direction", "string", 0, 0, false, "Wave direction: up/down/left/right/upleft/upright/downleft/downright", ""});
        opts.push_back({"--warmup", "float", 0.0, 100000.0, true, "Warmup time in seconds to stabilize waves", "0.0"});
        opts.push_back({"--light-angle", "float", -360.0, 360.0, true, "Light direction in degrees", "-45"});
        opts.push_back({"--light-intensity", "float", 0.0, 10.0, true, "Lighting effect strength", "0.3"});
        opts.push_back({"--interference", "float", 0.0, 1.0, true, "Wave interference amount 0.0-1.0", "1.0"});
        opts.push_back({"--no-displacement", "boolean", 0, 1, false, "Disable pixel displacement (brightness only)", "false"});
        opts.push_back({"--displacement-scale", "float", 0.0, 1000.0, true, "Displacement strength in pixels", "10.0"});
        opts.push_back({"--spawn-prob", "float", 0.0, 1.0, true, "Random source spawn probability", "0.06"});
        opts.push_back({"--offscreen-prob", "float", 0.0, 1.0, true, "Probability source is offscreen", "0.5"});
        opts.push_back({"--min-lifetime", "float", 0.0, 100000.0, true, "Min source lifetime in seconds", "2.0"});
        opts.push_back({"--max-lifetime", "float", 0.0, 100000.0, true, "Max source lifetime in seconds", "8.0"});
        return opts;
    }
    
    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        
        if (arg == "--sources" && i + 1 < argc) {
            numSources_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--amplitude" && i + 1 < argc) {
            baseAmplitude_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--frequency" && i + 1 < argc) {
            baseFrequency_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--speed" && i + 1 < argc) {
            baseSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--decay" && i + 1 < argc) {
            baseDecay_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--light-angle" && i + 1 < argc) {
            float degrees = std::atof(argv[++i]);
            lightAngle_ = degrees * kPi / 180.0f;
            return true;
        } else if (arg == "--light-intensity" && i + 1 < argc) {
            lightIntensity_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--interference" && i + 1 < argc) {
            waveInterference_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--no-displacement") {
            useDisplacement_ = false;
            return true;
        } else if (arg == "--displacement-scale" && i + 1 < argc) {
            displacementScale_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--spawn-prob" && i + 1 < argc) {
            sourceSpawnProb_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--offscreen-prob" && i + 1 < argc) {
            offscreenProb_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--min-lifetime" && i + 1 < argc) {
            minLifetime_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--max-lifetime" && i + 1 < argc) {
            maxLifetime_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--direction" && i + 1 < argc) {
            waveDirection_ = argv[++i];
            return true;
        } else if (arg == "--warmup" && i + 1 < argc) {
            warmupSeconds_ = std::atof(argv[++i]);
            return true;
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        frameCount_ = 0;

        // If user didn't request an explicit warmup, but we were informed of
        // the total frame count earlier via setTotalFrames(), default the
        // warmup to (video_length - 5s) when the video is shorter than 65s.
        if (warmupSeconds_ <= 0.0f && targetTotalFrames_ > 0) {
            double totalSecs = (double)targetTotalFrames_ / (double)fps_;
            //if (totalSecs >= 30.0 && totalSecs < 75.0) {
                warmupSeconds_ = std::max(0.0, totalSecs + 30.0);
                std::cout << "Defaulting warmup to " << warmupSeconds_ << "s based on video length\n";
            //}
        }
        
        // Prepare warmup recording if requested (allocate before initial sources)
        if (warmupSeconds_ > 0.0f) {
            warmupFrames_ = (int)std::round(warmupSeconds_ * fps_);
            if (warmupFrames_ > 0) warmupSpawns_.clear(), warmupSpawns_.resize(warmupFrames_);
        } else {
            warmupFrames_ = 0;
            warmupSpawns_.clear();
        }

        // Create initial wave sources (record as spawns at frame 0 so they replay at the end)
        std::uniform_real_distribution<float> distX(0.0f, width);
        std::uniform_real_distribution<float> distY(0.0f, height);
        std::uniform_real_distribution<float> distLife(minLifetime_, maxLifetime_);

        sources_.clear();
        for (int i = 0; i < numSources_; i++) {
            float x = distX(rng_);
            float y = distY(rng_);
            int lifetime = (int)(distLife(rng_) * fps_);

            // Create SpawnSpec for initial source to ensure it's recorded/replayed
            SpawnSpec spec;
            spec.x = x;
            spec.y = y;
            spec.duration = lifetime;
            // generate parameters using same distributions as createSource()
            std::uniform_real_distribution<float> distFreq(baseFrequency_ * 0.5f, baseFrequency_ * 2.0f);
            std::normal_distribution<float> distSpeed(baseSpeed_, baseSpeed_ * 0.15f);
            std::uniform_real_distribution<float> distDecay(baseDecay_ * 0.8f, baseDecay_ * 1.2f);
            std::uniform_real_distribution<float> distStrength(0.5f, 1.0f);
            float amplitudeMultiplier = waveDirection_.empty() ? 1.0f : 2.0f;
            std::normal_distribution<float> distAmp(baseAmplitude_ * amplitudeMultiplier, baseAmplitude_ * amplitudeMultiplier * 0.3f);

            spec.frequency = std::max(0.01f, distFreq(rng_));
            spec.amplitude = std::max(0.01f, distAmp(rng_));
            spec.targetStrength = distStrength(rng_);
            spec.speed = std::max(10.0f, distSpeed(rng_));
            spec.decay = distDecay(rng_);
            if (lifetime > 0) {
                spec.rampUpFrames = (int)(lifetime * 0.2f);
                spec.rampDownFrames = (int)(lifetime * 0.25f);
            } else {
                spec.rampUpFrames = fps_ * 2;
                spec.rampDownFrames = fps_ * 2;
            }

            createSourceFromSpec(spec, 0);
            if (warmupFrames_ > 0) warmupSpawns_[0].push_back(spec);
        }
        
        std::cout << "Wave effect initialized with " << numSources_ << " initial sources\n";
        if (useDisplacement_) {
            std::cout << "Using displacement + brightness mode with scale: " << displacementScale_ << " pixels\n";
            std::cout << "Light angle: " << (lightAngle_ * 180.0f / kPi) << " degrees\n";
        } else {
            std::cout << "Using brightness modulation only mode\n";
            std::cout << "Light angle: " << (lightAngle_ * 180.0f / kPi) << " degrees\n";
        }
        // Optional warmup: pre-simulate the system for a number of seconds to stabilize
        if (warmupSeconds_ > 0.0f && warmupFrames_ > 0) {
            std::cout << "Warming up simulation for " << warmupSeconds_ << "s (" << warmupFrames_ << " frames)";
            std::cout << "...\n";

            for (int i = 0; i < warmupFrames_; ++i) {
                update();
            }

            // After warmup, shift source start/end frames backwards so we can reset
            // the logical output frame counter to zero while preserving ages.
            for (auto& ws : sources_) {
                ws.startFrame -= warmupFrames_;
                if (ws.endFrame > 0) ws.endFrame -= warmupFrames_;
            }

            // Reset logical frame count so generation starts at frame 0
            frameCount_ = 0;

            std::cout << "Warmup complete. Resetting output frame count to 0.\n";
        }
        
        return true;
    }

    // Inform the effect how many total frames the output will contain. This
    // lets the effect align behavior (e.g. replaying warmup spawns at the end).
    void setTotalFrames(int totalFrames) override {
        targetTotalFrames_ = totalFrames;
    }
    
    // Bilinear interpolation for smooth sampling
    void samplePixel(const std::vector<uint8_t>& sourceFrame, float x, float y, uint8_t* rgb) {
        // Clamp coordinates
        x = std::clamp(x, 0.0f, (float)(width_ - 1));
        y = std::clamp(y, 0.0f, (float)(height_ - 1));
        
        int x0 = (int)std::floor(x);
        int y0 = (int)std::floor(y);
        int x1 = std::min(x0 + 1, width_ - 1);
        int y1 = std::min(y0 + 1, height_ - 1);
        
        float fx = x - x0;
        float fy = y - y0;
        
        // Get the four surrounding pixels
        int idx00 = (y0 * width_ + x0) * 3;
        int idx10 = (y0 * width_ + x1) * 3;
        int idx01 = (y1 * width_ + x0) * 3;
        int idx11 = (y1 * width_ + x1) * 3;
        
        // Bilinear interpolation for each color channel
        for (int c = 0; c < 3; c++) {
            float v00 = sourceFrame[idx00 + c];
            float v10 = sourceFrame[idx10 + c];
            float v01 = sourceFrame[idx01 + c];
            float v11 = sourceFrame[idx11 + c];
            
            float v0 = v00 * (1.0f - fx) + v10 * fx;
            float v1 = v01 * (1.0f - fx) + v11 * fx;
            float v = v0 * (1.0f - fy) + v1 * fy;
            
            rgb[c] = (uint8_t)std::clamp(v, 0.0f, 255.0f);
        }
    }
    
    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        if (!hasBackground) {
            // Without background, just show the waves as grayscale
            for (int y = 0; y < height_; y++) {
                for (int x = 0; x < width_; x++) {
                    float waveHeight = calculateWaveHeight(x, y);
                    
                    // Map wave height to brightness
                    float brightness = 0.5f + waveHeight;
                    brightness = std::clamp(brightness, 0.0f, 1.0f);
                    brightness *= fadeMultiplier;
                    
                    int idx = (y * width_ + x) * 3;
                    uint8_t value = (uint8_t)(brightness * 255);
                    frame[idx] = value;
                    frame[idx + 1] = value;
                    frame[idx + 2] = value;
                }
            }
        } else if (useDisplacement_) {
            // Displacement mode: distort the image AND apply brightness modulation
            // Make a copy of the current frame to sample from
            std::vector<uint8_t> originalFrame = frame;
            
            for (int y = 0; y < height_; y++) {
                for (int x = 0; x < width_; x++) {
                    float waveHeight = calculateWaveHeight(x, y);
                    
                    // Displacement direction: lower-right for positive waves, upper-left for negative
                    // This creates the "refraction" effect
                    float displacementX = waveHeight * displacementScale_;
                    float displacementY = waveHeight * displacementScale_;
                    
                    // Sample from the displaced position
                    float sourceX = x - displacementX;
                    float sourceY = y - displacementY;
                    
                    int idx = (y * width_ + x) * 3;
                    uint8_t rgb[3];
                    samplePixel(originalFrame, sourceX, sourceY, rgb);
                    
                    // Calculate brightness modulation based on directional lighting
                    float lightMod = calculateDirectionalLight(x, y, waveHeight);
                    float brightnessMod = 1.0f + lightMod;
                    brightnessMod = std::clamp(brightnessMod, 0.5f, 1.5f);
                    brightnessMod *= fadeMultiplier;
                    
                    // Apply both displacement AND brightness modulation
                    for (int c = 0; c < 3; c++) {
                        float modulated = (rgb[c] / 255.0f) * brightnessMod;
                        frame[idx + c] = (uint8_t)(std::clamp(modulated, 0.0f, 1.0f) * 255);
                    }
                }
            }
        } else {
            // Brightness modulation only mode (original behavior)
            for (int y = 0; y < height_; y++) {
                for (int x = 0; x < width_; x++) {
                    float waveHeight = calculateWaveHeight(x, y);
                    float lightMod = calculateDirectionalLight(x, y, waveHeight);
                    
                    // Apply lighting modulation
                    float brightnessMod = 1.0f + lightMod;
                    brightnessMod = std::clamp(brightnessMod, 0.5f, 1.5f);
                    brightnessMod *= fadeMultiplier;
                    
                    int idx = (y * width_ + x) * 3;
                    
                    for (int c = 0; c < 3; c++) {
                        float current = frame[idx + c] / 255.0f;
                        float modulated = current * brightnessMod;
                        frame[idx + c] = (uint8_t)(std::clamp(modulated, 0.0f, 1.0f) * 255);
                    }
                }
            }
        }
    }
    
    void update() override {
        frameCount_++;
        
        // Update existing wave sources
        for (auto& ws : sources_) {
            if (!ws.active) continue;
            
            int age = frameCount_ - ws.startFrame;
            int totalLifetime = (ws.endFrame > 0) ? (ws.endFrame - ws.startFrame) : -1;
            
            // Update strength (ramp up/down)
            if (totalLifetime > 0) {
                if (age < ws.rampUpFrames) {
                    // Ramp up phase
                    float t = (float)age / (float)ws.rampUpFrames;
                    // Smooth ease-in using smoothstep
                    t = t * t * (3.0f - 2.0f * t);
                    ws.currentStrength = t * ws.targetStrength;
                } else if (age > totalLifetime - ws.rampDownFrames) {
                    // Ramp down phase
                    int rampDownAge = age - (totalLifetime - ws.rampDownFrames);
                    float t = (float)rampDownAge / (float)ws.rampDownFrames;
                    // Smooth ease-out using smoothstep
                    t = t * t * (3.0f - 2.0f * t);
                    ws.currentStrength = ws.targetStrength * (1.0f - t);
                } else {
                    // Steady state
                    ws.currentStrength = ws.targetStrength;
                }
            } else {
                // Infinite lifetime - just ramp up
                if (age < ws.rampUpFrames) {
                    float t = (float)age / (float)ws.rampUpFrames;
                    t = t * t * (3.0f - 2.0f * t);
                    ws.currentStrength = t * ws.targetStrength;
                } else {
                    ws.currentStrength = ws.targetStrength;
                }
            }
            
            // Check if source should deactivate
            if (ws.endFrame > 0 && frameCount_ >= ws.endFrame) {
                ws.active = false;
                continue;
            }
            
            // Update phase (wave propagation)
            ws.phase += ws.speed / fps_;
        }
        
        // Remove inactive sources (keep active ones for efficiency)
        sources_.erase(
            std::remove_if(sources_.begin(), sources_.end(),
                [](const WaveSource& ws) { return !ws.active; }),
            sources_.end()
        );
        
        // Randomly spawn new sources
        spawnRandomSource();
    }
};

// Register the effect
REGISTER_EFFECT(WaveEffect, "waves", "Water wave ripples with interference and lighting")
