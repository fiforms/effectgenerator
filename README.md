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

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- FFmpeg (runtime dependency)

## FFmpeg Setup

The application will automatically search for FFmpeg in common locations. You can also:

1. Add FFmpeg to your system PATH
2. Set the `FFMPEG_PATH` environment variable:

**Linux/macOS:**
```bash
export FFMPEG_PATH=/path/to/ffmpeg
```

**Windows:**
```cmd
set FFMPEG_PATH=C:\path\to\ffmpeg.exe
```

Or permanently in Windows:
- Right-click "This PC" → Properties → Advanced system settings
- Environment Variables → New → Variable: `FFMPEG_PATH`, Value: path to ffmpeg.exe

## Usage

### List Available Effects

```bash
effectgenerator --list-effects
```

### Get Help for Specific Effect

```bash
effectgenerator --help-snowflake
```

### Generate Effect Video

```bash
# Basic snowfall
effectgenerator --effect snowflake --duration 10

# Heavy snow with custom settings
effectgenerator --effect snowflake --flakes 300 --size 5.0 --duration 15

# Overlay on image
effectgenerator --effect snowflake --background-image winter.jpg --output snowy.mp4

# Overlay on video
effectgenerator --effect snowflake --background-video input.mp4 --fade 2.0

# Custom resolution and FPS
effectgenerator --effect snowflake --width 3840 --height 2160 --fps 60
```

## Snowflake Effect Options

- `--flakes <int>` - Number of snowflakes (default: 150)
- `--size <float>` - Average snowflake size (default: 3.0)
- `--size-var <float>` - Size variance (default: 1.5)
- `--motion-x <float>` - Horizontal drift (default: 0.5)
- `--motion-y <float>` - Vertical fall speed (default: 2.0)
- `--randomness <float>` - Motion turbulence (default: 1.0)
- `--softness <float>` - Edge blur (default: 2.0)
- `--brightness <float>` - Maximum brightness 0.0-1.0 (default: 1.0)

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

## Architecture

```
effect_generator.h         - Framework interface
effect_generator.cpp       - Video generation logic
snowflake_effect.cpp       - Snowflake implementation
main.cpp                   - Command-line interface
```

### Effect Interface

Effects must implement:

- `getName()` - Effect identifier
- `getDescription()` - Short description
- `printHelp()` - Print effect-specific help
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

This is example code for educational purposes.

## Examples

```bash
# Gentle snowfall
effectgenerator --effect snowflake --flakes 100 --brightness 0.6 --softness 3.0

# Blizzard
effectgenerator --effect snowflake --flakes 500 --size 2.0 --randomness 3.0

# Windy snow
effectgenerator --effect snowflake --motion-x 3.0 --motion-y 2.0

# Add snow to your vacation video
effectgenerator --effect snowflake \\
  --background-video vacation.mp4 \\
  --flakes 200 --fade 1.5 \\
  --output vacation_snowy.mp4
```
