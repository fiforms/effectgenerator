// starfield_effect.cpp
// Starfield effect - gives feeling of flying through space

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>

struct Star {
    float x, y;
    float vx, vy; // velocity per frame
    float baseSize; // starting size
    float size;     // current size
    float brightness; // 0..1
    float r,g,b;    // color
    int shape; // 0=circle, 4=4-line, 6=6-line
};

class StarfieldEffect : public Effect {
private:
    int width_, height_, fps_;
    int numStars_;
    float speed_; // base speed (pixels per frame)
    float speedJitter_;
    float baseSize_;
    float maxSize_;
    float brightnessMax_;
    float centerX_, centerY_;
    int shapeMode_; // 0=circle, 4 or 6

    std::vector<Star> stars_;
    std::mt19937 rng_;

    void respawn(Star &s) {
        // Spawn at a random position. Slightly prefer positions closer to the center
        // by sampling from a 2D normal (centered) most of the time, but keep
        // some uniform samples so stars can still appear near edges occasionally.
        std::uniform_real_distribution<float> mixDist(0.0f, 1.0f);
        float mix = mixDist(rng_);
        // ~65% centered, ~35% uniform (tunable)
        if (mix < 0.65f) {
            float sd = std::min(width_, height_) * 0.18f; // standard deviation for center bias
            std::normal_distribution<float> ndx(centerX_, sd);
            std::normal_distribution<float> ndy(centerY_, sd);
            s.x = ndx(rng_);
            s.y = ndy(rng_);
            // clamp to frame bounds
            s.x = std::clamp(s.x, 0.0f, (float)width_);
            s.y = std::clamp(s.y, 0.0f, (float)height_);
        } else {
            std::uniform_real_distribution<float> posX(0.0f, (float)width_);
            std::uniform_real_distribution<float> posY(0.0f, (float)height_);
            s.x = posX(rng_);
            s.y = posY(rng_);
        }

        // Direction is away from center; if too close to center, pick a random direction
        float dx = s.x - centerX_;
        float dy = s.y - centerY_;
        float len = std::sqrt(dx*dx + dy*dy);
        float dirx, diry;
        if (len < 1e-3f) {
            std::uniform_real_distribution<float> angDist(0.0f, 2.0f * 3.14159265f);
            float a = angDist(rng_);
            dirx = std::cos(a);
            diry = std::sin(a);
        } else {
            dirx = dx / len;
            diry = dy / len;
        }

        // start with a relatively small speed; actual per-frame speed will scale with distance
        std::uniform_real_distribution<float> jitter(0.6f, 1.0f);
        float sp = speed_ * 0.3f * jitter(rng_);
        s.vx = dirx * sp;
        s.vy = diry * sp;

        // small base size when spawned
        std::uniform_real_distribution<float> sizeDist(baseSize_ * 0.4f, baseSize_ * 0.9f);
        s.baseSize = sizeDist(rng_);
        s.size = s.baseSize;

        // start dim; will brighten as distance grows
        std::uniform_real_distribution<float> brightDist(0.04f, 0.15f);
        s.brightness = brightDist(rng_);

        // subtle color variation (slightly bluish / white)
        std::uniform_real_distribution<float> colDist(-0.08f, 0.08f);
        float t = std::clamp(0.9f + colDist(rng_), 0.6f, 1.0f);
        s.r = s.g = s.b = t; // white-ish

        s.shape = shapeMode_;
    }

