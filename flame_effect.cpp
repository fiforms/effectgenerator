// flame_effect.cpp
// 2D flame and smoke fluid simulation (CPU)

#include "effect_generator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

class FlameEffect : public Effect {
private:
    int width_ = 0;
    int height_ = 0;
    int fps_ = 30;
    int frameCount_ = 0;

    int simWidthOpt_ = 480;
    int simHeightOpt_ = 270;
    int simWidth_ = 0;
    int simHeight_ = 0;

    int substeps_ = 2;
    int pressureIters_ = 30;
    int diffusionIters_ = 1;
    int threadsOpt_ = 0; // 0 = auto

    float timeScale_ = 1.0f;
    float sourceX_ = 0.5f;       // normalized
    float sourceWidth_ = 0.22f;  // normalized
    float sourceHeat_ = 3.2f;
    float sourceSmoke_ = 1.1f;
    float sourceUpdraft_ = 85.0f;
    float turbulence_ = 18.0f;

    float buoyancy_ = 48.0f;
    float cooling_ = 0.26f;
    float smokeDissipation_ = 0.08f;
    float velocityDamping_ = 0.10f;
    float vorticity_ = 24.0f;

    float flameIntensity_ = 1.25f;
    float smokeIntensity_ = 0.92f;

    std::vector<float> u_;
    std::vector<float> v_;
    std::vector<float> uTmp_;
    std::vector<float> vTmp_;
    std::vector<float> temp_;
    std::vector<float> tempTmp_;
    std::vector<float> smoke_;
    std::vector<float> smokeTmp_;
    std::vector<float> pressure_;
    std::vector<float> pressureTmp_;
    std::vector<float> divergence_;
    std::vector<float> curl_;

    inline int idx(int x, int y) const { return y * simWidth_ + x; }

    static float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

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

