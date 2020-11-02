# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Renesas device drivers.
#

obj-$(CONFIG_SH_ETH) += sh_eth.o

ravb-objs := ravb_main.o ravb_ptp.o

obj-$(CONFIG_RAVB) += ravb.o

obj-$(CONFIG_RENESAS_ETHER_SWITCH) += rswitch.o

rtsn_drv-objs := rtsn.o rtsn_ptp.o

obj-$(CONFIG_RTSN) += rtsn_drv.o
