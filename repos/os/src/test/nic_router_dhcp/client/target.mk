TARGET = test-nic_router_dhcp-client

LIBS += base net

SRC_CC += main.cc dhcp_client.cc ipv4_address_prefix.cc
SRC_CC += nic.cc ipv4_config.cc

INC_DIR += $(PRG_DIR) $(REP_DIR)/src/server/nic_router

vpath ipv4_address_prefix.cc $(REP_DIR)/src/server/nic_router

CC_CXX_WARN_STRICT_CONVERSION =
