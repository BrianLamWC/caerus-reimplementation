# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -g -std=c++17 $(shell pkg-config --cflags protobuf)

# Protobuf inputs and generated files
PROTO_DIR = ../proto
PROTO_PROTO = $(PROTO_DIR)/request.proto $(PROTO_DIR)/graph_snapshot.proto
PROTO_SRC = $(PROTO_DIR)/request.pb.cc $(PROTO_DIR)/graph_snapshot.pb.cc
PROTO_HDR = $(PROTO_DIR)/request.pb.h $(PROTO_DIR)/graph_snapshot.pb.h
PROTOC_VERSION = $(shell protoc --version | sed 's/[^A-Za-z0-9_.-]/_/g')
PROTO_VERSION_STAMP = $(PROTO_DIR)/.$(PROTOC_VERSION).stamp
PROTO_LIBS = $(shell pkg-config --libs protobuf)

# All .cpp files in the current directory
SRC = main.cpp

# Corresponding .o files
OBJ = $(SRC:.cpp=.o)

# Output executable
TARGET = main

# Rule to build the target
all: $(TARGET)

$(TARGET): $(PROTO_SRC) $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(PROTO_SRC) $(PROTO_LIBS)

$(OBJ): $(PROTO_HDR)

$(PROTO_VERSION_STAMP):
	@touch $@

$(PROTO_SRC) $(PROTO_HDR) &: $(PROTO_PROTO) $(PROTO_VERSION_STAMP)
	protoc -I=$(PROTO_DIR) --cpp_out=$(PROTO_DIR) $(PROTO_PROTO)

# Rule to build .o files from .cpp files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up object files and executable
clean:
	rm -f $(OBJ) $(TARGET)

# PHONY to prevent conflicts with files named "clean"
.PHONY: clean
