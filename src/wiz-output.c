/*
 * Copyright © 2009 Yogish Kulkarni <yogishkulkarni@gmail.com>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86i2c.h"
#include "xf86Crtc.h"
#include "xf86Modes.h"

#include "fbdevhw.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "wiz.h"


typedef struct _WizOutput {
	DisplayModePtr modes;
} WizOutputRec, *WizOutputPtr;

static void
WizOutputDPMS(xf86OutputPtr output, int mode) {}

static xf86OutputStatus
WizOutputDetect(xf86OutputPtr output);

static Bool
WizOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
		DisplayModePtr mode_adjusted);

static void
WizOutputPrepare(xf86OutputPtr output);

static void
WizOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
		DisplayModePtr adjusted_mode);

static int
WizOutputModeValid(xf86OutputPtr output, DisplayModePtr mode);

static Bool
WizOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
		DisplayModePtr mode_adjusted);

static void
WizOutputPrepare(xf86OutputPtr output);

static void WizOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
		DisplayModePtr adjusted_mode);

static void
WizOutputCommit(xf86OutputPtr output);

static void
WizOutputDestroy(xf86OutputPtr output);

static DisplayModePtr
WizOutputGetModes(xf86OutputPtr output);

static const xf86OutputFuncsRec wiz_output_funcs = {
	.create_resources = NULL,
	.dpms = WizOutputDPMS,
	.save = NULL,
	.restore = NULL,
	.mode_valid = WizOutputModeValid,
	.mode_fixup = WizOutputModeFixup,
	.prepare = WizOutputPrepare,
	.commit = WizOutputCommit,
	.mode_set = WizOutputModeSet,
	.detect = WizOutputDetect,
	.get_modes = WizOutputGetModes,
#ifdef RANDR_12_INTERFACE
	.set_property = NULL,
#endif
	.destroy = WizOutputDestroy
};

static void
ConvertModeFbToXfree(const struct fb_var_screeninfo *var, DisplayModePtr mode,
		Rotation *rotation) {
	mode->HDisplay = var->xres;
	mode->VDisplay = var->yres;

	mode->Clock = var->pixclock ? 1000000000 / var->pixclock : 0;
	mode->HSyncStart = mode->HDisplay + var->right_margin;
	mode->HSyncEnd = mode->HSyncStart + var->hsync_len;
	mode->HTotal = mode->HSyncEnd + var->left_margin;
	mode->VSyncStart = mode->VDisplay + var->lower_margin;
	mode->VSyncEnd = mode->VSyncStart + var->vsync_len;
	mode->VTotal = mode->VSyncEnd + var->upper_margin;

	mode->Flags = 0;

	xf86SetModeCrtc(mode, 0);

	if (rotation) {
		switch (var->rotate) {
			case FB_ROTATE_UR:
				*rotation = RR_Rotate_0;
				break;
			case FB_ROTATE_CW:
				*rotation = RR_Rotate_90;
				break;
			case FB_ROTATE_UD:
				*rotation = RR_Rotate_180;
				break;
			case FB_ROTATE_CCW:
				*rotation = RR_Rotate_270;
				break;
		}
	}
}

void
WizOutputInit(ScrnInfoPtr pScrn) {
	WizPtr pWiz = WizPTR(pScrn);
	xf86OutputPtr output;
	WizOutputPtr pWizOutput;
	DisplayModePtr mode;

	output = xf86OutputCreate(pScrn, &wiz_output_funcs, "LCD");
	if (!output)
		return;

	output->possible_crtcs = 1;
	output->possible_clones = 0;

	pWizOutput = (WizOutputPtr)xnfalloc(sizeof(WizOutputRec));
	if (!pWizOutput) {
		output->driver_private = NULL;
		return;
	}
	output->driver_private = pWizOutput;
	pWizOutput->modes = NULL;

	mode = xnfalloc(sizeof(DisplayModeRec));
	if (!mode)
		return;

	mode->next = NULL;
	mode->prev = NULL;

	ConvertModeFbToXfree(&pWiz->fb_var, mode, NULL);
	xf86SetModeDefaultName(mode);

	mode->type = M_T_PREFERRED | M_T_DRIVER;
	pWizOutput->modes = xf86ModesAdd(pWizOutput->modes, mode);

	mode = xf86DuplicateMode(mode);
	if (!mode)
		return;

	if (mode->VDisplay <= 320) {
		mode->HSyncStart = mode->HDisplay * 2 + (mode->HDisplay - mode->HSyncStart);
		mode->HSyncEnd   = mode->HDisplay * 2 + (mode->HDisplay - mode->HSyncEnd);
		mode->HTotal     = mode->HDisplay * 2 + (mode->HDisplay - mode->HTotal);
		mode->HDisplay   *= 2;
		mode->HSyncStart = mode->VDisplay * 2 + (mode->VSyncStart - mode->HDisplay);
		mode->HSyncEnd   = mode->VDisplay * 2 + (mode->VSyncEnd - mode->HDisplay);
		mode->HTotal     = mode->VDisplay * 2 + (mode->VTotal - mode->HDisplay);
		mode->VDisplay   *= 2;
	} else {
		mode->HSyncStart = mode->HDisplay / 2 + (mode->HDisplay - mode->HSyncStart);
		mode->HSyncEnd   = mode->HDisplay / 2 + (mode->HDisplay - mode->HSyncEnd);
		mode->HTotal     = mode->HDisplay / 2 + (mode->HDisplay - mode->HTotal);
		mode->HDisplay   /= 2;
		mode->HSyncStart = mode->VDisplay / 2 + (mode->VSyncStart - mode->HDisplay);
		mode->HSyncEnd   = mode->VDisplay / 2 + (mode->VSyncEnd - mode->HDisplay);
		mode->HTotal     = mode->VDisplay / 2 + (mode->VTotal - mode->HDisplay);
		mode->VDisplay   /= 2;
	}

	xf86SetModeCrtc(mode, 0);
	xf86SetModeDefaultName(mode);
	mode->type = M_T_DRIVER;

	pWizOutput->modes = xf86ModesAdd(pWizOutput->modes, mode);
}

static xf86OutputStatus
WizOutputDetect(xf86OutputPtr output) {
	return XF86OutputStatusConnected;
}

static int
WizOutputModeValid(xf86OutputPtr output, DisplayModePtr mode) {
	return MODE_OK;
}

static Bool
WizOutputModeFixup(xf86OutputPtr output, DisplayModePtr mode,
		DisplayModePtr mode_adjusted) {
	return TRUE;
}

static void
WizOutputPrepare(xf86OutputPtr output) {
}

static void
WizOutputModeSet(xf86OutputPtr output, DisplayModePtr mode,
		DisplayModePtr adjusted_mode) {
}

static void
WizOutputCommit(xf86OutputPtr output) {
}

static void WizOutputDestroy(xf86OutputPtr output) {
	WizOutputPtr pWizOutput = output->driver_private;
	while (pWizOutput->modes)
		xf86DeleteMode(&pWizOutput->modes, pWizOutput->modes);
	xfree(pWizOutput);
}

static DisplayModePtr WizOutputGetModes(xf86OutputPtr output) {
	WizPtr pWiz = WizPTR(output->scrn);
	WizOutputPtr pWizOutput = output->driver_private;

	output->mm_width = pWiz->fb_var.width;
	output->mm_height = pWiz->fb_var.height;
	if (pWizOutput)
		return xf86DuplicateModes(NULL, pWizOutput->modes);
	return NULL;
}

