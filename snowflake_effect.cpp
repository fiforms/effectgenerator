// snowflake_effect.cpp
// Snowflake effect implementation

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>

struct Snowflake {
    float x, y;
    float vx, vy;
    float radius;
    float opacity;
    float baseVx, baseVy;
};

class SnowflakeEffect : public Effect {
private:
    int width_, height_, fps_;
    int numFlakes_;
    float avgSize_, sizeVariance_;
    float avgMotionX_, avgMotionY_;
    float motionRandomness_;
    float softness_;
    float maxBrightness_;
    
    std::vector<Snowflake> flakes_;
    std::mt19937 rng_;
    
    void resetFlake(Snowflake& f) {
        std::uniform_real_distribution<float> distX(0, width_);
        std::normal_distribution<float> distVx(avgMotionX_, motionRandomness_);
        std::normal_distribution<float> distVy(avgMotionY_, motionRandomness_);
        std::normal_distribution<float> distSize(avgSize_, sizeVariance_);
        std::uniform_real_distribution<float> distOpacity(0.3f, maxBrightness_);
        
        f.radius = std::max(1.0f, distSize(rng_));
        f.y = -(f.radius + softness_ + 2);
        f.x = distX(rng_);
        f.baseVx = distVx(rng_);
        f.baseVy = distVy(rng_);
        f.vx = f.baseVx;
        f.vy = f.baseVy;
        f.opacity = distOpacity(rng_);
    }
    
    void drawCircle(std::vector<uint8_t>& frame, int cx, int cy, float radius, float opacity, float fadeMultiplier) {
        float effectiveRadius = radius + softness_;
        
        int minX = std::max(0, (int)(cx - effectiveRadius - 2));
        int maxX = std::min(width_ - 1, (int)(cx + effectiveRadius + 2));
        int minY = std::max(0, (int)(cy - effectiveRadius - 2));
        int maxY = std::min(height_ - 1, (int)(cy + effectiveRadius + 2));
        
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                
                float alpha = 0.0f;
                
                if (dist < effectiveRadius) {
                    if (dist <= radius) {
                        float t = dist / radius;
                        alpha = 1.0f - t * 0.1f;
                    } else {
                        float t = (dist - radius) / softness_;
                        t = t * t * (3.0f - 2.0f * t);
                        alpha = 0.9f * (1.0f - t);
                    }
                }
                
                alpha = std::clamp(alpha * opacity * fadeMultiplier, 0.0f, 1.0f);
                
                if (alpha > 0.005f) {
                    int idx = (y * width_ + x) * 3;
                    for (int c = 0; c < 3; c++) {
                        float current = frame[idx + c] / 255.0f;
                        float result = std::min(1.0f, current + alpha);
                        frame[idx + c] = (uint8_t)(255 * result);
                    }
                }
            }
        }
    }
    
public:
    SnowflakeEffect()
        : numFlakes_(150), avgSize_(3.0f), sizeVariance_(1.5f),
          avgMotionX_(0.5f), avgMotionY_(2.0f), motionRandomness_(1.0f),
          softness_(2.0f), maxBrightness_(1.0f), rng_(std::random_device{}()) {}
    
    std::string getName() const override {
        return "snowflake";
    }
    
    std::string getDescription() const override {
        return "Realistic falling snowflakes with soft edges and natural motion";
    }
    
    void printHelp() const override {
        std::cout << "Snowflake Effect Options:\n"
                  << "  --flakes <int>         Number of snowflakes (default: 150)\n"
                  << "  --size <float>         Average snowflake size (default: 3.0)\n"
                  << "  --size-var <float>     Size variance (default: 1.5)\n"
                  << "  --motion-x <float>     Average X motion per frame (default: 0.5)\n"
                  << "  --motion-y <float>     Average Y motion per frame (default: 2.0)\n"
                  << "  --randomness <float>   Motion randomness (default: 1.0)\n"
                  << "  --softness <float>     Edge softness/blur (default: 2.0)\n"
                  << "  --brightness <float>   Max brightness 0.0-1.0 (default: 1.0)\n";
    }
    
    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        
        if (arg == "--flakes" && i + 1 < argc) {
            numFlakes_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--size" && i + 1 < argc) {
            avgSize_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--size-var" && i + 1 < argc) {
            sizeVariance_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--motion-x" && i + 1 < argc) {
            avgMotionX_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--motion-y" && i + 1 < argc) {
            avgMotionY_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--randomness" && i + 1 < argc) {
            motionRandomness_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--softness" && i + 1 < argc) {
            softness_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--brightness" && i + 1 < argc) {
            maxBrightness_ = std::atof(argv[++i]);
            return true;
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        
        std::uniform_real_distribution<float> distX(0, width);
        std::uniform_real_distribution<float> distY(0, height);
        std::normal_distribution<float> distSize(avgSize_, sizeVariance_);
        std::normal_distribution<float> distVx(avgMotionX_, motionRandomness_);
        std::normal_distribution<float> distVy(avgMotionY_, motionRandomness_);
        std::uniform_real_distribution<float> distOpacity(0.3f, maxBrightness_);
        
        flakes_.clear();
        for (int i = 0; i < numFlakes_; i++) {
            Snowflake f;
            f.x = distX(rng_);
            f.y = distY(rng_);
            f.baseVx = distVx(rng_);
            f.baseVy = distVy(rng_);
            f.vx = f.baseVx;
            f.vy = f.baseVy;
            f.radius = std::max(1.0f, distSize(rng_));
            f.opacity = distOpacity(rng_);
            flakes_.push_back(f);
        }
        
        return true;
    }
    
    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        for (const auto& f : flakes_) {
            drawCircle(frame, (int)f.x, (int)f.y, f.radius, f.opacity, fadeMultiplier);
        }
    }
    
    void update() override {
        std::normal_distribution<float> perturbVx(0, motionRandomness_ * 0.1f);
        std::normal_distribution<float> perturbVy(0, motionRandomness_ * 0.1f);
        
        for (auto& f : flakes_) {
            f.vx = f.baseVx + perturbVx(rng_);
            f.vy = f.baseVy + perturbVy(rng_);
            
            f.x += f.vx;
            f.y += f.vy;
            
            if (f.y > height_ + f.radius + softness_) {
                resetFlake(f);
            }
            if (f.x < -(f.radius + softness_)) f.x = width_ + f.radius + softness_;
            if (f.x > width_ + f.radius + softness_) f.x = -(f.radius + softness_);
        }
    }
};

// Register the effect
REGISTER_EFFECT(SnowflakeEffect, "snowflake", "Realistic falling snowflakes")
