// effect_generator.cpp
// Main implementation of the video generator framework

#include "effect_generator.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

std::string VideoGenerator::findFFmpeg() {
    // 1. Check environment variable first
    const char* envPath = std::getenv("FFMPEG_PATH");
    if (envPath && envPath[0] != '\0') {
        return std::string(envPath);
    }
    
    // 2. Try common locations
#ifdef _WIN32
    const char* testPaths[] = {
        "ffmpeg.exe",
        "C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\ffmpeg\\bin\\ffmpeg.exe"
    };
#else
    const char* testPaths[] = {
        "ffmpeg",
        "/usr/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        "/opt/homebrew/bin/ffmpeg"
    };
#endif
    
    for (const char* path : testPaths) {
        // Test if ffmpeg is accessible
        std::string testCmd = std::string(path) + " -version";
#ifdef _WIN32
        testCmd += " >nul 2>&1";
#else
        testCmd += " >/dev/null 2>&1";
#endif
        if (system(testCmd.c_str()) == 0) {
            return std::string(path);
        }
    }
    
    return "ffmpeg"; // Fall back to PATH
}

VideoGenerator::VideoGenerator(int width, int height, int fps, float fadeDuration, int crf)
    : width_(width), height_(height), fps_(fps), fadeDuration_(fadeDuration), crf_(crf),
      hasBackground_(false), isVideo_(false), videoInput_(nullptr), ffmpegOutput_(nullptr) {
    frame_.resize(width * height * 3);
    ffmpegPath_ = findFFmpeg();
}

VideoGenerator::~VideoGenerator() {
    if (videoInput_) pclose(videoInput_);
    if (ffmpegOutput_) pclose(ffmpegOutput_);
}

bool VideoGenerator::loadBackgroundImage(const char* filename) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
            "\"%s\" -i \"%s\" -vf \"scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d\" "
            "-f rawvideo -pix_fmt rgb24 -",
            ffmpegPath_.c_str(), filename, width_, height_, width_, height_);
    
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        std::cerr << "Failed to load background image: " << filename << "\n";
        return false;
    }
    
    backgroundBuffer_.resize(width_ * height_ * 3);
    size_t bytesRead = fread(backgroundBuffer_.data(), 1, backgroundBuffer_.size(), pipe);
    pclose(pipe);
    
    if (bytesRead != backgroundBuffer_.size()) {
        std::cerr << "Failed to read complete background image\n";
        return false;
    }
    
    std::cout << "Background image loaded: " << filename << "\n";
    return true;
}

bool VideoGenerator::startBackgroundVideo(const char* filename) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
            "\"%s\" -i \"%s\" -vf \"scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d\" "
            "-f rawvideo -pix_fmt rgb24 -r %d -",
            ffmpegPath_.c_str(), filename, width_, height_, width_, height_, fps_);
    
    videoInput_ = popen(cmd, "r");
    if (!videoInput_) {
        std::cerr << "Failed to open background video: " << filename << "\n";
        return false;
    }
    
    backgroundBuffer_.resize(width_ * height_ * 3);
    std::cout << "Background video opened: " << filename << "\n";
    return true;
}

bool VideoGenerator::readVideoFrame() {
    if (!videoInput_) return false;
    
    size_t bytesRead = fread(backgroundBuffer_.data(), 1, backgroundBuffer_.size(), videoInput_);
    return bytesRead == backgroundBuffer_.size();
}

bool VideoGenerator::setBackgroundImage(const char* filename) {
    hasBackground_ = loadBackgroundImage(filename);
    isVideo_ = false;
    return hasBackground_;
}

bool VideoGenerator::setBackgroundVideo(const char* filename) {
    hasBackground_ = startBackgroundVideo(filename);
    isVideo_ = true;
    return hasBackground_;
}

bool VideoGenerator::startFFmpegOutput(const char* filename) {
    char cmd[2048];
    
    // Check for custom FFmpeg parameters from environment
    const char* customParams = std::getenv("FFMPEG_PARAMETERS");
    
    if (customParams && customParams[0] != '\0') {
        // Use custom parameters from environment variable
        snprintf(cmd, sizeof(cmd),
                "\"%s\" -y -f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate %d -i - "
                "%s \"%s\"",
                ffmpegPath_.c_str(), width_, height_, fps_, customParams, filename);
        std::cout << "Using custom FFmpeg parameters from FFMPEG_PARAMETERS\n";
    } else {
        // Use default parameters with configurable CRF
        snprintf(cmd, sizeof(cmd),
                "\"%s\" -y -f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate %d -i - "
                "-c:v libx264 -preset medium -crf %d -pix_fmt yuv420p \"%s\"",
                ffmpegPath_.c_str(), width_, height_, fps_, crf_, filename);
    }
    
    ffmpegOutput_ = popen(cmd, "w");
    if (!ffmpegOutput_) {
        std::cerr << "Failed to open FFmpeg output pipe\n";
        return false;
    }
    return true;
}

