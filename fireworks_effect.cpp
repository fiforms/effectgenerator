// fireworks_effect.cpp
// Fireworks effect implementation

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

struct Spark {
    float x, y;
    float vx, vy;
    float life;      // 0.0 to 1.0
    float decay;     // how fast it fades
    float r, g, b;   // color
    float size;
    bool active;
};

struct Rocket {
    float x, y;
    float vx, vy;
    float targetY;
    float r, g, b;   // explosion color
    bool active;
    bool exploded;
    float trailLife;
};

class FireworksEffect : public Effect {
private:
    int width_, height_, fps_;
    int frameCount_;
    float gravity_;
    int maxRockets_;
    int sparksPerRocket_;
    float launchFrequency_;  // rockets per second
    float sparkSpeed_;
    float sparkSize_;
    float trailIntensity_;
    float horizontalDrift_;
    int sparksVariance_;
    float launchRandomness_;

    // For Groundfire-like effect
    bool groundFireEnabled_;
    float groundFireRate_;          // bursts per second
    int groundFireSparks_;          // sparks per burst
    float groundFireSpread_;        // radians, e.g. PI/3
    float groundR_, groundG_, groundB_;

    
    
    std::vector<Rocket> rockets_;
    std::vector<Spark> sparks_;
    std::mt19937 rng_;
    
    float nextLaunchTime_;
    float nextGroundFireTime_;
    
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
    
    void launchRocket() {
        // Find inactive rocket slot
        Rocket* r = nullptr;
        for (auto& rocket : rockets_) {
            if (!rocket.active) {
                r = &rocket;
                break;
            }
        }
        if (!r) return; // all slots full
        
        std::uniform_real_distribution<float> distX(width_ * 0.2f, width_ * 0.8f);
        std::uniform_real_distribution<float> distTargetY(height_ * 0.2f, height_ * 0.6f);
        std::uniform_real_distribution<float> distHue(0.0f, 1.0f);
        std::uniform_real_distribution<float> distVx(-horizontalDrift_, horizontalDrift_);
        
        r->x = distX(rng_);
        r->y = height_;
        r->targetY = distTargetY(rng_);
        r->vx = distVx(rng_);
        
        // Calculate velocity needed to reach target height
        // Using simpler physics: v = sqrt(2 * g * h) where h is distance to travel
        float distance = r->y - r->targetY;
        r->vy = -std::sqrt(2.0f * gravity_ * distance);
        
        // Random color
        float hue = distHue(rng_);
        hsvToRgb(hue, 0.9f, 1.0f, r->r, r->g, r->b);
        
        r->active = true;
        r->exploded = false;
        r->trailLife = 1.0f;
    }
    
    void explodeRocket(Rocket& r) {
        std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> distSpeed(0.5f, 1.5f);
        std::uniform_real_distribution<float> distDecay(0.008f, 0.02f);
        std::uniform_real_distribution<float> distSize(0.5f, 1.5f);
        
        // Randomize number of sparks for this explosion
        int sparkVariance = (int)(sparksVariance_ * 0.5f);
        std::uniform_int_distribution<int> distSparkCount(
            std::max(10, sparksPerRocket_ - sparkVariance),
            sparksPerRocket_ + sparkVariance
        );
        int sparksToCreate = distSparkCount(rng_);
        
        // Create burst of sparks
        int sparksCreated = 0;
        for (auto& spark : sparks_) {
            if (!spark.active && sparksCreated < sparksToCreate) {
                float angle = distAngle(rng_);
                float speed = distSpeed(rng_) * sparkSpeed_;
                
                spark.x = r.x;
                spark.y = r.y;
                spark.vx = std::cos(angle) * speed + r.vx * 0.3f;
                spark.vy = std::sin(angle) * speed + r.vy * 0.3f;
                spark.life = 1.0f;
                spark.decay = distDecay(rng_);
                spark.r = r.r;
                spark.g = r.g;
                spark.b = r.b;
                spark.size = sparkSize_ * distSize(rng_);
                spark.active = true;
                
                sparksCreated++;
            }
        }
        
        r.exploded = true;
        r.active = false;
    }
    
    void emitGroundFire() {
        std::uniform_real_distribution<float> distX(0.1f * width_, 0.9f * width_);
        std::uniform_real_distribution<float> distAngle(
            -groundFireSpread_ * 0.5f,
            groundFireSpread_ * 0.5f
        );
        std::uniform_real_distribution<float> distSpeed(1.0f, 4.0f);
        std::uniform_real_distribution<float> distDecay(0.015f, 0.03f);
        std::uniform_real_distribution<float> distSize(0.4f, 1.2f);

        float baseX = distX(rng_);
        float baseY = height_ - 2.0f;

        int created = 0;
        for (auto& s : sparks_) {
            if (!s.active && created < groundFireSparks_) {
                float angle = -3.14159265f / 2.0f + distAngle(rng_);
                float speed = distSpeed(rng_) * sparkSpeed_;

                s.x = baseX;
                s.y = baseY;
                s.vx = std::cos(angle) * speed;
                s.vy = std::sin(angle) * speed;
                s.life = 1.0f;
                s.decay = distDecay(rng_);
                s.r = groundR_;
                s.g = groundG_;
                s.b = groundB_;
                s.size = sparkSize_ * distSize(rng_);
                s.active = true;

                created++;
            }
        }
    }

