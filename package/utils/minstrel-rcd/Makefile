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
	CONFIG_MINSTREL_RCD_MQTT_SSL \
	CONFIG_MINSTREL_RCD_MQTT_NOSSL

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/minstrel-rcd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Control daemon for mac80211 minstrel rate control
  DEPENDS:=+libubox +libuci +MINSTREL_RCD_MQTT_NOSSL:libmosquitto-nossl +MINSTREL_RCD_MQTT_SSL:libmosquitto-ssl
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
choice
	prompt "mqtt support"
	default MINSTREL_RCD_NONE

	config MINSTREL_RCD_NONE
		bool "None"

	config MINSTREL_RCD_MQTT_SSL
		bool "with SSL"

	config MINSTREL_RCD_MQTT_NOSSL
		bool "without SSL"
endchoice
endef

ifeq ($(CONFIG_MINSTREL_RCD_NONE),y)
	CONFFILE:=minstrel-rcd.config
endif

ifeq ($(CONFIG_MINSTREL_RCD_MQTT_NOSSL),y)
	CONFFILE:=minstrel-rcd-mqtt-nossl.config
	TARGET_CFLAGS += -DCONFIG_MQTT
	CMAKE_OPTIONS:="-DCONFIG_MQTT=1"
endif

ifeq ($(CONFIG_MINSTREL_RCD_MQTT_SSL),y)
	CONFFILE:=minstrel-rcd-mqtt-ssl.config
	TARGET_CFLAGS += -DCONFIG_MQTT
	CMAKE_OPTIONS:="-DCONFIG_MQTT=1"
endif

define Package/minstrel-rcd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) ./files/minstrel-rcd.init $(1)/etc/init.d/minstrel-rcd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/minstrel-rcd $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/$(CONFFILE) $(1)/etc/config/minstrel-rcd
endef

$(eval $(call BuildPackage,minstrel-rcd))

