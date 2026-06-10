# ==============================================================
# mykernel — OS kernel
# ==============================================================
# Usage:
#   make              → Debug 빌드  → bin/x64/Debug/mykernel.efi
#                                   → ../_bootpartition/.../EFI/BOOT/os.bin
#   make CONFIG=Release
#   make clean
# ==============================================================

CONFIG   ?= Debug
PLATFORM := x64

CXX     := g++
LD      := g++
OBJCOPY := objcopy

CXXFLAGS := \
  -m64 -std=c++20 -masm=intel -Werror \
  -ffreestanding -fno-rtti -fno-exceptions \
  -fno-zero-initialized-in-bss -fno-common \
  -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mno-80387 \
  -msoft-float -mno-red-zone -g

ifeq ($(CONFIG),Release)
  CXXFLAGS += -O2
else
  CXXFLAGS += -O0
endif

INCLUDES := -Iinclude

LDFLAGS  := -T linker.ld -nostdlib

OUTDIR   := bin/$(PLATFORM)/$(CONFIG)
TARGET   := $(OUTDIR)/mykernel.efi
BOOT_OUT := ../_bootpartition/$(PLATFORM)/$(CONFIG)/EFI/BOOT/os.bin

# ---------------------------------------------------------------
# 소스 파일 — 지정 디렉터리에서 *.cpp 자동 수집
# ---------------------------------------------------------------
SRCS := $(shell find . -name '*.cpp')

OBJS := $(addprefix $(OUTDIR)/,$(SRCS:.cpp=.o))

# ---------------------------------------------------------------

.PHONY: all clean

all: $(TARGET)
	@mkdir -p $(dir $(BOOT_OUT))
	$(OBJCOPY) -O binary \
	  --only-section=.text --only-section=.rodata --only-section=.data \
	  $(TARGET) $(BOOT_OUT)
	@echo "[mykernel] done → $(TARGET)"
	@echo "[mykernel] objcopy → $(BOOT_OUT)"

$(TARGET): $(OBJS)
	@mkdir -p $(OUTDIR)
	$(LD) $(LDFLAGS) -o $@ $^

# 서브디렉터리 오브젝트 파일 규칙
$(OUTDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf bin
