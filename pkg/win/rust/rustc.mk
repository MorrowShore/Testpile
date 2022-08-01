# This file is part of MXE. See LICENSE.md for licensing information.

PKG                     := rustc
$(PKG)_WEBSITE          := https://www.rust-lang.org/
$(PKG)_VERSION          := 1.62.0
$(PKG)_DEPS_$(BUILD)    := cc

ifneq (, $(findstring darwin,$(BUILD)))
    BUILD_TRIPLET = $(firstword $(call split,-,$(BUILD)))-apple-darwin
else
    ifneq (, $(findstring linux,$(BUILD)))
        BUILD_TRIPLET = $(firstword $(call split,-,$(BUILD)))-unknown-linux-gnu
    else
        BUILD_TRIPLET = $(BUILD)
    endif
endif

TARGET_TRIPLET          = $(firstword $(call split,-,$(TARGET)))-pc-windows-gnu
$(PKG)_FILE             := $(PKG)-$($(PKG)_VERSION)-$(BUILD_TRIPLET).tar.gz
$(PKG)_URL              := https://static.rust-lang.org/dist/$($(PKG)_FILE)

# CHECKSUMS
CHECKSUM_rustc-1.62.0-x86_64-unknown-linux-gnu      :=  892752484043f7a129f7c80fb5b71c7745fbfd8542d4ed4c7c7f18898a7add16

$(PKG)_CHECKSUM         := $(CHECKSUM_$(PKG)-$($(PKG)_VERSION)-$(BUILD_TRIPLET))
$(PKG)_TARGETS          := $(BUILD)

define $(PKG)_UPDATE
    stable_date := $(shell curl https://static.rust-lang.org/dist/channel-rust-stable-date.txt)
    $(WGET) -q -O- 'https://static.rust-lang.org/dist/$(stable_date)/' | \
    $(SED) -n 's,.*$(PKG)-\([0-9][^>]*\)\.tar.*,\1,p' | \
    grep -v 'alpha\|beta\|rc\|git\|nightly' | \
    $(SORT) -Vr | \
    head -1
endef

define $(PKG)_BUILD
    $(SOURCE_DIR)/$(PKG)-$($(PKG)_VERSION)-$(BUILD_TRIPLET)/install.sh --prefix=$(PREFIX)/$(BUILD) --verbose
endef