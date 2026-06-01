ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := wmma_vv_overlap

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

CONFIGS += -DEXT_V_ENABLE -DEXT_TCU_ENABLE -DVLEN=256

KERNEL_LIB := vortex2

include ../common.mk
