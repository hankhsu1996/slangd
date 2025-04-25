# Stage 1: Build sysroot from Ubuntu 18.04
FROM ubuntu:18.04 AS sysroot-builder

RUN apt-get update && apt-get install -y \
    libc6-dev \
    libstdc++-8-dev \
    tar

RUN mkdir /sysroot && \
    tar -czf /sysroot/sysroot-glibc-2.27.tar.gz \
        /lib /lib64 /usr/lib /usr/lib64 /usr/include

# Stage 2: Final builder image based on Ubuntu 22.04
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/bin:${PATH}"

RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    lld \
    curl \
    zip \
    unzip \
    gnupg \
    python3 \
    git \
    cmake \
    ca-certificates

RUN curl -LO https://github.com/bazelbuild/bazel/releases/download/8.1.1/bazel-8.1.1-installer-linux-x86_64.sh && \
    chmod +x bazel-8.1.1-installer-linux-x86_64.sh && \
    ./bazel-8.1.1-installer-linux-x86_64.sh --user && \
    ln -s /root/bin/bazel /usr/local/bin/bazel && \
    rm bazel-8.1.1-installer-linux-x86_64.sh

COPY --from=sysroot-builder /sysroot/sysroot-glibc-2.27.tar.gz /opt/
RUN mkdir -p /opt/sysroot-glibc-2.27 && \
    tar -xzf /opt/sysroot-glibc-2.27.tar.gz -C /opt/sysroot-glibc-2.27

ENV SYSROOT=/opt/sysroot-glibc-2.27
ENV CC="clang --sysroot=$SYSROOT"
ENV CXX="clang++ --sysroot=$SYSROOT"

WORKDIR /src
