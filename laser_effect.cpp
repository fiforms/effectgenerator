// laser_effect.cpp
// Radial laser/spotlight effect with animated rays

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

struct Ray {
    float angle;          // Center angle of the ray in radians
    float width;          // Angular width of the ray
    float intensity;      // Brightness 0.0-1.0
    float targetAngle;    // Target angle for morphing
    float targetWidth;    // Target width for morphing
    float targetIntensity;// Target intensity for morphing
    float morphSpeed;     // How fast it morphs
};

class LaserEffect : public Effect {
private:
    int width_, height_, fps_;
    
    // Focal point
    float focalX_, focalY_;
    float focalVx_, focalVy_;
    float focalMotionX_, focalMotionY_;
    float focalMotionRandom_;
    
    // Ray settings
    int numRays_;
    float baseIntensity_;
    float rayWidth_;
    float rayWidthVar_;
    float morphSpeed_;
    float rotationSpeed_;
    
    // Color
    float colorR_, colorG_, colorB_;
    
    std::vector<Ray> rays_;
    std::mt19937 rng_;
    float globalRotation_;
    
    void initRays() {
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * M_PI);
        std::normal_distribution<float> distWidth(rayWidth_, rayWidthVar_);
        std::uniform_real_distribution<float> distIntensity(baseIntensity_ * 0.5f, baseIntensity_);
        
