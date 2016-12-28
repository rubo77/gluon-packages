include $(TOPDIR)/rules.mk

PKG_NAME:=ddhcpd
PKG_VERSION:=0.0.1
PKG_RELEASE:=$(GLUON_BRANCH)

PKG_INSTALL := 1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(GLUONDIR)/include/package.mk

define Package/ddhcpd
  SECTION:=gluon
  CATEGORY:=Gluon
  DEPENDS:=+gluon-core
  TITLE:=ddhcpd
endef

define Package/ddhcpd/description
	ddhcpd
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ddhcpd/* $(PKG_BUILD_DIR)
endef

#hier darf kein Compile block!

define Package/ddhcpd/install
  # $(CP) ./files/* $(1)/
	$(CP) $(PKG_INSTALL_DIR)/* $(1)/
endef

$(eval $(call BuildPackage,ddhcpd))
