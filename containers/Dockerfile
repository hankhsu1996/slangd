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
    software-properties-common \
    debootstrap \
    ca-certificates

RUN curl -LO https://github.com/bazelbuild/bazel/releases/download/8.1.1/bazel-8.1.1-installer-linux-x86_64.sh && \
    chmod +x bazel-8.1.1-installer-linux-x86_64.sh && \
    ./bazel-8.1.1-installer-linux-x86_64.sh --user && \
    ln -s /root/bin/bazel /usr/local/bin/bazel && \
    rm bazel-8.1.1-installer-linux-x86_64.sh

RUN debootstrap --variant=minbase --arch=amd64 bionic /opt/sysroot-glibc-2.27 http://old-releases.ubuntu.com/ubuntu/

ENV SYSROOT=/opt/sysroot-glibc-2.27
ENV CC="clang --sysroot=$SYSROOT"
ENV CXX="clang++ --sysroot=$SYSROOT"

WORKDIR /src
