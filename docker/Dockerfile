FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        gdb \
        git \
        ccache \
        entr \
        jq \
        curl \
        wget \
        ca-certificates \
        vim \
        less \
        tree \
        procps \
        strace \
        ripgrep \
        qt6-base-dev \
        qt6-svg-dev \
        libqt6svg6 \
        libgl1-mesa-dri \
        libgl1 \
        libglx-mesa0 \
        libxkbcommon-x11-0 \
        libxkbcommon-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/workspace/docker/watch.sh"]