    void drawParticle(std::vector<uint8_t>& frame, float x, float y, float size, 
                      float r, float g, float b, float alpha, float fadeMultiplier) {
        int cx = (int)x;
        int cy = (int)y;
        int radius = (int)(size + 1.5f);
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                int px = cx + dx;
                int py = cy + dy;
                
                if (px < 0 || px >= width_ || py < 0 || py >= height_) continue;
                
                float dist = std::sqrt(dx * dx + dy * dy);
                float particleAlpha = 0.0f;
                
                if (dist < size) {
                    particleAlpha = 1.0f - (dist / size) * 0.3f;
                } else if (dist < size + 1.5f) {
                    float t = (dist - size) / 1.5f;
                    particleAlpha = (1.0f - t) * 0.8f;
                }
                
                particleAlpha *= alpha * fadeMultiplier;
                particleAlpha = std::clamp(particleAlpha, 0.0f, 1.0f);
                
                if (particleAlpha > 0.005f) {
                    int idx = (py * width_ + px) * 3;
                    float currentR = frame[idx + 0] / 255.0f;
                    float currentG = frame[idx + 1] / 255.0f;
                    float currentB = frame[idx + 2] / 255.0f;
                    
                    float resR = std::min(1.0f, currentR + particleAlpha * r);
                    float resG = std::min(1.0f, currentG + particleAlpha * g);
                    float resB = std::min(1.0f, currentB + particleAlpha * b);
                    
                    frame[idx + 0] = (uint8_t)(255 * resR);
                    frame[idx + 1] = (uint8_t)(255 * resG);
                    frame[idx + 2] = (uint8_t)(255 * resB);
                }
            }
        }
    }
    
