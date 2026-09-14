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
        php-cli \
        qt6-base-dev \
        qt6-svg-dev \
        libqt6svg6 \
        libgl1-mesa-dri \
        libgl1 \
        libglx-mesa0 \
        libxkbcommon-x11-0 \
        libxkbcommon-dev \
    && rm -rf /var/lib/apt/lists/*

# PowerShell (pwsh): os testes do helper `kip` e do completion em PowerShell
# (test_kip_shell_helper, test_completion_installer) pulam sem ele. Repositório
# da Microsoft para o Ubuntu 24.04; camada separada pra não refazer o apt acima.
RUN curl -fsSL -o /tmp/packages-microsoft-prod.deb \
        https://packages.microsoft.com/config/ubuntu/24.04/packages-microsoft-prod.deb \
    && dpkg -i /tmp/packages-microsoft-prod.deb \
    && rm /tmp/packages-microsoft-prod.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends powershell \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/workspace/docker/watch.sh"]