float VideoGenerator::getFadeMultiplier(int frameNumber, int totalFrames) {
    if (fadeDuration_ <= 0.0f) return 1.0f;
    
    int fadeFrames = (int)(fadeDuration_ * fps_);
    
    if (frameNumber < fadeFrames) {
        return (float)frameNumber / fadeFrames;
    } else if (frameNumber >= totalFrames - fadeFrames) {
        return (float)(totalFrames - frameNumber) / fadeFrames;
    }
    return 1.0f;
}

bool VideoGenerator::generate(Effect* effect, int durationSec, const char* outputFile) {
    if (!effect) return false;
    
    std::cout << "FFmpeg path: " << ffmpegPath_ << "\n";
    
    if (!effect->initialize(width_, height_, fps_)) {
        std::cerr << "Effect initialization failed\n";
        return false;
    }
    
    if (!startFFmpegOutput(outputFile)) {
        return false;
    }
    
    // If duration is -1 and we have a video background, run until video ends
    bool autoDetectDuration = (durationSec == -1 && isVideo_);
    int totalFrames = autoDetectDuration ? INT_MAX : fps_ * durationSec;
    
    if (autoDetectDuration) {
        std::cout << "Generating frames until input video ends...\n";
    } else {
        std::cout << "Generating " << totalFrames << " frames (" << durationSec << "s @ " << fps_ << " fps)...\n";
    }
    
    int frameCount = 0;
    
    while (frameCount < totalFrames) {
        // Read video frame if needed
        if (isVideo_ && hasBackground_) {
            if (!readVideoFrame()) {
                if (autoDetectDuration) {
                    std::cout << "\nInput video ended at frame " << frameCount 
                              << " (" << frameCount / fps_ << " seconds)\n";
                    break; // End of video, stop gracefully
                } else {
                    std::cerr << "\nWarning: Video ended at frame " << frameCount 
                              << ", continuing with last frame\n";
                }
            }
        }
        
        // Prepare frame with background
        if (hasBackground_) {
            std::copy(backgroundBuffer_.begin(), backgroundBuffer_.end(), frame_.begin());
        } else {
            std::fill(frame_.begin(), frame_.end(), 0);
        }
        
        // Render effect
        // For auto-detected duration, use fade based on current frame count
        float fadeMultiplier;
        if (hasBackground_) {
            if (autoDetectDuration) {
                // Calculate fade on-the-fly
                int fadeFrames = (int)(fadeDuration_ * fps_);
                if (frameCount < fadeFrames) {
                    fadeMultiplier = (float)frameCount / fadeFrames;
                } else {
                    fadeMultiplier = 1.0f;
                    // Note: fade-out won't work with auto-detect since we don't know when video ends
                }
            } else {
                fadeMultiplier = getFadeMultiplier(frameCount, totalFrames);
            }
        } else {
            fadeMultiplier = 1.0f;
        }
        
        effect->renderFrame(frame_, hasBackground_, fadeMultiplier);
        
        // Apply fade to entire frame for black background mode
        if (!hasBackground_ && fadeDuration_ > 0.0f && !autoDetectDuration) {
            fadeMultiplier = getFadeMultiplier(frameCount, totalFrames);
            if (fadeMultiplier < 1.0f) {
                for (size_t j = 0; j < frame_.size(); j++) {
                    frame_[j] = (uint8_t)(frame_[j] * fadeMultiplier);
                }
            }
        }
        
        // Write frame
        fwrite(frame_.data(), 1, frame_.size(), ffmpegOutput_);
        
        // Update effect state
        effect->update();
        
        frameCount++;
        
        if (frameCount % fps_ == 0) {
            std::cout << "Progress: " << frameCount / fps_ << " seconds\r" << std::flush;
        }
    }
    
    pclose(ffmpegOutput_);
    ffmpegOutput_ = nullptr;
    
    std::cout << "\nVideo saved to: " << outputFile << "\n";
    return true;
}
