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

**Implemented Effects**

- **snowflake**: Realistic falling snowflakes with soft edges and natural motion. Key options: `--flakes`, `--size`, `--size-var`, `--motion-x`, `--motion-y`, `--randomness`, `--softness`, `--brightness`, `--brightness-speed`, `--hue`, `--saturation`, `--hue-range`, `--spin`, `--spin-axis`. Example: `effectgenerator --effect snowflake --flakes 200 --duration 10`.

- **laser**: Animated radial rays / spotlight with moving focal point and morphing rays. Key options: `--focal-x`, `--focal-y`, `--focal-motion-x`, `--focal-motion-y`, `--focal-random`, `--rays`, `--intensity`, `--ray-width`, `--ray-width-var`, `--morph-speed`, `--rotation`, `--color-r`, `--color-g`, `--color-b`. Example: `effectgenerator --effect laser --rays 12 --intensity 0.8 --rotation 0.5 --duration 8`.

- **loopfade**: Create a seamless looping video by crossfading the end back to the start. Requires a `--background-video` and an explicit `--duration`. Key option: `--crossfade-duration`. Example: `effectgenerator --effect loopfade --background-video input.mp4 --duration 10 --crossfade-duration 1.5 --output loop.mp4`.

- **waves**: Water wave ripples with interference, warmup, directional lighting and optional pixel displacement. Key options: `--sources`, `--amplitude`, `--frequency`, `--speed`, `--decay`, `--direction`, `--warmup`, `--light-angle`, `--light-intensity`, `--interference`, `--no-displacement`, `--displacement-scale`, `--spawn-prob`, `--offscreen-prob`, `--min-lifetime`, `--max-lifetime`. Example: `effectgenerator --effect waves --amplitude 0.6 --direction right --warmup 30 --duration 12`.

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
