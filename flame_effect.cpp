// flame_effect.cpp
// 2D flame and smoke fluid simulation (CPU)

#include "effect_generator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

class FlameEffect : public Effect {
private:
    struct SourcePoint {
        float x = 0.5f;
        float y = 0.97f;
        float scale = 1.0f;
    };

    int width_ = 0;
    int height_ = 0;
    int fps_ = 30;
    int frameCount_ = 0;

    float simMultiplier_ = 2.0f;
    int simWidth_ = 0;
    int simHeight_ = 0;
    float simPadLeft_ = 0.0f;
    float simPadRight_ = 0.0f;
    float simPadTop_ = 0.0f;
    float simPadBottom_ = 0.0f;

    int substeps_ = 2;
    int pressureIters_ = 12;
    int diffusionIters_ = 1;
    int threadsOpt_ = 0; // 0 = auto

    float timeScale_ = 1.0f;
    float sourceX_ = 0.5f;       // normalized
    float sourceY_ = 0.97f;      // normalized (0=top, 1=bottom)
    float sourceWidth_ = 0.02f;  // normalized base width
    float sourceHeight_ = 0.12f; // normalized source region height
    float sourceSpread_ = 1.75f; // width expansion above base
    int burnerMode_ = 1;         // 0=gaussian, 1=tiki, 2=hybrid
    float sourceHeat_ = 3.2f;
    float sourceSmoke_ = 1.1f;
    float sourceUpdraft_ = 200.0f;
    float turbulence_ = 18.0f;
    float wobble_ = 0.1f;
    float flicker_ = 0.75f;      // heat flicker amount (0..1+)
    float crosswind_ = 6.0f;
    float initialAir_ = 40.0f;

    float buoyancy_ = 220.0f;
    float cooling_ = 0.45f;
    float coolingAloftBoost_ = 0.5f;
    float smokeDissipation_ = 0.5f;
    float velocityDamping_ = 0.10f;
    float vorticity_ = 75.0f;

    float flameIntensity_ = 1.25f;
    float flameCutoff_ = 0.15f;
    float flameSharpness_ = 2.0f;
    float smokeIntensity_ = 0.92f;
    float smokiness_ = 0.85f;
    float smokeDarkness_ = 0.1f;
    float ageRate_ = 1.6f;
    float ageCooling_ = 0.68f;
    float agePower_ = 1.5f;
    float ageTaper_ = 1.1f;
    float heatFlickerGain_ = 1.0f;
    float heatFlickerTarget_ = 1.0f;
    float heatFlickerTimer_ = 0.0f;
    float heatFlickerRecover_ = 1.1f;
    std::mt19937 rng_{std::random_device{}()};

    std::vector<float> u_;
    std::vector<float> v_;
    std::vector<float> uTmp_;
    std::vector<float> vTmp_;
    std::vector<float> temp_;
    std::vector<float> tempTmp_;
    std::vector<float> smoke_;
    std::vector<float> smokeTmp_;
    std::vector<float> age_;
    std::vector<float> ageTmp_;
    std::vector<float> pressure_;
    std::vector<float> pressureTmp_;
    std::vector<float> divergence_;
    std::vector<float> curl_;
    std::vector<SourcePoint> sourcePoints_;

    inline int idx(int x, int y) const { return y * simWidth_ + x; }

    static float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

    bool parseSourcesSpec(const std::string& spec) {
        std::vector<SourcePoint> parsed;
        std::stringstream ss(spec);
        std::string token;
        while (std::getline(ss, token, ';')) {
            if (token.empty()) continue;
            auto c0 = token.find(',');
            if (c0 == std::string::npos) continue;
            auto c1 = token.find(',', c0 + 1);
            std::string xs = token.substr(0, c0);
            std::string ys;
            std::string sscl;
            if (c1 == std::string::npos) {
                ys = token.substr(c0 + 1);
            } else {
                ys = token.substr(c0 + 1, c1 - c0 - 1);
                sscl = token.substr(c1 + 1);
            }
            char* endX = nullptr;
            char* endY = nullptr;
            float x = std::strtof(xs.c_str(), &endX);
            float y = std::strtof(ys.c_str(), &endY);
            if (endX == xs.c_str() || endY == ys.c_str()) continue;
            float scl = 1.0f;
            if (!sscl.empty()) {
                char* endS = nullptr;
                float rawScale = std::strtof(sscl.c_str(), &endS);
                if (endS != sscl.c_str()) scl = rawScale;
            }
            parsed.push_back({
                std::clamp(x, -10.0f, 10.0f),
                std::clamp(y, -10.0f, 10.0f),
                std::clamp(scl, 0.0f, 8.0f)
            });
        }
        if (parsed.empty()) return false;
        sourcePoints_ = std::move(parsed);
        return true;
    }

