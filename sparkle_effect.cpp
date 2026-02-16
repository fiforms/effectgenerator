// sparkle_effect.cpp
// Sparkle effect: detect edges in background and place moving sparkles.

#include "effect_generator.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct Sparkle {
    float x, y;
    float size;
    float baseIntensity;
    float intensity;
    float targetIntensity;
    float phase;
    float r, g, b;
    bool isStar;
};

struct Hotspot {
    int x, y;
    float score;
};

class SparkleEffect : public Effect {
private:
    int width_, height_, fps_;
    int frameCount_;
    int numSparkles_;
    int maxHotspots_;
    float edgeThreshold_;
    float trackingRadius_;
    float nmsRadius_;
    float spotSize_;
    float starSize_;
    float starFraction_;
    float rotationSpeedDeg_;
    float twinkleSpeed_;
    float intensityScale_;
    float fadeInSec_;
    float fadeOutSec_;
    float brightThreshold_;
    float brightBias_;
    bool customColorEnabled_;
    float customColorR_;
    float customColorG_;
    float customColorB_;

    std::vector<float> luma_;
    std::vector<float> gradient_;
    std::vector<Sparkle> sparkles_;
    std::mt19937 rng_;

    static constexpr float kPi = 3.14159265f;

    float clamp01(float v) const {
        return std::min(1.0f, std::max(0.0f, v));
    }

