# Makefile for Effect Generator
# Works on Linux, macOS, and Windows (MinGW)

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra

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

# Source files
SOURCES = main.cpp effect_generator.cpp snowflake_effect.cpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^
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
	@echo "  make clean    - Remove build artifacts"
	@echo "  make install  - Install to /usr/local/bin (Linux/macOS)"
	@echo "  make uninstall- Uninstall from /usr/local/bin (Linux/macOS)"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Usage after building:"
	@echo "  ./$(TARGET) --help"
	@echo "  ./$(TARGET) --list-effects"
	@echo "  ./$(TARGET) --effect snowflake --flakes 200"

.PHONY: all clean install uninstall help
