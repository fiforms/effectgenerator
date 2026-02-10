// effect_generator.cpp
// Main implementation of the video generator framework

#include "effect_generator.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
#else
    #include <sys/wait.h>
    #include <fcntl.h>
#endif

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

std::string trimQuotes(const std::string& value) {
    if (value.size() >= 2) {
        char first = value.front();
        char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool fileExists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    return access(path.c_str(), X_OK) == 0;
#endif
}

std::string joinPath(const std::string& dir, const std::string& leaf) {
    if (dir.empty()) return leaf;
    char back = dir.back();
    if (back == '/' || back == '\\') return dir + leaf;
    return dir + PATH_SEPARATOR + leaf;
}

std::string findInPath(const std::string& exeName) {
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv || pathEnv[0] == '\0') return "";
    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    std::string paths(pathEnv);
    std::stringstream ss(paths);
    std::string segment;
    while (std::getline(ss, segment, separator)) {
        std::string dir = trimQuotes(segment);
        if (dir.empty()) continue;
        std::string candidate = joinPath(dir, exeName);
        if (fileExists(candidate)) return candidate;
    }
    return "";
}

#ifdef _WIN32
bool hasExeSuffix(const std::string& name) {
    if (name.size() < 4) return false;
    std::string tail = name.substr(name.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return tail == ".exe";
}
#endif

#ifdef _WIN32
std::string escapeWindowsArg(const std::string& arg) {
    bool needsQuotes = arg.empty();
    for (char c : arg) {
        if (c == ' ' || c == '\t' || c == '"') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return arg;
    std::string out;
    out.push_back('"');
    size_t i = 0;
    while (i < arg.size()) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            backslashes++;
            i++;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(arg[i]);
        }
        i++;
    }
    out.push_back('"');
    return out;
}

std::string buildCommandLine(const std::vector<std::string>& args) {
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(' ');
        cmd += escapeWindowsArg(args[i]);
    }
    return cmd;
}
#endif

VideoGenerator::ProcessPipe VideoGenerator::spawnProcessPipe(const std::vector<std::string>& args, const char* mode, bool quiet) {
    VideoGenerator::ProcessPipe result;
    if (args.empty() || !mode) return result;
    bool readMode = mode[0] == 'r';
    bool writeMode = mode[0] == 'w';
    if (!readMode && !writeMode) return result;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdoutRead = NULL;
    HANDLE childStdoutWrite = NULL;
    HANDLE childStdinRead = NULL;
    HANDLE childStdinWrite = NULL;

    if (readMode) {
        if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) return result;
        SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);
    } else {
        if (!CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0)) return result;
        SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
    }

    HANDLE hNullRead = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hNullWrite = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readMode ? hNullRead : childStdinRead;
    si.hStdOutput = readMode ? childStdoutWrite : hNullWrite;
    si.hStdError = quiet ? hNullWrite : si.hStdOutput;

    PROCESS_INFORMATION pi{};
    std::string cmdLine = buildCommandLine(args);
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (readMode && childStdoutWrite) CloseHandle(childStdoutWrite);
    if (writeMode && childStdinRead) CloseHandle(childStdinRead);
    if (hNullRead) CloseHandle(hNullRead);
    if (hNullWrite) CloseHandle(hNullWrite);

    if (!created) {
        if (readMode && childStdoutRead) CloseHandle(childStdoutRead);
        if (writeMode && childStdinWrite) CloseHandle(childStdinWrite);
        return result;
    }

    int fd = -1;
    if (readMode) {
        fd = _open_osfhandle((intptr_t)childStdoutRead, _O_RDONLY);
    } else {
        fd = _open_osfhandle((intptr_t)childStdinWrite, 0);
    }
    if (fd == -1) {
        if (readMode && childStdoutRead) CloseHandle(childStdoutRead);
        if (writeMode && childStdinWrite) CloseHandle(childStdinWrite);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return result;
    }

    FILE* stream = _fdopen(fd, readMode ? "r" : "w");
    if (!stream) {
        _close(fd);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return result;
    }

    result.stream = stream;
    result.proc = pi;
    return result;
