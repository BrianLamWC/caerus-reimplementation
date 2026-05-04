# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -g -std=c++17 $(shell pkg-config --cflags protobuf)

# Additional protobuf flags and files
PROTO_SRC = ../proto/request.pb.cc ../proto/graph_snapshot.pb.cc
PROTO_LIBS = $(shell pkg-config --libs protobuf)

# Build directory
BUILDDIR = build

# All .cpp files in the current directory
SRC = $(wildcard *.cpp)

# Corresponding .o files in the build directory
OBJ = $(patsubst %.cpp,$(BUILDDIR)/%.o,$(SRC))

# Output executable in the build directory
TARGET = $(BUILDDIR)/main

# Rule to build the target executable
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(PROTO_SRC) $(PROTO_LIBS)

# Pattern rule to build .o files in the build directory from .cpp files
$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up the build directory
clean:
	rm -rf $(BUILDDIR)

# PHONY targets to avoid name conflicts with files
.PHONY: all clean
