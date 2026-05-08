# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -g -std=c++17 $(shell pkg-config --cflags protobuf)

# Additional protobuf flags and files
PROTO_SRC = ../proto/request.pb.cc ../proto/graph_snapshot.pb.cc
PROTO_LIBS = $(shell pkg-config --libs protobuf)

# All .cpp files in the current directory
SRC = main.cpp

# Corresponding .o files
OBJ = $(SRC:.cpp=.o)

# Output executable
TARGET = main

# Rule to build the target
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(PROTO_SRC) $(PROTO_LIBS)

# Rule to build .o files from .cpp files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up object files and executable
clean:
	rm -f $(OBJ) $(TARGET)

# PHONY to prevent conflicts with files named "clean"
.PHONY: clean
