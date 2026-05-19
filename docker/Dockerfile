FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4Toolchain
ENV PATH=$OO_PS4_TOOLCHAIN/bin/linux:$PATH

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates \
      clang-15 \
      curl \
      lld-15 \
      llvm-15 \
      make \
      tar \
    && rm -rf /var/lib/apt/lists/*

RUN ln -sf /usr/bin/clang-15 /usr/local/bin/clang && \
    ln -sf /usr/bin/ld.lld-15 /usr/local/bin/ld.lld

ARG OO_TOOLCHAIN_URL=https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz
RUN mkdir -p /opt/OpenOrbis && \
    curl -L "$OO_TOOLCHAIN_URL" -o /tmp/oo.tar.gz && \
    tar -xzf /tmp/oo.tar.gz -C /opt/OpenOrbis --strip-components=1 && \
    rm /tmp/oo.tar.gz && \
    chmod +x $OO_PS4_TOOLCHAIN/bin/linux/* 2>/dev/null || true

WORKDIR /work

CMD ["make", "prx"]