    void addSources(float dt) {
        float cx = sourceX_ * (simWidth_ - 1);
        float halfW = std::max(1.0f, sourceWidth_ * simWidth_ * 0.5f);
        int minX = std::max(1, (int)std::floor(cx - halfW - 2.0f));
        int maxX = std::min(simWidth_ - 2, (int)std::ceil(cx + halfW + 2.0f));

        int yStart = std::max(1, simHeight_ - 8);
        int yEnd = simHeight_ - 2;
        int phase = frameCount_;

        for (int y = yStart; y <= yEnd; ++y) {
            float fy = (float)(yEnd - y) / std::max(1, yEnd - yStart);
            float yWeight = 0.55f + 0.45f * (1.0f - fy);
            for (int x = minX; x <= maxX; ++x) {
                float dx = std::fabs((float)x - cx);
                float xWeight = 1.0f - dx / (halfW + 1e-4f);
                if (xWeight <= 0.0f) continue;
                xWeight = xWeight * xWeight;

                float n = hash3(x, y, phase);
                float pulse = 0.86f + 0.14f * std::sin(0.19f * (float)phase + (float)x * 0.07f);
                float shape = xWeight * yWeight * pulse;

                int i = idx(x, y);
                temp_[i] += sourceHeat_ * shape * dt;
                smoke_[i] += sourceSmoke_ * (0.7f + 0.3f * n) * shape * dt;
                v_[i] -= sourceUpdraft_ * shape * dt;
                u_[i] += ((n - 0.5f) * 2.0f) * turbulence_ * shape * dt;
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

    void stepSimulation(float dt) {
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
        temp_.swap(tempTmp_);
        smoke_.swap(smokeTmp_);

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

    std::vector<EffectOption> getOptions() const override {
        using Opt = EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--sim-width", "int", 64, 4096, true, "Fluid simulation width (rendered via upscale/downscale)", "480"});
        opts.push_back({"--sim-height", "int", 64, 4096, true, "Fluid simulation height (rendered via upscale/downscale)", "270"});
        opts.push_back({"--threads", "int", 0, 128, true, "Thread count for simulation passes (0 = auto)", "0"});
        opts.push_back({"--substeps", "int", 1, 8, true, "Simulation substeps per output frame", "2"});
        opts.push_back({"--pressure-iters", "int", 4, 160, true, "Pressure solver iterations", "30"});
        opts.push_back({"--diffusion-iters", "int", 0, 8, true, "Scalar diffusion iterations", "1"});
        opts.push_back({"--timescale", "float", 0.1, 5.0, true, "Simulation speed multiplier", "1.0"});
        opts.push_back({"--source-x", "float", 0.0, 1.0, true, "Burner X position in normalized coordinates", "0.5"});
        opts.push_back({"--source-width", "float", 0.02, 1.0, true, "Burner width as fraction of sim width", "0.22"});
        opts.push_back({"--source-heat", "float", 0.0, 20.0, true, "Heat injection strength", "3.2"});
        opts.push_back({"--source-smoke", "float", 0.0, 10.0, true, "Smoke injection strength", "1.1"});
        opts.push_back({"--source-updraft", "float", 0.0, 300.0, true, "Initial upward velocity impulse", "85.0"});
        opts.push_back({"--turbulence", "float", 0.0, 120.0, true, "Lateral jitter from source turbulence", "18.0"});
        opts.push_back({"--buoyancy", "float", 0.0, 200.0, true, "Buoyancy from temperature", "48.0"});
        opts.push_back({"--cooling", "float", 0.0, 3.0, true, "Temperature cooling rate", "0.26"});
        opts.push_back({"--smoke-dissipation", "float", 0.0, 3.0, true, "Smoke dissipation rate", "0.08"});
        opts.push_back({"--velocity-damping", "float", 0.0, 3.0, true, "Velocity damping rate", "0.10"});
        opts.push_back({"--vorticity", "float", 0.0, 100.0, true, "Vorticity confinement strength", "24.0"});
        opts.push_back({"--flame-intensity", "float", 0.0, 5.0, true, "Brightness of flame emission", "1.25"});
        opts.push_back({"--smoke-intensity", "float", 0.0, 3.0, true, "Opacity of smoke", "0.92"});
        return opts;
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--sim-width" && i + 1 < argc) { simWidthOpt_ = std::atoi(argv[++i]); return true; }
        if (arg == "--sim-height" && i + 1 < argc) { simHeightOpt_ = std::atoi(argv[++i]); return true; }
        if (arg == "--threads" && i + 1 < argc) { threadsOpt_ = std::atoi(argv[++i]); return true; }
        if (arg == "--substeps" && i + 1 < argc) { substeps_ = std::atoi(argv[++i]); return true; }
        if (arg == "--pressure-iters" && i + 1 < argc) { pressureIters_ = std::atoi(argv[++i]); return true; }
        if (arg == "--diffusion-iters" && i + 1 < argc) { diffusionIters_ = std::atoi(argv[++i]); return true; }
        if (arg == "--timescale" && i + 1 < argc) { timeScale_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-x" && i + 1 < argc) { sourceX_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-width" && i + 1 < argc) { sourceWidth_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-heat" && i + 1 < argc) { sourceHeat_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-smoke" && i + 1 < argc) { sourceSmoke_ = std::atof(argv[++i]); return true; }
        if (arg == "--source-updraft" && i + 1 < argc) { sourceUpdraft_ = std::atof(argv[++i]); return true; }
        if (arg == "--turbulence" && i + 1 < argc) { turbulence_ = std::atof(argv[++i]); return true; }
        if (arg == "--buoyancy" && i + 1 < argc) { buoyancy_ = std::atof(argv[++i]); return true; }
        if (arg == "--cooling" && i + 1 < argc) { cooling_ = std::atof(argv[++i]); return true; }
        if (arg == "--smoke-dissipation" && i + 1 < argc) { smokeDissipation_ = std::atof(argv[++i]); return true; }
        if (arg == "--velocity-damping" && i + 1 < argc) { velocityDamping_ = std::atof(argv[++i]); return true; }
        if (arg == "--vorticity" && i + 1 < argc) { vorticity_ = std::atof(argv[++i]); return true; }
        if (arg == "--flame-intensity" && i + 1 < argc) { flameIntensity_ = std::atof(argv[++i]); return true; }
        if (arg == "--smoke-intensity" && i + 1 < argc) { smokeIntensity_ = std::atof(argv[++i]); return true; }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = std::max(1, fps);
        frameCount_ = 0;

        simWidth_ = std::clamp(simWidthOpt_, 64, 4096);
        simHeight_ = std::clamp(simHeightOpt_, 64, 4096);

        substeps_ = std::clamp(substeps_, 1, 8);
        pressureIters_ = std::clamp(pressureIters_, 4, 160);
        diffusionIters_ = std::clamp(diffusionIters_, 0, 8);
        sourceX_ = std::clamp(sourceX_, 0.0f, 1.0f);
        sourceWidth_ = std::clamp(sourceWidth_, 0.02f, 1.0f);
        timeScale_ = std::clamp(timeScale_, 0.1f, 5.0f);

        size_t n = (size_t)simWidth_ * (size_t)simHeight_;
        u_.assign(n, 0.0f);
        v_.assign(n, 0.0f);
        uTmp_.assign(n, 0.0f);
        vTmp_.assign(n, 0.0f);
        temp_.assign(n, 0.0f);
        tempTmp_.assign(n, 0.0f);
        smoke_.assign(n, 0.0f);
        smokeTmp_.assign(n, 0.0f);
        pressure_.assign(n, 0.0f);
        pressureTmp_.assign(n, 0.0f);
        divergence_.assign(n, 0.0f);
        curl_.assign(n, 0.0f);
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool /*hasBackground*/, float fadeMultiplier) override {
        float sxScale = (float)simWidth_ / std::max(1, width_);
        float syScale = (float)simHeight_ / std::max(1, height_);

        for (int y = 0; y < height_; ++y) {
            float sy = ((float)y + 0.5f) * syScale - 0.5f;
            for (int x = 0; x < width_; ++x) {
                float sx = ((float)x + 0.5f) * sxScale - 0.5f;
                float t = sampleBilinear(temp_, sx, sy);
                float s = sampleBilinear(smoke_, sx, sy);

                float flame = clamp01((t - 0.08f) * 1.45f) * clamp01(1.15f - s * 0.55f);
                flame = flame * flame;
                float smoke = clamp01(s * 0.95f);

                float fr, fg, fb;
                flamePalette(clamp01(flame * 1.2f), fr, fg, fb);

                float flameAdd = flameIntensity_ * flame * fadeMultiplier;
                float smokeAlpha = smokeIntensity_ * smoke * (1.0f - 0.6f * flame) * fadeMultiplier;
                smokeAlpha = clamp01(smokeAlpha);

                float smokeShade = std::clamp(0.10f + 0.25f * clamp01(t * 0.7f), 0.0f, 1.0f);
                float smokeR = smokeShade;
                float smokeG = smokeShade;
                float smokeB = smokeShade + 0.01f;

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