#else
    int pipefd[2];
    if (pipe(pipefd) != 0) return result;

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
    if (pid == 0) {
        int devNullIn = open("/dev/null", O_RDONLY);
        int devNullOut = open("/dev/null", O_WRONLY);
        if (readMode) {
            dup2(pipefd[1], STDOUT_FILENO);
            if (quiet && devNullOut != -1) dup2(devNullOut, STDERR_FILENO);
            if (devNullIn != -1) dup2(devNullIn, STDIN_FILENO);
        } else {
            dup2(pipefd[0], STDIN_FILENO);
            if (devNullOut != -1) {
                dup2(devNullOut, STDOUT_FILENO);
                if (quiet) dup2(devNullOut, STDERR_FILENO);
            }
        }
        if (devNullIn != -1) close(devNullIn);
        if (devNullOut != -1) close(devNullOut);
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> cargs;
        cargs.reserve(args.size() + 1);
        for (const auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
        cargs.push_back(nullptr);
        execvp(cargs[0], cargs.data());
        _exit(127);
    }

    if (readMode) {
        close(pipefd[1]);
        result.stream = fdopen(pipefd[0], "r");
    } else {
        close(pipefd[0]);
        result.stream = fdopen(pipefd[1], "w");
    }
    if (!result.stream) {
        if (readMode) close(pipefd[0]); else close(pipefd[1]);
        return result;
    }
    result.pid = pid;
    return result;
#endif
}

void VideoGenerator::closeProcessPipe(VideoGenerator::ProcessPipe& proc) {
    if (!proc.stream) return;
    fclose(proc.stream);
    proc.stream = nullptr;
#ifdef _WIN32
    if (proc.proc.hProcess) {
        WaitForSingleObject(proc.proc.hProcess, INFINITE);
        CloseHandle(proc.proc.hThread);
        CloseHandle(proc.proc.hProcess);
        proc.proc.hProcess = NULL;
        proc.proc.hThread = NULL;
    }
#else
    if (proc.pid > 0) {
        int status = 0;
        waitpid(proc.pid, &status, 0);
        proc.pid = -1;
    }
#endif
}

std::string VideoGenerator::findFFmpeg(std::string binaryName) {
    // 1. Check environment variable first
    const char* envPtr = binaryName == "ffmpeg" ? std::getenv("FFMPEG_PATH") : std::getenv("FFPROBE_PATH");
    if (envPtr && envPtr[0] != '\0') {
        return trimQuotes(std::string(envPtr));
    }

#ifdef _WIN32
    if (!hasExeSuffix(binaryName)) {
        binaryName += ".exe";
    }
#endif

    // 2. Look on PATH
    std::string fromPath = findInPath(binaryName);
    if (!fromPath.empty()) {
        return fromPath;
    }
    
    // 2. Try common locations
#ifdef _WIN32
    const std::string testPaths[] = {
        binaryName,
        "C:\\Program Files\\ffmpeg\\bin\\" + binaryName,
        "C:\\ffmpeg\\bin\\" + binaryName
    };
#else
    // FIXME: substitute ffmpeg literal with binaryName
    const std::string testPaths[] = {
        binaryName,
        "/usr/bin/" + binaryName,
        "/usr/local/bin/" + binaryName,
        "/opt/homebrew/bin/" + binaryName
    };
#endif
    
    for (const std::string &path : testPaths) {
        if (fileExists(path)) {
            return path;
        }
    }

    return ""; // Not found
}

VideoGenerator::VideoGenerator(int width, int height, int fps, float fadeDuration, float maxFadeRatio, int crf, std::string audioCodec, std::string audioBitrate)
    : width_(width), height_(height), fps_(fps), fadeDuration_(fadeDuration), maxFadeRatio_(maxFadeRatio), crf_(crf), audioCodec_(audioCodec), audioBitrate_(audioBitrate),
      warmupSeconds_(0.0f), hasBackground_(false), isVideo_(false) {
    frame_.resize(width * height * 3);
    ffmpegPath_ = findFFmpeg("ffmpeg");
}

VideoGenerator::~VideoGenerator() {
    closeProcessPipe(videoInput_);
    closeProcessPipe(ffmpegOutput_);
}