public:
    FireworksEffect()
        : width_(1920), height_(1080), fps_(30), frameCount_(0),
          gravity_(0.5f), maxRockets_(10), sparksPerRocket_(100),
          sparksVariance_(50), launchFrequency_(0.5f), launchRandomness_(0.5f),
          sparkSpeed_(5.0f), sparkSize_(2.0f), trailIntensity_(0.5f),
          horizontalDrift_(2.0f), nextLaunchTime_(0.0f),
          groundFireEnabled_(false),
          groundFireRate_(5.0f),
          groundFireSparks_(80),
          groundFireSpread_(3.14159265f / 3.0f),
          groundR_(1.0f), groundG_(1.0f), groundB_(0.85f), // soft white/yellow
          rng_(std::random_device{}()) {}
    
    std::string getName() const override {
        return "fireworks";
    }
    
    std::string getDescription() const override {
        return "Colorful fireworks with rockets launching and exploding into sparks";
    }
    
    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--frequency", "float", 0.1, 5.0, true, "Rockets launched per second", "0.5"});
        opts.push_back({"--frequency-randomness", "float", 0.0, 1.0, true, "Randomness in launch timing (0=regular, 1=very random)", "0.5"});
        opts.push_back({"--sparks", "int", 10, 500, true, "Average sparks per explosion", "100"});
        opts.push_back({"--sparks-variance", "int", 0, 200, true, "Variance in spark count per explosion", "50"});
        opts.push_back({"--gravity", "float", 0.01, 1.0, true, "Gravity strength", "0.5"});
        opts.push_back({"--speed", "float", 0.5, 10.0, true, "Spark speed multiplier", "5.0"});
        opts.push_back({"--size", "float", 0.5, 10.0, true, "Spark size", "2.0"});
        opts.push_back({"--trail", "float", 0.0, 1.0, true, "Rocket trail intensity", "0.5"});
        opts.push_back({"--drift", "float", 0.0, 10.0, true, "Horizontal drift of rocket trajectories", "2.0"});

        // Additional options for Groundfire-like effect
        opts.push_back({"--ground-fire", "bool", 0, 1, false,
            "Enable ground fireworks (sparks shooting upward)", "false"});

        opts.push_back({"--ground-fire-rate", "float", 0.1, 10.0, true,
            "Ground fire bursts per second", "5.0"});

        opts.push_back({"--ground-fire-color", "string", 0, 0, true,
            "Ground fire color (white, yellow, or hex #RRGGBB)", "white"});

        return opts;
    }
    
    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        
        if (arg == "--frequency" && i + 1 < argc) {
            launchFrequency_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--frequency-randomness" && i + 1 < argc) {
            launchRandomness_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--sparks" && i + 1 < argc) {
            sparksPerRocket_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--sparks-variance" && i + 1 < argc) {
            sparksVariance_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--gravity" && i + 1 < argc) {
            gravity_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--speed" && i + 1 < argc) {
            sparkSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--size" && i + 1 < argc) {
            sparkSize_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--trail" && i + 1 < argc) {
            trailIntensity_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--drift" && i + 1 < argc) {
            horizontalDrift_ = std::atof(argv[++i]);
            return true;
        
        } else if (arg == "--ground-fire") {
            groundFireEnabled_ = true;
            return true;
        } else if (arg == "--ground-fire-rate" && i + 1 < argc) {
            groundFireRate_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--ground-fire-color" && i + 1 < argc) {
            parseColor(argv[++i], groundR_, groundG_, groundB_);
            return true;
        }
        return false;
    }
    
    void parseColor(const std::string& s, float& r, float& g, float& b) {
        if (s == "white") {
            r = g = b = 1.0f;
        } else if (s == "yellow") {
            r = 1.0f; g = 0.9f; b = 0.6f;
        } else if (s.size() == 7 && s[0] == '#') {
            r = std::stoi(s.substr(1,2), nullptr, 16) / 255.0f;
            g = std::stoi(s.substr(3,2), nullptr, 16) / 255.0f;
            b = std::stoi(s.substr(5,2), nullptr, 16) / 255.0f;
        }
    }


    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        frameCount_ = 0;
        
        // Initialize rocket pool
        rockets_.resize(maxRockets_);
        for (auto& r : rockets_) {
            r.active = false;
            r.exploded = false;
        }
        
        // Initialize spark pool (enough for multiple simultaneous explosions)
        sparks_.resize(maxRockets_ * sparksPerRocket_);
        for (auto& s : sparks_) {
            s.active = false;
        }
        
        // Schedule first rocket
        std::uniform_real_distribution<float> distDelay(0.0f, 1.0f / launchFrequency_);
        nextLaunchTime_ = distDelay(rng_);
        nextGroundFireTime_ = 0.0f;
        
        return true;
    }
    
    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        // Draw sparks
        for (const auto& spark : sparks_) {
            if (spark.active) {
                float alpha = spark.life;
                drawParticle(frame, spark.x, spark.y, spark.size, 
                           spark.r, spark.g, spark.b, alpha, fadeMultiplier);
            }
        }
        
        // Draw rockets and their trails
        for (const auto& rocket : rockets_) {
            if (rocket.active && !rocket.exploded) {
                // Draw rocket
                drawParticle(frame, rocket.x, rocket.y, 2.5f, 
                           1.0f, 0.9f, 0.7f, 1.0f, fadeMultiplier);
                
                // Draw trail
                if (trailIntensity_ > 0.0f) {
                    float trailLength = 10.0f;
                    int trailSteps = 8;
                    for (int i = 1; i <= trailSteps; i++) {
                        float t = (float)i / trailSteps;
                        float tx = rocket.x - rocket.vx * t * trailLength;
                        float ty = rocket.y - rocket.vy * t * trailLength;
                        float alpha = (1.0f - t) * trailIntensity_ * 0.6f;
                        drawParticle(frame, tx, ty, 1.5f, 
                                   rocket.r, rocket.g, rocket.b, alpha, fadeMultiplier);
                    }
                }
            }
        }
    }
    
    void update() override {
        float dt = 1.0f / fps_;
        float time = frameCount_ * dt;
        
        // Launch new rockets
        if (time >= nextLaunchTime_) {
            launchRocket();
            // Add randomness to launch interval
            float baseInterval = 1.0f / launchFrequency_;
            std::uniform_real_distribution<float> distRandomness(
                1.0f - launchRandomness_, 
                1.0f + launchRandomness_
            );
            nextLaunchTime_ = time + baseInterval * distRandomness(rng_);
        }
        
        // Update rockets
        for (auto& rocket : rockets_) {
            if (rocket.active && !rocket.exploded) {
                rocket.x += rocket.vx;
                rocket.y += rocket.vy;
                rocket.vy += gravity_;
                
                // Explode when reaching target height or starting to fall
                if (rocket.y <= rocket.targetY || rocket.vy > 0) {
                    explodeRocket(rocket);
                }
            }
        }
        
        // Update sparks
        for (auto& spark : sparks_) {
            if (spark.active) {
                spark.x += spark.vx;
                spark.y += spark.vy;
                spark.vy += gravity_;
                spark.life -= spark.decay;
                
                if (spark.life <= 0.0f) {
                    spark.active = false;
                }
            }
        }

        // Emit ground fire if enabled
        if (groundFireEnabled_ && time >= nextGroundFireTime_) {
            emitGroundFire();
            nextGroundFireTime_ = time + 1.0f / groundFireRate_;
        }
        
        frameCount_++;
    }
};

// Register the effect
REGISTER_EFFECT(FireworksEffect, "fireworks", "Colorful fireworks display with rockets and explosions")