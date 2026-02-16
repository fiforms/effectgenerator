// laser_effect.cpp
// Radial laser/spotlight effect with animated rays

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

struct Ray {
    float angle;          // Center angle of the ray in radians
    float width;          // Angular width of the ray
    float baseIntensity;  // Brightness baseline 0.0-1.0
    float driftSpeed;     // Angular drift speed (radians/sec)
    float phase;          // Modulation phase
    float pulseSpeed;     // Intensity pulse speed (radians/sec)
};

class LaserEffect : public Effect {
private:
    struct RaySample {
        float intensity;
        float overlap;
    };

    int width_, height_, fps_;
    
    // Focal point
    float focalX_, focalY_;
    float focalVx_, focalVy_;
    bool focalXSet_, focalYSet_;
    float focalMotionX_, focalMotionY_;
    float focalMotionRandom_;
    
    // Ray settings
    int numRays_;
    float baseIntensity_;
    float rayWidth_;
    float rayWidthVar_;
    float morphSpeed_;
    float rotationSpeed_;
    float beamHardness_;
    float highlightBoost_;
    float shadowProtect_;
    float saturationBoost_;
    float pulseDepth_;
    
    // Color
    float colorR_, colorG_, colorB_;
    
    std::vector<Ray> rays_;
    std::mt19937 rng_;
    float globalRotation_;
    float timeSec_;

    bool parseHexColor(const std::string& value, float& outR, float& outG, float& outB) {
        if (value.size() != 7 || value[0] != '#') return false;
        auto hexNibble = [](char c) -> int {
            unsigned char uc = (unsigned char)c;
            if (uc >= '0' && uc <= '9') return (int)(uc - '0');
            uc = (unsigned char)std::tolower(uc);
            if (uc >= 'a' && uc <= 'f') return (int)(10 + (uc - 'a'));
            return -1;
        };
        int hiR = hexNibble(value[1]), loR = hexNibble(value[2]);
        int hiG = hexNibble(value[3]), loG = hexNibble(value[4]);
        int hiB = hexNibble(value[5]), loB = hexNibble(value[6]);
        if (hiR < 0 || loR < 0 || hiG < 0 || loG < 0 || hiB < 0 || loB < 0) return false;
        int r = hiR * 16 + loR;
        int g = hiG * 16 + loG;
        int b = hiB * 16 + loB;
        outR = r / 255.0f;
        outG = g / 255.0f;
        outB = b / 255.0f;
        return true;
    }
    
    void initRays() {
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * kPi);
        std::normal_distribution<float> distWidth(rayWidth_, rayWidthVar_);
        std::uniform_real_distribution<float> distIntensity(baseIntensity_ * 0.5f, baseIntensity_);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * kPi);
        std::uniform_real_distribution<float> distPulseSpeed(0.08f, 0.24f);
        
        rays_.clear();
        for (int i = 0; i < numRays_; i++) {
            Ray r;
            r.angle = distAngle(rng_);
            r.width = std::max(0.01f, distWidth(rng_));
            r.baseIntensity = distIntensity(rng_);
            r.phase = distPhase(rng_);
            r.pulseSpeed = distPulseSpeed(rng_);

            float groupSign = (i % 2 == 0) ? 1.0f : -1.0f;
            float groupScale = 0.3f + 0.7f * ((i % 5) / 4.0f);
            r.driftSpeed = groupSign * morphSpeed_ * groupScale;
            
            rays_.push_back(r);
        }
    }
    
    void updateRays() {
        float dt = 1.0f / std::max(1, fps_);
        timeSec_ += dt;

        for (auto& r : rays_) {
            r.angle += r.driftSpeed * dt;

            float widthMod = 1.0f + 0.15f * std::sin(timeSec_ * 0.35f + r.phase * 0.5f);
            r.width += (std::max(0.01f, rayWidth_ * widthMod) - r.width) * std::clamp(morphSpeed_, 0.0f, 1.0f);

            while (r.angle > 2.0f * kPi) r.angle -= 2.0f * kPi;
            while (r.angle < 0.0f) r.angle += 2.0f * kPi;
        }
        
        // Apply global rotation
        globalRotation_ += rotationSpeed_ * dt;
        while (globalRotation_ > 2.0f * kPi) globalRotation_ -= 2.0f * kPi;
    }
    
    void updateFocalPoint() {
        std::normal_distribution<float> perturbX(0, focalMotionRandom_);
        std::normal_distribution<float> perturbY(0, focalMotionRandom_);
        
        // Add perturbations to velocity
        focalVx_ = focalMotionX_ + perturbX(rng_);
        focalVy_ = focalMotionY_ + perturbY(rng_);
        
        // Update position
        focalX_ += focalVx_;
        focalY_ += focalVy_;

        // Keep focal point in a large roaming envelope, allowing off-screen centers.
        float roamX = width_ * 2.5f;
        float roamY = height_ * 2.5f;
        if (focalX_ < -roamX) focalX_ += 2.0f * roamX;
        if (focalX_ > width_ + roamX) focalX_ -= 2.0f * roamX;
        if (focalY_ < -roamY) focalY_ += 2.0f * roamY;
        if (focalY_ > height_ + roamY) focalY_ -= 2.0f * roamY;
    }
    
    RaySample getRaySample(float angle, float distance) {
        // Normalize angle
        while (angle > 2.0f * kPi) angle -= 2.0f * kPi;
        while (angle < 0.0f) angle += 2.0f * kPi;
        
        // Apply global rotation
        angle += globalRotation_;
        while (angle > 2.0f * kPi) angle -= 2.0f * kPi;
        
        float combinedIntensity = 0.0f;
        float strongestIntensity = 0.0f;
        
        for (const auto& r : rays_) {
            float angleDiff = angle - r.angle;
            // Handle wrap-around
            while (angleDiff > kPi) angleDiff -= 2.0f * kPi;
            while (angleDiff < -kPi) angleDiff += 2.0f * kPi;
            
            float absAngleDiff = std::abs(angleDiff);
            
            // Check if within ray width
            if (absAngleDiff < r.width / 2.0f) {
                // Harder edge profile while preserving a bright ray core.
                float t = absAngleDiff / (r.width / 2.0f);
                float falloff = std::pow(std::max(0.0f, 1.0f - t), std::max(0.1f, beamHardness_));
                
                float distFalloff = 1.0f / (1.0f + distance * 0.0004f);
                float pulse = 1.0f + pulseDepth_ * std::sin(timeSec_ * r.pulseSpeed + r.phase);
                
                float intensity = std::max(0.0f, r.baseIntensity * pulse * falloff * distFalloff);
                combinedIntensity += intensity;
                strongestIntensity = std::max(strongestIntensity, intensity);
            }
        }

        RaySample sample;
        sample.intensity = std::min(1.0f, combinedIntensity);
        sample.overlap = std::clamp(combinedIntensity - strongestIntensity, 0.0f, sample.intensity);
        return sample;
    }
    
