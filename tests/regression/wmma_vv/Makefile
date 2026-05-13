ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := wmma_vv

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

CONFIGS += -DEXT_V_ENABLE -DEXT_TCU_ENABLE -DNUM_THREADS=8 -DVLEN=256

KERNEL_LIB := vortex2

include ../common.mk
