FROM ubuntu:22.04 AS builder

# Base + toolchain
RUN apt update && apt install -y \
    software-properties-common

# GCC 13
RUN add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
    apt update && apt install -y \
    gcc-13 g++-13

ENV CC=gcc-13
ENV CXX=g++-13

# Deps système
RUN apt install -y \
    make \
    perl \
    cmake \
    git \
    curl \
    pkg-config \
    libsodium-dev \
    nlohmann-json3-dev \
    libicu-dev \
    zlib1g-dev \
    ca-certificates

# -------- OpenSSL 3 --------
WORKDIR /tmp

RUN curl -LO https://www.openssl.org/source/openssl-3.2.1.tar.gz && \
    tar -xzf openssl-3.2.1.tar.gz && \
    cd openssl-3.2.1 && \
    ./Configure linux-x86_64 no-shared --prefix=/opt/openssl-3 && \
    make -j$(nproc) && \
    make install_sw

ENV OPENSSL_ROOT_DIR=/opt/openssl-3
ENV OPENSSL_LIBRARIES=/opt/openssl-3/lib
ENV OPENSSL_INCLUDE_DIR=/opt/openssl-3/include

# -------- CMake 3.31 --------
RUN curl -L https://github.com/Kitware/CMake/releases/download/v3.31.0/cmake-3.31.0-linux-x86_64.tar.gz \
    | tar -xz -C /opt

ENV PATH="/opt/cmake-3.31.0-linux-x86_64/bin:${PATH}"

# -------- Build UTX --------
WORKDIR /build
COPY . .

RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
    -DOPENSSL_ROOT_DIR=/opt/openssl-3

RUN cmake --build build -j

RUN strip /build/build/utx

# -------- Export --------
FROM ubuntu:20.04
COPY --from=builder /build/build/utx /usr/local/bin/utx
CMD ["utx"]