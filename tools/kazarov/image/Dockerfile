# Toolchain image for Chronicle and ChronicleTwo. Built locally, one tag per
# checkout state: docker build --build-arg KZ_IMAGE_ID=<tag> -t kazarov-dev:<tag> image
#
# linux/amd64 only: wibo runs the Windows-hosted Metrowerks compiler and linker
# and needs a real x86_64 Linux kernel.
#
# Debian for glibc (the toolchain binaries are glibc-linked), trixie because
# binutils-mips-ps2-decompals needs glibc 2.38.

# SatansFiddle, the compiler wrapper that stands in for statefix in ChronicleTwo.
FROM debian:trixie-slim AS satansfiddle-build
ARG SATANSFIDDLE_REV=365415fa2fd7bf69e899044b704b571a64ed4e6c
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates git cargo build-essential liblldb-19-dev \
    && rm -rf /var/lib/apt/lists/*
ENV LLDB_INCLUDE=/usr/lib/llvm-19/include
ENV LLDB_LIB_DIR=/usr/lib/llvm-19/lib
WORKDIR /satansfiddle
RUN git init \
    && git remote add origin https://github.com/Adubbz/SatansFiddle.git \
    && git fetch --depth 1 origin ${SATANSFIDDLE_REV} \
    && git checkout --detach FETCH_HEAD
COPY patches/satansfiddle-nested-arguments.patch /tmp/satansfiddle-nested-arguments.patch
RUN git apply --check /tmp/satansfiddle-nested-arguments.patch \
    && git apply /tmp/satansfiddle-nested-arguments.patch
COPY patches/satansfiddle-control-context.patch /tmp/satansfiddle-control-context.patch
RUN git apply --check /tmp/satansfiddle-control-context.patch \
    && git apply /tmp/satansfiddle-control-context.patch
COPY patches/satansfiddle-placement-new.patch /tmp/satansfiddle-placement-new.patch
RUN git apply --check /tmp/satansfiddle-placement-new.patch \
    && git apply /tmp/satansfiddle-placement-new.patch
COPY patches/satansfiddle-external-constructors.patch /tmp/satansfiddle-external-constructors.patch
RUN git apply --check /tmp/satansfiddle-external-constructors.patch \
    && git apply /tmp/satansfiddle-external-constructors.patch
# Rust 1.85 can place native libraries before the LLDB C++ archive, so the
# stage-local flags supply the trailing libraries to the CLI, the test runners
# and the dependency build scripts.
ENV RUSTFLAGS="-L native=/usr/lib/llvm-19/lib -C link-arg=-llldb -C link-arg=-lstdc++"
RUN cargo build --release --locked --jobs 3 \
    && cp target/release/satansfiddle /satansfiddle-production \
    && cargo test --release --locked --jobs 3 --features hook-test-faults \
    && mkdir /satansfiddle-tests \
    && cp target/release/satansfiddle /satansfiddle-tests/satansfiddle-fault-test \
    && find target/release/deps -maxdepth 1 -name 'compiler_cli-*' -type f -executable \
        -exec cp {} /satansfiddle-tests/ \;

#
# Image
#
FROM debian:trixie-slim

# Tool versions
ARG BINUTILS_VERSION=v0.10
ARG CLANGD_VERSION=22.1.6
ARG OBJDIFF_VERSION=v3.7.3
ARG LLVM_VERSION=23

ENV DEBIAN_FRONTEND=noninteractive
ENV BINUTILS=/usr/local/binutils-mips-ps2-decompals
ENV VIRTUAL_ENV=/opt/venv
ENV PATH=${VIRTUAL_ENV}/bin:/usr/lib/llvm-${LLVM_VERSION}/bin:${PATH}:${BINUTILS}
ENV M2C=/opt/kazarov/m2c/m2c.py
ENV MWCCPS2_DEBUGGER_DIR=/opt/kazarov/mwccps2-debugger
ENV KZ_MWCC_DEBUG_DIR=/opt/kazarov/mwcc-debug
ENV LLDB_DEBUGSERVER_PATH=/usr/lib/llvm-19/bin/lldb-server

# Build tools, debuggers, and the libraries the PC port links against.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        coreutils \
        git \
        gnupg \
        sudo \
        unzip \
        util-linux \
        wget \
        less \
        python3 \
        python3-venv \
        cmake \
        ninja-build \
        gdb \
        lldb-19 \
        build-essential \
        doxygen \
        libsdl3-dev \
        libvulkan-dev \
        mesa-vulkan-drivers \
        vulkan-validationlayers \
        nlohmann-json3-dev \
        glslang-tools \
        libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# clang, lld and llvm for the PC port. Debian stops at LLVM 19, so these come
# from apt.llvm.org and install under /usr/lib/llvm-<version>/bin.
RUN wget -qO /etc/apt/trusted.gpg.d/apt.llvm.org.asc https://apt.llvm.org/llvm-snapshot.gpg.key \
    && echo "deb http://apt.llvm.org/trixie/ llvm-toolchain-trixie-${LLVM_VERSION} main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-${LLVM_VERSION} \
        lld-${LLVM_VERSION} \
        llvm-${LLVM_VERSION} \
    && rm -rf /var/lib/apt/lists/*

# Assembler, objcopy, objdump, readelf and nm for the PS2 decompilation projects.
RUN wget -O /tmp/binutils.tar.gz \
        https://github.com/decompals/binutils-mips-ps2-decompals/releases/download/${BINUTILS_VERSION}/binutils-mips-ps2-decompals-linux-x86-64.tar.gz \
    && mkdir -p ${BINUTILS} \
    && tar xzf /tmp/binutils.tar.gz -C ${BINUTILS} \
    && rm /tmp/binutils.tar.gz

# wibo runs the Windows-hosted Metrowerks compiler and linker. The pinned image
# carries the unstripped loader symbols that the LLDB hooks need.
COPY --from=ghcr.io/decompals/wibo@sha256:3a89948cf841cd6ae2555eb9d8b8ba60c35700852afc5fd9bd62660ef3d201f5 /usr/local/bin/wibo /usr/bin/
COPY --from=satansfiddle-build /satansfiddle-production /usr/local/bin/satansfiddle
COPY --from=satansfiddle-build /satansfiddle-tests/ /usr/local/libexec/satansfiddle-tests/

# clangd formats the tree with the same clang-format it bundles; the two must
# agree on .clang-format, so both come from the same version.
RUN wget -O /tmp/clangd.zip \
        https://github.com/clangd/clangd/releases/download/${CLANGD_VERSION}/clangd-linux-${CLANGD_VERSION}.zip \
    && unzip -q /tmp/clangd.zip -d /opt \
    && ln -s /opt/clangd_${CLANGD_VERSION}/bin/clangd /usr/local/bin/clangd \
    && ln -s /opt/clangd_${CLANGD_VERSION}/bin/clangd /usr/bin/clangd \
    && rm /tmp/clangd.zip

# Single static binary that compares each unit against retail.
RUN wget -O /usr/local/bin/objdiff-cli \
        https://github.com/encounter/objdiff/releases/download/${OBJDIFF_VERSION}/objdiff-cli-linux-x86_64 \
    && chmod +x /usr/local/bin/objdiff-cli

# splat is the disassembler; its MIPS support is an extra and must be named.
# libclang turns the headers into the context m2c reads. graphviz is what m2c
# imports; m2c vendors its own pycparser.
RUN python3 -m venv ${VIRTUAL_ENV} \
    && ${VIRTUAL_ENV}/bin/python -m pip install --no-cache-dir \
        pycdlib "splat64[mips]==0.50.0" libclang "graphviz~=0.20.1" toml levenshtein

# m2c and mwccps2-debugger, pinned by commit.
RUN mkdir -p /opt/kazarov/m2c /opt/kazarov/mwccps2-debugger \
    && git -C /opt/kazarov/m2c init \
    && git -C /opt/kazarov/m2c remote add origin https://github.com/matt-kempster/m2c \
    && git -C /opt/kazarov/m2c fetch --depth 1 origin 708d2d2cb2698f091a92492b328f73b24209f72d \
    && git -C /opt/kazarov/m2c checkout --detach FETCH_HEAD \
    && git -C /opt/kazarov/mwccps2-debugger init \
    && git -C /opt/kazarov/mwccps2-debugger remote add origin https://github.com/Raikaru/mwccps2-debugger \
    && git -C /opt/kazarov/mwccps2-debugger fetch --depth 1 origin 3bca7ed2146f85d4e3be478b3d166fce77724499 \
    && git -C /opt/kazarov/mwccps2-debugger checkout --detach FETCH_HEAD

# MWCC 2.3.3 capture tools: capture.py, gdb_capture.py, replay.py and the profile.
COPY mwcc-debug /opt/kazarov/mwcc-debug
RUN chmod -R a+rX /opt/kazarov

# Minimal LGPL FFmpeg (MPEG-2 video decoder only) for the PC port's movies, linked statically.
COPY kz-ffmpeg /opt/kazarov/bin/kz-ffmpeg
RUN apt-get update \
    && apt-get install -y --no-install-recommends nasm \
    && rm -rf /var/lib/apt/lists/* \
    && python3 /opt/kazarov/bin/kz-ffmpeg --out /opt/kazarov/ffmpeg --jobs "$(nproc)" \
    && chmod -R a+rX /opt/kazarov/ffmpeg
ENV KZ_FFMPEG_DIR=/opt/kazarov/ffmpeg

# Last layer, so a new KZ_IMAGE_ID rebuilds nothing else.
ARG KZ_IMAGE_ID
RUN printf '%s\n' "$KZ_IMAGE_ID" > /opt/kazarov/image-id

