// snowflake_effect.cpp
// Snowflake effect implementation

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <iostream>

struct Snowflake {
    float x, y;
    float vx, vy;
    float radius;
    float opacity;
    float baseVx, baseVy;
    // lifetime tracking: time since spawn and timeout (seconds)
    float timeAlive;
    float timeoutSeconds;
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
    enum class ShapeMode {
        Ellipse = 0,
        Heart = 1
    };

    enum class ColorMode {
        Solid = 0,
        Pink = 1,
        Red = 2,
        Valentine = 3
    };

    int width_, height_, fps_;
    int numFlakes_;
    float avgSize_, sizeVariance_;
    float minSize_, maxSize_, sizeBias_;
    float avgMotionX_, avgMotionY_;
    float motionRandomness_;
    float softness_;
    float maxBrightness_;
    float brightnessSpeed_;
    // how long (seconds) a flake fades out after timeout
    float timeoutFadeDuration_;
    // color controls
    float baseHue_; // 0..1
    float baseSaturation_; // 0..1
    float baseValue_; // 0..1
    float hueRange_; // 0..1
    int frameCount_;
    float spinFraction_;
    float spinMinAspect_;
    int spinAxis_; // 0=random, 1=horizontal, 2=vertical, 3=off
    ShapeMode shapeMode_;
    ColorMode colorMode_;
    
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

    void rgbToHsv(float r, float g, float b, float &h, float &s, float &v) {
        float maxc = std::max(r, std::max(g, b));
        float minc = std::min(r, std::min(g, b));
        float delta = maxc - minc;
        v = maxc;
        s = (maxc <= 0.0f) ? 0.0f : (delta / maxc);
        if (delta <= 0.000001f) {
            h = 0.0f;
            return;
        }
        if (maxc == r) {
            h = (g - b) / delta;
            if (g < b) h += 6.0f;
        } else if (maxc == g) {
            h = 2.0f + (b - r) / delta;
        } else {
            h = 4.0f + (r - g) / delta;
        }
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
        if (h >= 1.0f) h -= 1.0f;
    }

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

