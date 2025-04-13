FROM --platform=linux/amd64 ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    autoconf \
    automake \
    libtool \
    curl \
    cmake \
    git \
    pkg-config \
    nodejs \
    npm

ENV CAPNP_VERSION=1.1.0

RUN curl -O https://capnproto.org/capnproto-c++-${CAPNP_VERSION}.tar.gz \
    && tar -xzf capnproto-c++-${CAPNP_VERSION}.tar.gz
    
RUN cd capnproto-c++-${CAPNP_VERSION} \
    && ./configure \
    && make -j$(nproc) \
    && make install \
    && ldconfig \
    && cd .. \
    && rm -rf capnproto-c++-${CAPNP_VERSION} capnproto-c++-${CAPNP_VERSION}.tar.gz

RUN apt-get install -y software-properties-common && \
    add-apt-repository ppa:ubuntu-toolchain-r/test && \
    apt-get install -y gcc-13 g++-13

RUN rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
