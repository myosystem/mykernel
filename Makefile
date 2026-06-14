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
  -msoft-float -mno-red-zone -g

ifeq ($(CONFIG),Release)
  CXXFLAGS += -O2
else
  CXXFLAGS += -O0
endif

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
# stub — *.S 파일 → 별도 .o 산출물
# ---------------------------------------------------------------
STUB_SRCS := $(shell find ./stub -name '*.S' 2>/dev/null)
STUB_OBJS := $(addprefix $(OUTDIR)/,$(STUB_SRCS:.S=.o))

# ---------------------------------------------------------------

.PHONY: all stub clean

all: $(TARGET) stub

stub: $(STUB_OBJS)
	@mkdir -p $(BOOT_DIR)
	@for obj in $(STUB_OBJS); do \
	  cp $$obj $(BOOT_DIR)/$$(basename $$obj); \
	  echo "[stub] → $(BOOT_DIR)/$$(basename $$obj)"; \
	done

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
