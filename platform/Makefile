############################################################################
# apps/platform/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

APPDIR ?= $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/..)

include $(APPDIR)/Make.defs

# When invoked bare (the %/.dirlinks pattern rule passes no variables)
# the NuttX Make.defs have not been parsed and CONFIG_ARCH_BOARD is
# empty; read it from the NuttX .config so the dirlink still resolves.

ifeq ($(CONFIG_ARCH_BOARD),)
  CONFIG_ARCH_BOARD := $(shell grep -s -m1 -E '^CONFIG_ARCH_BOARD=' $(APPDIR)/../NuttX/.config 2>/dev/null | cut -d'"' -f2)
endif
ifeq ($(CONFIG_ARCH_BOARD),)
  CONFIG_ARCH_BOARD := $(shell grep -s -m1 -E '^CONFIG_ARCH_BOARD_' $(APPDIR)/../NuttX/boards/risc-v/ch32h41x/ch32h41x-evb/configs/nsh/defconfig 2>/dev/null | sed -e 's/^CONFIG_ARCH_BOARD_//' -e 's/=y//')
endif

CONFIG_ARCH_BOARD ?= dummy

# Directories

PLATFORMDIR = board
DUMMYDIR    = $(APPDIR)$(DELIM)platform$(DELIM)dummy

ifeq ($(CONFIG_ARCH_BOARD_CUSTOM),y)
  LINKDIR   = $(DUMMYDIR)
else
  BOARDDIR  = $(APPDIR)$(DELIM)platform$(DELIM)$(CONFIG_ARCH_BOARD)
  LINKDIR   = $(if $(wildcard $(BOARDDIR)$(DELIM)Make.defs),$(BOARDDIR),$(DUMMYDIR))
endif

VPATH       += board
DEPPATH += --obj-path bin
DEPPATH += --obj-suffix $(OBJEXT)
DEPPATH += --dep-path $(PLATFORMDIR)

# Binaries

-include $(PLATFORMDIR)$(DELIM)Make.defs

# Build context setup

$(TOPDIR)$(DELIM).config:

$(PLATFORMDIR): $(TOPDIR)$(DELIM).config
	@echo "LN: platform$(DELIM)board to $(LINKDIR)"
	$(Q) $(DIRLINK) $(LINKDIR) $(PLATFORMDIR)

dirlinks: $(PLATFORMDIR)

context:: dirlinks

clean_context:
	$(Q) $(DIRUNLINK) $(PLATFORMDIR)

clean::
	$(Q) $(MAKE) -C bin TOPDIR="$(TOPDIR)" APPDIR="$(APPDIR)" clean

include $(APPDIR)/Application.mk
