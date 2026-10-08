# LK2 NDS demake build environment: BlocksDS + asset tools (python/pillow, ffmpeg, .NET for FastVideoDS encoder)
FROM skylyrac/blocksds:slim-latest

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        make git ca-certificates gcc libc6-dev \
        python3 python3-pil python3-numpy \
        ffmpeg libavcodec60 libavformat60 libavutil58 libswscale7 libswresample4 \
        dotnet-sdk-8.0 \
    && rm -rf /var/lib/apt/lists/*

ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 \
    DOTNET_NOLOGO=1 \
    FFMPEG_LIB_DIR=/usr/lib/x86_64-linux-gnu

WORKDIR /work
