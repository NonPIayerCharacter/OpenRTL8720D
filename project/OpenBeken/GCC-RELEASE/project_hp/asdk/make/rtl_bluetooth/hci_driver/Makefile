include $(MAKE_INCLUDE_GEN)

.PHONY: all clean

#*****************************************************************************#
#                                  VARIABLES                                  #
#*****************************************************************************#
DIR = $(BASEDIR)/component/common/bluetooth/realtek
MODULE_IFLAGS += \
	-I$(DIR)/sdk/example/hci_driver/hci \
	-I$(DIR)/sdk/example/hci_driver/inc \
	-I$(DIR)/sdk/example/hci_driver/platform/amebad/inc

CSRC += \
	$(DIR)/sdk/example/hci_driver/hci/hci_process/hci_process.c \
	$(DIR)/sdk/example/hci_driver/hci/hci_process/hci_standalone.c \
	$(DIR)/sdk/example/hci_driver/hci/hci_transport/hci_h4.c \
	$(DIR)/sdk/example/hci_driver/hci/hci_if_rtk.c \
	$(DIR)/sdk/example/hci_driver/platform/amebad/hci/hci_platform.c \
	$(DIR)/sdk/example/hci_driver/platform/amebad/hci/hci_uart.c

OBJS = $(CSRC:.c=.o)
TOBJS = $(notdir $(OBJS))
LOBJS = $(addprefix objs/,$(TOBJS))

#*****************************************************************************#
#                          RULES TO GENERATE TARGETS                          #
#*****************************************************************************#

# LIBBLUETOOTH_OBJS_PATH = $(shell pwd)/objs
# Define the Rules to build the core targets
all: CORE_TARGETS COPY_RAM_OBJS
	# mkdir -p $(LIBBLUETOOTH_OBJS_PATH)
	# for i in $(OBJS); do \
	# 	 $(COPY)  $$i $(LIBBLUETOOTH_OBJS_PATH) -f;\
	# 	done
	# $(REMOVE) *.i
	# $(REMOVE) *.s
	# $(REMOVE) *.d

#*****************************************************************************#
#                            GENERATE OBJECT FILE                             #
#*****************************************************************************#
CORE_TARGETS: $(OBJS)

#*****************************************************************************#
#                           RULES TO CLEAN TARGETS                            #
#*****************************************************************************#
clean: CLEAN_OBJS
	$(REMOVE) *.o
	$(REMOVE) *.i
	$(REMOVE) *.s
	$(REMOVE) *.d

-include $(DEPS)
