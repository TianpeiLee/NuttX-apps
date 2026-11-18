############################################################################
# apps/testing/fputest/Makefile
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(APPDIR)/Make.defs

# FPU arithmetic / context-switch test built-in application info

PROGNAME  = $(CONFIG_TESTING_FPUTEST_PROGNAME)
PRIORITY  = $(CONFIG_TESTING_FPUTEST_PRIORITY)
STACKSIZE = $(CONFIG_TESTING_FPUTEST_STACKSIZE)
MODULE    = $(CONFIG_TESTING_FPUTEST)

# fputest main

MAINSRC = fputest_main.c

include $(APPDIR)/Application.mk
