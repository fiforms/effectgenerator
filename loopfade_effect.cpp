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
    
    // Storage for the beginning frames (offset by crossfade duration)
    std::vector<std::vector<uint8_t>> beginningFrames_;
    bool capturedBeginning_;
    
public:
    LoopFadeEffect()
        : crossfadeDuration_(1.5f), crossfadeFrames_(0), currentFrame_(0), 
          expectedTotalFrames_(-1), capturedBeginning_(false) {}
    
    std::string getName() const override {
        return "loopfade";
    }
    
    std::string getDescription() const override {
        return "Create seamless looping video with crossfade (requires background video and explicit duration)";
    }
    
    void printHelp() const override {
        std::cout << "Loop Fade Effect Options:\n"
                  << "  --crossfade-duration <float>  Crossfade duration in seconds (default: 1.5)\n"
                  << "\n"
                  << "Usage:\n"
                  << "  effectgenerator --effect loopfade --background-video input.mp4 --duration 10\n"
                  << "\n"
                  << "IMPORTANT: You MUST specify --duration explicitly for this effect!\n"
                  << "\n"
                  << "This effect creates a seamless loop by:\n"
                  << "1. Capturing frames starting at T=crossfade_duration\n"
                  << "2. Crossfading those frames at the end of the video\n"
                  << "3. Result: Last frame smoothly transitions to first frame\n";
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
        currentFrame_ = 0;
        capturedBeginning_ = false;
        
        // Pre-allocate storage for beginning frames
        beginningFrames_.resize(crossfadeFrames_);
        for (auto& frame : beginningFrames_) {
            frame.resize(width * height * 3);
        }
        
        std::cout << "Loop fade: " << crossfadeFrames_ << " frames (" 
                  << crossfadeDuration_ << "s) crossfade\n";
        std::cout << "IMPORTANT: Make sure you specified --duration!\n";
        
        return true;
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
                    std::cout << "Capturing beginning frames for crossfade...\n";
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
            std::cout << "Total frames: " << totalFrames << ", crossfade starts at frame " 
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
                    std::cout << "Starting crossfade at frame " << frameIndex << "...\n";
                }
            }
        }
    }
};

// Register the effect
REGISTER_EFFECT(LoopFadeEffect, "loopfade", "Seamless loop with crossfade")
