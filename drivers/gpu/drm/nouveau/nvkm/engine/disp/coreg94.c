/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "dmacnv50.h"
#include "rootnv50.h"

#include <nvif/class.h>

const struct nv50_disp_mthd_list
g94_disp_core_mthd_sor = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0600, 0x610794 },
		{}
	}
};

const struct nv50_disp_chan_mthd
g94_disp_core_chan_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_core_mthd_base },
		{    "DAC", 3, &g84_disp_core_mthd_dac  },
		{    "SOR", 4, &g94_disp_core_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_core_mthd_pior },
		{   "HEAD", 2, &g84_disp_core_mthd_head },
		{}
	}
};

const struct nv50_disp_dmac_oclass
g94_disp_core_oclass = {
	.base.oclass = GT206_DISP_CORE_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv50_disp_core_new,
	.func = &nv50_disp_core_func,
	.mthd = &g94_disp_core_chan_mthd,
	.chid = 0,
};