    bool applyPreset(const std::string& name) {
        if (name == "smallcandle") {
            burnerMode_ = 0; // gaussian
            pressureIters_ = 16;
            sourceWidth_ = 0.008f;
            sourceHeight_ = 0.075f;
            sourceSpread_ = 1.05f;
            sourceHeat_ = 1.9f;
            sourceSmoke_ = 0.20f;
            sourceUpdraft_ = 110.0f;
            turbulence_ = 6.0f;
            wobble_ = 0.05f;
            flicker_ = 0.38f;
            crosswind_ = 1.2f;
            initialAir_ = 8.0f;
            buoyancy_ = 105.0f;
            cooling_ = 0.72f;
            coolingAloftBoost_ = 1.0f;
            smokeDissipation_ = 0.90f;
            vorticity_ = 28.0f;
            flameIntensity_ = 1.05f;
            smokiness_ = 0.10f;
            smokeDarkness_ = 0.04f;
            ageRate_ = 1.25f;
            ageCooling_ = 1.35f;
            ageTaper_ = 1.65f;
            return true;
        }
        if (name == "candle") {
            burnerMode_ = 0; // gaussian
            pressureIters_ = 16;
            sourceWidth_ = 0.012f;
            sourceHeight_ = 0.10f;
            sourceSpread_ = 1.15f;
            sourceHeat_ = 2.4f;
            sourceSmoke_ = 0.25f;
            sourceUpdraft_ = 135.0f;
            turbulence_ = 8.0f;
            wobble_ = 0.06f;
            flicker_ = 0.45f;
            crosswind_ = 1.5f;
            initialAir_ = 10.0f;
            buoyancy_ = 120.0f;
            cooling_ = 0.65f;
            coolingAloftBoost_ = 0.90f;
            smokeDissipation_ = 0.85f;
            vorticity_ = 35.0f;
            flameIntensity_ = 1.15f;
            smokiness_ = 0.12f;
            smokeDarkness_ = 0.05f;
            ageRate_ = 1.2f;
            ageCooling_ = 1.2f;
            ageTaper_ = 1.5f;
            return true;
        }
        if (name == "campfire") {
            burnerMode_ = 2; // hybrid
            pressureIters_ = 12;
            sourceWidth_ = 0.060f;
            sourceHeight_ = 0.14f;
            sourceSpread_ = 1.9f;
            sourceHeat_ = 3.6f;
            sourceSmoke_ = 1.5f;
            sourceUpdraft_ = 150.0f;
            turbulence_ = 35.0f;
            wobble_ = 0.22f;
            flicker_ = 0.80f;
            crosswind_ = 8.0f;
            initialAir_ = 30.0f;
            buoyancy_ = 180.0f;
            cooling_ = 0.38f;
            coolingAloftBoost_ = 0.42f;
            smokeDissipation_ = 0.35f;
            vorticity_ = 70.0f;
            flameIntensity_ = 1.35f;
            smokiness_ = 1.1f;
            smokeDarkness_ = 0.42f;
            ageRate_ = 1.5f;
            ageCooling_ = 0.70f;
            ageTaper_ = 1.1f;
            return true;
        }
        if (name == "bonfire") {
            burnerMode_ = 2; // hybrid
            pressureIters_ = 10;
            sourceWidth_ = 0.10f;
            sourceHeight_ = 0.16f;
            sourceSpread_ = 2.2f;
            sourceHeat_ = 4.5f;
            sourceSmoke_ = 2.0f;
            sourceUpdraft_ = 190.0f;
            turbulence_ = 90.0f;
            wobble_ = 0.35f;
            flicker_ = 2.0f;
            crosswind_ = 42.0f;
            initialAir_ = 65.0f;
            buoyancy_ = 240.0f;
            cooling_ = 0.30f;
            coolingAloftBoost_ = 0.35f;
            smokeDissipation_ = 0.22f;
            vorticity_ = 85.0f;
            flameIntensity_ = 1.55f;
            smokiness_ = 1.5f;
            smokeDarkness_ = 0.70f;
            ageRate_ = 1.3f;
            ageCooling_ = 0.60f;
            ageTaper_ = 1.0f;
            return true;
        }
        if (name == "smoketrail") {
            burnerMode_ = 0; // gaussian
            pressureIters_ = 12;
            sourceWidth_ = 0.04f;
            sourceHeight_ = 0.08f;
            sourceSpread_ = 1.5f;
            sourceHeat_ = 3.5f;
            sourceSmoke_ = 1.1f;
            sourceUpdraft_ = 70.0f;
            turbulence_ = 80.0f;
            wobble_ = 0.12f;
            flicker_ = 0.75f;
            crosswind_ = 20.0f;
            initialAir_ = 40.0f;
            buoyancy_ = 220.0f;
            cooling_ = 0.2f;
            coolingAloftBoost_ = 0.01f;
            smokeDissipation_ = 0.001f;
            velocityDamping_ = 0.06f;
            vorticity_ = 99.0f;
            flameIntensity_ = 0.0f;  // smoke-only look
            smokeIntensity_ = 0.7f;
            smokiness_ = 1.6f;
            smokeDarkness_ = 0.1f;
            ageRate_ = 0.7f;
            ageCooling_ = 0.25f;
            agePower_ = 1.0f;
            ageTaper_ = 1.1f;
            return true;
        }
        return false;
    }

    int workerCount() const {
        int hw = (int)std::thread::hardware_concurrency();
        if (hw <= 0) hw = 1;
        if (threadsOpt_ > 0) return std::max(1, threadsOpt_);
        return hw;
    }

    template <typename Fn>
    void parallelRows(int yBegin, int yEnd, const Fn& fn) {
        int rows = yEnd - yBegin;
        if (rows <= 0) return;

        int workers = workerCount();
        if (workers <= 1 || rows < workers * 8) {
            fn(yBegin, yEnd);
            return;
        }

        int chunk = (rows + workers - 1) / workers;
        std::vector<std::thread> pool;
        pool.reserve((size_t)std::max(0, workers - 1));

        for (int w = 0; w < workers - 1; ++w) {
            int a = yBegin + w * chunk;
            int b = std::min(yEnd, a + chunk);
            if (a >= b) break;
            pool.emplace_back([&fn, a, b]() { fn(a, b); });
        }

        int mainA = yBegin + (workers - 1) * chunk;
        if (mainA < yEnd) fn(mainA, yEnd);

        for (auto& t : pool) t.join();
    }

    static float hash3(int x, int y, int z) {
        uint32_t n = (uint32_t)x * 1597334677u ^ (uint32_t)y * 3812015801u ^ (uint32_t)z * 2798796415u;
        n = (n ^ (n >> 13)) * 1274126177u;
        n ^= (n >> 16);
        return (n & 0x00ffffffu) * (1.0f / 16777215.0f);
    }

    float sampleBilinear(const std::vector<float>& f, float x, float y) const {
        x = std::clamp(x, 0.0f, (float)(simWidth_ - 1));
        y = std::clamp(y, 0.0f, (float)(simHeight_ - 1));

        int x0 = (int)std::floor(x);
        int y0 = (int)std::floor(y);
        int x1 = std::min(simWidth_ - 1, x0 + 1);
        int y1 = std::min(simHeight_ - 1, y0 + 1);

        float tx = x - x0;
        float ty = y - y0;

        float v00 = f[idx(x0, y0)];
        float v10 = f[idx(x1, y0)];
        float v01 = f[idx(x0, y1)];
        float v11 = f[idx(x1, y1)];

        float a = v00 + (v10 - v00) * tx;
        float b = v01 + (v11 - v01) * tx;
        return a + (b - a) * ty;
    }

    void clearBoundaries(std::vector<float>& field) {
        for (int x = 0; x < simWidth_; ++x) {
            field[idx(x, 0)] = 0.0f;
            field[idx(x, simHeight_ - 1)] = 0.0f;
        }
        for (int y = 0; y < simHeight_; ++y) {
            field[idx(0, y)] = 0.0f;
            field[idx(simWidth_ - 1, y)] = 0.0f;
        }
    }

    void clearVelocityBoundaries() {
        clearBoundaries(u_);
        clearBoundaries(v_);
    }