    static float smoothstep(float edge0, float edge1, float x) {
        float t = std::clamp((x - edge0) / std::max(0.00001f, edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    
    void assignFlakeColor(Snowflake& f) {
        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
        float hue = baseHue_;
        float sat = std::clamp(baseSaturation_, 0.0f, 1.0f);
        float val = std::clamp(baseValue_, 0.0f, 1.0f);

        if (colorMode_ == ColorMode::Pink) {
            hue = 0.92f;
            sat = 0.62f;
            val = 0.95f;
        } else if (colorMode_ == ColorMode::Red) {
            hue = 0.0f;
            sat = 0.92f;
            val = 0.95f;
        } else if (colorMode_ == ColorMode::Valentine) {
            // Mix pink and red hearts.
            bool pink = (dist01(rng_) < 0.5f);
            if (pink) {
                hue = 0.92f;
                sat = 0.62f;
                val = 0.95f;
            } else {
                hue = 0.0f;
                sat = 0.92f;
                val = 0.95f;
            }
        }

        if (hueRange_ > 0.0f) {
            float halfRange = hueRange_ * 0.5f;
            std::uniform_real_distribution<float> distHueOffset(-halfRange, halfRange);
            hue = hue + distHueOffset(rng_);
            if (hue < 0.0f) hue += 1.0f;
            if (hue >= 1.0f) hue -= 1.0f;
        }

        float r, g, b;
        hsvToRgb(hue, sat, val, r, g, b);
        f.colorR = r;
        f.colorG = g;
        f.colorB = b;
    }

    void resetFlake(Snowflake& f) {
        std::uniform_real_distribution<float> distX(0, width_);
        std::normal_distribution<float> distVx(avgMotionX_, motionRandomness_);
        std::normal_distribution<float> distVy(avgMotionY_, motionRandomness_);
        // Use an exponential distribution for size so there are many more
        // small flakes and fewer large ones (closer flakes). We offset by
        // `minSize_` so the distribution produces values >= minSize_. The
        // `sizeBias_` controls the rate: lambda = sizeBias_ / avgSize_. A
        // larger `sizeBias_` makes the mean smaller (more tiny flakes).
        float lambda = sizeBias_ / std::max(0.0001f, avgSize_);
        std::exponential_distribution<float> distSizeExp(lambda);
        std::uniform_real_distribution<float> distOpacity(0.3f, maxBrightness_);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> distBrightFreq(0.2f, 1.2f);
        std::uniform_real_distribution<float> distBrightAmp(0.05f, 0.6f);
        std::uniform_real_distribution<float> distSizeFreq(0.1f, 0.8f);
        std::uniform_real_distribution<float> distSizeAmp(0.02f, 0.6f);
        
        float sampledSize = minSize_ + distSizeExp(rng_);
        // clamp to configured bounds
        float maxSize = std::max(minSize_, maxSize_);
        f.radius = std::max(minSize_, std::min(sampledSize, maxSize));
        f.y = -(f.radius + softness_ + 2);
        f.x = distX(rng_);
        f.baseVx = distVx(rng_);
        f.baseVy = distVy(rng_);
        // scale velocity exponentially with size so larger (closer) flakes move faster
        const float sizeSpeedScale = std::exp((f.radius - avgSize_) / avgSize_);
        f.baseVx *= sizeSpeedScale;
        f.baseVy *= sizeSpeedScale;
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
        bool enableSpin = (spinAxis_ != 3);
        if (shapeMode_ == ShapeMode::Heart) {
            // Heart animation is only supported for vertical-axis spin.
            enableSpin = (spinAxis_ == 2);
        }
        if (shapeMode_ == ShapeMode::Heart && spinAxis_ == 2) {
            f.spinEnabled = true;
            f.spinHorizontal = false; // vertical-axis spin narrows width
            f.sizeAmpX = 0.0f;
            f.sizeAmpY = 0.0f;
        } else if (enableSpin && (std::uniform_real_distribution<float>(0.0f,1.0f)(rng_) < spinFraction_)) {
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

        assignFlakeColor(f);
        // lifetime tracking: reset time and compute timeout based on estimated time to cross screen
        f.timeAlive = 0.0f;
        // estimate time to cross frame vertically (avoid divide by zero)
        float estVy = std::max(0.1f, std::abs(f.baseVy));
        float estTimeToCross = (height_ > 0) ? (height_ / estVy) : 6.0f;
        std::uniform_real_distribution<float> distTimeoutFactor(0.5f, 1.5f);
        f.timeoutSeconds = estTimeToCross * distTimeoutFactor(rng_);
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

    void drawHeart(std::vector<uint8_t>& frame, int cx, int cy, float rx, float ry, float opacity, float fadeMultiplier, float colR, float colG, float colB) {
        // The implicit heart curve reaches beyond unit radius:
        // max |x| ~= 1.139, max y ~= 1.236. We normalize by inverse extents so
        // requested rx/ry correspond to the *final* visible extents.
        const float heartExtentX = 1.14f;
        const float heartExtentY = 1.24f;

        float normRx = std::max(0.0001f, rx / heartExtentX);
        float normRy = std::max(0.0001f, ry / heartExtentY);
        float effectiveRx = rx + softness_;
        float effectiveRy = ry + softness_;

        int minX = std::max(0, (int)(cx - effectiveRx - 2));
        int maxX = std::min(width_ - 1, (int)(cx + effectiveRx + 2));
        int minY = std::max(0, (int)(cy - effectiveRy - 2));
        int maxY = std::min(height_ - 1, (int)(cy + effectiveRy + 2));

        float softNorm = std::max(0.001f, softness_ / std::max(rx, ry));

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;

                float nx = dx / normRx;
                // Invert Y so the point sits downward in image coordinates.
                float ny = -dy / normRy;

                float a = nx * nx + ny * ny - 1.0f;
                float F = (a * a * a) - (nx * nx * ny * ny * ny);

                float dFdx = 6.0f * nx * a * a - 2.0f * nx * ny * ny * ny;
                float dFdy = 6.0f * ny * a * a - 3.0f * nx * nx * ny * ny;
                float grad = std::sqrt(dFdx * dFdx + dFdy * dFdy) + 0.0001f;
                float signedDist = F / grad;

                float coverage = 1.0f - smoothstep(-softNorm, softNorm, signedDist);
                float alpha = std::clamp(coverage * opacity * fadeMultiplier, 0.0f, 1.0f);

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
    
public:
    SnowflakeEffect()
        : numFlakes_(150), avgSize_(3.0f), sizeVariance_(1.5f), minSize_(0.5f), maxSize_(-1.0f), sizeBias_(2.0f),
            avgMotionX_(0.5f), avgMotionY_(2.0f), motionRandomness_(1.0f),
            softness_(2.0f), maxBrightness_(1.0f), brightnessSpeed_(1.0f), timeoutFadeDuration_(0.8f), baseHue_(0.0f), baseSaturation_(0.0f), baseValue_(1.0f), hueRange_(0.0f), frameCount_(0), spinFraction_(0.55f), spinMinAspect_(0.1f), spinAxis_(0), shapeMode_(ShapeMode::Ellipse), colorMode_(ColorMode::Solid), rng_(std::random_device{}()) {}
        
    
    std::string getName() const override {
        return "snowflake";
    }
    
    std::string getDescription() const override {
        return "Realistic falling snowflakes with soft edges and natural motion";
    }

    // Provide machine-readable option metadata. This is used by the CLI
    // to export structured help (JSON) or to format textual help.
    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--flakes", "int", 1, 10000, true, "Number of snowflakes", "150"});
        opts.push_back({"--size", "float", 0.01, 50.0, true, "Average snowflake size", "3.0"});
        opts.push_back({"--size-var", "float", 0.0, 50.0, true, "Size variance", "1.5", true});
        opts.push_back({"--motion-x", "float", -50.0, 50.0, true, "Average X motion per frame", "0.5"});
        opts.push_back({"--motion-y", "float", -50.0, 50.0, true, "Average Y motion per frame", "2.0"});
        opts.push_back({"--randomness", "float", 0.0, 20.0, true, "Motion randomness", "1.0"});
        opts.push_back({"--softness", "float", 0.0, 50.0, true, "Edge softness/blur", "2.0", true});
        opts.push_back({"--brightness", "float", 0.0, 1.0, true, "Max brightness 0.0-1.0", "1.0", true});
        opts.push_back({"--pulse", "float", 0.0, 100.0, true, "Average speed of brightness pulsing (set 0 to disable)", "1.0"});
        opts.push_back({"--color", "string.color", 0, 0, false, "Base flake color", "white", false,
                        {"white", "pink", "red", "valentine"}});
        opts.push_back({"--hue-range", "float", 0.0, 1.0, true, "Hue range 0.0-1.0", "0.0", true});
        opts.push_back({"--shape", "string", 0, 0, false, "Flake shape: circle|heart", "circle", false,
                        {"circle", "heart"}});
        opts.push_back({"--spin-axis", "string", 0, 0, false, "Spin mode/axis: off|random|h|horizontal|v|vertical (heart spin only animates when set to vertical)", "random", true,
                        {"off", "none", "random", "h", "horizontal", "v", "vertical"}});
        opts.push_back({"--min-size", "float", 0.01, 10.0, true, "Minimum flake size", "0.5", true});
        opts.push_back({"--max-size", "float", 0.01, 600.0, true, "Maximum flake size (default: avgSize*6)", "", true});
        opts.push_back({"--size-bias", "float", 0.0, 100.0, true, "Bias for exponential size distribution (>0). Larger => more small flakes", "2.0", true});
        return opts;
    }
    
    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        auto parseBoolValue = [&](bool defaultWhenNoValue) -> bool {
            if (i + 1 < argc) {
                std::string v = argv[i + 1];
                if (v == "true" || v == "1" || v == "yes" || v == "on") {
                    ++i;
                    return true;
                }
                if (v == "false" || v == "0" || v == "no" || v == "off") {
                    ++i;
                    return false;
                }
            }
            return defaultWhenNoValue;
        };
        
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
        } else if (arg == "--color" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "white") {
                colorMode_ = ColorMode::Solid;
                baseHue_ = 0.0f;
                baseSaturation_ = 0.0f;
                baseValue_ = 1.0f;
            } else if (v == "pink") {
                colorMode_ = ColorMode::Pink;
            } else if (v == "red") {
                colorMode_ = ColorMode::Red;
            } else if (v == "valentine") {
                colorMode_ = ColorMode::Valentine;
            } else {
                float r = 1.0f, g = 1.0f, b = 1.0f;
                if (!parseHexColor(v, r, g, b)) {
                    std::cerr << "Invalid --color '" << v << "'. Use white|pink|red|valentine|#RRGGBB\n";
                    return false;
                }
                colorMode_ = ColorMode::Solid;
                rgbToHsv(r, g, b, baseHue_, baseSaturation_, baseValue_);
            }
            return true;
        } else if (arg == "--hue-range" && i + 1 < argc) {
            hueRange_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--shape" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "heart") {
                shapeMode_ = ShapeMode::Heart;
            } else {
                shapeMode_ = ShapeMode::Ellipse;
            }
            return true;
        } else if (arg == "--hue" && i + 1 < argc) {
            // Backward-compatible alias
            colorMode_ = ColorMode::Solid;
            baseHue_ = std::clamp((float)std::atof(argv[++i]), 0.0f, 1.0f);
            return true;
        } else if (arg == "--saturation" && i + 1 < argc) {
            // Backward-compatible alias
            colorMode_ = ColorMode::Solid;
            baseSaturation_ = std::clamp((float)std::atof(argv[++i]), 0.0f, 1.0f);
            return true;
        } else if (arg == "--color-mode" && i + 1 < argc) {
            // Backward-compatible alias mapping to --color presets
            std::string v = argv[++i];
            if (v == "pink") colorMode_ = ColorMode::Pink;
            else if (v == "red") colorMode_ = ColorMode::Red;
            else if (v == "valentine") colorMode_ = ColorMode::Valentine;
            else colorMode_ = ColorMode::Solid;
            return true;
        } else if (arg == "--heart-spin") {
            // Backwards-compatible alias:
            // true => vertical spin mode, false => spin off.
            spinAxis_ = parseBoolValue(true) ? 2 : 3;
            return true;
        } else if (arg == "--no-spin") {
            // Backwards-compatible alias:
            // true => spin off, false => random axis.
            bool disableSpin = parseBoolValue(true);
            spinAxis_ = disableSpin ? 3 : 0;
            return true;
        } else if (arg == "--spin-axis" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "h" || v == "horizontal") spinAxis_ = 1;
            else if (v == "v" || v == "vertical") spinAxis_ = 2;
            else if (v == "off" || v == "none") spinAxis_ = 3;
            else spinAxis_ = 0;
            return true;
        }
        else if (arg == "--min-size" && i + 1 < argc) {
            minSize_ = std::atof(argv[++i]);
            if (minSize_ < 0.01f) minSize_ = 0.01f;
            return true;
        } else if (arg == "--max-size" && i + 1 < argc) {
            maxSize_ = std::atof(argv[++i]);
            if (maxSize_ < minSize_) maxSize_ = minSize_;
            return true;
        } else if (arg == "--size-bias" && i + 1 < argc) {
            sizeBias_ = std::atof(argv[++i]);
            if (sizeBias_ <= 0.0f) sizeBias_ = 1.0f;
            return true;
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        if(maxSize_ < 0.0f) {
            maxSize_ = avgSize_ * 6.0f;
        }
        
        std::uniform_real_distribution<float> distX(0, width);
        std::uniform_real_distribution<float> distY(0, height);
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

        for (auto& f : flakes_) {
            // brightness pulse (per-flake random phase/frequency/amplitude)
            float tBright = time * f.brightnessFreq * TWO_PI + f.brightnessPhase;
            float brightFactor = 1.0f + f.brightnessAmp * std::sin(tBright);
            float opacity = std::clamp(f.opacity * brightFactor, 0.0f, 1.0f);

            // size/shape pulse (separate time/phase)
            float tSize = time * f.sizeFreq * TWO_PI + f.sizePhase;
            float rx, ry;
            if (shapeMode_ == ShapeMode::Heart) {
                ry = std::max(0.5f, f.radius);
                if (f.spinEnabled) {
                    float s = std::sin(tSize);
                    float v = (s >= 0.0f) ? std::sqrt(s) : -std::sqrt(-s);
                    float mag = std::abs(v);
                    float absAspect = spinMinAspect_ + (1.0f - spinMinAspect_) * mag;
                    rx = std::max(0.05f, f.radius * absAspect);
                } else {
                    rx = std::max(0.5f, f.radius);
                }
            } else if (f.spinEnabled) {
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

            // per-flake fade after timeout: if the flake has exceeded its timeout and
            // hasn't yet left the screen, start fading it out over `timeoutFadeDuration_` seconds.
            float perFlakeFade = 1.0f;
            if (f.timeAlive >= f.timeoutSeconds && f.y <= height_ + f.radius + softness_) {
                float fadeProgress = (f.timeAlive - f.timeoutSeconds) / std::max(0.0001f, timeoutFadeDuration_);
                if (fadeProgress >= 1.0f) {
                    // fully faded: respawn the flake instead of drawing
                    resetFlake(f);
                    continue;
                }
                perFlakeFade = 1.0f - fadeProgress;
            }

            if (shapeMode_ == ShapeMode::Heart) {
                drawHeart(frame, (int)f.x, (int)f.y, rx, ry, opacity, fadeMultiplier * perFlakeFade, f.colorR, f.colorG, f.colorB);
            } else {
                drawEllipse(frame, (int)f.x, (int)f.y, rx, ry, opacity, fadeMultiplier * perFlakeFade, f.colorR, f.colorG, f.colorB);
            }
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
            // advance per-flake lifetime (avoid divide by zero fps)
            if (fps_ > 0) f.timeAlive += 1.0f / (float)fps_;
            
            if (f.y > height_ + f.radius + softness_ || 
                f.y < -(f.radius + softness_) ||
                f.x < -(f.radius + softness_) || 
                f.x > width_ + f.radius + softness_) {
                resetFlake(f);
            }
            // If a flake has exceeded its timeout plus fade duration, respawn it
            if (f.timeAlive >= f.timeoutSeconds + timeoutFadeDuration_) {
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
