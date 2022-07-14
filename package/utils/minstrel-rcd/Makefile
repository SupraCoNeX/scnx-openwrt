#
# Copyright (C) 2021 Felix Fietkau <nbd@nbd.name>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=minstrel-rcd
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)-$(PKG_RELEASE)

PKG_CONFIG_DEPENDS:= \
  CONFIG_MQTT_SUPPORT \
  CONFIG_ZSTD_COMPRESSION

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/minstrel-rcd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Control daemon for mac80211 minstrel rate control
  DEPENDS:=+libubox +libuci +MQTT_SUPPORT:libmosquitto-ssl +ZSTD_COMPRESSION:libzstd
  MAINTAINER:=Felix Fietkau <nbd@nbd.name>
endef

define Package/minstrel-rcd/description
  Daemon for remote controlling mac80211 minstrel rate control
  algorithm.
endef

define Package/minstrel-rcd/conffiles
/etc/config/minstrel-rcd
endef

define Package/minstrel-rcd/config
	config MQTT_SUPPORT
	  bool "mqtt support"
	  default n

	config ZSTD_COMPRESSION
	  bool "zstd compression"
	  default n
endef

ifeq ($(CONFIG_MQTT_SUPPORT),y)
	CMAKE_OPTIONS += -DCMAKE_CONFIG_MQTT=y
else
	CONFFILE:=minstrel-rcd.config
endif

ifeq ($(CONFIG_ZSTD_COMPRESSION),y)
	CMAKE_OPTIONS += -DCMAKE_CONFIG_ZSTD=y
	DICTFILE:=dictionary.zdict
endif

define Package/minstrel-rcd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) ./files/minstrel-rcd.init $(1)/etc/init.d/minstrel-rcd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/minstrel-rcd $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/minstrel-rcd.config $(1)/etc/config/minstrel-rcd
ifeq ($(CONFIG_ZSTD_COMPRESSION),y)
	$(INSTALL_DIR) $(1)/lib/minstrel-rcd
	$(INSTALL_DATA) ./files/$(DICTFILE) $(1)/lib/minstrel-rcd/dictionary.zdict
endif
endef

$(eval $(call BuildPackage,minstrel-rcd))

