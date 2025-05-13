#!/bin/bash

# Detect platform
OS="$(uname -s)"

case "$OS" in
    Linux*)
        PLATFORM="linux"
        ;;
    Darwin*)
        PLATFORM="macos"
        ;;
    MINGW* | MSYS* | CYGWIN*)
        PLATFORM="windows"
        ;;
    *)
        echo "Unsupported platform: $OS"
        exit 1
        ;;
esac


# Set platform-specific flags
PREFIX=""
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""
EXTRA_LIBS=""
TARGET_OS=""
ARCH="x86_64"

if [ "$PLATFORM" = "linux" ]; then
    PREFIX="$HOME/ffmpeg_build_lgpl"
    EXTRA_CFLAGS="-I$HOME/ffmpeg_build_lgpl/include"
    EXTRA_LDFLAGS="-L$HOME/ffmpeg_build_lgpl/lib"
    EXTRA_LIBS="-lpthread -lm -lstdc++"
    TARGET_OS="linux"
elif [ "$PLATFORM" = "macos" ]; then
    PREFIX="./builds"
    EXTRA_LIBS="-lpthread -lm -lc++"
    TARGET_OS="darwin"
elif [ "$PLATFORM" = "windows" ]; then
    PREFIX="./builds"
    EXTRA_LIBS="-lpthread -lm -lstdc++"
    TARGET_OS="mingw32"
fi

./configure \
  --target-os="$TARGET_OS" \
  --arch="$ARCH" \
  --prefix="$PREFIX" \
  --pkg-config-flags="--static" \
  --extra-cflags="$EXTRA_CFLAGS" \
  --extra-ldflags="$EXTRA_LDFLAGS" \
  --extra-libs="$EXTRA_LIBS" \
  --cc=gcc \
  --cxx=g++ \
  --enable-shared \
  --disable-static \
  --disable-doc \
  --disable-debug \
  --enable-small \
  \
  --enable-version3 \
  --disable-gpl \
  --disable-nonfree \
  \
  --disable-decoder=prores \
  --disable-encoder=prores \
  --disable-encoder=prores_ks \
  --disable-encoder=prores_aw \
  \
  --enable-decoder=prores_apple \
  \
  --enable-protocol=file \
  --enable-protocol=pipe \
  \
  --enable-demuxer=mov \
  --enable-demuxer=wav \
  --enable-demuxer=aac \
  --enable-demuxer=mp3 \
  --enable-demuxer=matroska \
  --enable-demuxer=flac \
  --enable-demuxer=ogg \
  --enable-demuxer=avi \
  \
  --enable-muxer=mov \
  --enable-muxer=mp4 \
  --enable-muxer=matroska \
  --enable-muxer=image2 \
  --enable-muxer=wav \
  --enable-muxer=aac \
  --enable-muxer=ogg \
  --enable-muxer=flac \
  \
  --enable-parser=prores \
  --enable-parser=h264 \
  --enable-parser=mpeg4video \
  --enable-parser=hevc \
  \
  --enable-decoder=aac \
  --enable-decoder=ac3 \
  --enable-decoder=mp3 \
  --enable-decoder=flac \
  --enable-decoder=vorbis \
  --enable-decoder=opus \
  --enable-decoder=h264 \
  --enable-decoder=mpeg4 \
  --enable-decoder=hevc \
  --enable-decoder=png \
  --enable-decoder=rawvideo \
  --enable-decoder=mjpeg \
  --enable-decoder=vp8 \
  --enable-decoder=vp9 \
  --enable-decoder=theora \
  \
  --enable-encoder=rawvideo \
  --enable-encoder=png \
  --enable-encoder=aac \
  --enable-encoder=libmp3lame \
  \
  --enable-libmp3lame \
  \
  --enable-bsf=extract_extradata \
  --enable-bsf=h264_mp4toannexb \
  --enable-bsf=hevc_mp4toannexb \
  --enable-bsf=prores_metadata
