#!/bin/bash

DIR="${2:-./}"
  
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
EXTRA_CONFIGURE_FLAGS=""
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""
EXTRA_LIBS=""
TARGET_OS=""
ARCH="x86_64"
COMPILER="--cc=gcc --cxx=g++"
  
if [ "$PLATFORM" = "linux" ]; then
    PREFIX="$HOME/ffmpeg_build_lgpl"
    EXTRA_CFLAGS="-I$HOME/ffmpeg_build_lgpl/include"
    EXTRA_LDFLAGS="-L$HOME/ffmpeg_build_lgpl/lib"
    EXTRA_LIBS="-lpthread -lm -lstdc++"
    TARGET_OS="linux"
elif [ "$PLATFORM" = "macos" ]; then
    ARCH=$1
    COMPILER="--cc=clang --cxx=clang++"
    PREFIX="./builds/${ARCH}"
    EXTRA_CONFIGURE_FLAGS="--disable-asm"
    
    if [ "$ARCH" = "x86_64" ]; then
        EXTRA_CFLAGS="-I/usr/local/include -arch ${ARCH}"
        EXTRA_LDFLAGS="-L/usr/local/lib"
    else
        EXTRA_CFLAGS="-I/opt/homebrew/include -arch ${ARCH}"
        EXTRA_LDFLAGS="-L/opt/homebrew/lib"
    fi
    EXTRA_LDFLAGS+=" -Wl,-rpath,@loader_path/../Frameworks -arch ${ARCH}"
    EXTRA_LIBS="-lpthread -lm -lc++"
    TARGET_OS="darwin"

    # comment for release
    EXTRA_CONFIGURE_FLAGS+=" --enable-debug --disable-optimizations --disable-stripping"
    EXTRA_CFLAGS+=" -O0 -g"
    #EXTRA_CONFIGURE_FLAGS+=" --disable-debug"
  
elif [ "$PLATFORM" = "windows" ]; then
    PREFIX="./builds"
    EXTRA_LIBS="-lpthread -lm -lstdc++"
    TARGET_OS="mingw32"
fi

echo "Configuring for " ${ARCH}

$DIR/configure \
  --target-os="$TARGET_OS" \
  ${EXTRA_CONFIGURE_FLAGS} \
  --arch=${ARCH} \
  --prefix="$PREFIX" \
  --pkg-config-flags="--static" \
  --extra-cflags="$EXTRA_CFLAGS" \
  --extra-ldflags="$EXTRA_LDFLAGS" \
  --extra-libs="$EXTRA_LIBS" \
  ${COMPILER} \
  --enable-shared \
  --disable-static \
  --disable-doc \
  --enable-small \
  --disable-devices \
  --disable-filters \
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
  --enable-bsf=prores_metadata || exit 255
