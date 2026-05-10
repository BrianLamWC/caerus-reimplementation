# Base image
FROM ubuntu:22.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    g++ \
    pkg-config \
    libprotobuf-dev \
    protobuf-compiler \
    uuid-dev \
    openssh-client \
    rsync \
    jq \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy all source code into the image
COPY . .

RUN make -C Server
RUN make -C test

# Set working directory to Server
WORKDIR /app/Server

# Set default command (change if you want client/server by default)
CMD ["/bin/bash"]

# Expose ports (change if needed)
EXPOSE 8001 7001
