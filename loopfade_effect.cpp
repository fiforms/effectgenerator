// loopfade_effect.cpp
// Creates seamless looping videos with crossfade

#include "effect_generator.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>

class LoopFadeEffect : public Effect {
private:
    int width_, height_, fps_;
    float crossfadeDuration_;
    int crossfadeFrames_;
    int currentFrame_;
    int expectedTotalFrames_;
    float globalWarmupSeconds_;
    
    // Storage for the beginning frames (offset by crossfade duration)
    std::vector<std::vector<uint8_t>> beginningFrames_;
    bool capturedBeginning_;
    
public:
    LoopFadeEffect()
        : crossfadeDuration_(1.5f), crossfadeFrames_(0), currentFrame_(0), 
          expectedTotalFrames_(-1), globalWarmupSeconds_(0.0f), capturedBeginning_(false) {}
    
    std::string getName() const override {
        return "loopfade";
    }
    
    std::string getDescription() const override {
        return "Create seamless looping video with crossfade (requires background video and explicit duration)";
    }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--crossfade-duration", "float", 0.0, 10000.0, true, "Crossfade duration in seconds", "1.5"});
        return opts;
    }
    
    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        
        if (arg == "--crossfade-duration" && i + 1 < argc) {
            crossfadeDuration_ = std::atof(argv[++i]);
            return true;
        }
        
        return false;
    }
    
    bool initialize(int width, int height, int fps) override {
        width_ = width;
        height_ = height;
        fps_ = fps;
        
        crossfadeFrames_ = (int)(crossfadeDuration_ * fps);
        // Ignore global warmup by offsetting the internal frame counter.
        // The generator will call update() warmupFrames times before rendering.
        int warmupFrames = (int)std::round(std::max(0.0f, globalWarmupSeconds_) * fps_);
        currentFrame_ = -warmupFrames;
        capturedBeginning_ = false;
        
        // Pre-allocate storage for beginning frames
        beginningFrames_.resize(crossfadeFrames_);
        for (auto& frame : beginningFrames_) {
            frame.resize(width * height * 3);
        }
        
        std::cerr << "Loop fade: " << crossfadeFrames_ << " frames (" 
                  << crossfadeDuration_ << "s) crossfade\n";        
        return true;
    }

    void setGlobalWarmupSeconds(float seconds) override {
        globalWarmupSeconds_ = std::max(0.0f, seconds);
    }
    
    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        if (!hasBackground) {
            std::cerr << "ERROR: loopfade effect requires --background-video\n";
            return;
        }
        
        // Capture frames from start
        if (currentFrame_ < crossfadeFrames_) {
            int storeIdx = currentFrame_;
            if (storeIdx < (int)beginningFrames_.size()) {
                std::copy(frame.begin(), frame.end(), beginningFrames_[storeIdx].begin());
                if (!capturedBeginning_ && storeIdx == 0) {
                    std::cerr << "Capturing beginning frames for crossfade...\n";
                    capturedBeginning_ = true;
                }
            }
        }
    }
    
    void update() override {
        currentFrame_++;
    }
    
    void postProcess(std::vector<uint8_t>& frame, int frameIndex, int totalFrames, bool& dropFrame) override {
        // Do not drop frames by default
        dropFrame = false;

        // Store total frames on first call
        if (expectedTotalFrames_ == -1) {
            expectedTotalFrames_ = totalFrames;
            std::cerr << "Total frames: " << totalFrames << ", crossfade starts at frame " 
                      << (totalFrames - crossfadeFrames_) << "\n";
        }

        if(frameIndex <= crossfadeFrames_) {
            // Drop the initial frames 
            dropFrame = true;
            return;
        }

        // Calculate if we're in the crossfade zone
        int fadeStartFrame = totalFrames - crossfadeFrames_;
        
        if (frameIndex >= fadeStartFrame && frameIndex < totalFrames) {
            int fadeFrameIdx = frameIndex - fadeStartFrame;
            
            if (fadeFrameIdx >= 0 && fadeFrameIdx < (int)beginningFrames_.size()) {
                // Calculate crossfade alpha (0.0 at start of fade, 1.0 at end)
                float alpha = (float)(fadeFrameIdx + 1) / (float)crossfadeFrames_;
                
                const auto& beginFrame = beginningFrames_[fadeFrameIdx];
                
                // Crossfade: blend current frame with beginning frame
                for (size_t i = 0; i < frame.size(); i++) {
                    float current = frame[i] / 255.0f;
                    float begin = beginFrame[i] / 255.0f;
                    
                    // Linear crossfade
                    float result = current * (1.0f - alpha) + begin * alpha;
                    frame[i] = (uint8_t)(std::clamp(result, 0.0f, 1.0f) * 255);
                }
                
                // Debug: print when crossfade starts
                if (fadeFrameIdx == 0) {
                    std::cerr << "Starting crossfade at frame " << frameIndex << "...\n";
                }
            }
        }
    }
};

// Register the effect
REGISTER_EFFECT(LoopFadeEffect, "loopfade", "Seamless loop with crossfade")