    void drawCircle(std::vector<uint8_t>& frame, int cx, int cy, float radius, float opacity, float colR, float colG, float colB) {
        float effR = radius + 2.0f;
        int minX = std::max(0, (int)std::floor(cx - effR));
        int maxX = std::min(width_ - 1, (int)std::ceil(cx + effR));
        int minY = std::max(0, (int)std::floor(cy - effR));
        int maxY = std::min(height_ - 1, (int)std::ceil(cy + effR));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;
                float d = std::sqrt(dx*dx + dy*dy);
                float alpha = 0.0f;
                if (d <= radius) alpha = 1.0f - (d / std::max(0.0001f, radius)) * 0.15f;
                else {
                    float t = (d - radius) / 2.0f;
                    alpha = std::max(0.0f, 0.9f * (1.0f - t));
                }
                alpha = std::clamp(alpha * opacity, 0.0f, 1.0f);
                if (alpha > 0.003f) {
                    int idx = (y * width_ + x) * 3;
                    float addR = alpha * colR;
                    float addG = alpha * colG;
                    float addB = alpha * colB;
                    float curR = frame[idx + 0] / 255.0f;
                    float curG = frame[idx + 1] / 255.0f;
                    float curB = frame[idx + 2] / 255.0f;
                    frame[idx + 0] = (uint8_t)(255 * std::min(1.0f, curR + addR));
                    frame[idx + 1] = (uint8_t)(255 * std::min(1.0f, curG + addG));
                    frame[idx + 2] = (uint8_t)(255 * std::min(1.0f, curB + addB));
                }
            }
        }
    }

    void drawStarLines(std::vector<uint8_t>& frame, int cx, int cy, float baseLineWidth, float maxLen, float opacity, float colR, float colG, float colB, int lines) {
        // Optimized star-line rasterization:
        // - For each axis (2 or 3 axes producing 4/6 rays) step along the line and draw small disks
        // - Disk radius and alpha taper with distance from the center, and overall line length scales with opacity (brightness)
        const float PI = 3.14159265f;
        float drawLen = std::min(maxLen, 220.0f + baseLineWidth * 10.0f);
        // shorten length for dimmer stars so lines are short when star is dim
        float effectiveLen = std::max(2.0f, drawLen * std::clamp(opacity, 0.0f, 1.0f));

        std::vector<float> angles;
        if (lines == 4) {
            angles = {0.0f, PI / 2.0f}; // horizontal & vertical
        } else if (lines == 6) {
            // Use 60-degree spacing so the 6-point star is even
            angles = {0.0f, PI / 3.0f, 2.0f * PI / 3.0f}; // three axes (both directions produce 6 rays)
        } else {
            return;
        }

        // Helper: draw a small disk centered at (fx,fy) with given radius and alpha
        // Use a cubic smoothstep falloff for smoother anti-aliasing without expensive exp()
        auto drawDisk = [&](float fx, float fy, float radius, float alpha) {
            if (alpha <= 0.001f || radius <= 0.25f) return;
            int rI = (int)std::ceil(radius + 0.5f);
            int minX = std::max(0, (int)std::floor(fx - rI));
            int maxX = std::min(width_ - 1, (int)std::ceil(fx + rI));
            int minY = std::max(0, (int)std::floor(fy - rI));
            int maxY = std::min(height_ - 1, (int)std::ceil(fy + rI));
            float rr = std::max(0.0001f, radius);
            float invRR = 1.0f / rr;
            for (int yy = minY; yy <= maxY; ++yy) {
                for (int xx = minX; xx <= maxX; ++xx) {
                    float dx = (xx + 0.5f) - fx;
                    float dy = (yy + 0.5f) - fy;
                    float dnorm = (dx*dx + dy*dy) * (invRR * invRR); // squared normalized distance
                    if (dnorm > 1.0f) continue;
                    // normalized distance in [0,1]
                    float d = std::sqrt(dnorm);
                    // cubic smoothstep: s = d*d*(3-2*d)
                    float s = d * d * (3.0f - 2.0f * d);
                    float a = 1.0f - s; // smooth falloff from 1->0
                    a = std::clamp(a * alpha, 0.0f, 1.0f);
                    if (a <= 0.003f) continue;
                    int idx = (yy * width_ + xx) * 3;
                    float curR = frame[idx + 0] / 255.0f;
                    float curG = frame[idx + 1] / 255.0f;
                    float curB = frame[idx + 2] / 255.0f;
                    frame[idx + 0] = (uint8_t)(255 * std::min(1.0f, curR + a * colR));
                    frame[idx + 1] = (uint8_t)(255 * std::min(1.0f, curG + a * colG));
                    frame[idx + 2] = (uint8_t)(255 * std::min(1.0f, curB + a * colB));
                }
            }
        };

        // For each axis, step along the line and place small disks
        for (float a : angles) {
            float ca = std::cos(a), sa = std::sin(a);
            // Choose sampling step size dependent on brightness so bright stars get denser sampling
            float stepSize = std::clamp(0.6f - opacity * 0.35f, 0.25f, 1.0f);
            int steps = (int)std::max(1.0f, std::floor(effectiveLen / stepSize));
            float step = (steps > 0) ? (effectiveLen / steps) : effectiveLen;
            // sample from -len to +len to create a line through the star (subpixel stepping)
            for (float s = -effectiveLen; s <= effectiveLen + 0.001f; s += step) {
                float sAbs = std::fabs(s);
                if (sAbs > effectiveLen) continue;
                // taper factor along the line (1 at center, 0 at ends)
                float along = 1.0f - (sAbs / effectiveLen);
                if (along <= 0.01f) continue;
                // local width and alpha (slightly expanded width for coverage)
                float localWidth = baseLineWidth * along * 1.15f;
                if (localWidth < 0.35f) continue;
                float localAlpha = opacity * along * 0.95f;

                float px = cx + ca * s;
                float py = cy + sa * s;
                drawDisk(px, py, localWidth, localAlpha);
            }
        }
    }

