#!/bin/bash

./configure \
  --prefix="$HOME/ffmpeg_build_lgpl" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I$PWD/external/prores_apple -I$HOME/ffmpeg_build_lgpl/include" \
  --extra-ldflags="-L$PWD/external/prores_apple -L$HOME/ffmpeg_build_lgpl/lib" \
  --extra-libs="-lpthread -lm -lProRes64 -lstdc++" \
  --bindir="$HOME/bin_lgpl" \
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
  --disable-decoder=prores_ks \
  --disable-decoder=prores_aw \
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
