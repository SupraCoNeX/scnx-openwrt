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
	CONFIG_MINSTREL_RCD_NO_MQTT \
	CONFIG_MINSTREL_RCD_MQTT

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/minstrel-rcd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Control daemon for mac80211 minstrel rate control
  DEPENDS:=+libubox +libuci +MINSTREL_RCD_MQTT:libmosquitto-ssl
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
	default MINSTREL_RCD_NO_MQTT

	config MINSTREL_RCD_NO_MQTT
		bool "No"

	config MINSTREL_RCD_MQTT
		bool "Yes"
endchoice
endef

ifeq ($(CONFIG_MINSTREL_RCD_NO_MQTT),y)
	CONFFILE:=minstrel-rcd.config
endif

ifeq ($(CONFIG_MINSTREL_RCD_MQTT),y)
	CONFFILE:=minstrel-rcd-mqtt.config
	CMAKE_OPTIONS += -DCONFIG_MQTT=1
endif

define Package/minstrel-rcd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) ./files/minstrel-rcd.init $(1)/etc/init.d/minstrel-rcd
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/minstrel-rcd $(1)/usr/sbin/
	$(INSTALL_DATA) ./files/$(CONFFILE) $(1)/etc/config/minstrel-rcd
endef

$(eval $(call BuildPackage,minstrel-rcd))

