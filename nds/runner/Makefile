ifeq ($(MOD),gkd_pixel2)
export CROSS=aarch64-linux-
export PATH=/opt/flip/bin:$(shell echo $$PATH)
CFLAGS  += -DGKD_PIXEL2
CFLAGS  += -I/opt/flip/aarch64-flip-linux-gnu/sysroot/usr/include/SDL2
LDFLAGS += -ljson-c
endif

ifeq ($(MOD),gkd_miniplus)
export CROSS=aarch64-linux-
export PATH=/opt/flip/bin:$(shell echo $$PATH)
CFLAGS  += -DGKD_MINIPLUS
CFLAGS  += -I/opt/flip/aarch64-flip-linux-gnu/sysroot/usr/include/SDL2
LDFLAGS += -ljson-c
endif

ifeq ($(MOD),trimui_brick)
export CROSS=aarch64-linux-gnu-
export PATH=/opt/brick/bin:$(shell echo $$PATH)
CFLAGS  += -DTRIMUI_BRICK
CFLAGS  += -I/opt/brick/usr/include
CFLAGS  += -I/opt/brick/usr/include/SDL2
LDFLAGS += -lrt
LDFLAGS += -lusc
LDFLAGS += -lIMGegl
LDFLAGS += -lPVROCL
LDFLAGS += -ljson-c
LDFLAGS += -lsrv_um
LDFLAGS += -lpthread
LDFLAGS += -lglslcompiler
LDFLAGS += -L/opt/brick/usr/lib
endif

ifeq ($(MOD),r36s)
CFLAGS  += -DR36S
CFLAGS  += -I../sdl2/include
CFLAGS  += -I../include
CFLAGS  += -I..
CFLAGS  += -I../json-c
LDFLAGS += -L../sdl2/build/.libs
LDFLAGS += -L../common
LDFLAGS += -L../detour
LDFLAGS += -lcommon
LDFLAGS += -ldtr
LDFLAGS += -ljson-c
LDFLAGS += -lSDL2
LDFLAGS += -lSDL2_image
LDFLAGS += -lSDL2_ttf
LDFLAGS += -lEGL
LDFLAGS += -lGLESv2
LDFLAGS += -lpng16
LDFLAGS += -ljpeg
LDFLAGS += -ltiff
LDFLAGS += -lwebp
LDFLAGS += -ldrm
LDFLAGS += -lfreetype
LDFLAGS += -lz
LDFLAGS += -llzma
LDFLAGS += -ljbig
LDFLAGS += -lzstd
LDFLAGS += -lrt
LDFLAGS += -lpthread
LDFLAGS += -lm
LDFLAGS += -Wl,-rpath-link,../common
LDFLAGS += -Wl,-rpath-link,../detour
LDFLAGS += -Wl,-rpath-link,../sdl2/build/.libs
endif

export CC=${CROSS}gcc
export AR=${CROSS}ar
export AS=${CROSS}as
export LD=${CROSS}ld
export CXX=${CROSS}g++

REL_VER  = $(shell git rev-parse HEAD | cut -c 1-8)
CFLAGS  += -DREL_VER=0x$(REL_VER)

TARGET   = runner
CFLAGS  += -DRUNNER
CFLAGS  += -O3
CFLAGS  += -I.
CFLAGS  += -I../common
LDFLAGS += -lSDL2
LDFLAGS += -lSDL2_image
LDFLAGS += -lEGL
LDFLAGS += -lGLESv2
SOURCES  = runner.c ../common/common.c
OBJS = runner.o common.o

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

runner.o: runner.c
	$(CC) -c runner.c -o runner.o $(CFLAGS)

common.o: ../common/common.c
	$(CC) -c ../common/common.c -o common.o $(CFLAGS)

.PHONY: clean
clean:
	rm -rf $(TARGET) $(OBJS)
