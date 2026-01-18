// mist_effect.cpp
// Soft mist/smoke drift effect

#include "effect_generator.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <iostream>

class MistEffect : public Effect {
private:
    int width_, height_, fps_;
    int frameCount_;

    float opacity_;
    float scale_;
    float speedX_;
    float speedY_;
    float threshold_;
    float warpScale_;
    float warpStrength_;
    float warpSpeed_;
    float heightBias_;
    float tint_;

    float mistR_;
    float mistG_;
    float mistB_;

    static float smoothstep(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static float hash2(int x, int y) {
        int n = x * 374761393 + y * 668265263;
        n = (n ^ (n >> 13)) * 1274126177;
        n = n ^ (n >> 16);
        return (n & 0x7fffffff) / 2147483647.0f;
    }

    float valueNoise(float x, float y) const {
        int xi = (int)std::floor(x);
        int yi = (int)std::floor(y);
        float xf = x - xi;
        float yf = y - yi;

        float v00 = hash2(xi, yi);
        float v10 = hash2(xi + 1, yi);
        float v01 = hash2(xi, yi + 1);
        float v11 = hash2(xi + 1, yi + 1);

        float u = smoothstep(xf);
        float v = smoothstep(yf);

        float x1 = lerp(v00, v10, u);
        float x2 = lerp(v01, v11, u);
        return lerp(x1, x2, v);
    }

    float fbm(float x, float y) const {
        float sum = 0.0f;
        float amp = 0.5f;
        float freq = 1.0f;
        float norm = 0.0f;
        for (int i = 0; i < 3; i++) {
            sum += valueNoise(x * freq, y * freq) * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / std::max(0.0001f, norm);
    }

public:
    MistEffect()
        : width_(0), height_(0), fps_(30), frameCount_(0),
          opacity_(0.7f), scale_(0.002f), speedX_(0.15f), speedY_(0.08f),
          threshold_(0.55f), warpScale_(0.0015f), warpStrength_(0.35f), warpSpeed_(0.005f),
          heightBias_(0.25f), tint_(0.0f),
          mistR_(0.92f), mistG_(0.94f), mistB_(0.96f) {}

    std::string getName() const override { return "mist"; }
    std::string getDescription() const override { return "Soft mist/smoke drift using layered noise"; }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--opacity", "float", 0.0, 2.0, true, "Mist opacity multiplier", "0.7"});
        opts.push_back({"--scale", "float", 0.0001, 0.05, true, "Noise scale (lower is larger features)", "0.002"});
        opts.push_back({"--speed-x", "float", -1.0, 1.0, true, "Horizontal drift speed in noise units/sec", "0.15"});
        opts.push_back({"--speed-y", "float", -1.0, 1.0, true, "Vertical drift speed in noise units/sec", "0.08"});
        opts.push_back({"--threshold", "float", 0.0, 0.99, true, "Threshold for mist coverage", "0.55"});
        opts.push_back({"--warp-scale", "float", 0.0001, 0.05, true, "Scale of the warp field", "0.0015"});
        opts.push_back({"--warp-strength", "float", 0.0, 2.0, true, "Warp strength in noise units", "0.35"});
        opts.push_back({"--warp-speed", "float", -1.0, 1.0, true, "Warp drift speed in noise units/sec", "0.005"});
        opts.push_back({"--height-bias", "float", 0.0, 1.0, true, "Bias density toward the bottom (0..1)", "0.25"});
        opts.push_back({"--tint", "float", -1.0, 1.0, true, "Tint (-1 cool, 0 neutral, 1 warm)", "0.0"});
        return opts;
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--opacity" && i + 1 < argc) {
            opacity_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--scale" && i + 1 < argc) {
            scale_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--speed-x" && i + 1 < argc) {
            speedX_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--speed-y" && i + 1 < argc) {
            speedY_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--threshold" && i + 1 < argc) {
            threshold_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--warp-scale" && i + 1 < argc) {
            warpScale_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--warp-strength" && i + 1 < argc) {
            warpStrength_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--warp-speed" && i + 1 < argc) {
            warpSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--height-bias" && i + 1 < argc) {
            heightBias_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--tint" && i + 1 < argc) {
            tint_ = std::atof(argv[++i]);
            return true;
        }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        frameCount_ = 0;

        tint_ = std::clamp(tint_, -1.0f, 1.0f);
        float baseR = 0.92f;
        float baseG = 0.94f;
        float baseB = 0.96f;
        mistR_ = std::clamp(baseR + tint_ * 0.06f, 0.0f, 1.0f);
        mistG_ = std::clamp(baseG + tint_ * 0.02f, 0.0f, 1.0f);
        mistB_ = std::clamp(baseB - tint_ * 0.06f, 0.0f, 1.0f);
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool /*hasBackground*/, float fadeMultiplier) override {
        float t = (fps_ > 0) ? (frameCount_ / (float)fps_) : 0.0f;
        float invHeight = (height_ > 1) ? (1.0f / (height_ - 1)) : 0.0f;

        for (int y = 0; y < height_; y++) {
            float ny = y * scale_;
            float wy = y * warpScale_;
            float pos = y * invHeight;
            float heightFactor = (1.0f - heightBias_) + heightBias_ * pos;

            for (int x = 0; x < width_; x++) {
                float nx = x * scale_;
                float wx = x * warpScale_;

                float warpX = (valueNoise(wx + t * warpSpeed_, wy + t * warpSpeed_) - 0.5f) * warpStrength_;
                float warpY = (valueNoise(wx + 17.1f + t * warpSpeed_, wy + 43.2f + t * warpSpeed_) - 0.5f) * warpStrength_;

                float noiseVal = fbm(nx + warpX + t * speedX_, ny + warpY + t * speedY_);
                float a = (noiseVal - threshold_) / std::max(0.0001f, 1.0f - threshold_);
                a = smoothstep(a);
                a *= opacity_ * heightFactor * fadeMultiplier;

                if (a <= 0.0005f) {
                    continue;
                }

                int idx = (y * width_ + x) * 3;
                float dstR = frame[idx + 0] / 255.0f;
                float dstG = frame[idx + 1] / 255.0f;
                float dstB = frame[idx + 2] / 255.0f;

                float outR = 1.0f - (1.0f - dstR) * (1.0f - mistR_ * a);
                float outG = 1.0f - (1.0f - dstG) * (1.0f - mistG_ * a);
                float outB = 1.0f - (1.0f - dstB) * (1.0f - mistB_ * a);

                frame[idx + 0] = (uint8_t)(std::clamp(outR, 0.0f, 1.0f) * 255.0f);
                frame[idx + 1] = (uint8_t)(std::clamp(outG, 0.0f, 1.0f) * 255.0f);
                frame[idx + 2] = (uint8_t)(std::clamp(outB, 0.0f, 1.0f) * 255.0f);
            }
        }
    }

    void update() override {
        frameCount_++;
    }
};

REGISTER_EFFECT(MistEffect, "mist", "Soft mist/smoke drift using layered noise")