public:
    StarfieldEffect()
        : numStars_(200), speed_(8.0f), speedJitter_(0.35f), baseSize_(1.0f), maxSize_(8.0f), brightnessMax_(1.0f), centerX_(0.0f), centerY_(0.0f), shapeMode_(0), rng_(std::random_device{}()) {}

    std::string getName() const override { return "starfield"; }
    std::string getDescription() const override { return "Starfield: simulate flying through space from a center point"; }

    void printHelp() const override {
        std::cout << "Starfield Effect Options:\n"
                  << "  --stars <int>          Number of stars (default: 200)\n"
                  << "  --speed <float>        Base speed in pixels/frame (default: 8.0)\n"
                  << "  --speed-jitter <f>     Fractional jitter on speed (default: 0.35)\n"
                  << "  --size <float>         Base star size in pixels (default: 1.0)\n"
                  << "  --max-size <float>     Max visual size as star moves outward (default: 8.0)\n"
                  << "  --center-x <float>     Center X in pixels (default: center of frame)\n"
                  << "  --center-y <float>     Center Y in pixels (default: center of frame)\n"
                  << "  --shape <round|4|6>    Star shape: round, 4 (cross), or 6 (three-line Webb-like)\n";
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--stars" && i + 1 < argc) { numStars_ = std::atoi(argv[++i]); return true; }
        if (arg == "--speed" && i + 1 < argc) { speed_ = std::atof(argv[++i]); return true; }
        if (arg == "--speed-jitter" && i + 1 < argc) { speedJitter_ = std::atof(argv[++i]); return true; }
        if (arg == "--size" && i + 1 < argc) { baseSize_ = std::atof(argv[++i]); return true; }
        if (arg == "--max-size" && i + 1 < argc) { maxSize_ = std::atof(argv[++i]); return true; }
        if (arg == "--center-x" && i + 1 < argc) { centerX_ = std::atof(argv[++i]); return true; }
        if (arg == "--center-y" && i + 1 < argc) { centerY_ = std::atof(argv[++i]); return true; }
        if (arg == "--shape" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "round" || v == "circle") shapeMode_ = 0;
            else if (v == "4") shapeMode_ = 4;
            else if (v == "6") shapeMode_ = 6;
            else shapeMode_ = 0;
            return true;
        }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width; height_ = height; fps_ = fps;
        if (centerX_ <= 0.0f) centerX_ = width_ * 0.5f;
        if (centerY_ <= 0.0f) centerY_ = height_ * 0.5f;

        stars_.clear();
        stars_.reserve(numStars_);
        for (int i = 0; i < numStars_; ++i) {
            Star s; respawn(s);
            // optionally disperse starting positions slightly for warm look
            std::uniform_real_distribution<float> jitter(-2.0f, 2.0f);
            s.x += jitter(rng_);
            s.y += jitter(rng_);
            stars_.push_back(s);
        }
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool /*hasBackground*/, float fadeMultiplier) override {
        // Determine maximum possible distance from center to corner for scaling
        float maxLen = std::sqrt((float)width_ * width_ + (float)height_ * height_);

        for (const auto &s : stars_) {
            float dx = s.x - centerX_;
            float dy = s.y - centerY_;
            float dist = std::sqrt(dx*dx + dy*dy);

            // size grows with distance (clamped)
            float t = dist / maxLen;
            float size = std::min(maxSize_, s.baseSize + t * (maxSize_ - s.baseSize) * 1.6f);

            // brightness increases with distance (and with base brightness)
            float brightness = std::clamp(s.brightness + t * 0.9f, 0.0f, brightnessMax_);
            brightness *= fadeMultiplier;

            if (s.shape == 0) {
                drawCircle(frame, (int)std::round(s.x), (int)std::round(s.y), size, brightness, s.r, s.g, s.b);
            } else {
                // draw a small core plus lines
                drawCircle(frame, (int)std::round(s.x), (int)std::round(s.y), size * 0.6f, brightness * 1.1f, s.r, s.g, s.b);
                float baseLineWidth = std::max(0.5f, size * 0.35f);
                float lineLen = std::min(maxLen, 120.0f + dist * 0.8f);
                drawStarLines(frame, (int)std::round(s.x), (int)std::round(s.y), baseLineWidth, lineLen, brightness * 0.9f, s.r, s.g, s.b, s.shape);
            }

        }
    }

    void update() override {
        // Move stars; recompute speed each frame so they accelerate with distance from center
        float maxLen = std::sqrt((float)width_ * width_ + (float)height_ * height_);
        for (auto &s : stars_) {
            // direction from center to star
            float dx = s.x - centerX_;
            float dy = s.y - centerY_;
            float dist = std::sqrt(dx*dx + dy*dy);
            float dirx = 0.0f, diry = 0.0f;
            if (dist > 1e-4f) {
                dirx = dx / dist;
                diry = dy / dist;
            } else {
                // fallback direction
                dirx = 1.0f; diry = 0.0f;
            }

            // speed scales up with distance: slow near center, accelerate toward edges
            float norm = std::clamp(dist / maxLen, 0.0f, 1.0f);
            // non-linear scaling for a stronger acceleration near edges
            float speedScale = 1.0f + 6.0f * (norm * norm);
            float targetSpeed = speed_ * speedScale;

            // apply per-star jitter so motion isn't uniform
            std::uniform_real_distribution<float> starJit(1.0f - speedJitter_, 1.0f + speedJitter_);
            float sp = targetSpeed * starJit(rng_);

            s.vx = dirx * sp;
            s.vy = diry * sp;

            s.x += s.vx;
            s.y += s.vy;

            // update size/brightness with distance
            float sizeT = std::min(1.0f, norm);
            s.size = std::min(maxSize_, s.baseSize + sizeT * (maxSize_ - s.baseSize));
            s.brightness = std::min(brightnessMax_, s.brightness + sizeT * 0.9f);

            // if outside bounds (with margin) respawn
            float margin = 16.0f + maxSize_ * 2.0f;
            if (s.x < -margin || s.x > width_ + margin || s.y < -margin || s.y > height_ + margin) {
                respawn(s);
            }
        }
    }
};

// Register the effect
REGISTER_EFFECT(StarfieldEffect, "starfield", "Starfield: simulate flying through space from a center point")
