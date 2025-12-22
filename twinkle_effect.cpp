// twinkle_effect.cpp
// Twinkling stars effect: static stars that fade in/out randomly.

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>

struct Star {
    float x, y;
    float radius;
    float baseOpacity;
    float phase;
    float freq;
    float amp;
    int type; // 0 = small round, 1 = star-of-bethlehem
    float colorR, colorG, colorB;
    // lifecycle
    float age; // seconds
    float lifeDuration; // seconds until disappear
    float respawnTimer; // seconds until respawn when dead
    bool alive;
};

class TwinkleEffect : public Effect {
private:
    int width_, height_, fps_;
    int numStars_;
    float avgSpeed_; // controls pulsing frequency
    float softness_;
    float smallMaxRadius_;
    float bethlehemWidth_; // branch width
    float bethlehemCenterBoost_;
    int frameCount_;
    std::vector<Star> stars_;
    std::mt19937 rng_;
    int mode_; // 0=small,1=bethlehem,2=mixed
    float mixRatio_; // when mixed, fraction of small stars (0..1)
    float groundThreshold_ = 0.0f; // portion of ground to avoid placing stars on

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

    // Draw a star-of-bethlehem centered at (cx,cy) with orientation 0 (vertical/horizontal cross)
    // Uses an asymptotic decay along branches and gaussian falloff perpendicular to branch.
    void drawStarBethlehem(std::vector<uint8_t>& frame, int cx, int cy, float intensity, float fadeMultiplier, float colR, float colG, float colB) {
        // Precompute bounds depending on branch lengths
        float maxLen = std::max(width_, height_) * 0.5f;
            // We'll limit drawing to a local box for performance; bigger box for longer tails
            int radius = (int)std::ceil(std::min( (float)std::max(width_, height_), 180.0f ));
        int minX = std::max(0, cx - radius);
        int maxX = std::min(width_ - 1, cx + radius);
        int minY = std::max(0, cy - radius);
        int maxY = std::min(height_ - 1, cy + radius * 2);

        // branch directions: up, right, left, down (down is longer)
        const float dirs[4][2] = {{0,-1},{1,0},{-1,0},{0,1}};
        // relative decay per branch; down (index 3) has much smaller decay => much longer tail
        // smaller values make the asymptote 1/(1 + k*t) decay slower -> longer tail
        const float decayBase[4] = {0.08f, 0.08f, 0.08f, 0.02f};
        const float widthBase = bethlehemWidth_;

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float rx = (x + 0.5f) - cx;
                float ry = (y + 0.5f) - cy;

                float acc = 0.0f;

                // center glow
                float dist2 = (rx*rx + ry*ry);
                float center = std::exp(-dist2 / (2.0f * 1.5f * 1.5f));
                acc += center * 0.9f;

                for (int b = 0; b < 4; b++) {
                    float dx = dirs[b][0];
                    float dy = dirs[b][1];
                    // projection along branch
                    float t = rx * dx + ry * dy; // positive in branch direction
                    if (t <= 0.0f) continue;
                    // perpendicular distance
                    float px = rx - t * dx;
                    float py = ry - t * dy;
                    float perp2 = px*px + py*py;

                    // asymptotic decay along branch: ~ 1/(1 + decay * t)
                    float branchDecay = decayBase[b];
                    float along = 5.0f / (1.0f + branchDecay * t * t);
                    // gaussian perpendicular falloff
                    float perp = std::exp(-perp2 / (2.0f * widthBase * widthBase));
                    acc += along * perp * 1.2f;
                }

                // scale by intensity and fade
                float alpha = std::clamp(acc * intensity * fadeMultiplier, 0.0f, 1.0f);
                if (alpha > 0.001f) {
                    int idx = (y * width_ + x) * 3;
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
    TwinkleEffect()
        : numStars_(120), avgSpeed_(0.45f), softness_(1.5f), smallMaxRadius_(2.5f), bethlehemWidth_(2.0f), bethlehemCenterBoost_(0.5f), frameCount_(0), rng_(std::random_device{}()), mode_(2), mixRatio_(0.95f), groundThreshold_(0.0f) {}

    std::string getName() const override { return "twinkle"; }
    std::string getDescription() const override { return "Static twinkling stars: small round stars and Star-of-Bethlehem shapes"; }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--stars", "int", 1, 100000, true, "Number of stars", "120"});
        opts.push_back({"--twinkle-speed", "float", 0.0, 10000.0, true, "Average twinkle speed", "0.45"});
        opts.push_back({"--softness", "float", 0.0, 10000.0, true, "Edge softness/blur", "1.5"});
        opts.push_back({"--type", "string", 0, 0, false, "Star type: small, bethlehem, or mixed", "mixed"});
        opts.push_back({"--mix-ratio", "float", 0.0, 1.0, true, "When mixed, fraction of small stars (0..1)", "0.95"});
        opts.push_back({"--ground-threshold", "float", 0.0, 0.95, true, "Portion of ground to avoid placing stars on (0.0-0.95)", "0.0"});
        return opts;
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--stars" && i + 1 < argc) {
            numStars_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--twinkle-speed" && i + 1 < argc) {
            avgSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--softness" && i + 1 < argc) {
            softness_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--type" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "small") mode_ = 0;
            else if (v == "bethlehem") mode_ = 1;
            else mode_ = 2;
            return true;
        } else if (arg == "--mix-ratio" && i + 1 < argc) {
            mixRatio_ = std::atof(argv[++i]);
            if (mixRatio_ < 0.0f) mixRatio_ = 0.0f;
            if (mixRatio_ > 1.0f) mixRatio_ = 1.0f;
            return true;
        } else if (arg == "--ground-threshold" && i + 1 < argc) {
            groundThreshold_ = std::atof(argv[++i]);
            if (groundThreshold_ < 0.0f) groundThreshold_ = 0.0f;
            if (groundThreshold_ > 0.95f) groundThreshold_ = 0.95f;
            return true;
        }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width; height_ = height; fps_ = fps;

        std::uniform_real_distribution<float> distX(0.0f, (float)width_);
        std::uniform_real_distribution<float> distY(0.0f, (float)height_ * (1.0f - groundThreshold_));
        std::uniform_real_distribution<float> distRadius(0.6f, smallMaxRadius_);
        std::uniform_real_distribution<float> distOpacity(0.2f, 1.0f);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * 3.14159265f);
        // lower frequency range for slower/fuzzier twinkle
        std::uniform_real_distribution<float> distFreq(0.1f, 0.7f);
        std::uniform_real_distribution<float> distAmp(0.2f, 1.0f);
        std::uniform_real_distribution<float> distColor(0.9f, 1.0f);
        std::uniform_real_distribution<float> distLife(3.0f, 10.0f); // life duration in seconds
        std::uniform_real_distribution<float> distRespawn(0.5f, 3.0f);