    static bool parseHexColor(const std::string& value, float& outR, float& outG, float& outB) {
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

    void assignSparkleColor(Sparkle& s, float tint) {
        if (customColorEnabled_) {
            // Keep a subtle variation so large fields of sparkles don't look flat.
            float t = 1.0f + tint * 0.35f;
            s.r = clamp01(customColorR_ * t);
            s.g = clamp01(customColorG_ * t);
            s.b = clamp01(customColorB_ * t);
            return;
        }
        s.r = clamp01(1.0f + tint);
        s.g = clamp01(1.0f + tint * 0.6f);
        s.b = clamp01(1.0f + tint * 0.2f);
    }

    void drawSoftDisk(std::vector<uint8_t>& frame, float cx, float cy, float radius, float opacity, float colR, float colG, float colB) {
        if (opacity <= 0.001f || radius <= 0.1f) return;
        int rI = (int)std::ceil(radius * 2.2f + 1.0f);
        int minX = std::max(0, (int)std::floor(cx - rI));
        int maxX = std::min(width_ - 1, (int)std::ceil(cx + rI));
        int minY = std::max(0, (int)std::floor(cy - rI));
        int maxY = std::min(height_ - 1, (int)std::ceil(cy + rI));
        float rr = std::max(0.0001f, radius);
        float invSigma2 = 1.0f / (rr * rr * 0.85f);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;
                float dist2 = dx * dx + dy * dy;
                float a = std::exp(-dist2 * invSigma2) * opacity;
                if (a <= 0.003f) continue;
                int idx = (y * width_ + x) * 3;
                float curR = frame[idx + 0] / 255.0f;
                float curG = frame[idx + 1] / 255.0f;
                float curB = frame[idx + 2] / 255.0f;
                frame[idx + 0] = (uint8_t)(255 * std::min(1.0f, curR + a * colR));
                frame[idx + 1] = (uint8_t)(255 * std::min(1.0f, curG + a * colG));
                frame[idx + 2] = (uint8_t)(255 * std::min(1.0f, curB + a * colB));
            }
        }
    }

    void drawStar4(std::vector<uint8_t>& frame, float cx, float cy, float size, float angle, float opacity, float colR, float colG, float colB) {
        float len = std::max(2.0f, size * 4.0f);
        float baseWidth = std::max(0.25f, size * 0.35f);
        drawSoftDisk(frame, cx, cy, size * 0.9f, opacity * 0.9f, colR, colG, colB);

        for (int axis = 0; axis < 2; ++axis) {
            float a = angle + axis * (kPi * 0.5f);
            float ca = std::cos(a);
            float sa = std::sin(a);
            float step = std::clamp(0.45f - opacity * 0.25f, 0.12f, 0.6f);
            for (float s = -len; s <= len + 0.001f; s += step) {
                float distFrac = std::fabs(s) / len;
                float alphaFall = std::exp(-3.6f * distFrac);
                float widthFall = std::exp(-2.4f * distFrac);
                float localWidth = baseWidth * widthFall;
                if (localWidth < 0.12f) continue;
                float localAlpha = opacity * alphaFall * 0.8f;
                float px = cx + ca * s;
                float py = cy + sa * s;
                drawSoftDisk(frame, px, py, localWidth, localAlpha, colR, colG, colB);
            }
        }
    }

    void detectHotspots(const std::vector<uint8_t>& frame, std::vector<Hotspot>& hotspots, float& maxScore) {
        hotspots.clear();
        maxScore = 0.0f;
        if (width_ < 3 || height_ < 3) return;

        luma_.assign(width_ * height_, 0.0f);
        std::vector<uint8_t> brightMask(width_ * height_, 0);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                int idx = (y * width_ + x) * 3;
                float r = frame[idx + 0];
                float g = frame[idx + 1];
                float b = frame[idx + 2];
                float lum = 0.299f * r + 0.587f * g + 0.114f * b;
                int li = y * width_ + x;
                luma_[li] = lum;
                brightMask[li] = (lum >= brightThreshold_) ? 1 : 0;
            }
        }

        gradient_.assign(width_ * height_, 0.0f);
        for (int y = 1; y < height_ - 1; ++y) {
            for (int x = 1; x < width_ - 1; ++x) {
                int idx = y * width_ + x;
                float tl = luma_[(y - 1) * width_ + (x - 1)];
                float tc = luma_[(y - 1) * width_ + x];
                float tr = luma_[(y - 1) * width_ + (x + 1)];
                float ml = luma_[y * width_ + (x - 1)];
                float mr = luma_[y * width_ + (x + 1)];
                float bl = luma_[(y + 1) * width_ + (x - 1)];
                float bc = luma_[(y + 1) * width_ + x];
                float br = luma_[(y + 1) * width_ + (x + 1)];

                float gx = -tl + tr - 2.0f * ml + 2.0f * mr - bl + br;
                float gy = -tl - 2.0f * tc - tr + bl + 2.0f * bc + br;
                float mag = std::sqrt(gx * gx + gy * gy);
                if (mag < edgeThreshold_) continue;
                float absGx = std::fabs(gx);
                float absGy = std::fabs(gy);
                float cornerness = 0.0f;
                if (absGx > 0.0001f && absGy > 0.0001f) {
                    cornerness = std::min(absGx, absGy) / std::max(absGx, absGy);
                }
                float score = mag * (0.7f + 0.6f * cornerness);
                if (brightBias_ > 0.0f) {
                    bool nearBright = false;
                    for (int oy = -1; oy <= 1 && !nearBright; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (brightMask[(y + oy) * width_ + (x + ox)]) {
                                nearBright = true;
                                break;
                            }
                        }
                    }
                    if (nearBright) {
                        score *= (1.0f + brightBias_);
                    } else {
                        score *= std::max(0.0f, 1.0f - 0.35f * brightBias_);
                    }
                }
                gradient_[idx] = score;
                if (score > maxScore) maxScore = score;
            }
        }

        std::vector<Hotspot> candidates;
        candidates.reserve((size_t)width_ * height_ / 4);
        for (int y = 1; y < height_ - 1; ++y) {
            for (int x = 1; x < width_ - 1; ++x) {
                float score = gradient_[y * width_ + x];
                if (score <= 0.0f) continue;
                candidates.push_back({x, y, score});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const Hotspot& a, const Hotspot& b) { return a.score > b.score; });

        float nmsRad2 = nmsRadius_ * nmsRadius_;
        for (const auto& cand : candidates) {
            if ((int)hotspots.size() >= maxHotspots_) break;
            bool tooClose = false;
            for (const auto& picked : hotspots) {
                float dx = (float)cand.x - picked.x;
                float dy = (float)cand.y - picked.y;
                if (dx * dx + dy * dy < nmsRad2) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose) hotspots.push_back(cand);
        }
    }

    void seedSparkleAt(const Hotspot& hs, float maxScore, Sparkle& s) {
        std::uniform_real_distribution<float> sizeJit(0.75f, 1.25f);
        std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * kPi);
        std::uniform_real_distribution<float> tintDist(-0.07f, 0.07f);
        s.x = (float)hs.x;
        s.y = (float)hs.y;
        s.size = (s.isStar ? starSize_ : spotSize_) * sizeJit(rng_);
        s.baseIntensity = (maxScore > 0.0f) ? clamp01(hs.score / maxScore) : 0.85f;
        s.intensity = 0.0f;
        s.targetIntensity = s.baseIntensity;
        s.phase = phaseDist(rng_);
        float tint = tintDist(rng_);
        assignSparkleColor(s, tint);
    }

    void seedSparkleRandom(Sparkle& s) {
        std::uniform_real_distribution<float> posX(0.0f, (float)width_);
        std::uniform_real_distribution<float> posY(0.0f, (float)height_);
        std::uniform_real_distribution<float> sizeJit(0.75f, 1.25f);
        std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * kPi);
        std::uniform_real_distribution<float> tintDist(-0.07f, 0.07f);
        s.x = posX(rng_);
        s.y = posY(rng_);
        s.size = (s.isStar ? starSize_ : spotSize_) * sizeJit(rng_);
        s.baseIntensity = 0.7f;
        s.intensity = 0.0f;
        s.targetIntensity = s.baseIntensity;
        s.phase = phaseDist(rng_);
        float tint = tintDist(rng_);
        assignSparkleColor(s, tint);
    }

    void ensureSparkles(const std::vector<Hotspot>& hotspots, float maxScore) {
        if ((int)sparkles_.size() == numSparkles_) return;
        sparkles_.clear();
        sparkles_.reserve(numSparkles_);
        std::uniform_real_distribution<float> typeDist(0.0f, 1.0f);
        for (int i = 0; i < numSparkles_; ++i) {
            Sparkle s{};
            s.isStar = typeDist(rng_) < starFraction_;
            if (!hotspots.empty()) {
                const Hotspot& hs = hotspots[i % hotspots.size()];
                seedSparkleAt(hs, maxScore, s);
            } else {
                seedSparkleRandom(s);
            }
            sparkles_.push_back(s);
        }
    }

