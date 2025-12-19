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
    // Pulsing parameters
    float brightnessPhase;
    float brightnessFreq;
    float brightnessAmp;

    float sizePhase;
    float sizeFreq;
    float sizeAmpX;
    float sizeAmpY;
    bool spinHorizontal;
    bool spinEnabled;
    // per-flake color
    float colorR;
    float colorG;
    float colorB;
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
    float brightnessSpeed_;
    // color controls
    float avgHue_; // 0..1
    float saturation_; // 0..1
    float hueRange_; // 0..1
    int frameCount_;
    bool spin_;
    float spinFraction_;
    float spinMinAspect_;
    int spinAxis_; // 0=random, 1=horizontal, 2=vertical
    
    std::vector<Snowflake> flakes_;
    std::mt19937 rng_;
    
    void hsvToRgb(float h, float s, float v, float &r, float &g, float &b) {
        if (s <= 0.0f) {
            r = g = b = v;
            return;
        }
        float hh = h * 6.0f;
        if (hh >= 6.0f) hh = 0.0f;
        int i = (int)hh;
        float ff = hh - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * ff);
        float t = v * (1.0f - s * (1.0f - ff));
        switch (i) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            case 5: default: r = v; g = p; b = q; break;
        }
    }
    
    void resetFlake(Snowflake& f) {
        std::uniform_real_distribution<float> distX(0, width_);
        std::normal_distribution<float> distVx(avgMotionX_, motionRandomness_);
        std::normal_distribution<float> distVy(avgMotionY_, motionRandomness_);
        std::normal_distribution<float> distSize(avgSize_, sizeVariance_);
        std::uniform_real_distribution<float> distOpacity(0.3f, maxBrightness_);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> distBrightFreq(0.2f, 1.2f);
        std::uniform_real_distribution<float> distBrightAmp(0.05f, 0.6f);
        std::uniform_real_distribution<float> distSizeFreq(0.1f, 0.8f);
        std::uniform_real_distribution<float> distSizeAmp(0.02f, 0.6f);
        
        f.radius = std::max(1.0f, distSize(rng_));
        f.y = -(f.radius + softness_ + 2);
        f.x = distX(rng_);
        f.baseVx = distVx(rng_);
        f.baseVy = distVy(rng_);
        f.vx = f.baseVx;
        f.vy = f.baseVy;
        f.opacity = distOpacity(rng_);
        // brightness/pulse params
        f.brightnessPhase = distPhase(rng_);
        // scale per-flake frequency by the user-controlled average speed
        f.brightnessFreq = distBrightFreq(rng_) * brightnessSpeed_;
        // if brightnessSpeed_ is zero (or negative), disable pulsing by zeroing amplitude
        if (brightnessSpeed_ <= 0.0f) {
            f.brightnessAmp = 0.0f;
        } else {
            f.brightnessAmp = distBrightAmp(rng_);
        }

        // size/shape pulse params (separate from brightness)
        f.sizePhase = distPhase(rng_);
        f.sizeFreq = distSizeFreq(rng_);
        // size/shape pulse params
        // Decide whether this flake will spin (only a portion do)
        if (spin_ && (std::uniform_real_distribution<float>(0.0f,1.0f)(rng_) < spinFraction_)) {
            f.spinEnabled = true;
            // Decide axis according to spinAxis_ setting
            if (spinAxis_ == 1) {
                f.spinHorizontal = true;
            } else if (spinAxis_ == 2) {
                f.spinHorizontal = false;
            } else {
                f.spinHorizontal = (std::uniform_real_distribution<float>(0.0f,1.0f)(rng_) < 0.5f);
            }
            // store small amplitude values in case non-spin fallback needed
            f.sizeAmpX = distSizeAmp(rng_) * 0.3f;
            f.sizeAmpY = distSizeAmp(rng_) * 0.3f;
        } else {
            f.spinEnabled = false;
            float v = distSizeAmp(rng_) * 0.25f; // small uniform wobble when spin disabled
            f.sizeAmpX = v;
            f.sizeAmpY = v;
        }

        // determine per-flake color based on avgHue_, saturation_ and hueRange_
        float hue = avgHue_;
        if (hueRange_ > 0.0f) {
            float halfRange = hueRange_ * 0.5f;
            std::uniform_real_distribution<float> distHueOffset(-halfRange, halfRange);
            hue = hue + distHueOffset(rng_);
            // wrap hue into [0,1]
            if (hue < 0.0f) hue += 1.0f;
            if (hue >= 1.0f) hue -= 1.0f;
        }
        float sat = std::clamp(saturation_, 0.0f, 1.0f);
        float r, g, b;
        hsvToRgb(hue, sat, 1.0f, r, g, b);
        f.colorR = r;
        f.colorG = g;
        f.colorB = b;
    }
    
    void drawEllipse(std::vector<uint8_t>& frame, int cx, int cy, float rx, float ry, float opacity, float fadeMultiplier, float colR, float colG, float colB) {
        float effectiveRx = rx + softness_;
        float effectiveRy = ry + softness_;

        int minX = std::max(0, (int)(cx - effectiveRx - 2));
        int maxX = std::min(width_ - 1, (int)(cx + effectiveRx + 2));
        int minY = std::max(0, (int)(cy - effectiveRy - 2));
        int maxY = std::min(height_ - 1, (int)(cy + effectiveRy + 2));

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;

                // normalize by radii to compute ellipse distance
                float nx = dx / std::max(0.0001f, rx);
                float ny = dy / std::max(0.0001f, ry);
                float ellDist = std::sqrt(nx * nx + ny * ny);

                float alpha = 0.0f;

                if (ellDist < 1.0f + (softness_ / std::max(rx, ry))) {
                    if (ellDist <= 1.0f) {
                        float t = ellDist;
                        alpha = 1.0f - t * 0.12f;
                    } else {
                        float t = (ellDist - 1.0f) * (std::max(rx, ry) / softness_);
                        t = t * t * (3.0f - 2.0f * t);
                        alpha = 0.9f * (1.0f - t);
                    }
                }

                alpha = std::clamp(alpha * opacity * fadeMultiplier, 0.0f, 1.0f);

                if (alpha > 0.005f) {
                    int idx = (y * width_ + x) * 3;
                    // add colored contribution scaled by alpha
                    float addR = alpha * colR;
                    float addG = alpha * colG;
                    float addB = alpha * colB;
                    float currentR = frame[idx + 0] / 255.0f;
                    float currentG = frame[idx + 1] / 255.0f;
                    float currentB = frame[idx + 2] / 255.0f;
                    float resR = std::min(1.0f, currentR + addR);
                    float resG = std::min(1.0f, currentG + addG);
                    float resB = std::min(1.0f, currentB + addB);
                    frame[idx + 0] = (uint8_t)(255 * resR);
                    frame[idx + 1] = (uint8_t)(255 * resG);
                    frame[idx + 2] = (uint8_t)(255 * resB);
                }
            }
        }
    }
    
