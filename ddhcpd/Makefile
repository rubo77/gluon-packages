include $(TOPDIR)/rules.mk

PKG_NAME:=ddhcpd
PKG_VERSION:=0.0.1
PKG_RELEASE:=$(GLUON_BRANCH)

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(GLUONDIR)/include/package.mk

define Package/ddhcpd
  SECTION:=gluon
  CATEGORY:=Gluon
  DEPENDS:=+gluon-core
  TITLE:=ddhcpd
endef

define Package/ddhcpd/description
	Addhcpd
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile
endef

define Package/ddhcpd/install
	$(CP) ./files/* $(1)/
endef

$(eval $(call BuildPackage,ddhcpd))
