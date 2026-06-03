ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := wmma_vv_rvv

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

CONFIGS += -DEXT_V_ENABLE -DVLEN=256 -DNUM_THREADS=8

KERNEL_LIB := vortex2

include ../common.mk