    void seedInitialAirFlow() {
        if (initialAir_ <= 0.0f) return;
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                float ny = y / std::max(1.0f, (float)(simHeight_ - 1));
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    float nx = x / std::max(1.0f, (float)(simWidth_ - 1));
                    float n0 = hash3(x * 3, y * 3, 17) - 0.5f;
                    float n1 = hash3(x * 7, y * 7, 53) - 0.5f;
                    float base = (0.65f * n0 + 0.35f * n1) * (0.4f + 0.6f * (1.0f - ny));
                    u_[idx(x, y)] = base * initialAir_ * 1.2f;
                    v_[idx(x, y)] = (hash3(x * 5, y * 5, 97) - 0.5f) * initialAir_ * 0.6f * (1.0f - nx * 0.2f);
                }
            }
        });
        clearVelocityBoundaries();
    }

    void applyAmbientAirMotion(float dt) {
        if (crosswind_ <= 0.0f && wobble_ <= 0.0f) return;
        float t = frameCount_ / std::max(1.0f, (float)fps_);
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                float ny = y / std::max(1.0f, (float)(simHeight_ - 1));
                float flowBand = std::pow(1.0f - ny, 1.35f);
                float globalWind = std::sin(t * 1.1f + ny * 7.0f) * crosswind_ * flowBand;
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    int i = idx(x, y);
                    float localNoise = (hash3(x, y, frameCount_ + 1234) - 0.5f) * 2.0f;
                    u_[i] += (globalWind + localNoise * wobble_ * 4.0f) * dt;
                    v_[i] += localNoise * wobble_ * 1.2f * dt;
                }
            }
        });
    }

    void updateHeatFlicker(float dt) {
        if (flicker_ <= 0.0f) {
            heatFlickerGain_ = 1.0f;
            heatFlickerTarget_ = 1.0f;
            heatFlickerTimer_ = 0.0f;
            return;
        }

        heatFlickerTimer_ -= dt;
        if (heatFlickerTimer_ <= 0.0f) {
            std::uniform_real_distribution<float> intervalDist(0.2f, 3.0f);
            std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
            heatFlickerTimer_ = intervalDist(rng_);

            float f = std::clamp(flicker_, 0.0f, 1.5f);
            float minDrop = std::clamp(1.0f - f * 0.8f, 0.2f, 1.0f);
            float maxDrop = std::clamp(1.0f - f * 0.5f, minDrop, 1.0f);
            std::uniform_real_distribution<float> dropDist(minDrop, maxDrop);
            heatFlickerGain_ *= dropDist(rng_);
            heatFlickerGain_ = std::clamp(heatFlickerGain_, 0.15f, 2.0f);

            heatFlickerTarget_ = 1.0f + f * (0.1f + 0.35f * unitDist(rng_));
            heatFlickerRecover_ = 0.8f + f * (1.4f + 0.6f * unitDist(rng_));
        }

        float alpha = std::clamp(heatFlickerRecover_ * dt, 0.0f, 1.0f);
        heatFlickerGain_ += (heatFlickerTarget_ - heatFlickerGain_) * alpha;
    }

    void addSources(float dt) {
        struct EmitterParams {
            float sourceWidth;
            float sourceHeight;
            float sourceSpread;
            float sourceHeat;
            float sourceSmoke;
            float sourceUpdraft;
            float turbulence;
            float wobble;
        };

        auto scaledEmitterParams = [&](float sourceScale) {
            // Source scale multiplies the emitter knobs that differ between candle and smallcandle.
            float s = std::clamp(sourceScale, 0.0f, 8.0f);
            EmitterParams p;
            p.sourceWidth = sourceWidth_ * s;
            p.sourceHeight = sourceHeight_ * s;
            p.sourceSpread = sourceSpread_ * s;
            p.sourceHeat = sourceHeat_ * s;
            p.sourceSmoke = sourceSmoke_ * s;
            p.sourceUpdraft = sourceUpdraft_ * s;
            p.turbulence = turbulence_ * s;
            p.wobble = wobble_ * s;
            return p;
        };

        float padX = std::max(0.0f, simPadLeft_) + std::max(0.0f, simPadRight_);
        float padY = std::max(0.0f, simPadTop_) + std::max(0.0f, simPadBottom_);
        float domainW = 1.0f + padX;
        float domainH = 1.0f + padY;
        float visibleSimW = simWidth_ / std::max(0.0001f, domainW);
        float visibleSimH = simHeight_ / std::max(0.0001f, domainH);
        int phase = frameCount_;
        std::vector<SourcePoint> activeSources;
        if (sourcePoints_.empty()) {
            activeSources.push_back({sourceX_, sourceY_, 1.0f});
        } else {
            activeSources = sourcePoints_;
        }

        auto injectGaussian = [&](const SourcePoint& sp, float modeScale, const EmitterParams& ep) {
            float sxNorm = (sp.x + simPadLeft_) / std::max(0.0001f, domainW);
            float syNorm = (sp.y + simPadTop_) / std::max(0.0001f, domainH);
            float halfWBase = std::max(0.6f, ep.sourceWidth * visibleSimW * 0.5f);
            float cxBase = sxNorm * (simWidth_ - 1);
            float flick = std::sin(frameCount_ * 0.27f) * 0.7f + std::sin(frameCount_ * 0.11f + 1.2f) * 0.4f;
            float cx = cxBase + flick * (1.0f + ep.wobble * 2.0f);
            float sourceY = syNorm * (simHeight_ - 1);
            float sigmaY = std::max(0.9f, ep.sourceHeight * visibleSimH * 0.24f);
            int yStart = std::max(1, (int)std::floor(sourceY - 3.0f * sigmaY));
            int yEnd = std::min(simHeight_ - 2, (int)std::ceil(sourceY + 1.5f * sigmaY));
            int minX = std::max(1, (int)std::floor(cx - halfWBase * 2.4f - 4.0f));
            int maxX = std::min(simWidth_ - 2, (int)std::ceil(cx + halfWBase * 2.4f + 4.0f));

            for (int y = yStart; y <= yEnd; ++y) {
                float dy = ((float)y - sourceY) / sigmaY;
                float yWeight = std::exp(-0.5f * dy * dy);
                float rise = std::clamp((sourceY - (float)y) / std::max(1.0f, sigmaY * 2.4f), 0.0f, 1.0f);
                float plumeHalfW = halfWBase * (1.0f + ep.sourceSpread * 0.55f * rise);
                for (int x = minX; x <= maxX; ++x) {
                    float dx = std::fabs((float)x - cx);
                    float nx = dx / (plumeHalfW + 1e-4f);
                    if (nx >= 2.5f) continue;
                    float xWeight = std::exp(-0.95f * nx * nx);

                    float n = hash3(x, y, phase);
                    float pulse = 0.82f + 0.18f * std::sin(0.19f * (float)phase + (float)x * 0.09f + (float)y * 0.04f);
                    float shape = xWeight * yWeight * pulse * modeScale;

                    int i = idx(x, y);
                    temp_[i] += ep.sourceHeat * heatFlickerGain_ * shape * dt;
                    smoke_[i] += ep.sourceSmoke * smokiness_ * (0.7f + 0.3f * n) * shape * dt;
                    age_[i] = std::min(age_[i], 0.03f + 0.05f * (1.0f - n));
                    v_[i] -= ep.sourceUpdraft * shape * dt;
                    u_[i] += ((n - 0.5f) * 2.0f) * ep.turbulence * (0.8f + 0.5f * rise + ep.wobble) * shape * dt;
                }
            }
        };

        auto injectTiki = [&](const SourcePoint& sp, float modeScale, const EmitterParams& ep) {
            float sxNorm = (sp.x + simPadLeft_) / std::max(0.0001f, domainW);
            float syNorm = (sp.y + simPadTop_) / std::max(0.0001f, domainH);
            float halfWBase = std::max(0.6f, ep.sourceWidth * visibleSimW * 0.5f);
            float cxBase = sxNorm * (simWidth_ - 1);
            float flick = std::sin(frameCount_ * 0.23f) * 0.6f + std::sin(frameCount_ * 0.13f + 0.8f) * 0.35f;
            float cx = cxBase + flick * (0.9f + ep.wobble * 1.8f);
            float sourceTop = syNorm * (simHeight_ - 1);
            float regionH = std::max(2.0f, ep.sourceHeight * visibleSimH);
            int yStart = std::max(1, (int)std::floor(sourceTop - regionH));
            int yEnd = std::min(simHeight_ - 2, (int)std::ceil(sourceTop));
            int minX = std::max(1, (int)std::floor(cx - halfWBase * (1.0f + ep.sourceSpread) - 3.0f));
            int maxX = std::min(simWidth_ - 2, (int)std::ceil(cx + halfWBase * (1.0f + ep.sourceSpread) + 3.0f));

            for (int y = yStart; y <= yEnd; ++y) {
                float h = (float)(yEnd - y) / std::max(1, yEnd - yStart);
                float plumeHalfW = halfWBase * (0.40f + ep.sourceSpread * h);
                float yWeight = 0.45f + 0.55f * (1.0f - h);
                for (int x = minX; x <= maxX; ++x) {
                    float dx = std::fabs((float)x - cx);
                    float xWeight = 1.0f - dx / (plumeHalfW + 1e-4f);
                    if (xWeight <= 0.0f) continue;
                    xWeight = xWeight * xWeight * xWeight;

                    float n = hash3(x, y, phase + 77);
                    float pulse = 0.84f + 0.16f * std::sin(0.17f * (float)phase + (float)x * 0.08f);
                    float shape = xWeight * yWeight * pulse * modeScale;

                    int i = idx(x, y);
                    temp_[i] += ep.sourceHeat * heatFlickerGain_ * shape * dt;
                    smoke_[i] += ep.sourceSmoke * smokiness_ * (0.65f + 0.35f * n) * shape * dt;
                    age_[i] = std::min(age_[i], 0.02f + 0.04f * (1.0f - n));
                    // Tiki base gives a slightly stronger base push.
                    v_[i] -= ep.sourceUpdraft * 1.15f * shape * dt;
                    u_[i] += ((n - 0.5f) * 2.0f) * ep.turbulence * (0.7f + 0.8f * h + ep.wobble) * shape * dt;
                }
            }
        };

        for (const auto& sp : activeSources) {
            EmitterParams ep = scaledEmitterParams(sp.scale);
            if (burnerMode_ == 1) {
                injectTiki(sp, 1.0f, ep);
            } else if (burnerMode_ == 2) {
                injectGaussian(sp, 0.65f, ep);
                injectTiki(sp, 0.45f, ep);
            } else {
                injectGaussian(sp, 1.0f, ep);
            }
        }
    }

    void advect(const std::vector<float>& src, const std::vector<float>& velX, const std::vector<float>& velY,
                std::vector<float>& dst, float dt, float damping, bool clampPositive) {
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    int i = idx(x, y);
                    float backX = (float)x - velX[i] * dt;
                    float backY = (float)y - velY[i] * dt;
                    float val = sampleBilinear(src, backX, backY) * damping;
                    dst[i] = clampPositive ? std::max(0.0f, val) : val;
                }
            }
        });
        clearBoundaries(dst);
    }

    void applyDiffusion(std::vector<float>& field, std::vector<float>& tempBuf, float amount, bool clampPositive) {
        if (diffusionIters_ <= 0 || amount <= 0.0f) return;
        for (int iter = 0; iter < diffusionIters_; ++iter) {
            parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    for (int x = 1; x < simWidth_ - 1; ++x) {
                        int i = idx(x, y);
                        float lap = field[idx(x - 1, y)] + field[idx(x + 1, y)] +
                                    field[idx(x, y - 1)] + field[idx(x, y + 1)] - 4.0f * field[i];
                        float v = field[i] + lap * amount;
                        tempBuf[i] = clampPositive ? std::max(0.0f, v) : v;
                    }
                }
            });
            clearBoundaries(tempBuf);
            field.swap(tempBuf);
        }
    }

    void computeCurl() {
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    float dVyDx = 0.5f * (v_[idx(x + 1, y)] - v_[idx(x - 1, y)]);
                    float dUxDy = 0.5f * (u_[idx(x, y + 1)] - u_[idx(x, y - 1)]);
                    curl_[idx(x, y)] = dVyDx - dUxDy;
                }
            }
        });
    }

    void applyVorticityConfinement(float dt) {
        if (vorticity_ <= 0.0f) return;
        computeCurl();
        parallelRows(2, simHeight_ - 2, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 2; x < simWidth_ - 2; ++x) {
                    int i = idx(x, y);
                    float cL = std::fabs(curl_[idx(x - 1, y)]);
                    float cR = std::fabs(curl_[idx(x + 1, y)]);
                    float cB = std::fabs(curl_[idx(x, y - 1)]);
                    float cT = std::fabs(curl_[idx(x, y + 1)]);
                    float gradX = 0.5f * (cR - cL);
                    float gradY = 0.5f * (cT - cB);
                    float mag = std::sqrt(gradX * gradX + gradY * gradY) + 1e-5f;
                    gradX /= mag;
                    gradY /= mag;
                    float vort = curl_[i];
                    u_[i] += gradY * (-vort) * vorticity_ * dt;
                    v_[i] += -gradX * (-vort) * vorticity_ * dt;
                }
            }
        });
    }

    void applyBuoyancy(float dt) {
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    int i = idx(x, y);
                    float force = buoyancy_ * temp_[i] - 4.0f * smoke_[i];
                    v_[i] -= force * dt;
                }
            }
        });
    }

    void computeDivergence() {
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    divergence_[idx(x, y)] = 0.5f * (
                        u_[idx(x + 1, y)] - u_[idx(x - 1, y)] +
                        v_[idx(x, y + 1)] - v_[idx(x, y - 1)]
                    );
                }
            }
        });
    }

    void projectVelocity() {
        computeDivergence();
        std::fill(pressure_.begin(), pressure_.end(), 0.0f);

        for (int iter = 0; iter < pressureIters_; ++iter) {
            parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    for (int x = 1; x < simWidth_ - 1; ++x) {
                        int i = idx(x, y);
                        float p = pressure_[idx(x - 1, y)] + pressure_[idx(x + 1, y)] +
                                  pressure_[idx(x, y - 1)] + pressure_[idx(x, y + 1)] -
                                  divergence_[i];
                        pressureTmp_[i] = 0.25f * p;
                    }
                }
            });
            clearBoundaries(pressureTmp_);
            pressure_.swap(pressureTmp_);
        }

        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    int i = idx(x, y);
                    u_[i] -= 0.5f * (pressure_[idx(x + 1, y)] - pressure_[idx(x - 1, y)]);
                    v_[i] -= 0.5f * (pressure_[idx(x, y + 1)] - pressure_[idx(x, y - 1)]);
                }
            }
        });
        clearVelocityBoundaries();
    }

    void clampScalars() {
        size_t n = temp_.size();
        for (size_t i = 0; i < n; ++i) {
            temp_[i] = std::clamp(temp_[i], 0.0f, 2.0f);
            smoke_[i] = std::clamp(smoke_[i], 0.0f, 2.0f);
        }
    }

    void applyAloftCooling(float dt) {
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                // y=0 top, y=simHeight-1 bottom: cool more as smoke/flame rises.
                // Add low-frequency jitter so the transition does not appear as a hard line.
                float ny = y / std::max(1.0f, (float)(simHeight_ - 1));
                float aloft = 1.0f - ny;
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    int i = idx(x, y);
                    float jitter = 0.75f + 0.25f * hash3(x / 4, y / 4, frameCount_ / 2);
                    float localCooling = std::max(0.0f, cooling_ + coolingAloftBoost_ * aloft * jitter);
                    float coolMul = std::clamp(1.0f - localCooling * dt, 0.0f, 1.0f);
                    float ageBoost = ageCooling_ * std::pow(std::max(0.0f, age_[i]), agePower_);
                    float ageMul = std::clamp(1.0f - ageBoost * dt, 0.0f, 1.0f);
                    temp_[i] *= coolMul * ageMul;
                }
            }
        });
    }

    void ageField(float dt) {
        parallelRows(1, simHeight_ - 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 1; x < simWidth_ - 1; ++x) {
                    int i = idx(x, y);
                    age_[i] = std::clamp(age_[i] + ageRate_ * dt, 0.0f, 8.0f);
                }
            }
        });
    }

    void stepSimulation(float dt) {
        updateHeatFlicker(dt);
        applyAmbientAirMotion(dt);
        addSources(dt);

        float velDamp = std::clamp(1.0f - velocityDamping_ * dt, 0.0f, 1.0f);
        advect(u_, u_, v_, uTmp_, dt, velDamp, false);
        advect(v_, u_, v_, vTmp_, dt, velDamp, false);
        u_.swap(uTmp_);
        v_.swap(vTmp_);
        clearVelocityBoundaries();

        applyVorticityConfinement(dt);
        applyBuoyancy(dt);
        projectVelocity();

        float tempDamp = std::clamp(1.0f - cooling_ * dt, 0.0f, 1.0f);
        float smokeDamp = std::clamp(1.0f - smokeDissipation_ * dt, 0.0f, 1.0f);
        advect(temp_, u_, v_, tempTmp_, dt, tempDamp, true);
        advect(smoke_, u_, v_, smokeTmp_, dt, smokeDamp, true);
        advect(age_, u_, v_, ageTmp_, dt, 1.0f, true);
        temp_.swap(tempTmp_);
        smoke_.swap(smokeTmp_);
        age_.swap(ageTmp_);

        ageField(dt);
        applyAloftCooling(dt);
        applyDiffusion(temp_, tempTmp_, 0.02f * dt, true);
        applyDiffusion(smoke_, smokeTmp_, 0.012f * dt, true);

        clampScalars();
    }

    static void flamePalette(float t, float& r, float& g, float& b) {
        t = clamp01(t);
        if (t < 0.25f) {
            float k = t / 0.25f;
            r = 0.7f * k;
            g = 0.1f * k;
            b = 0.0f;
        } else if (t < 0.55f) {
            float k = (t - 0.25f) / 0.30f;
            r = 0.7f + 0.3f * k;
            g = 0.1f + 0.5f * k;
            b = 0.02f * k;
        } else if (t < 0.82f) {
            float k = (t - 0.55f) / 0.27f;
            r = 1.0f;
            g = 0.6f + 0.35f * k;
            b = 0.02f + 0.18f * k;
        } else {
            float k = (t - 0.82f) / 0.18f;
            r = 1.0f;
            g = 0.95f + 0.05f * k;
            b = 0.2f + 0.8f * k;
        }
    }