bool VideoGenerator::loadBackgroundImage(const char* filename) {
    std::vector<std::string> args = {
        ffmpegPath_,
        "-i", filename,
        "-vf", "scale=" + std::to_string(width_) + ":" + std::to_string(height_) + ":force_original_aspect_ratio=increase,crop=" + std::to_string(width_) + ":" + std::to_string(height_),
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "-"
    };
    VideoGenerator::ProcessPipe pipe = spawnProcessPipe(args, "r", true);
    if (!pipe.stream) {
        std::cerr << "Failed to load background image: " << filename << "\n";
        return false;
    }
    
    backgroundBuffer_.resize(width_ * height_ * 3);
    size_t bytesRead = fread(backgroundBuffer_.data(), 1, backgroundBuffer_.size(), pipe.stream);
    closeProcessPipe(pipe);
    
    if (bytesRead != backgroundBuffer_.size()) {
        std::cerr << "Failed to read complete background image\n";
        return false;
    }
    
    std::cout << "Background image loaded: " << filename << "\n";
    return true;
}

bool VideoGenerator::startBackgroundVideo(const char* filename) {
    std::vector<std::string> args = {
        ffmpegPath_,
        "-i", filename,
        "-vf", "scale=" + std::to_string(width_) + ":" + std::to_string(height_) + ":force_original_aspect_ratio=increase,crop=" + std::to_string(width_) + ":" + std::to_string(height_),
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "-r", std::to_string(fps_),
        "-hide_banner",
        "-loglevel", "error",
        "-"
    };
    videoInput_ = spawnProcessPipe(args, "r", true);
    if (!videoInput_.stream) {
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

    std::string ffprobe = findFFmpeg("ffprobe");
    if (ffprobe.empty()) {
        std::cerr << "ffprobe not found; cannot probe video duration\n";
        return -1.0;
    }

    std::vector<std::string> args = {
        ffprobe,
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        filename
    };
    VideoGenerator::ProcessPipe pipe = spawnProcessPipe(args, "r", true);
    if (!pipe.stream) return -1.0;

    char buf[128];
    std::string out;
    while (fgets(buf, sizeof(buf), pipe.stream)) {
        out += buf;
    }
    closeProcessPipe(pipe);

    if (out.empty()) return -1.0;

    // Parse as double
    double secs = atof(out.c_str());
    if (secs <= 0.0) return -1.0;
    return secs;
}

bool VideoGenerator::readVideoFrame() {
    if (!videoInput_.stream) return false;
    
    size_t bytesRead = fread(backgroundBuffer_.data(), 1, backgroundBuffer_.size(), videoInput_.stream);
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
    if (ffmpegPath_.empty()) {
        std::cerr << "FFmpeg path not set or FFmpeg not found\n";
        return false;
    }

    // Check for custom FFmpeg parameters from environment
    const char* customParams = std::getenv("FFMPEG_PARAMETERS");
    if (customParams && customParams[0] != '\0') {
        // Use custom parameters from environment variable (simple shell-like split)
        std::vector<std::string> args = {
            ffmpegPath_,
            "-y",
            "-f", "rawvideo",
            "-pixel_format", "rgb24",
            "-video_size", std::to_string(width_) + "x" + std::to_string(height_),
            "-framerate", std::to_string(fps_),
            "-i", "-"
        };
        std::vector<std::string> extra;
        {
            std::string cur;
            bool inQuote = false;
            char quoteChar = '\0';
            for (const char* p = customParams; *p; ++p) {
                char c = *p;
                if ((c == '"' || c == '\'') && (!inQuote || c == quoteChar)) {
                    if (inQuote && c == quoteChar) {
                        inQuote = false;
                        quoteChar = '\0';
                    } else if (!inQuote) {
                        inQuote = true;
                        quoteChar = c;
                    }
                    continue;
                }
                if (!inQuote && std::isspace(static_cast<unsigned char>(c))) {
                    if (!cur.empty()) {
                        extra.push_back(cur);
                        cur.clear();
                    }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) extra.push_back(cur);
        }
        for (const auto& e : extra) args.push_back(e);
        args.push_back(filename);
        std::cout << "Using custom FFmpeg parameters from FFMPEG_PARAMETERS\n";
        ffmpegOutput_ = spawnProcessPipe(args, "w", true);
        if (!ffmpegOutput_.stream) {
            std::cerr << "Failed to open FFmpeg output pipe\n";
            return false;
        }
        return true;
    } else {
        // Determine output extension (lowercase)
        std::string outExt;
        if (filename) {
            std::string out(filename);
            size_t dot = out.find_last_of('.');
            if (dot != std::string::npos && dot + 1 < out.size()) {
                outExt = out.substr(dot + 1);
                std::transform(outExt.begin(), outExt.end(), outExt.begin(), [](unsigned char c){ return std::tolower(c); });
            }
        }

        // Build Audio parameters if specified
        std::vector<std::string> audioArgs1;
        std::vector<std::string> audioArgs2;
        if (!audioCodec_.empty()) {
            audioArgs1 = {"-i", backgroundVideo_, "-map", "0:v:0", "-map", "1:a:0"};

            if (audioBitrate_.empty()) {
                audioArgs2 = {"-c:a", audioCodec_};
            } else {
                audioArgs2 = {"-c:a", audioCodec_, "-b:a", audioBitrate_};
            }  
        }

        // Use different codec parameters depending on extension
        if (outExt == "webm") {
            std::vector<std::string> args = {
                ffmpegPath_,
                "-y",
                "-f", "rawvideo",
                "-pixel_format", "rgb24",
                "-video_size", std::to_string(width_) + "x" + std::to_string(height_),
                "-framerate", std::to_string(fps_),
                "-i", "-",
                "-c:v", "libsvtav1",
                "-preset", "7",
                "-crf", std::to_string(crf_),
                "-pix_fmt", "yuv420p",
                filename,
                "-hide_banner",
                "-loglevel", "error"
            };
            ffmpegOutput_ = spawnProcessPipe(args, "w", true);
        } else if (outExt == "mov") {
            std::vector<std::string> args = {
                ffmpegPath_,
                "-y",
                "-f", "rawvideo",
                "-pixel_format", "rgb24",
                "-video_size", std::to_string(width_) + "x" + std::to_string(height_),
                "-framerate", std::to_string(fps_),
                "-i", "-",
                "-c:v", "prores_ks",
                "-profile:v", "3",
                "-qscale:v", std::to_string(crf_),
                "-pix_fmt", "yuv422p10le",
                filename,
                "-hide_banner",
                "-loglevel", "error"
            };
            ffmpegOutput_ = spawnProcessPipe(args, "w", true);
        } else {
            std::vector<std::string> args = {
                ffmpegPath_,
                "-y",
                "-f", "rawvideo",
                "-pixel_format", "rgb24",
                "-video_size", std::to_string(width_) + "x" + std::to_string(height_),
                "-framerate", std::to_string(fps_),
                "-i", "-"
            };
            if (!audioArgs1.empty()) {
                args.insert(args.end(), audioArgs1.begin(), audioArgs1.end());
            }
            args.insert(args.end(), {"-c:v", "libx264", "-preset", "medium", "-crf", std::to_string(crf_), "-pix_fmt", "yuv420p"});
            if (!audioArgs2.empty()) {
                args.insert(args.end(), audioArgs2.begin(), audioArgs2.end());
            }
            args.insert(args.end(), {"-movflags", "faststart", filename, "-hide_banner", "-loglevel", "error"});
            ffmpegOutput_ = spawnProcessPipe(args, "w", true);
        }
    }
    
    if (!ffmpegOutput_.stream) {
        std::cerr << "Failed to open FFmpeg output pipe\n";
        return false;
    }
    return true;
}

float VideoGenerator::getFadeMultiplier(int frameNumber, int totalFrames) {
    if (fadeDuration_ <= 0.0f) return maxFadeRatio_;
    
    int fadeFrames = (int)(fadeDuration_ * fps_);
    
    if (frameNumber < fadeFrames) {
        return (float)frameNumber * maxFadeRatio_ / fadeFrames;
    } else if (frameNumber >= totalFrames - fadeFrames) {
        return (float)(totalFrames - frameNumber) * maxFadeRatio_ / fadeFrames;
    }
    return maxFadeRatio_;
}

bool VideoGenerator::generate(const std::vector<Effect*>& effects, int durationSec, const char* outputFile) {
    if (effects.empty()) return false;

    std::cout << "FFmpeg path: " << ffmpegPath_ << "\n";

    int totalFrames = 0;
    if (isVideo_ && durationSec <= 0) {
        double secs = probeVideoDuration(backgroundVideo_.c_str());
        if (secs > 0.0) {
            totalFrames = (int)std::round(secs * fps_);
            std::cout << "Auto-detected background video duration: " << secs << "s (" << totalFrames << " frames)\n";
        } else {
            totalFrames = INT_MAX;
            std::cout << "Could not probe video duration; generating until input video ends...\n";
        }
    } else if (durationSec > 0) {
        totalFrames = fps_ * durationSec;
        std::cout << "Generating " << totalFrames << " frames (" << durationSec << "s @ " << fps_ << " fps)...\n";
    } else {
        std::cerr << "No duration provided and no background video available\n";
        return false;
    }
    bool autoDetectDuration = (totalFrames == INT_MAX);

    for (Effect* effect : effects) {
        if (!effect) return false;
        if (totalFrames != INT_MAX) {
            effect->setTotalFrames(totalFrames);
        }
        effect->setGlobalWarmupSeconds(std::max(0.0f, warmupSeconds_));
        if (!effect->initialize(width_, height_, fps_)) {
            std::cerr << "Effect initialization failed\n";
            return false;
        }
    }

    int warmupFrames = (int)std::round(std::max(0.0f, warmupSeconds_) * fps_);
    if (warmupFrames > 0) {
        std::cout << "Warmup: advancing simulation by " << warmupFrames
                  << " frames (" << warmupSeconds_ << "s)\n";
        for (Effect* effect : effects) {
            for (int i = 0; i < warmupFrames; ++i) {
                effect->update();
            }
        }
    }

    if (!startFFmpegOutput(outputFile)) {
        return false;
    }

    struct FramePacket {
        std::vector<uint8_t> frame;
        int frameIndex = 0;
        bool end = false;
    };

    class FrameQueue {
    public:
        explicit FrameQueue(size_t capacity) : capacity_(capacity) {}

        bool push(FramePacket&& packet) {
            std::unique_lock<std::mutex> lock(mu_);
            cvNotFull_.wait(lock, [&]() { return closed_ || queue_.size() < capacity_; });
            if (closed_) return false;
            queue_.push_back(std::move(packet));
            cvNotEmpty_.notify_one();
            return true;
        }

        bool pop(FramePacket& out) {
            std::unique_lock<std::mutex> lock(mu_);
            cvNotEmpty_.wait(lock, [&]() { return closed_ || !queue_.empty(); });
            if (queue_.empty()) return false;
            out = std::move(queue_.front());
            queue_.pop_front();
            cvNotFull_.notify_one();
            return true;
        }

        void close() {
            std::lock_guard<std::mutex> lock(mu_);
            closed_ = true;
            cvNotEmpty_.notify_all();
            cvNotFull_.notify_all();
        }

    private:
        std::mutex mu_;
        std::condition_variable cvNotEmpty_;
        std::condition_variable cvNotFull_;
        std::deque<FramePacket> queue_;
        const size_t capacity_;
        bool closed_ = false;
    };

    auto computeStageFade = [&](int frameIndex, bool stageHasBackground) -> float {
        if (!stageHasBackground) return 1.0f;
        if (autoDetectDuration) {
            int fadeFrames = (int)(fadeDuration_ * fps_);
            if (fadeFrames <= 0) return 1.0f;
            if (frameIndex < fadeFrames) return (float)frameIndex / fadeFrames;
            return 1.0f;
        }
        return getFadeMultiplier(frameIndex, totalFrames);
    };

    const size_t queueCapacity = 8;
    std::vector<std::unique_ptr<FrameQueue>> stageQueues;
    stageQueues.reserve(effects.size());
    for (size_t i = 0; i < effects.size(); ++i) {
        stageQueues.push_back(std::make_unique<FrameQueue>(queueCapacity));
    }

    std::atomic<bool> sourceEnded(false);
    std::atomic<int> sourceFrameCount(0);
    std::vector<std::thread> workers;
    workers.reserve(effects.size());

    for (size_t stage = 0; stage < effects.size(); ++stage) {
        workers.emplace_back([&, stage]() {
            Effect* effect = effects[stage];
            const bool stageHasBackground = hasBackground_ || stage > 0;
            FrameQueue* outputQueue = stageQueues[stage].get();
            FrameQueue* inputQueue = (stage == 0) ? nullptr : stageQueues[stage - 1].get();

            int stageFrameIndex = 0;
            while (stageFrameIndex < totalFrames) {
                std::vector<uint8_t> frame(width_ * height_ * 3);
                int logicalFrame = stageFrameIndex;

                if (stage == 0) {
                    if (isVideo_ && hasBackground_) {
                        if (!readVideoFrame()) {
                            if (autoDetectDuration) {
                                sourceEnded.store(true);
                                break;
                            }
                        }
                    }

                    if (hasBackground_) {
                        std::copy(backgroundBuffer_.begin(), backgroundBuffer_.end(), frame.begin());
                    } else {
                        std::fill(frame.begin(), frame.end(), 0);
                    }
                } else {
                    FramePacket input;
                    if (!inputQueue->pop(input)) {
                        break;
                    }
                    if (input.end) {
                        break;
                    }
                    logicalFrame = input.frameIndex;
                    frame = std::move(input.frame);
                }

                float fadeMultiplier = computeStageFade(logicalFrame, stageHasBackground);
                effect->renderFrame(frame, stageHasBackground, fadeMultiplier);

                bool dropFrame = false;
                effect->postProcess(frame, logicalFrame, autoDetectDuration ? logicalFrame : totalFrames, dropFrame);
                effect->update();

                if (!dropFrame) {
                    FramePacket out;
                    out.frame = std::move(frame);
                    out.frameIndex = logicalFrame;
                    out.end = false;
                    if (!outputQueue->push(std::move(out))) {
                        break;
                    }
                }

                ++stageFrameIndex;
            }

            if (stage == 0) {
                sourceFrameCount.store(stageFrameIndex);
            }

            FramePacket endPacket;
            endPacket.end = true;
            outputQueue->push(std::move(endPacket));
            outputQueue->close();
        });
    }

    int writtenFrames = 0;
    FrameQueue* finalQueue = stageQueues.back().get();
    while (true) {
        FramePacket packet;
        if (!finalQueue->pop(packet)) {
            break;
        }
        if (packet.end) {
            break;
        }

        if (!hasBackground_ && fadeDuration_ > 0.0f && !autoDetectDuration) {
            float fadeMultiplier = getFadeMultiplier(packet.frameIndex, totalFrames);
            if (fadeMultiplier < 1.0f) {
                for (size_t j = 0; j < packet.frame.size(); ++j) {
                    packet.frame[j] = (uint8_t)(packet.frame[j] * fadeMultiplier);
                }
            }
        }

        fwrite(packet.frame.data(), 1, packet.frame.size(), ffmpegOutput_.stream);
        ++writtenFrames;
        if (writtenFrames % fps_ == 0) {
            std::cout << "Progress: " << writtenFrames / fps_ << " seconds\r" << std::flush;
        }
    }

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    closeProcessPipe(ffmpegOutput_);

    if (sourceEnded.load() && autoDetectDuration) {
        int endedAt = sourceFrameCount.load();
        std::cout << "\nInput video ended at frame " << endedAt
                  << " (" << endedAt / fps_ << " seconds)\n";
    }

    std::cout << "\nVideo saved to: " << outputFile << "\n";
    return true;
}

bool VideoGenerator::generate(Effect* effect, int durationSec, const char* outputFile) {
    if (!effect) return false;
    std::vector<Effect*> effects{effect};
    return generate(effects, durationSec, outputFile);
}
