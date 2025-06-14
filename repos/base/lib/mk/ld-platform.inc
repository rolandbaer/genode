SHARED_LIB = yes
DIR        = $(BASE_DIR)/src/lib/ldso

LIB_IS_DYNAMIC_LINKER = yes

include $(BASE_DIR)/mk/base-libs.mk

LIBS         = $(BASE_LIBS)
SRC_CC       = main.cc test.cc exception.cc dependency.cc debug.cc \
               shared_object.cc
SRC_S        = jmp_slot.s
INC_DIR     += $(DIR)/include
INC_DIR     += $(BASE_DIR)/src/include
LD_OPT      += -Bsymbolic-functions --version-script=symbol.map
LD_OPT      += --dynamic-list=$(DIR)/ld.dl

# enable use of 'check_abi'
SYMBOLS := $(BASE_DIR)/lib/symbols/ld

#
# Generate symbol map from the ABI
#
$(LIB).lib.so: symbol.map
$(LIB).abi.so: symbol.map
symbol.map: $(MAKEFILE_LIST)
symbol.map: $(BASE_DIR)/lib/symbols/ld
	$(MSG_CONVERT)$@
	$(VERBOSE)(echo -e "{\n\tglobal:";\
	           sed -n "s/^\(\w\+\) .*/\t\t\1;/p" $<;\
	           echo -e "\tlocal: *;\n};") > $@

ifneq ($(filter linux, $(SPECS)),)

ENTRY_POINT = _start_initial_stack
LD_SCRIPT_SO = $(call select_from_repositories,src/ld/stack_area.ld) \
               -T$(BASE_DIR)/src/ld/genode_rel.ld

ifneq ($(filter x86_32, $(SPECS)),)
LD_OPT     += -T$(DIR)/linux-32.ld
endif

#
# Set dynamic linker ELF type to executable
#
ifeq ($(called_from_lib_mk),yes)
$(LIB).lib.so.stripped: postprocess.tag
postprocess.tag: $(LIB).lib.so
	$(VERBOSE)printf "\x02" |\
	          dd of=$(LIB).lib.so bs=1 seek=16 count=1 conv=notrunc \
	          2> /dev/null
	$(VERBOSE)touch $@
endif

else
ENTRY_POINT = _start
LD_OPT     += -T$(DIR)/linker.ld
endif

vpath %.cc $(DIR)

# vi:ft=make