        rays_.clear();
        for (int i = 0; i < numRays_; i++) {
            Ray r;
            r.angle = distAngle(rng_);
            r.width = std::max(0.01f, distWidth(rng_));
            r.intensity = distIntensity(rng_);
            
            // Set initial targets
            r.targetAngle = distAngle(rng_);
            r.targetWidth = std::max(0.01f, distWidth(rng_));
            r.targetIntensity = distIntensity(rng_);
            r.morphSpeed = morphSpeed_;
            
            rays_.push_back(r);
        }
    }
    
    void updateRays() {
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * M_PI);
        std::normal_distribution<float> distWidth(rayWidth_, rayWidthVar_);
        std::uniform_real_distribution<float> distIntensity(baseIntensity_ * 0.5f, baseIntensity_);
        
        for (auto& r : rays_) {
            // Morph towards target
            float angleError = r.targetAngle - r.angle;
            // Handle wrap-around
            while (angleError > M_PI) angleError -= 2.0f * M_PI;
            while (angleError < -M_PI) angleError += 2.0f * M_PI;
            
            r.angle += angleError * r.morphSpeed;
            r.width += (r.targetWidth - r.width) * r.morphSpeed;
            r.intensity += (r.targetIntensity - r.intensity) * r.morphSpeed;
            
            // Normalize angle
            while (r.angle > 2.0f * M_PI) r.angle -= 2.0f * M_PI;
            while (r.angle < 0.0f) r.angle += 2.0f * M_PI;
            
            // Check if close to target, set new target
            if (std::abs(angleError) < 0.1f && 
                std::abs(r.width - r.targetWidth) < 0.01f &&
                std::abs(r.intensity - r.targetIntensity) < 0.05f) {
                r.targetAngle = distAngle(rng_);
                r.targetWidth = std::max(0.01f, distWidth(rng_));
                r.targetIntensity = distIntensity(rng_);
            }
        }
        
        // Apply global rotation
        globalRotation_ += rotationSpeed_ / fps_;
        while (globalRotation_ > 2.0f * M_PI) globalRotation_ -= 2.0f * M_PI;
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
        
        // Bounce off edges with some damping
        if (focalX_ < 0 || focalX_ >= width_) {
            focalVx_ = -focalVx_ * 0.8f;
            focalX_ = std::clamp(focalX_, 0.0f, (float)width_ - 1);
        }
        if (focalY_ < 0 || focalY_ >= height_) {
            focalVy_ = -focalVy_ * 0.8f;
            focalY_ = std::clamp(focalY_, 0.0f, (float)height_ - 1);
        }
    }
    
    float getRayIntensity(float angle, float distance) {
        // Normalize angle
        while (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
        while (angle < 0.0f) angle += 2.0f * M_PI;
        
        // Apply global rotation
        angle += globalRotation_;
        while (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
        
        float maxIntensity = 0.0f;
        
        for (const auto& r : rays_) {
            float angleDiff = angle - r.angle;
            // Handle wrap-around
            while (angleDiff > M_PI) angleDiff -= 2.0f * M_PI;
            while (angleDiff < -M_PI) angleDiff += 2.0f * M_PI;
            
            float absAngleDiff = std::abs(angleDiff);
            
            // Check if within ray width
            if (absAngleDiff < r.width / 2.0f) {
                // Smooth falloff from center of ray
                float t = absAngleDiff / (r.width / 2.0f);
                float falloff = 1.0f - t * t; // Quadratic falloff
                
                // Distance falloff (optional, makes rays fade with distance)
                float distFalloff = 1.0f / (1.0f + distance * 0.0005f);
                
                float intensity = r.intensity * falloff * distFalloff;
                maxIntensity = std::max(maxIntensity, intensity);
            }
        }
        
        return maxIntensity;
    }
    
public:
    LaserEffect()
        : focalX_(0), focalY_(0), focalVx_(0), focalVy_(0),
          focalMotionX_(0.0f), focalMotionY_(0.0f), focalMotionRandom_(2.0f),
          numRays_(8), baseIntensity_(0.5f), rayWidth_(0.3f), rayWidthVar_(0.1f),
          morphSpeed_(0.05f), rotationSpeed_(0.0f),
          colorR_(1.0f), colorG_(1.0f), colorB_(1.0f),
          rng_(std::random_device{}()), globalRotation_(0.0f) {}
    
    std::string getName() const override {
        return "laser";
    }
    
    std::string getDescription() const override {
        return "Animated radial rays/laser effect with moving focal point";
    }
    
    void printHelp() const override {
        std::cout << "This function is going away\n";
    }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--focal-x", "float", -10000000.0, 10000000.0, true, "Initial focal point X (pixels, default: center)", ""});
        opts.push_back({"--focal-y", "float", -10000000.0, 10000000.0, true, "Initial focal point Y (pixels, default: center)", ""});
        opts.push_back({"--focal-motion-x", "float", -10000.0, 10000.0, true, "Focal point X velocity (pixels/frame)", "0.0"});
        opts.push_back({"--focal-motion-y", "float", -10000.0, 10000.0, true, "Focal point Y velocity (pixels/frame)", "0.0"});
        opts.push_back({"--focal-random", "float", 0.0, 10000.0, true, "Focal motion randomness (stddev)", "2.0"});
        opts.push_back({"--rays", "int", 1, 10000, true, "Number of rays", "8"});
        opts.push_back({"--intensity", "float", 0.0, 1.0, true, "Base ray intensity 0.0-1.0", "0.5"});
        opts.push_back({"--ray-width", "float", 0.01, 10.0, true, "Ray angular width in radians", "0.3"});
        opts.push_back({"--ray-width-var", "float", 0.0, 10.0, true, "Ray width variance", "0.1"});
        opts.push_back({"--morph-speed", "float", 0.0, 1.0, true, "Ray morphing speed 0.0-1.0", "0.05"});
        opts.push_back({"--rotation", "float", -10000.0, 10000.0, true, "Global rotation speed (radians/sec)", "0.0"});
        opts.push_back({"--color-r", "float", 0.0, 1.0, true, "Red component 0.0-1.0", "1.0"});
        opts.push_back({"--color-g", "float", 0.0, 1.0, true, "Green component 0.0-1.0", "1.0"});
        opts.push_back({"--color-b", "float", 0.0, 1.0, true, "Blue component 0.0-1.0", "1.0"});
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
        } else if (arg == "--color-r" && i + 1 < argc) {
            colorR_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--color-g" && i + 1 < argc) {
            colorG_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--color-b" && i + 1 < argc) {
            colorB_ = std::atof(argv[++i]);
            return true;
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        
        // Default focal point to center if not set
        if (focalX_ == 0.0f) focalX_ = width / 2.0f;
        if (focalY_ == 0.0f) focalY_ = height / 2.0f;
        
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
                float intensity = getRayIntensity(angle, distance);
                intensity *= fadeMultiplier;
                
                if (intensity > 0.01f) {
                    int idx = (y * width_ + x) * 3;
                    
                    // Apply colored light additively
                    float currentR = frame[idx] / 255.0f;
                    float currentG = frame[idx + 1] / 255.0f;
                    float currentB = frame[idx + 2] / 255.0f;
                    
                    float addR = intensity * colorR_;
                    float addG = intensity * colorG_;
                    float addB = intensity * colorB_;
                    
                    frame[idx] = (uint8_t)(std::min(1.0f, currentR + addR) * 255);
                    frame[idx + 1] = (uint8_t)(std::min(1.0f, currentG + addG) * 255);
                    frame[idx + 2] = (uint8_t)(std::min(1.0f, currentB + addB) * 255);
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
