# Stage 1: Build sysroot from Ubuntu 18.04
FROM ubuntu:18.04 AS sysroot-builder

RUN apt-get update && apt-get install -y \
    libc6-dev \
    libstdc++-8-dev \
    tar

RUN mkdir /sysroot && \
    tar -czf /sysroot/sysroot-glibc-2.27.tar.gz \
        /lib \
        /usr/lib \
        /usr/include

# Stage 2: Final builder image based on Ubuntu 22.04
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/bin:${PATH}"

RUN apt-get update && apt-get install -y \
    build-essential \
    curl \
    zip \
    unzip \
    gnupg \
    python3 \
    git \
    cmake \
    ca-certificates \
    xz-utils

# Install LLVM 19.1.7 (Clang + LLD etc.)
RUN curl -LO https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/LLVM-19.1.7-Linux-X64.tar.xz && \
    mkdir -p /opt/clang-19 && \
    tar -xf LLVM-19.1.7-Linux-X64.tar.xz -C /opt/clang-19 --strip-components=1 && \
    ln -sf /opt/clang-19/bin/clang /usr/bin/clang && \
    ln -sf /opt/clang-19/bin/clang++ /usr/bin/clang++ && \
    rm LLVM-19.1.7-Linux-X64.tar.xz

# Install Bazel 8.1.1
RUN curl -LO https://github.com/bazelbuild/bazel/releases/download/8.1.1/bazel-8.1.1-installer-linux-x86_64.sh && \
    chmod +x bazel-8.1.1-installer-linux-x86_64.sh && \
    ./bazel-8.1.1-installer-linux-x86_64.sh --user && \
    ln -s /root/bin/bazel /usr/local/bin/bazel && \
    rm bazel-8.1.1-installer-linux-x86_64.sh

# Extract sysroot
COPY --from=sysroot-builder /sysroot/sysroot-glibc-2.27.tar.gz /opt/
RUN mkdir -p /opt/sysroot-glibc-2.27 && \
    tar -xzf /opt/sysroot-glibc-2.27.tar.gz -C /opt/sysroot-glibc-2.27

ENV SYSROOT=/opt/sysroot-glibc-2.27
ENV CC="clang --sysroot=$SYSROOT"
ENV CXX="clang++ --sysroot=$SYSROOT"

WORKDIR /src