public:
    std::string getName() const override { return "flame"; }
    std::string getDescription() const override {
        return "Authentic flame and smoke using 2D fluid dynamics on a configurable simulation grid";
    }

    void printConfig(std::ostream& os) const override {
        const char* burner = (burnerMode_ == 0) ? "gaussian" : (burnerMode_ == 1 ? "tiki" : "hybrid");
        os << "burner: " << burner << "\n";
        os << "sim: " << simWidth_ << "x" << simHeight_ << ", substeps=" << substeps_
           << ", pressure_iters=" << pressureIters_ << ", diffusion_iters=" << diffusionIters_
           << ", threads=" << threadsOpt_ << "\n";
        os << "sim_multiplier=" << simMultiplier_ << "\n";
        os << "sim_padding: left=" << simPadLeft_ << ", right=" << simPadRight_
           << ", top=" << simPadTop_ << ", bottom=" << simPadBottom_ << "\n";
        os << "sources: ";
        if (sourcePoints_.empty()) {
            os << "[" << sourceX_ << "," << sourceY_ << ",1.0]";
        } else {
            for (size_t i = 0; i < sourcePoints_.size(); ++i) {
                if (i) os << ";";
                os << "[" << sourcePoints_[i].x << "," << sourcePoints_[i].y << "," << sourcePoints_[i].scale << "]";
            }
        }
        os << "\n";
        os << "source_width=" << sourceWidth_ << ", source_height=" << sourceHeight_
           << ", source_spread=" << sourceSpread_ << ", source_heat=" << sourceHeat_
           << ", source_smoke=" << sourceSmoke_ << ", source_updraft=" << sourceUpdraft_
           << ", turbulence=" << turbulence_ << "\n";
        os << "wobble=" << wobble_ << ", flicker=" << flicker_ << ", crosswind=" << crosswind_
           << ", initial_air=" << initialAir_ << "\n";
        os << "buoyancy=" << buoyancy_ << ", cooling=" << cooling_ << ", cooling_aloft=" << coolingAloftBoost_
           << ", smoke_dissipation=" << smokeDissipation_ << ", velocity_damping=" << velocityDamping_
           << ", vorticity=" << vorticity_ << "\n";
        os << "flame_intensity=" << flameIntensity_ << ", flame_cutoff=" << flameCutoff_
           << ", flame_sharpness=" << flameSharpness_ << "\n";
        os << "smokiness=" << smokiness_ << ", smoke_intensity=" << smokeIntensity_
           << ", smoke_darkness=" << smokeDarkness_ << "\n";
        os << "age_rate=" << ageRate_ << ", age_cooling=" << ageCooling_
           << ", age_power=" << agePower_ << ", age_taper=" << ageTaper_ << "\n";
    }

    std::vector<EffectOption> getOptions() const override {
        using Opt = EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--sim-multiplier", "float", 0.25, 16.0, true, "Simulation size divisor after padding expansion (output*(1+padding)/multiplier)", "2.0"});
        opts.push_back({"--sim-pad-left", "float", 0.0, 4.0, true, "Extra simulation width left of visible frame (in visible-frame widths)", "0.0"});
        opts.push_back({"--sim-pad-right", "float", 0.0, 4.0, true, "Extra simulation width right of visible frame (in visible-frame widths)", "0.0"});
        opts.push_back({"--sim-pad-top", "float", 0.0, 4.0, true, "Extra simulation height above visible frame (in visible-frame heights)", "0.0"});
        opts.push_back({"--sim-pad-bottom", "float", 0.0, 4.0, true, "Extra simulation height below visible frame (in visible-frame heights)", "0.0"});
        opts.push_back({"--threads", "int", 0, 128, true, "Thread count for simulation passes (0 = auto)", "0"});
        opts.push_back({"--substeps", "int", 1, 8, true, "Simulation substeps per output frame", "2"});
        opts.push_back({"--pressure-iters", "int", 4, 160, true, "Pressure solver iterations", "12"});
        opts.push_back({"--diffusion-iters", "int", 0, 8, true, "Scalar diffusion iterations", "1"});
        opts.push_back({"--timescale", "float", 0.1, 5.0, true, "Simulation speed multiplier", "1.0"});
        opts.push_back({"--preset", "string", 0, 0, false, "Preset look: smallcandle, candle, campfire, bonfire, smoketrail", ""});
        opts.push_back({"--source-x", "float", -10.0, 10.0, true, "Burner X in visible-frame normalized coords (0..1 onscreen; <0/>1 offscreen)", "0.5"});
        opts.push_back({"--source-y", "float", -10.0, 10.0, true, "Burner Y in visible-frame normalized coords (0..1 onscreen; <0/>1 offscreen)", "0.97"});
        opts.push_back({"--sources", "string", 0, 0, false, "Multiple burner points as 'x1,y1,s1;x2,y2,s2;...' (scale s optional, default 1.0)", ""});
        opts.push_back({"--burner", "string", 0, 0, false, "Burner model: gaussian, tiki, or hybrid", "tiki"});
        opts.push_back({"--source-width", "float", 0.01, 1.0, true, "Base burner width as fraction of sim width", "0.02"});
        opts.push_back({"--source-height", "float", 0.01, 1.0, true, "Source region height as fraction of sim height", "0.12"});
        opts.push_back({"--source-spread", "float", 0.2, 4.0, true, "How quickly the flame widens above the base", "1.75"});
        opts.push_back({"--source-heat", "float", 0.0, 20.0, true, "Heat injection strength", "3.2"});
        opts.push_back({"--source-smoke", "float", 0.0, 10.0, true, "Smoke injection strength", "1.1"});
        opts.push_back({"--source-updraft", "float", 0.0, 300.0, true, "Initial upward velocity impulse", "200.0"});
        opts.push_back({"--turbulence", "float", 0.0, 120.0, true, "Lateral jitter from source turbulence", "18.0"});
        opts.push_back({"--wobble", "float", 0.0, 3.0, true, "Base side-to-side source wobble / airflow jitter", "0.1"});
        opts.push_back({"--flicker", "float", 0.0, 1.5, true, "Heat flicker amount (random drop/rebuild cycles)", "0.75"});
        opts.push_back({"--crosswind", "float", 0.0, 80.0, true, "Ambient lateral air motion strength", "6.0"});
        opts.push_back({"--initial-air", "float", 0.0, 80.0, true, "Initial random airflow strength", "40.0"});
        opts.push_back({"--buoyancy", "float", 0.0, 300.0, true, "Buoyancy from temperature", "220.0"});
        opts.push_back({"--cooling", "float", 0.0, 3.0, true, "Temperature cooling rate", "0.45"});
        opts.push_back({"--cooling-aloft", "float", 0.0, 4.0, true, "Extra cooling toward the top of the frame", "0.5"});
        opts.push_back({"--smoke-dissipation", "float", 0.0, 3.0, true, "Smoke dissipation rate", "0.5"});
        opts.push_back({"--velocity-damping", "float", 0.0, 3.0, true, "Velocity damping rate", "0.10"});
        opts.push_back({"--vorticity", "float", 0.0, 200.0, true, "Vorticity confinement strength", "75.0"});
        opts.push_back({"--flame-intensity", "float", 0.0, 5.0, true, "Brightness of flame emission", "1.25"});
        opts.push_back({"--smoke-intensity", "float", 0.0, 3.0, true, "Opacity of smoke", "0.92"});
        opts.push_back({"--flame-cutoff", "float", 0.0, 1.5, true, "Heat rolloff scale for smooth flame response (lower = easier ignition)", "0.15"});
        opts.push_back({"--flame-sharpness", "float", 0.5, 6.0, true, "Curve exponent for smooth flame response", "2.0"});
        opts.push_back({"--smokiness", "float", 0.0, 2.0, true, "Overall amount of smoke produced and rendered", "0.85"});
        opts.push_back({"--smoke-darkness", "float", 0.0, 1.0, true, "Smoke color from light gray (0) to near-black (1)", "0.1"});
        opts.push_back({"--age-rate", "float", 0.0, 8.0, true, "How fast emitted flame ages as it rises", "1.6"});
        opts.push_back({"--age-cooling", "float", 0.0, 8.0, true, "Extra cooling strength based on thermal age", "0.68"});
        opts.push_back({"--age-power", "float", 0.5, 4.0, true, "Power curve for age-based cooling/taper", "1.5"});
        opts.push_back({"--age-taper", "float", 0.0, 4.0, true, "How strongly thermal age suppresses visible flame", "1.1"});
        return opts;
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--sim-multiplier" && i + 1 < argc) { simMultiplier_ = std::atof(argv[++i]); return true; }
        if (arg == "--sim-pad-left" && i + 1 < argc) { simPadLeft_ = std::atof(argv[++i]); return true; }
        if (arg == "--sim-pad-right" && i + 1 < argc) { simPadRight_ = std::atof(argv[++i]); return true; }
        if (arg == "--sim-pad-top" && i + 1 < argc) { simPadTop_ = std::atof(argv[++i]); return true; }
        if (arg == "--sim-pad-bottom" && i + 1 < argc) { simPadBottom_ = std::atof(argv[++i]); return true; }
        if (arg == "--threads" && i + 1 < argc) { threadsOpt_ = std::atoi(argv[++i]); return true; }
        if (arg == "--substeps" && i + 1 < argc) { substeps_ = std::atoi(argv[++i]); return true; }
        if (arg == "--pressure-iters" && i + 1 < argc) { pressureIters_ = std::atoi(argv[++i]); return true; }
        if (arg == "--diffusion-iters" && i + 1 < argc) { diffusionIters_ = std::atoi(argv[++i]); return true; }
        if (arg == "--timescale" && i + 1 < argc) { timeScale_ = std::atof(argv[++i]); return true; }
        if (arg == "--preset" && i + 1 < argc) { applyPreset(argv[++i]); return true; }
        if (arg == "--source-x" && i + 1 < argc) { sourceX_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-y" && i + 1 < argc) { sourceY_ = std::atof(argv[++i]); return true; }
        if (arg == "--sources" && i + 1 < argc) { parseSourcesSpec(argv[++i]); return true; }
        if (arg == "--burner" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "gaussian") burnerMode_ = 0;
            else if (v == "tiki") burnerMode_ = 1;
            else if (v == "hybrid") burnerMode_ = 2;
            return true;
        }
        if (arg == "--source-width" && i + 1 < argc) { sourceWidth_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-height" && i + 1 < argc) { sourceHeight_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-spread" && i + 1 < argc) { sourceSpread_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-heat" && i + 1 < argc) { sourceHeat_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-smoke" && i + 1 < argc) { sourceSmoke_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-updraft" && i + 1 < argc) { sourceUpdraft_ = std::atof(argv[++i]); return true; }
        if (arg == "--turbulence" && i + 1 < argc) { turbulence_ = std::atof(argv[++i]); return true; }
        if (arg == "--wobble" && i + 1 < argc) { wobble_ = std::atof(argv[++i]); return true; }
        if (arg == "--flicker" && i + 1 < argc) { flicker_ = std::atof(argv[++i]); return true; }
        if (arg == "--crosswind" && i + 1 < argc) { crosswind_ = std::atof(argv[++i]); return true; }
        if (arg == "--initial-air" && i + 1 < argc) { initialAir_ = std::atof(argv[++i]); return true; }
        if (arg == "--buoyancy" && i + 1 < argc) { buoyancy_ = std::atof(argv[++i]); return true; }
        if (arg == "--cooling" && i + 1 < argc) { cooling_ = std::atof(argv[++i]); return true; }
        if (arg == "--cooling-aloft" && i + 1 < argc) { coolingAloftBoost_ = std::atof(argv[++i]); return true; }
        if (arg == "--smoke-dissipation" && i + 1 < argc) { smokeDissipation_ = std::atof(argv[++i]); return true; }
        if (arg == "--velocity-damping" && i + 1 < argc) { velocityDamping_ = std::atof(argv[++i]); return true; }
        if (arg == "--vorticity" && i + 1 < argc) { vorticity_ = std::atof(argv[++i]); return true; }
        if (arg == "--flame-intensity" && i + 1 < argc) { flameIntensity_ = std::atof(argv[++i]); return true; }
        if (arg == "--smoke-intensity" && i + 1 < argc) { smokeIntensity_ = std::atof(argv[++i]); return true; }
        if (arg == "--flame-cutoff" && i + 1 < argc) { flameCutoff_ = std::atof(argv[++i]); return true; }
        if (arg == "--flame-sharpness" && i + 1 < argc) { flameSharpness_ = std::atof(argv[++i]); return true; }
        if (arg == "--smokiness" && i + 1 < argc) { smokiness_ = std::atof(argv[++i]); return true; }
        if (arg == "--smoke-darkness" && i + 1 < argc) { smokeDarkness_ = std::atof(argv[++i]); return true; }
        if (arg == "--age-rate" && i + 1 < argc) { ageRate_ = std::atof(argv[++i]); return true; }
        if (arg == "--age-cooling" && i + 1 < argc) { ageCooling_ = std::atof(argv[++i]); return true; }
        if (arg == "--age-power" && i + 1 < argc) { agePower_ = std::atof(argv[++i]); return true; }
        if (arg == "--age-taper" && i + 1 < argc) { ageTaper_ = std::atof(argv[++i]); return true; }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = std::max(1, fps);
        frameCount_ = 0;

        substeps_ = std::clamp(substeps_, 1, 8);
        pressureIters_ = std::clamp(pressureIters_, 4, 160);
        diffusionIters_ = std::clamp(diffusionIters_, 0, 8);
        simMultiplier_ = std::clamp(simMultiplier_, 0.25f, 16.0f);
        simPadLeft_ = std::clamp(simPadLeft_, 0.0f, 4.0f);
        simPadRight_ = std::clamp(simPadRight_, 0.0f, 4.0f);
        simPadTop_ = std::clamp(simPadTop_, 0.0f, 4.0f);
        simPadBottom_ = std::clamp(simPadBottom_, 0.0f, 4.0f);
        float domainW = 1.0f + simPadLeft_ + simPadRight_;
        float domainH = 1.0f + simPadTop_ + simPadBottom_;
        float simWf = ((float)width_ * domainW) / std::max(0.0001f, simMultiplier_);
        float simHf = ((float)height_ * domainH) / std::max(0.0001f, simMultiplier_);
        simWidth_ = std::clamp((int)std::lround(simWf), 64, 4096);
        simHeight_ = std::clamp((int)std::lround(simHf), 64, 4096);
        sourceX_ = std::clamp(sourceX_, -10.0f, 10.0f);
        sourceY_ = std::clamp(sourceY_, -10.0f, 10.0f);
        sourceWidth_ = std::clamp(sourceWidth_, 0.01f, 1.0f);
        sourceHeight_ = std::clamp(sourceHeight_, 0.01f, 1.0f);
        sourceSpread_ = std::clamp(sourceSpread_, 0.2f, 4.0f);
        burnerMode_ = std::clamp(burnerMode_, 0, 2);
        timeScale_ = std::clamp(timeScale_, 0.1f, 5.0f);
        for (auto& p : sourcePoints_) {
            p.x = std::clamp(p.x, -10.0f, 10.0f);
            p.y = std::clamp(p.y, -10.0f, 10.0f);
            p.scale = std::clamp(p.scale, 0.0f, 8.0f);
        }
        wobble_ = std::clamp(wobble_, 0.0f, 3.0f);
        flicker_ = std::clamp(flicker_, 0.0f, 1.5f);
        crosswind_ = std::clamp(crosswind_, 0.0f, 80.0f);
        initialAir_ = std::clamp(initialAir_, 0.0f, 80.0f);
        coolingAloftBoost_ = std::clamp(coolingAloftBoost_, 0.0f, 4.0f);
        flameCutoff_ = std::clamp(flameCutoff_, 0.0f, 1.5f);
        flameSharpness_ = std::clamp(flameSharpness_, 0.5f, 6.0f);
        smokiness_ = std::clamp(smokiness_, 0.0f, 2.0f);
        smokeDarkness_ = std::clamp(smokeDarkness_, 0.0f, 1.0f);
        ageRate_ = std::clamp(ageRate_, 0.0f, 8.0f);
        ageCooling_ = std::clamp(ageCooling_, 0.0f, 8.0f);
        agePower_ = std::clamp(agePower_, 0.5f, 4.0f);
        ageTaper_ = std::clamp(ageTaper_, 0.0f, 4.0f);

        size_t n = (size_t)simWidth_ * (size_t)simHeight_;
        u_.assign(n, 0.0f);
        v_.assign(n, 0.0f);
        uTmp_.assign(n, 0.0f);
        vTmp_.assign(n, 0.0f);
        temp_.assign(n, 0.0f);
        tempTmp_.assign(n, 0.0f);
        smoke_.assign(n, 0.0f);
        smokeTmp_.assign(n, 0.0f);
        age_.assign(n, 8.0f);
        ageTmp_.assign(n, 8.0f);
        pressure_.assign(n, 0.0f);
        pressureTmp_.assign(n, 0.0f);
        divergence_.assign(n, 0.0f);
        curl_.assign(n, 0.0f);
        seedInitialAirFlow();
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool /*hasBackground*/, float fadeMultiplier) override {
        float padX = std::max(0.0f, simPadLeft_) + std::max(0.0f, simPadRight_);
        float padY = std::max(0.0f, simPadTop_) + std::max(0.0f, simPadBottom_);
        float domainW = 1.0f + padX;
        float domainH = 1.0f + padY;

        for (int y = 0; y < height_; ++y) {
            float vy = ((float)y + 0.5f) / std::max(1, height_);
            float sy = ((vy + simPadTop_) / std::max(0.0001f, domainH)) * (simHeight_ - 1);
            for (int x = 0; x < width_; ++x) {
                float vx = ((float)x + 0.5f) / std::max(1, width_);
                float sx = ((vx + simPadLeft_) / std::max(0.0001f, domainW)) * (simWidth_ - 1);
                float t = sampleBilinear(temp_, sx, sy);
                float s = sampleBilinear(smoke_, sx, sy);
                float a = sampleBilinear(age_, sx, sy);

                // Smooth non-threshold flame visibility:
                // heat follows a soft-logistic curve and fades continuously with thermal age.
                float heatTerm = std::pow(t / (t + flameCutoff_ + 1e-4f), flameSharpness_);
                float ageFade = 1.0f / (1.0f + std::pow(std::max(0.0f, a) * ageTaper_, agePower_));
                float flame = clamp01(heatTerm * ageFade * clamp01(1.10f - s * 0.62f));
                float smoke = clamp01(s * (0.55f + 0.75f * smokiness_));

                float fr, fg, fb;
                flamePalette(clamp01(flame * 1.2f), fr, fg, fb);

                float flameAdd = flameIntensity_ * flame * fadeMultiplier;
                float smokeAlpha = smokeIntensity_ * smokiness_ * smoke * (1.0f - 0.6f * flame) * fadeMultiplier;
                smokeAlpha = clamp01(smokeAlpha);

                float heatMix = clamp01(t * 0.7f);
                float lightShade = 0.30f + 0.35f * heatMix;
                float darkShade = 0.01f + 0.10f * heatMix;
                float smokeShade = lightShade * (1.0f - smokeDarkness_) + darkShade * smokeDarkness_;
                float smokeR = smokeShade;
                float smokeG = smokeShade;
                float smokeB = smokeShade + 0.01f * (1.0f - smokeDarkness_);

                int i = (y * width_ + x) * 3;
                float dstR = frame[i + 0] / 255.0f;
                float dstG = frame[i + 1] / 255.0f;
                float dstB = frame[i + 2] / 255.0f;

                dstR = dstR * (1.0f - smokeAlpha) + smokeR * smokeAlpha;
                dstG = dstG * (1.0f - smokeAlpha) + smokeG * smokeAlpha;
                dstB = dstB * (1.0f - smokeAlpha) + smokeB * smokeAlpha;

                dstR = std::min(1.0f, dstR + fr * flameAdd);
                dstG = std::min(1.0f, dstG + fg * flameAdd);
                dstB = std::min(1.0f, dstB + fb * flameAdd);

                frame[i + 0] = (uint8_t)(dstR * 255.0f);
                frame[i + 1] = (uint8_t)(dstG * 255.0f);
                frame[i + 2] = (uint8_t)(dstB * 255.0f);
            }
        }
    }

    void update() override {
        float dt = (timeScale_ / (float)fps_) / (float)substeps_;
        for (int s = 0; s < substeps_; ++s) {
            stepSimulation(dt);
        }
        frameCount_++;
    }
};

REGISTER_EFFECT(FlameEffect, "flame", "Authentic flame and smoke with 2D fluid simulation")
