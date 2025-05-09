ROOT_DIR := $(realpath ../..)
include $(ROOT_DIR)/config.mk

all:
	$(MAKE) -C basic
	$(MAKE) -C demo
	$(MAKE) -C dogfood
	$(MAKE) -C dropout
	$(MAKE) -C dotproductx
	$(MAKE) -C mstress
	$(MAKE) -C io_addr
	$(MAKE) -C printf
	$(MAKE) -C diverge
	$(MAKE) -C sort
	$(MAKE) -C fence
	$(MAKE) -C vecaddx
	$(MAKE) -C sgemmx
	$(MAKE) -C conv3x
	$(MAKE) -C relu
	$(MAKE) -C sgemm2x
	$(MAKE) -C stencil3d

run-simx:
	$(MAKE) -C basic run-simx
	$(MAKE) -C demo run-simx
	$(MAKE) -C dogfood run-simx
	$(MAKE) -C dropout run-simx
	$(MAKE) -C dotproductx run-simx
	$(MAKE) -C mstress run-simx
	$(MAKE) -C io_addr run-simx
	$(MAKE) -C printf run-simx
	$(MAKE) -C diverge run-simx
	$(MAKE) -C sort run-simx
	$(MAKE) -C fence run-simx
	$(MAKE) -C vecaddx run-simx
	$(MAKE) -C sgemmx run-simx
	$(MAKE) -C conv3x run-simx
	$(MAKE) -C relu run-simx
	$(MAKE) -C sgemm2x run-simx
	$(MAKE) -C stencil3d run-simx

run-rtlsim:
	$(MAKE) -C basic run-rtlsim
	$(MAKE) -C demo run-rtlsim
	$(MAKE) -C dogfood run-rtlsim
	$(MAKE) -C dropout run-rtlsim
	$(MAKE) -C dotproductx run-rtlsim
	$(MAKE) -C mstress run-rtlsim
	$(MAKE) -C io_addr run-rtlsim
	$(MAKE) -C printf run-rtlsim
	$(MAKE) -C diverge run-rtlsim
	$(MAKE) -C sort run-rtlsim
	$(MAKE) -C fence run-rtlsim
	$(MAKE) -C vecaddx run-rtlsim
	$(MAKE) -C sgemmx run-rtlsim
	$(MAKE) -C conv3x run-rtlsim
	$(MAKE) -C relu run-rtlsim
	$(MAKE) -C sgemm2x run-rtlsim
	$(MAKE) -C stencil3d run-rtlsim

clean:
	$(MAKE) -C basic clean
	$(MAKE) -C demo clean
	$(MAKE) -C dogfood clean
	$(MAKE) -C dropout clean
	$(MAKE) -C dotproductx clean
	$(MAKE) -C mstress clean
	$(MAKE) -C io_addr clean
	$(MAKE) -C printf clean
	$(MAKE) -C diverge clean
	$(MAKE) -C sort clean
	$(MAKE) -C fence clean
	$(MAKE) -C vecaddx clean
	$(MAKE) -C sgemmx clean
	$(MAKE) -C conv3x clean
	$(MAKE) -C relu clean
	$(MAKE) -C sgemm2x clean
	$(MAKE) -C stencil3d clean
