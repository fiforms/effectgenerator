// effect_generator.cpp
// Main implementation of the video generator framework

#include "effect_generator.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cmath>

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
    // Remember the background video path for duration probing
    backgroundVideo_ = filename;
    return true;
}

double VideoGenerator::probeVideoDuration(const char* filename) {
    if (!filename) return -1.0;

    // Derive ffprobe path from ffmpeg path when possible
    std::string ffprobe = "ffprobe";
    std::string ffmpegBase = ffmpegPath_;
    size_t pos = ffmpegBase.find_last_of("/\\");
    std::string bin = (pos == std::string::npos) ? ffmpegBase : ffmpegBase.substr(pos + 1);
    if (bin.find("ffmpeg") != std::string::npos) {
        std::string dir = (pos == std::string::npos) ? std::string() : ffmpegBase.substr(0, pos + 1);
        std::string probeName = bin;
        // Replace "ffmpeg" with "ffprobe" in the binary name
        size_t r = probeName.find("ffmpeg");
        if (r != std::string::npos) probeName.replace(r, 6, "ffprobe");
        ffprobe = dir + probeName;
    }

    // Build command to get duration in seconds (quiet output)
    char cmd[4096];
#ifdef _WIN32
    // Windows: redirect stderr to NUL
    snprintf(cmd, sizeof(cmd), "\"%s\" -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"%s\" 2>NUL", ffprobe.c_str(), filename);
#else
    snprintf(cmd, sizeof(cmd), "\"%s\" -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"%s\" 2>/dev/null", ffprobe.c_str(), filename);
#endif

    FILE* pipe = popen(cmd, "r");
    if (!pipe) return -1.0;

    char buf[128];
    std::string out;
    while (fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    pclose(pipe);

    if (out.empty()) return -1.0;

    // Parse as double
    double secs = atof(out.c_str());
    if (secs <= 0.0) return -1.0;
    return secs;
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
    
    // If durationSec <= 0 and we have a video background, try to probe the video's length
    int totalFrames = 0;
    if (isVideo_ && durationSec <= 0) {
        double secs = probeVideoDuration(backgroundVideo_.c_str());
        if (secs > 0.0) {
            totalFrames = (int)std::round(secs * fps_);
            std::cout << "Auto-detected background video duration: " << secs << "s (" << totalFrames << " frames)\n";
        } else {
            // Fallback: generate until input video ends (previous behavior)
            totalFrames = INT_MAX;
            std::cout << "Could not probe video duration; generating until input video ends...\n";
        }
    } else if (durationSec > 0) {
        totalFrames = fps_ * durationSec;
        std::cout << "Generating " << totalFrames << " frames (" << durationSec << "s @ " << fps_ << " fps)...\n";
    } else {
        // No duration and not a video background: default to 0 frames (no output)
        std::cerr << "No duration provided and no background video available\n";
        return false;
    }
    // If probing failed we set totalFrames to INT_MAX to indicate "run until input ends"
    bool autoDetectDuration = (totalFrames == INT_MAX);
    
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
        
        // Allow effect to do post-processing with frame index knowledge
        // The effect can set `dropFrame` to true to omit writing this frame.
        // Note: for auto-detect, we use frameCount as both current and "total" since we don't know end yet
        bool dropFrame = false;
        effect->postProcess(frame_, frameCount, autoDetectDuration ? frameCount : totalFrames, dropFrame);

        // Apply fade to entire frame for black background mode
        if (!hasBackground_ && fadeDuration_ > 0.0f && !autoDetectDuration) {
            fadeMultiplier = getFadeMultiplier(frameCount, totalFrames);
            if (fadeMultiplier < 1.0f) {
                for (size_t j = 0; j < frame_.size(); j++) {
                    frame_[j] = (uint8_t)(frame_[j] * fadeMultiplier);
                }
            }
        }
        
        // Write frame unless the effect requested it be dropped
        if (!dropFrame) {
            fwrite(frame_.data(), 1, frame_.size(), ffmpegOutput_);
        }
        
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
