# makefile for RGE project
# written by rockcarry

# 编译器定义
CC      = arm-mingw32ce-gcc
STRIP   = arm-mingw32ce-strip
CCFLAGS = -Wall -I../ffmpeg/include
LDFLAGS = -L../ffmpeg/lib

# 所有的目标文件
OBJS = \
    adev-cmn.o \
    adev-win.o \
    vdev-cmn.o \
    vdev-gdi.o \
    ffrender.o \
    pktqueue.o \
    ffplayer.o

# 所有的可执行目标
EXES = \
    testplayer.exe

# 输出的库文件
DLL = fanplayer.dll

# 编译规则
all : $(DLL) $(EXES)

$(DLL) : $(OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $(OBJS) -lavutil -lavformat -lavcodec -lavfilter -lswresample -lswscale
	$(STRIP) $@

%.o : %.c %.h stdefine.h
	$(CC) $(CCFLAGS) -c -o $@ $<

%.o : %.c stdefine.h
	$(CC) $(CCFLAGS) -c -o $@ $<

%.exe : %.c $(DLL)
	$(CC) $(CCFLAGS) -o $@ $< $(DLL)
	$(STRIP) $@

clean :
	-rm -f *.o
	-rm -f *.dll
	-rm -f *.exe

# rockcarry
# 2018.8.10
# THE END