        stars_.clear();
        stars_.reserve(numStars_);
        for (int i = 0; i < numStars_; ++i) {
            Star s;
            s.x = distX(rng_);
            s.y = distY(rng_);
            s.radius = distRadius(rng_);
            s.baseOpacity = distOpacity(rng_);
            s.phase = distPhase(rng_);
            s.freq = distFreq(rng_) * avgSpeed_;
            s.amp = distAmp(rng_);
            s.colorR = distColor(rng_);
            s.colorG = distColor(rng_);
            s.colorB = distColor(rng_);

            if (mode_ == 0) s.type = 0;
            else if (mode_ == 1) s.type = 1;
            else {
                // mixed mode: small with probability mixRatio_
                std::uniform_real_distribution<float> coin(0.0f, 1.0f);
                s.type = (coin(rng_) < mixRatio_) ? 0 : 1;
            }
            // lifecycle defaults
            s.age = 0.0f;
            s.lifeDuration = distLife(rng_);
            s.respawnTimer = 0.0f;
            s.alive = true;
            stars_.push_back(s);
        }

        frameCount_ = 0;
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        const float TWO_PI = 6.28318530718f;
        float time = (fps_ > 0) ? (frameCount_ / (float)fps_) : 0.0f;

        for (const auto& s : stars_) {
            // skip dead stars (they will be handled in update/respawn)
            if (!s.alive) continue;

            // twinkle via sinusoidal + random amplitude
            float t = time * s.freq * TWO_PI + s.phase;
            float pulse = 0.5f + 0.5f * std::sin(t);
            // base intensity
            float intensity = s.baseOpacity * (0.4f + 0.6f * s.amp * pulse);

            // stars fade in and out smoothly
            float lifeLeftFactor = 1.0f;
            const float fadeOutSec = 2.0f; // seconds to fade out at end of life
            if (s.age < fadeOutSec) {
                // fading in
                lifeLeftFactor = std::clamp(s.age / fadeOutSec, 0.0f, 1.0f);
            } 
            if (s.age >= s.lifeDuration - fadeOutSec) {
                float rem = s.lifeDuration - s.age;
                lifeLeftFactor = std::clamp(rem / fadeOutSec, 0.0f, 1.0f);
            }
            intensity *= lifeLeftFactor;

            if (s.type == 0) {
                // small round star
                float rx = std::max(0.6f, s.radius);
                float ry = rx;
                drawEllipse(frame, (int)s.x, (int)s.y, rx, ry, intensity, fadeMultiplier, s.colorR, s.colorG, s.colorB);
            } else {
                // star-of-bethlehem
                drawStarBethlehem(frame, (int)s.x, (int)s.y, intensity * bethlehemCenterBoost_, fadeMultiplier, s.colorR, s.colorG, s.colorB);
            }
        }
    }

    void update() override {
        float dt = (fps_ > 0) ? (1.0f / (float)fps_) : 0.0333f;
        frameCount_++;

        std::uniform_real_distribution<float> distX(0.0f, (float)width_);
        std::uniform_real_distribution<float> distY(0.0f, (float)height_ * (1.0f - groundThreshold_));
        std::uniform_real_distribution<float> distRadius(0.6f, smallMaxRadius_);
        std::uniform_real_distribution<float> distOpacity(0.2f, 1.0f);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> distFreq(0.1f, 0.7f);
        std::uniform_real_distribution<float> distAmp(0.2f, 1.0f);
        std::uniform_real_distribution<float> distColor(0.9f, 1.0f);
        std::uniform_real_distribution<float> distLife(3.0f, 15.0f);
        std::uniform_real_distribution<float> distRespawn(0.5f, 3.0f);

        for (auto& s : stars_) {
            if (s.alive) {
                s.age += dt;
                if (s.age >= s.lifeDuration) {
                    // star disappears and starts respawn timer
                    s.alive = false;
                    s.respawnTimer = distRespawn(rng_);
                }
            } else {
                s.respawnTimer -= dt;
                if (s.respawnTimer <= 0.0f) {
                    // respawn star at new location with new parameters
                    s.x = distX(rng_);
                    s.y = distY(rng_);
                    s.radius = distRadius(rng_);
                    s.baseOpacity = distOpacity(rng_);
                    s.phase = distPhase(rng_);
                    s.freq = distFreq(rng_) * avgSpeed_;
                    s.amp = distAmp(rng_);
                    s.colorR = distColor(rng_);
                    s.colorG = distColor(rng_);
                    s.colorB = distColor(rng_);
                    s.age = 0.0f;
                    s.lifeDuration = distLife(rng_);
                    s.respawnTimer = 0.0f;
                    s.alive = true;
                }
            }
        }
    }
};

// Register the effect
REGISTER_EFFECT(TwinkleEffect, "twinkle", "Static twinkling stars (small + Star-of-Bethlehem shapes)")
