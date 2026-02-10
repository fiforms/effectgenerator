# Effect Generator

A modular video effects generator that can overlay various effects on videos or images using FFmpeg.

## Features

- **Modular Architecture**: Easy to add new effects
- **Cross-Platform**: Works on Linux, macOS, and Windows (MinGW)
- **FFmpeg Integration**: Automatic FFmpeg detection with environment variable support
- **Background Support**: Overlay effects on images or videos
- **Fade Effects**: Smooth fade in/out transitions
- **High Quality**: Optimized rendering with antialiasing

## Building

### Linux/macOS

```bash
make
```

### Windows (MinGW)

```bash
mingw32-make
```

Or using MSYS2:
```bash
make
```

### Installation (Linux/macOS)

```bash
sudo make install
```

This installs to `/usr/local/bin/effectgenerator`

## Dependencies

- A recent C++17 compatible compiler (like GCC)
- FFmpeg and Ffprobe (runtime dependencies)

## FFmpeg Setup

The application will automatically search for FFmpeg and Ffprobe in common locations. You can also:

1. Add FFmpeg to your system PATH
2. Set the `FFMPEG_PATH` and `FFPROBE_PATH` environment variables:

## Usage

### List Available Effects

```bash
effectgenerator --list-effects
```

### Show Version

```bash
effectgenerator --version
```

### Get Help for Specific Effect

```bash
effectgenerator --help-snowflake
```

### Generate Effect Video

```bash
# Basic snowfall
effectgenerator --effect snowflake --duration 10 --output snowfall.mp4

# Heavy snow with custom settings
effectgenerator --effect snowflake --flakes 300 --size 5.0 --duration 15 --output longer.mp4

# Overlay on image
effectgenerator --effect snowflake --background-image winter.jpg --output snowy.mp4

# Overlay on video
effectgenerator --effect snowflake --background-video input.mp4 --fade 2.0 --output output.mp4

# Layer multiple effects (order matters)
effectgenerator --fade 2 --background-video input.mp4 --effect laser --rays 10 --rotation 2 --effect sparkle --effect loopfade --output layered.mp4

# Custom resolution and FPS
effectgenerator --effect snowflake --width 3840 --height 2160 --fps 60  --output faster.mp4
```

## Adding New Effects

1. Create a new file `myeffect_effect.cpp`
2. Include `effect_generator.h`
3. Implement the `Effect` interface
4. Register with `REGISTER_EFFECT` macro
5. Add to Makefile `SOURCES`
6. Rebuild

Example:

```cpp
#include "effect_generator.h"

class MyEffect : public Effect {
    // Implement virtual methods...
};

REGISTER_EFFECT(MyEffect, "myeffect", "Description")
```

### Effect Interface

Effects must implement:

- `getName()` - Effect identifier
- `getDescription()` - Short description
- `getOptions()` - effect-specific parameters
- `parseArgs()` - Parse command-line arguments
- `initialize()` - Set up effect (called once)
- `renderFrame()` - Render one frame
- `update()` - Update animation state

## Platform Notes

### Windows
- Requires MinGW-w64 or similar
- FFmpeg binary must be in PATH or set via `FFMPEG_PATH`
- Executable is `effectgenerator.exe`

### Linux
- Install FFmpeg: `sudo apt install ffmpeg` (Debian/Ubuntu)
- Or: `sudo dnf install ffmpeg` (Fedora)

### macOS
- Install FFmpeg: `brew install ffmpeg`

## License

MIT License


```