public:
    SparkleEffect()
        : width_(0), height_(0), fps_(30), frameCount_(0),
          numSparkles_(120), maxHotspots_(400), edgeThreshold_(80.0f),
          trackingRadius_(10.0f), nmsRadius_(10.0f),
          spotSize_(2.8f), starSize_(5.2f), starFraction_(0.35f),
          rotationSpeedDeg_(25.0f), twinkleSpeed_(1.6f),
          intensityScale_(1.0f), fadeInSec_(0.6f), fadeOutSec_(1.2f),
          brightThreshold_(235.0f), brightBias_(0.8f),
          customColorEnabled_(false), customColorR_(1.0f), customColorG_(1.0f), customColorB_(1.0f),
          rng_(std::random_device{}()) {}

    std::string getName() const override { return "sparkle"; }
    std::string getDescription() const override { return "Edge-aware sparkles that follow moving edges and corners"; }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--sparkles", "int", 1, 200000, true, "Number of sparkles", "120"});
        opts.push_back({"--max-hotspots", "int", 1, 500000, true, "Maximum edge hotspots to consider", "400"});
        opts.push_back({"--edge-threshold", "float", 0.0, 10000.0, true, "Edge detection threshold", "80"});
        opts.push_back({"--track-radius", "float", 0.0, 10000.0, true, "Max distance to lock onto a moving edge (pixels)", "10"});
        opts.push_back({"--nms-radius", "float", 0.0, 10000.0, true, "Hotspot separation radius (pixels)", "10"});
        opts.push_back({"--spot-size", "float", 0.1, 10000.0, true, "Soft spot sparkle radius", "2.8"});
        opts.push_back({"--star-size", "float", 0.1, 10000.0, true, "4-point star sparkle size", "5.2"});
        opts.push_back({"--star-fraction", "float", 0.0, 1.0, true, "Fraction of sparkles that are stars", "0.35"});
        opts.push_back({"--rotation-speed", "float", -10000.0, 10000.0, true, "Star rotation speed (deg/sec)", "25"});
        opts.push_back({"--twinkle-speed", "float", 0.0, 10000.0, true, "Twinkle speed (cycles/sec)", "1.6"});
        opts.push_back({"--intensity", "float", 0.0, 100.0, true, "Sparkle intensity multiplier", "1.0"});
        opts.push_back({"--fade-in", "float", 0.0, 1000.0, true, "Seconds to fade sparkles in", "0.6"});
        opts.push_back({"--fade-out", "float", 0.0, 1000.0, true, "Seconds to fade sparkles out", "1.2"});
        opts.push_back({"--bright-threshold", "float", 0.0, 255.0, true, "Luma threshold for bright-edge bias", "235"});
        opts.push_back({"--bright-bias", "float", 0.0, 10.0, true, "Bias strength favoring edges near bright pixels", "0.8"});
        opts.push_back({"--color", "string", 0, 0, false, "Sparkle color: auto|white|#RRGGBB", "auto"});
        return opts;
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--sparkles" && i + 1 < argc) {
            numSparkles_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--max-hotspots" && i + 1 < argc) {
            maxHotspots_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--edge-threshold" && i + 1 < argc) {
            edgeThreshold_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--track-radius" && i + 1 < argc) {
            trackingRadius_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--nms-radius" && i + 1 < argc) {
            nmsRadius_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--spot-size" && i + 1 < argc) {
            spotSize_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--star-size" && i + 1 < argc) {
            starSize_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--star-fraction" && i + 1 < argc) {
            starFraction_ = std::atof(argv[++i]);
            if (starFraction_ < 0.0f) starFraction_ = 0.0f;
            if (starFraction_ > 1.0f) starFraction_ = 1.0f;
            return true;
        } else if (arg == "--rotation-speed" && i + 1 < argc) {
            rotationSpeedDeg_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--twinkle-speed" && i + 1 < argc) {
            twinkleSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--intensity" && i + 1 < argc) {
            intensityScale_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--fade-in" && i + 1 < argc) {
            fadeInSec_ = std::max(0.0f, (float)std::atof(argv[++i]));
            return true;
        } else if (arg == "--fade-out" && i + 1 < argc) {
            fadeOutSec_ = std::max(0.0f, (float)std::atof(argv[++i]));
            return true;
        } else if (arg == "--bright-threshold" && i + 1 < argc) {
            brightThreshold_ = std::clamp((float)std::atof(argv[++i]), 0.0f, 255.0f);
            return true;
        } else if (arg == "--bright-bias" && i + 1 < argc) {
            brightBias_ = std::max(0.0f, (float)std::atof(argv[++i]));
            return true;
        } else if (arg == "--color" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "auto") {
                customColorEnabled_ = false;
                return true;
            }
            if (v == "white") {
                customColorEnabled_ = true;
                customColorR_ = 1.0f;
                customColorG_ = 1.0f;
                customColorB_ = 1.0f;
                return true;
            }
            float r = 1.0f, g = 1.0f, b = 1.0f;
            if (!parseHexColor(v, r, g, b)) {
                std::cerr << "Invalid --color '" << v << "'. Use auto|white|#RRGGBB.\n";
                return false;
            }
            customColorEnabled_ = true;
            customColorR_ = r;
            customColorG_ = g;
            customColorB_ = b;
            return true;
        }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        frameCount_ = 0;
        sparkles_.clear();
        luma_.clear();
        gradient_.clear();
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool /*hasBackground*/, float fadeMultiplier) override {
        std::vector<Hotspot> hotspots;
        float maxScore = 0.0f;
        detectHotspots(frame, hotspots, maxScore);

        ensureSparkles(hotspots, maxScore);

        if (!hotspots.empty()) {
            std::vector<bool> used(hotspots.size(), false);
            float maxDist2 = trackingRadius_ * trackingRadius_;
            for (auto& s : sparkles_) {
                int best = -1;
                float bestDist2 = maxDist2;
                for (size_t h = 0; h < hotspots.size(); ++h) {
                    if (used[h]) continue;
                    float dx = (float)hotspots[h].x - s.x;
                    float dy = (float)hotspots[h].y - s.y;
                    float dist2 = dx * dx + dy * dy;
                    if (dist2 < bestDist2) {
                        bestDist2 = dist2;
                        best = (int)h;
                    }
                }
                if (best >= 0) {
                    used[best] = true;
                    s.x = (float)hotspots[best].x;
                    s.y = (float)hotspots[best].y;
                    s.baseIntensity = (maxScore > 0.0f) ? clamp01(hotspots[best].score / maxScore) : s.baseIntensity;
                    s.targetIntensity = s.baseIntensity;
                } else {
                    int fallback = -1;
                    for (size_t h = 0; h < hotspots.size(); ++h) {
                        if (!used[h]) { fallback = (int)h; break; }
                    }
                    if (fallback >= 0) {
                        used[fallback] = true;
                        s.x = (float)hotspots[fallback].x;
                        s.y = (float)hotspots[fallback].y;
                        s.baseIntensity = (maxScore > 0.0f) ? clamp01(hotspots[fallback].score / maxScore) : s.baseIntensity;
                        s.targetIntensity = s.baseIntensity;
                    } else {
                        s.targetIntensity = 0.0f;
                    }
                }
            }
        } else {
            for (auto& s : sparkles_) {
                s.targetIntensity = 0.0f;
            }
        }

        float angle = ((float)frameCount_ / std::max(1, fps_)) * rotationSpeedDeg_ * (kPi / 180.0f);
        float fade = fadeMultiplier * intensityScale_;
        for (const auto& s : sparkles_) {
            float twinkle = 0.55f + 0.45f * std::sin(s.phase);
            float opacity = clamp01(s.intensity * twinkle) * fade;
            if (opacity <= 0.001f) continue;
            if (s.isStar) {
                drawStar4(frame, s.x, s.y, s.size, angle, opacity, s.r, s.g, s.b);
            } else {
                drawSoftDisk(frame, s.x, s.y, s.size, opacity, s.r, s.g, s.b);
            }
        }
    }

    void update() override {
        frameCount_++;
        float phaseStep = (twinkleSpeed_ * 2.0f * kPi) / std::max(1, fps_);
        float dt = 1.0f / std::max(1, fps_);
        float inStep = (fadeInSec_ > 0.0f) ? (dt / fadeInSec_) : 1.0f;
        float outStep = (fadeOutSec_ > 0.0f) ? (dt / fadeOutSec_) : 1.0f;
        for (auto& s : sparkles_) {
            s.phase += phaseStep;
            if (s.phase > 1000.0f) s.phase -= 1000.0f;
            if (s.intensity < s.targetIntensity) {
                s.intensity = std::min(s.targetIntensity, s.intensity + inStep);
            } else if (s.intensity > s.targetIntensity) {
                s.intensity = std::max(s.targetIntensity, s.intensity - outStep);
            }
        }
    }
};

REGISTER_EFFECT(SparkleEffect, "sparkle", "Edge-aware sparkles that follow moving edges and corners")
