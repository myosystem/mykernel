# ==============================================================
# mykernel — OS kernel
# ==============================================================
# Usage:
#   make              → Debug 빌드  → bin/x64/Debug/mykernel.efi
#                                   → ../_bootpartition/.../EFI/BOOT/os.bin
#                                   → ../_bootpartition/.../EFI/BOOT/*.o (stub)
#   make stub         → stub/ 만 빌드
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
  -msoft-float -mno-red-zone \
  -fno-stack-protector -Werror=return-type -g

ifeq ($(CONFIG),Release)
  CXXFLAGS += -O2
else
  CXXFLAGS += -O0
endif

ALLOC ?= fpa
ifeq ($(ALLOC),slab)
  CXXFLAGS += -DSLAB_NEW
endif

ifdef TEST_MODE
  CXXFLAGS += -DTEST_MODE
endif

# 함수별 스택 프레임 크기 측정 (.su 파일 생성). 코드 생성엔 영향 없음.
CXXFLAGS += -fstack-usage

INCLUDES := -Iinclude

LDFLAGS  := -T linker.ld -nostdlib

OUTDIR    := bin/$(PLATFORM)/$(CONFIG)
TARGET    := $(OUTDIR)/mykernel.efi
BOOT_OUT  := ../_bootpartition/$(PLATFORM)/$(CONFIG)/EFI/BOOT/os.bin
BOOT_DIR  := ../_bootpartition/$(PLATFORM)/$(CONFIG)/EFI/BOOT

# ---------------------------------------------------------------
# 소스 파일 — stub/ 제외하고 *.cpp 자동 수집
# ---------------------------------------------------------------
SRCS := $(shell find . -path './stub' -prune -o -name '*.cpp' -print)

OBJS := $(addprefix $(OUTDIR)/,$(SRCS:.cpp=.o))

# ---------------------------------------------------------------
# stub — *.S 전부를 하나로 링크해 평평한 바이너리로. 커널이 signal.o 라는 이름으로 읽는다.
# ---------------------------------------------------------------
STUB_SRCS := $(shell find stub -name '*.S' 2>/dev/null)
STUB_OBJS := $(addprefix $(OUTDIR)/,$(STUB_SRCS:.S=.o))
STUB_ELF  := $(OUTDIR)/stub/signal.elf
STUB_OUT  := $(BOOT_DIR)/signal.o

# ---------------------------------------------------------------

.PHONY: all stub clean

all: $(TARGET) stub

stub: $(STUB_OBJS)
	@mkdir -p $(BOOT_DIR) $(OUTDIR)/stub
	ld -nostdlib -Ttext=0 -o $(STUB_ELF) $(STUB_OBJS)
	$(OBJCOPY) -O binary \
	  --only-section=.text --only-section=.rodata --only-section=.data \
	  $(STUB_ELF) $(STUB_OUT)
	@echo "[stub] → $(STUB_OUT)"

$(TARGET): $(OBJS)
	@mkdir -p $(OUTDIR)
	$(LD) $(LDFLAGS) -o $@ $^
	@mkdir -p $(BOOT_DIR)
	$(OBJCOPY) -O binary \
	  --only-section=.text --only-section=.rodata --only-section=.data \
	  $(TARGET) $(BOOT_OUT)
	@echo "[mykernel] done → $(TARGET)"
	@echo "[mykernel] objcopy → $(BOOT_OUT)"

# stub .S 빌드 규칙
$(OUTDIR)/stub/%.o: stub/%.S
	@mkdir -p $(dir $@)
	$(CXX) -m64 -c $< -o $@

# 서브디렉터리 오브젝트 파일 규칙
$(OUTDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf bin
