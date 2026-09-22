# OpenHomeNX - build per host (macOS di prova) e per R36S (ArkOS, via Docker).
# Compila i sorgenti originali di OpenHomeNX SENZA modificarli, usando:
#   - shim/switch.h  -> primo nell'include path, fornisce tipi libnx no-op
#   - stub/*.cpp     -> sostituiscono account/led/rete/update/forwarder
#   - crate Rust     -> libopenhome_switch.a (--features std)
# Directory "romfs:" e "sdmc:" (symlink) risolvono i path stile Switch.

# Repo-root relativo: platform/r36s -> repo root (niente path assoluti macchina).
TOPDIR  := $(CURDIR)
PROJ    := $(abspath $(TOPDIR)/../..)
BUILD   := $(TOPDIR)/build

# ---- host build (macOS di prova) ----
TARGET_ARCH := aarch64
TARGET_OS   := apple-darwin
RUST_TARGET := $(TARGET_ARCH)-$(TARGET_OS)
CXX         ?= clang++
PKG         ?= pkg-config
# Nome pacchetto pkg-config per SDL2_image/ttf (varia: macOS=minuscolo, Debian=Maiuscolo)
SDL2IMG     ?= sdl2_image
SDL2TTF     ?= sdl2_ttf
RUST_LIB    ?= $(PROJ)/rust/target/$(RUST_TARGET)/release/libopenhome_switch.a
RUST_FLAGS  := -p openhome_switch --features std

CXXFLAGS    := -std=c++20 -O2 -Wall -DOH_LINUX -DOH_DEBUG_LOG \
               -MMD -MP \
               -include $(TOPDIR)/shim/switch.h \
               -include $(TOPDIR)/shim/r36s_fix.h \
               -I$(TOPDIR)/shim \
               -I$(PROJ)/include \
               -I$(PROJ)/source \
               $(shell $(PKG) --cflags sdl2 $(SDL2IMG) $(SDL2TTF))

# ---- sorgenti ----
# Rete reale su Linux (update_net/remote_sync/autoupdate usano curl + shim
# socket/nifm/thread POSIX) — stub_network non serve piu'.
SRC_CORE := \
  backpack bank bank_manager boxart crypto_engine debug_log emulator form_names \
  handler_update handler_update_ffi i18n import_paths import_scan item_locations \
  md5 move_types poke_crypto pokedex pokedex_ffi pokemon pokemon_ffi \
  save_file save_file_ffi sc_block settings_cfg species_converter swish_crypto \
  theme trade_evo wondercard rominfo ui ui_backpack ui_bank ui_gallery ui_input ui_render ui_selectors \
  update_net autoupdate remote_sync

STUB_SRC := stub_system stub_forwarder stub_account

OFILES := $(addprefix $(BUILD)/,$(addsuffix .o,$(SRC_CORE) $(STUB_SRC) main))

# ---- regole ----
.PHONY: all rust clean run r36s docker-image

all: $(TOPDIR)/OpenHomeNX

# mbedcrypto statico: R36S ha mbedtls 3.x (.so.16), Debian bookworm linka
# la 2.x (.so.7) — statico evita il mismatch. curl resta dinamico (presente).
$(TOPDIR)/OpenHomeNX: $(OFILES) $(RUST_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $(OFILES) \
		$(RUST_LIB) \
		$(shell $(PKG) --libs sdl2 $(SDL2IMG) $(SDL2TTF)) -lpthread -ldl -lcurl -Wl,-Bstatic -lmbedcrypto -Wl,-Bdynamic

$(BUILD)/%.o: $(PROJ)/source/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/stub_%.o: $(TOPDIR)/stub/stub_%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

# -MMD -MP (sopra) genera un .d per ogni .o con le dipendenze sugli header
# inclusi: senza questo, cambiare un .h in $(PROJ)/include (es. ui.h) non
# invalidava i .o compilati in giri precedenti che lo includono transitivamente
# -- risultato: alcune TU ricompilate con il nuovo layout di una classe,
# altre linkate da .o vecchi con l'offset dei campi diverso -> mismatch ABI,
# segfault immediato e silenzioso in avvio (visto dal vivo aggiungendo due
# campi a UI). Include i .d se esistono; se BUILD viene cambiato da riga di
# comando (vedi target r36s sotto) il glob risolve comunque nella dir giusta.
-include $(wildcard $(BUILD)/*.d)

$(RUST_LIB):
	cd $(PROJ)/rust && cargo +nightly build $(RUST_FLAGS) --release --target $(RUST_TARGET)

rust:
	cd $(PROJ)/rust && cargo +nightly build $(RUST_FLAGS) --release --target $(RUST_TARGET)

run: $(TOPDIR)/OpenHomeNX
	cd $(TOPDIR) && ./OpenHomeNX

clean:
	rm -rf $(BUILD) $(TOPDIR)/OpenHomeNX

# ---- R36S: build in container Docker (arm64, veloce su Mac M4) ----
# Crea l'immagine e compila sia il crate Rust sia il C++ per aarch64-linux.
IMG ?= ohnx-r36s
docker-image:
	docker build -f $(TOPDIR)/Dockerfile.r36s -t $(IMG) $(TOPDIR)

# Mount del progetto OpenHomeNX (sorgenti) + della dir di build r36s.
# Dentro: CXX=g++, PKG=pkg-config, TARGET_OS=unknown-linux-gnu, RUST_TARGET
# impostato e CARGO_TARGET_DIR su volume per velocita'.
r36s: docker-image
	docker run --rm --platform linux/arm64 \
		-v $(PROJ):/src/OpenHomeNX \
		-v $(TOPDIR):/src/r36s \
		-w /src/OpenHomeNX \
		-e CARGO_TARGET_DIR=/src/r36s/rust-target \
		-e PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig \
		$(IMG) bash -lc "cd /src/r36s && \
			make PROJ=/src/OpenHomeNX CXX=g++ TARGET_OS=unknown-linux-gnu \
				BUILD=/src/r36s/build-r36s \
				SDL2IMG=SDL2_image SDL2TTF=SDL2_ttf \
				RUST_TARGET=aarch64-unknown-linux-gnu \
				RUST_LIB=/src/r36s/rust-target/aarch64-unknown-linux-gnu/release/libopenhome_switch.a \
				RUST_FLAGS='-p openhome_switch --features std' \
			"