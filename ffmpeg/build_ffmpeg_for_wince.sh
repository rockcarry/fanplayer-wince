#!/bin/bash
set -e


#++ build x264 ++#
if false; then
if [ ! -d x264 ]; then
  git clone git://git.videolan.org/x264.git
fi
cd x264
./configure \
--enable-strip \
--enable-static
make -j8 && make install
cd -
fi
#-- build x264 --#


#++ build ffmpeg ++#
if [ ! -d ffmpeg ]; then
  git clone -b fanplayer-n3.3.x https://github.com/rockcarry/ffmpeg
fi
cd ffmpeg
sed -i 's/-D__printf__=__gnu_printf__/-D__printf__=__printf__/g' configure
./configure \
--arch=arm \
--cpu=armv6 \
--target-os=mingw32ce \
--enable-cross-compile \
--cross-prefix=arm-mingw32ce- \
--prefix=$PWD/../ffmpeg-wince-sdk \
--enable-static \
--enable-shared \
--enable-small \
--disable-symver \
--disable-debug \
--disable-doc \
--disable-programs \
--disable-avdevice \
--disable-postproc \
--disable-encoders \
--disable-muxers   \
--disable-filters  \
--disable-swscale-alpha \
--disable-outdev=sdl2 \
--enable-muxer=mjpeg \
--enable-muxer=apng \
--enable-muxer=mp4 \
--enable-muxer=flv \
--enable-filter=yadif \
--enable-filter=rotate \
--enable-filter=scale \
--enable-filter=movie \
--enable-filter=overlay \
--enable-filter=hflip \
--enable-filter=vflip \
--enable-asm \
--enable-gpl \
--enable-version3 \
--enable-nonfree
sed -i 's/-D__printf__=__printf__/-D__printf__=__gnu_printf__/g' configure
sed -i 's/#define HAVE_COPYSIGN 0/#define HAVE_COPYSIGN 1/g' config.h
sed -i 's/#define HAVE_MAPVIEWOFFILE 1/#define HAVE_MAPVIEWOFFILE 0/g' config.h
make -j8 && make install
cd -
#++ build ffmpeg ++#

echo done

