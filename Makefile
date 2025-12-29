# Makefile for Effect Generator
# Works on Linux, macOS, and Windows (MinGW)

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
STATIC_LDFLAGS = -static -static-libgcc -static-libstdc++

# Detect OS
ifeq ($(OS),Windows_NT)
    # Windows (MinGW)
    TARGET = effectgenerator.exe
    RM = del /Q
    RMDIR = rmdir /S /Q
else
    # Linux/macOS
    TARGET = effectgenerator
    RM = rm -f
    RMDIR = rm -rf
endif

# Cross-compile for Windows from Linux (MinGW-w64)
CROSS_PREFIX ?= x86_64-w64-mingw32-
WINDOWS_CXX = $(CROSS_PREFIX)g++
WINDOWS_TARGET = effectgenerator.exe
WINDOWS_LDFLAGS =
WINDOWS_STATIC_LDFLAGS = -static -static-libgcc -static-libstdc++
WINDOWS_DLLS = libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll
WINDOWS_DLL_DIR = .

define copy_dll
	src="$$( $(WINDOWS_CXX) -print-file-name=$(1) )"; \
	if [ "$$src" = "$(1)" ] || [ ! -f "$$src" ]; then \
		echo "Missing $(1) in toolchain (searched via $(WINDOWS_CXX))."; \
		exit 1; \
	fi; \
	echo "Copying $$src -> $(WINDOWS_DLL_DIR)/"; \
	cp "$$src" "$(WINDOWS_DLL_DIR)/"
endef

# Source files
SOURCES = main.cpp effect_generator.cpp json_util.cpp snowflake_effect.cpp laser_effect.cpp loopfade_effect.cpp wave_effect.cpp starfield_effect.cpp twinkle_effect.cpp fireworks_effect.cpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Build complete: $(TARGET)"

# Compile
%.o: %.cpp effect_generator.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	$(RM) $(OBJECTS) $(TARGET)
	@echo "Clean complete"

# Install (Linux/macOS only)
install: $(TARGET)
	@if [ "$(OS)" != "Windows_NT" ]; then \
		cp $(TARGET) /usr/local/bin/; \
		echo "Installed to /usr/local/bin/$(TARGET)"; \
	else \
		echo "Install target not supported on Windows. Copy $(TARGET) to a directory in your PATH."; \
	fi

# Uninstall (Linux/macOS only)
uninstall:
	@if [ "$(OS)" != "Windows_NT" ]; then \
		rm -f /usr/local/bin/$(TARGET); \
		echo "Uninstalled from /usr/local/bin/$(TARGET)"; \
	fi

# Help
help:
	@echo "Effect Generator Build System"
	@echo "=============================="
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the application"
	@echo "  make windows  - Cross-compile for Windows (MinGW-w64)"
	@echo "  make windows-static - Cross-compile for Windows (static runtime)"
	@echo "  make windows-dlls - Copy MinGW runtime DLLs next to the .exe"
	@echo "  make static   - Build Linux static binary (x64)"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make install  - Install to /usr/local/bin (Linux/macOS)"
	@echo "  make uninstall- Uninstall from /usr/local/bin (Linux/macOS)"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Cross-compile notes:"
	@echo "  Requires MinGW-w64 (e.g., x86_64-w64-mingw32-g++)"
	@echo "  Override toolchain with: make windows CROSS_PREFIX=... (e.g., i686-w64-mingw32-)"
	@echo ""
	@echo "Usage after building:"
	@echo "  ./$(TARGET) --help"
	@echo "  ./$(TARGET) --list-effects"
	@echo "  ./$(TARGET) --effect snowflake --flakes 200"

.PHONY: all clean install uninstall help windows windows-static windows-dlls static

# Cross-compile target (Linux/macOS host)
windows: CXX = $(WINDOWS_CXX)
windows: TARGET = $(WINDOWS_TARGET)
windows: LDFLAGS = $(WINDOWS_LDFLAGS)
windows: $(TARGET)

# Cross-compile with static runtime (no libstdc++/libgcc DLLs needed)
windows-static: CXX = $(WINDOWS_CXX)
windows-static: TARGET = $(WINDOWS_TARGET)
windows-static: LDFLAGS = $(WINDOWS_STATIC_LDFLAGS)
windows-static: $(TARGET)

# Copy MinGW runtime DLLs next to the Windows binary (for Wine/Windows)
windows-dlls: CXX = $(WINDOWS_CXX)
windows-dlls:
	@$(foreach dll,$(WINDOWS_DLLS),$(call copy_dll,$(dll));)

# Build a static Linux binary (best effort; may require static libs installed)
static: TARGET = effectgenerator-static
static: LDFLAGS = $(STATIC_LDFLAGS)
static: $(TARGET)