public:
    LaserEffect()
        : focalX_(-100), focalY_(-500), focalVx_(0), focalVy_(0), focalXSet_(false), focalYSet_(false),
          focalMotionX_(0.0f), focalMotionY_(0.0f), focalMotionRandom_(0.08f),
          numRays_(12), baseIntensity_(0.5f), rayWidth_(0.5f), rayWidthVar_(0.3f),
          morphSpeed_(0.07f), rotationSpeed_(0.0f),
          beamHardness_(2.8f), highlightBoost_(1.4f), shadowProtect_(0.75f), saturationBoost_(1.4f), pulseDepth_(0.22f),
          colorR_(1.0f), colorG_(1.0f), colorB_(1.0f),
          rng_(std::random_device{}()), globalRotation_(0.0f), timeSec_(0.0f) {}
    
    std::string getName() const override {
        return "laser";
    }
    
    std::string getDescription() const override {
        return "Animated radial rays/laser effect with moving focal point";
    }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--focal-x", "float", -10000000.0, 10000000.0, true, "Initial focal point X (pixels)", "-100"});
        opts.push_back({"--focal-y", "float", -10000000.0, 10000000.0, true, "Initial focal point Y (pixels)", "-500"});
        opts.push_back({"--focal-motion-x", "float", -10000.0, 10000.0, true, "Focal point X velocity (pixels/frame)", "0.0"});
        opts.push_back({"--focal-motion-y", "float", -10000.0, 10000.0, true, "Focal point Y velocity (pixels/frame)", "0.0"});
        opts.push_back({"--focal-random", "float", 0.0, 10000.0, true, "Focal motion randomness (stddev)", "0.08"});
        opts.push_back({"--rays", "int", 1, 10000, true, "Number of rays", "12"});
        opts.push_back({"--intensity", "float", 0.0, 1.0, true, "Base ray intensity 0.0-1.0", "0.5"});
        opts.push_back({"--ray-width", "float", 0.01, 10.0, true, "Ray angular width in radians", "0.5"});
        opts.push_back({"--ray-width-var", "float", 0.0, 10.0, true, "Ray width variance", "0.3"});
        opts.push_back({"--morph-speed", "float", 0.0, 1.0, true, "Ray crossing drift speed 0.0-1.0", "0.07"});
        opts.push_back({"--rotation", "float", -10000.0, 10000.0, true, "Global rotation speed (radians/sec)", "0.0"});
        opts.push_back({"--beam-hardness", "float", 0.1, 20.0, true, "Beam edge hardness", "2.8"});
        opts.push_back({"--highlight-boost", "float", 0.0, 4.0, true, "Boost to highlights (darks protected)", "1.4"});
        opts.push_back({"--shadow-protect", "float", 0.0, 4.0, true, "How strongly dark areas resist brightening", "0.75"});
        opts.push_back({"--saturation-boost", "float", 0.0, 4.0, true, "Color saturation boost in lit areas", "1.4"});
        opts.push_back({"--pulse-depth", "float", 0.0, 2.0, true, "Per-ray breathing pulse depth", "0.22"});
        opts.push_back({"--color", "string.color", 0, 0, false, "Laser color", "white", false,
                        {"white", "yellow", "sodium", "xenon"}});
        return opts;
    }
    
    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        
        if (arg == "--focal-x" && i + 1 < argc) {
            focalX_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--focal-y" && i + 1 < argc) {
            focalY_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--focal-motion-x" && i + 1 < argc) {
            focalMotionX_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--focal-motion-y" && i + 1 < argc) {
            focalMotionY_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--focal-random" && i + 1 < argc) {
            focalMotionRandom_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--rays" && i + 1 < argc) {
            numRays_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--intensity" && i + 1 < argc) {
            baseIntensity_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--ray-width" && i + 1 < argc) {
            rayWidth_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--ray-width-var" && i + 1 < argc) {
            rayWidthVar_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--morph-speed" && i + 1 < argc) {
            morphSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--rotation" && i + 1 < argc) {
            rotationSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--beam-hardness" && i + 1 < argc) {
            beamHardness_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--highlight-boost" && i + 1 < argc) {
            highlightBoost_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--shadow-protect" && i + 1 < argc) {
            shadowProtect_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--saturation-boost" && i + 1 < argc) {
            saturationBoost_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--pulse-depth" && i + 1 < argc) {
            pulseDepth_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--color" && i + 1 < argc) {
            std::string v = argv[++i];
            std::string name = v;
            for (char& c : name) c = (char)std::tolower((unsigned char)c);
            float r = 1.0f, g = 1.0f, b = 1.0f;
            if (name == "white") {
                r = 1.0f; g = 1.0f; b = 1.0f;
            } else if (name == "yellow" || name == "sodium") {
                // Warm sodium-vapor style yellow.
                r = 1.0f; g = 0.84f; b = 0.35f;
            } else if (name == "xenon") {
                // Cool blue-white HID style.
                r = 0.86f; g = 0.93f; b = 1.0f;
            } else if (!parseHexColor(v, r, g, b)) {
                std::cerr << "Invalid --color '" << v
                          << "'. Use white|yellow|sodium|xenon|#RRGGBB.\n";
                return false;
            }
            colorR_ = r;
            colorG_ = g;
            colorB_ = b;
            return true;
        } else if (arg == "--color-r" && i + 1 < argc) {
            // Backward-compatible alias
            colorR_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--color-g" && i + 1 < argc) {
            // Backward-compatible alias
            colorG_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--color-b" && i + 1 < argc) {
            // Backward-compatible alias
            colorB_ = std::atof(argv[++i]);
            return true;
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        
        initRays();
        return true;
    }
    
    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        // Process each pixel
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                // Calculate angle and distance from focal point
                float dx = x - focalX_;
                float dy = y - focalY_;
                float angle = std::atan2(dy, dx);
                float distance = std::sqrt(dx * dx + dy * dy);
                
                // Get ray intensity at this angle
                RaySample sample = getRaySample(angle, distance);
                float intensity = sample.intensity * fadeMultiplier;
                float overlap = sample.overlap * fadeMultiplier;
                
                if (intensity > 0.01f) {
                    int idx = (y * width_ + x) * 3;
                    
                    // Apply colored light additively
                    float currentR = frame[idx] / 255.0f;
                    float currentG = frame[idx + 1] / 255.0f;
                    float currentB = frame[idx + 2] / 255.0f;

                    float luma = 0.2126f * currentR + 0.7152f * currentG + 0.0722f * currentB;
                    float highlightMask = std::pow(std::clamp(luma, 0.0f, 1.0f), std::max(0.0f, shadowProtect_));
                    float lift = intensity * (0.15f + highlightBoost_ * highlightMask);

                    float tintedR = std::clamp(currentR + lift * colorR_, 0.0f, 1.0f);
                    float tintedG = std::clamp(currentG + lift * colorG_, 0.0f, 1.0f);
                    float tintedB = std::clamp(currentB + lift * colorB_, 0.0f, 1.0f);

                    float gray = (tintedR + tintedG + tintedB) / 3.0f;
                    // Keep overlap additive-looking by applying saturation boost only to non-overlap energy.
                    float nonOverlapIntensity = std::max(0.0f, intensity - overlap);
                    float satAmount = std::max(0.0f, (saturationBoost_ - 1.0f) * nonOverlapIntensity);
                    float outR = std::clamp(gray + (tintedR - gray) * (1.0f + satAmount), 0.0f, 1.0f);
                    float outG = std::clamp(gray + (tintedG - gray) * (1.0f + satAmount), 0.0f, 1.0f);
                    float outB = std::clamp(gray + (tintedB - gray) * (1.0f + satAmount), 0.0f, 1.0f);

                    frame[idx] = static_cast<uint8_t>(outR * 255.0f);
                    frame[idx + 1] = static_cast<uint8_t>(outG * 255.0f);
                    frame[idx + 2] = static_cast<uint8_t>(outB * 255.0f);
                }
            }
        }
    }
    
    void update() override {
        updateRays();
        updateFocalPoint();
    }
};

// Register the effect
REGISTER_EFFECT(LaserEffect, "laser", "Animated radial laser/spotlight rays")
