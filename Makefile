#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
APP_TITLE	:=	OpenHomeNX
APP_VERSION :=	0.2.74
APP_AUTHOR	:=	JostenSyon

TARGET		:=	OpenHomeNX
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	romfs

RUST_LIB   := $(TOPDIR)/rust/target/aarch64-unknown-none/release/libopenhome_switch.a

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -fdata-sections -flto -fuse-linker-plugin \
			$(ARCH) $(DEFINES)

# APP_VERSION / APP_AUTHOR arrivano da include/app_version.h (generato dal target
# `genversion`), NON da -D: così un bump di APP_VERSION rigenera l'header e i .o
# che lo includono si ricompilano (con -D restavano stale -> "aggiorna ma sono
# ancora alla vecchia versione").
CFLAGS	+=	$(INCLUDE) -D__SWITCH__

# Debug logger attivo di default. Il toggle a runtime è il file
# <base>/debug.enable (di norma sdmc:/switch/OpenHomeNX/), overhead ~0 se assente.
# Per escluderlo del tutto dal binario: make DEBUG_LOG=0
ifneq ($(DEBUG_LOG),0)
CFLAGS	+=	-DOH_DEBUG_LOG
endif

# USB update: cerca un NRO più recente su drive USB (libusbhsfs, vendorizzata in
# ./libusbhsfs — build FAT-only ISC). Disattiva con: make USB_UPDATE=0
# Diagnosi "physical=0 mounted=0" (2026-09): make USB_LIB_DEBUG=1 collega la
# variante debug (libusbhsfsd.a, già vendorizzata) che logga i fallimenti di
# mount che l'API release non espone. SOLO per una build di diagnosi — non
# usarla per una release.
USBHSFS_DIR := $(TOPDIR)/libusbhsfs
USBHSFS_LIBNAME := $(if $(filter 1,$(USB_LIB_DEBUG)),usbhsfsd,usbhsfs)
ifneq ($(USB_UPDATE),0)
ifneq ($(wildcard $(USBHSFS_DIR)/lib/lib$(USBHSFS_LIBNAME).a),)
CFLAGS	+=	-DOH_USB_UPDATE -I$(USBHSFS_DIR)/include
USBHSFS_LIBS := -L$(USBHSFS_DIR)/lib -l$(USBHSFS_LIBNAME)
endif
endif

CXXFLAGS	:= $(CFLAGS) -fno-exceptions -ffunction-sections -fdata-sections -std=c++20

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:=	-lSDL2_image -lSDL2_ttf -lSDL2 \
			-lfreetype -lharfbuzz -lpng16 -ljpeg -lwebp -lz -lbz2 \
			-lEGL -lGLESv2 -lglapi -ldrm_nouveau \
			-lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
			$(USBHSFS_LIBS) \
			-lnx

LIBPATHS	:=	-L$(shell dirname $(RUST_LIB)) \
			-L$(LIBNX)/lib \
			-L$(PORTLIBS)/lib

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CURDIR) $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(LIBPATHS)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all release genversion

# `genversion` is defined before `all`, which would make it the implicit
# default goal — bare `make` (and build.sh) would then only regenerate the
# version header and skip the actual .nro build. Pin the default back to `all`.
.DEFAULT_GOAL := all

VERSION_HDR := $(TOPDIR)/include/app_version.h

# Rigenera include/app_version.h solo se il contenuto cambia (niente rebuild
# inutili). I sorgenti che mostrano/confrontano la versione lo #include-ano.
genversion:
	@printf '#pragma once\n#define APP_VERSION "%s"\n#define APP_AUTHOR "%s"\n#define BUILD_SHA "%s%s"\n' \
	  '$(APP_VERSION)' '$(APP_AUTHOR)' '$(shell git -C $(TOPDIR) rev-parse --short=7 HEAD 2>/dev/null || echo nogit)' '$(shell [ -z "$$(git -C $(TOPDIR) status --porcelain 2>/dev/null)" ] || echo -dirty)' > $(VERSION_HDR).tmp
	@if cmp -s $(VERSION_HDR).tmp $(VERSION_HDR) 2>/dev/null; then rm -f $(VERSION_HDR).tmp; \
	 else mv -f $(VERSION_HDR).tmp $(VERSION_HDR); echo "genversion -> v$(APP_VERSION)"; fi

#---------------------------------------------------------------------------------
all: genversion $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(TOPDIR)/Makefile

#---------------------------------------------------------------------------------
# Impacchetta la build corrente in dist/ per l'updater di rete: il .nro +
# latest.json { version, nro, sha256 }. Punta qui il webserver locale
# (`python3 -m http.server -d dist 8000`) o carica dist/* nelle release GitHub.
release: genversion all
	@mkdir -p dist
	@cp -f $(TARGET).nro dist/$(TARGET).nro
	@sha=`shasum -a 256 dist/$(TARGET).nro | cut -d' ' -f1`; \
	 printf '{\n  "version": "%s",\n  "nro": "%s.nro",\n  "sha256": "%s"\n}\n' \
	   "$(APP_VERSION)" "$(TARGET)" "$$sha" > dist/latest.json
	@echo "release -> dist/ (v$(APP_VERSION))"; cat dist/latest.json

#---------------------------------------------------------------------------------
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
	@cd $(TOPDIR)/rust && cargo +nightly clean


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@echo "built ... $(notdir $@)"

$(OUTPUT).elf	:	$(OFILES) $(RUST_LIB)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBPATHS) -lopenhome_switch $(LIBS)

# La lib Rust va ricostruita quando cambia QUALSIASI sorgente Rust (prima la
# regola non aveva prerequisiti: se il .a esisteva, make non lo rifaceva mai
# e il link falliva/stallava su simboli nuovi o vecchi).
RUST_SRCS := $(shell find $(TOPDIR)/rust -name '*.rs' -o -name 'Cargo.toml' -o -name 'Cargo.lock' 2>/dev/null)
$(RUST_LIB): $(RUST_SRCS)
	cd $(TOPDIR)/rust && cargo +nightly build -p openhome_switch --target aarch64-unknown-none --release

$(OFILES_SRC)	: $(HFILES_BIN) $(TOPDIR)/include/app_version.h

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------