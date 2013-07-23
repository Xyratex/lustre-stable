#
# GPL HEADER START
#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 only,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License version 2 for more details (a copy is included
# in the LICENSE file that accompanied this code).
#
# You should have received a copy of the GNU General Public License
# version 2 along with this program; If not, see
# http://www.lustre.org/lustre/docs/GPLv2.pdf
#
# Please contact Xyratex Technology, Ltd., Langstone Road, Havant, Hampshire.
# PO9 1SA, U.K. or visit www.xyratex.com if you need additional information or
# have any questions.
#
# GPL HEADER END
#

#
# Copyright (c) 2002, 2013, Xyratex Technology, Ltd . All rights reserved.
# Use is subject to license terms.
#

#
# Some portions of Lustre® software are subject to copyrights help by Intel Corp.
# Copyright (c) 2011-2013 Intel Corporation, Inc.
#

#
# This file is part of Lustre, http://www.lustre.org/
# Lustre® and the Lustre logo are registered trademarks of
# Xyratex Technology, Ltd  in the United States and/or other countries.
#

LYX2PDF = GS_OPTIONS=-dCompatibilityLevel=1.1 $(srcdir)/tex2pdf -overwrite
TEX2PDF = GS_OPTIONS=-dCompatibilityLevel=1.1 $(srcdir)/tex2pdf -overwrite
LYX2PS = lyx --export ps
LYX2TEX = lyx --export latex
LYX2TXT = lyx --export text
LYX2HTML = lyx --export html
LATEX = latex
DVIPS = dvips
PS2PDF = ps2pdf
TEXEXPAND = texexpand
SUFFIXES = .lin .lyx .pdf .ps .sgml .html .txt .tex .fig .eps .dvi

MANFILES = lustre.7 lfs.1 mount.lustre.8 mkfs.lustre.8 tunefs.lustre.8 lctl.8 \
	llverdev.8 llbackup.8 llapi_quotactl.3 llobdstat.8 llstat.8 \
	plot-llstat.8 l_getgroups.8 lst.8 routerstat.8 lshowmount.8 \
	ll_recover_lost_found_objs.8 llog_reader.8 llapi_file_open.3 \
	llapi_file_create.3 llapi_file_get_stripe.3 liblustreapi.7
if UTILS
man_MANS = $(MANFILES)
endif

LYXFILES= $(filter-out $(patsubst %.lin,%.lyx,$(wildcard *.lin)),\
	$(wildcard *.lin *.lyx))

CLEANFILES = *.aux *.tex *.log *.pdf

EXTRA_DIST = tex2pdf $(MANFILES) $(LYXFILES)

all:

# These variables are set by lbuild/check-build.
RPMRELEASE ?= RELEASE
KERNTYPE ?= chaos
KERNRPM ?= kernel-2.4.18lustre13-RELEASE.i386.rpm

.lyx.pdf:
	@echo $(LYX2PDF) $< && $(LYX2PDF) $< || printf "\n*** Warning: not creating PDF docs; install lyx to rectify this\n"

.lyx.ps:
	@echo $(LYX2PS) $< && $(LYX2PS) $< || printf "\n*** Warning: not creating PostScript docs; install lyx to rectify this\n"

.lyx.tex:
	@echo $(LYX2TEX) $< && $(LYX2TEX) $< || printf "\n*** Warning: not creating LaTeX docs; install lyx to rectify this\n"

.lyx.txt:
	@echo $(LYX2TXT) $< && $(LYX2TXT) $< || printf "\n*** Warning: not creating text docs; install lyx to rectify this\n"

.lyx.html:
	@echo $(LYX2HTML) $< && $(LYX2HTML) $< || printf "\n*** Warning: not creating HTML docs; install lyx to rectify this\n"

.tex.pdf:
	$(TEX2PDF) $<

.tex.dvi:
	$(LATEX) $<
	$(LATEX) $<

.dvi.ps:
	$(DVIPS) $< -o $@

.ps.pdf:
	$(PS2PDF) $< $@