public:
    SnowflakeEffect()
        : numFlakes_(150), avgSize_(3.0f), sizeVariance_(1.5f),
            avgMotionX_(0.5f), avgMotionY_(2.0f), motionRandomness_(1.0f),
            softness_(2.0f), maxBrightness_(1.0f), brightnessSpeed_(1.0f), avgHue_(0.0f), saturation_(0.0f), hueRange_(0.0f), frameCount_(0), spin_(false), spinFraction_(0.35f), spinMinAspect_(0.1f), spinAxis_(0), rng_(std::random_device{}()) {}
        
    
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
                  << "  --brightness <float>   Max brightness 0.0-1.0 (default: 1.0)\n"
                  << "  --pulse <float>        Average speed of brightness pulsing (default: 1.0). Set to 0 to disable pulsing.\n"
                  << "  --hue <float>          Average hue 0.0-1.0 (default: 0.0 - only matters when saturation>0)\n"
                  << "  --saturation <float>   Saturation 0.0-1.0 (default: 0.0 = white)\n"
                  << "  --hue-range <float>    Hue range 0.0-1.0 (0 = same hue, 1 = full range)\n"
                  << "  --spin                 Enable spin-like aspect morphing (gives 3D spin illusion)\n"
                  << "  --spin-axis <h|v|random>  Axis for spin: h=horizontal, v=vertical, random=per-flake random (default: random)\n";
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
        } else if (arg == "--pulse" && i + 1 < argc) {
            brightnessSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--hue" && i + 1 < argc) {
            avgHue_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--saturation" && i + 1 < argc) {
            saturation_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--hue-range" && i + 1 < argc) {
            hueRange_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--spin") {
            spin_ = true;
            return true;
        } else if (arg == "--spin-axis" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "h" || v == "horizontal") spinAxis_ = 1;
            else if (v == "v" || v == "vertical") spinAxis_ = 2;
            else spinAxis_ = 0;
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
            // use resetFlake to set sane defaults and pulsing params,
            // then place the flake randomly within the frame vertically
            resetFlake(f);
            f.x = distX(rng_);
            f.y = distY(rng_);
            flakes_.push_back(f);
        }
        frameCount_ = 0;
        
        return true;
    }
    
    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        const float TWO_PI = 6.28318530718f;
        float time = (fps_ > 0) ? (frameCount_ / (float)fps_) : 0.0f;

        for (const auto& f : flakes_) {
            // brightness pulse (per-flake random phase/frequency/amplitude)
            float tBright = time * f.brightnessFreq * TWO_PI + f.brightnessPhase;
            float brightFactor = 1.0f + f.brightnessAmp * std::sin(tBright);
            float opacity = std::clamp(f.opacity * brightFactor, 0.0f, 1.0f);

            // size/shape pulse (separate time/phase)
            float tSize = time * f.sizeFreq * TWO_PI + f.sizePhase;
            float rx, ry;
            if (f.spinEnabled) {
                // Use a signed waveform that crosses negative territory to simulate
                // a flip/rotation. Start with sin(t) in [-1,1], apply a signed
                // square-root to make the waveform move *faster* through zero
                // (so the narrow state is brief), then drive aspect magnitude
                // from `spinMinAspect_`..1.0. When the signed value is negative
                // we swap major/minor to visually flip the orientation.
                float s = std::sin(tSize);
                // signed sqrt: preserve sign, amplify small magnitudes away from 0
                float v = (s >= 0.0f) ? std::sqrt(s) : -std::sqrt(-s);
                float mag = std::abs(v);
                // magnitude maps from spinMinAspect_ (narrow) to 1.0 (full)
                float absAspect = spinMinAspect_ + (1.0f - spinMinAspect_) * mag;
                float major = f.radius;
                // allow the minor axis to get very small (so flakes can disappear briefly)
                float minor = std::max(0.05f, f.radius * absAspect);
                // Keep axis orientation constant regardless of the signed value.
                // We still allow the value to go negative (so the waveform crosses
                // zero quickly), but do not swap major/minor — this preserves the
                // chosen spin axis and prevents flipping between horizontal and
                // vertical shapes.
                if (f.spinHorizontal) {
                    rx = major;
                    ry = minor;
                } else {
                    rx = minor;
                    ry = major;
                }
            } else {
                rx = std::max(0.5f, f.radius * (1.0f + f.sizeAmpX));
                ry = std::max(0.5f, f.radius * (1.0f + f.sizeAmpY));
            }

            drawEllipse(frame, (int)f.x, (int)f.y, rx, ry, opacity, fadeMultiplier, f.colorR, f.colorG, f.colorB);
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
        // advance frame counter for pulse calculations
        frameCount_++;
    }
};

// Register the effect
REGISTER_EFFECT(SnowflakeEffect, "snowflake", "Realistic falling snowflakes")
