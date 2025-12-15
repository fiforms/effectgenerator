// wave_effect.cpp
// Water wave ripple effect with interference and directional lighting

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

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
    
    // Random generation
    float sourceSpawnProb_;    // Probability of spawning new source each frame
    float offscreenProb_;      // Probability source is offscreen
    float minLifetime_;        // Minimum wave source lifetime (seconds)
    float maxLifetime_;        // Maximum wave source lifetime (seconds)
    
    std::vector<WaveSource> sources_;
    std::mt19937 rng_;
    int frameCount_;
    
    void createSource(float x, float y, int startFrame, int duration) {
        std::uniform_real_distribution<float> distFreq(baseFrequency_ * 0.5f, baseFrequency_ * 2.0f);
        std::normal_distribution<float> distAmp(baseAmplitude_, baseAmplitude_ * 0.3f);
        std::normal_distribution<float> distSpeed(baseSpeed_, baseSpeed_ * 0.15f);
        std::uniform_real_distribution<float> distDecay(baseDecay_ * 0.8f, baseDecay_ * 1.2f);
        std::uniform_real_distribution<float> distStrength(0.5f, 1.0f);
        
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
    
    void spawnRandomSource() {
        std::uniform_real_distribution<float> prob(0.0f, 1.0f);
        std::uniform_real_distribution<float> distX(-width_ * 0.2f, width_ * 1.2f);
        std::uniform_real_distribution<float> distY(-height_ * 0.2f, height_ * 1.2f);
        std::uniform_real_distribution<float> distLife(minLifetime_, maxLifetime_);
        
        if (prob(rng_) < sourceSpawnProb_) {
            float x, y;
            
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
            
            int lifetime = (int)(distLife(rng_) * fps_);
            createSource(x, y, frameCount_, lifetime);
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
        : numSources_(3), baseAmplitude_(0.15f), baseFrequency_(0.02f),
          baseSpeed_(10.0f), baseDecay_(0.001f),
          lightAngle_(-M_PI / 4.0f), lightIntensity_(0.3f), waveInterference_(1.0f),
          sourceSpawnProb_(0.02f), offscreenProb_(0.3f),
          minLifetime_(2.0f), maxLifetime_(8.0f),
          rng_(std::random_device{}()), frameCount_(0) {}
    
    std::string getName() const override {
        return "waves";
    }
    
    std::string getDescription() const override {
        return "Water wave ripples with interference and directional lighting";
    }
    
    void printHelp() const override {
        std::cout << "Wave Effect Options:\n"
                  << "  --sources <int>         Initial number of wave sources (default: 3)\n"
                  << "  --amplitude <float>     Base wave amplitude (default: 0.15)\n"
                  << "  --frequency <float>     Base wave frequency (default: 0.02)\n"
                  << "  --speed <float>         Wave propagation speed (default: 10.0)\n"
                  << "  --decay <float>         Wave decay with distance (default: 0.001)\n"
                  << "  --light-angle <float>   Light direction in degrees (default: -45, top-left)\n"
                  << "  --light-intensity <float> Lighting effect strength (default: 0.3)\n"
                  << "  --interference <float>  Wave interference amount 0.0-1.0 (default: 1.0)\n"
                  << "  --spawn-prob <float>    Random source spawn probability (default: 0.02)\n"
                  << "  --offscreen-prob <float> Probability source is offscreen (default: 0.3)\n"
                  << "  --min-lifetime <float>  Min source lifetime in seconds (default: 2.0)\n"
                  << "  --max-lifetime <float>  Max source lifetime in seconds (default: 8.0)\n";
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
            lightAngle_ = degrees * M_PI / 180.0f;
            return true;
        } else if (arg == "--light-intensity" && i + 1 < argc) {
            lightIntensity_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--interference" && i + 1 < argc) {
            waveInterference_ = std::atof(argv[++i]);
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
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        frameCount_ = 0;
        
        // Create initial wave sources
        std::uniform_real_distribution<float> distX(0.0f, width);
        std::uniform_real_distribution<float> distY(0.0f, height);
        std::uniform_real_distribution<float> distLife(minLifetime_, maxLifetime_);
        
        sources_.clear();
        for (int i = 0; i < numSources_; i++) {
            float x = distX(rng_);
            float y = distY(rng_);
            int lifetime = (int)(distLife(rng_) * fps);
            createSource(x, y, 0, lifetime);
        }
        
        std::cout << "Wave effect initialized with " << numSources_ << " initial sources\n";
        std::cout << "Light angle: " << (lightAngle_ * 180.0f / M_PI) << " degrees\n";
        
        return true;
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
        } else {
            // With background, modulate brightness based on waves and lighting
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
