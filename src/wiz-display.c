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

#include "fbdevhw.h"
#include <linux/fb.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <unistd.h>

#define DPMS_SERVER
#include <X11/extensions/dpmsconst.h>

#include "wiz.h"

static void
WizCrtcDPMS(xf86CrtcPtr crtc, int mode);

static void
WizCrtcGammaSet(xf86CrtcPtr crtc,  CARD16 *red, CARD16 *green, CARD16 *blue,
		int size);

static void
WizCrtcDestroy(xf86CrtcPtr crtc);

#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)
static Bool
WizSetModeMajor(xf86CrtcPtr crtc, DisplayModePtr mode,
		Rotation rotation, int x, int y);

#else

static void
WizModeSet(xf86CrtcPtr crtc, DisplayModePtr mode,
		DisplayModePtr adjusted_mode, int x, int y);

static Bool
WizLock(xf86CrtcPtr crtc);

static Bool
WizModeFixup(xf86CrtcPtr crtc, DisplayModePtr mode,
		DisplayModePtr adjusted_mode);

static void
WizPrepare(xf86CrtcPtr crtc);

static void
WizCommit(xf86CrtcPtr crtc);

#endif

static const xf86CrtcFuncsRec wiz_crtc_funcs = {
	.dpms = WizCrtcDPMS,
	.save = NULL,
	.restore = NULL,
	.gamma_set = WizCrtcGammaSet,
	.shadow_allocate = NULL,
	.shadow_create = NULL,
	.shadow_destroy = NULL,
	.set_cursor_colors = NULL,
	.set_cursor_position = NULL,
	.show_cursor = NULL,
	.hide_cursor = NULL,
	.load_cursor_image = NULL,
	.load_cursor_argb = NULL,
	.destroy = WizCrtcDestroy,
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)
	.lock = NULL,
	.unlock = NULL,
	.mode_fixup = NULL,
	.prepare = NULL,
	.mode_set = NULL,
	.commit = NULL,
	.set_mode_major = WizSetModeMajor
#else
	.lock = WizLock,
	.unlock = NULL,
	.mode_fixup = WizModeFixup,
	.prepare = WizPrepare,
	.mode_set = WizModeSet,    
        .commit = WizCommit,
#endif
};

static void
ConvertModeXfreeToFb(const DisplayModePtr mode, const Rotation *rotation, struct fb_var_screeninfo *var) {
	Rotation rot;
	if (rotation)
		rot = *rotation;
	else
		rot = RR_Rotate_0;

	var->xres = mode->HDisplay;
	var->yres = mode->VDisplay;
	var->xres_virtual = mode->HDisplay;
	var->yres_virtual = mode->VDisplay;
	var->xoffset = 0;
	var->yoffset = 0;

	var->pixclock = mode->Clock ? 1000000000 / mode->Clock : 0;
	var->left_margin = mode->HTotal - mode->HSyncEnd;
	var->right_margin = mode->HSyncStart - mode->HDisplay;
	var->hsync_len = mode->HSyncEnd - mode->HSyncStart;
	var->upper_margin = mode->VTotal - mode->VSyncEnd;
	var->lower_margin = mode->VSyncStart - mode->VDisplay;
	var->vsync_len = mode->VSyncEnd - mode->VSyncStart;

	var->sync = 0;
	var->vmode = 0;
	switch (rot) {
		case RR_Rotate_0:
			var->rotate = FB_ROTATE_UR;
			break;
		case RR_Rotate_90:
			var->rotate = FB_ROTATE_CW;
			break;
		case RR_Rotate_180:
			var->rotate = FB_ROTATE_UD;
			break;
		case RR_Rotate_270:
			var->rotate = FB_ROTATE_CCW;
			break;
	}
}

Bool
WizCrtcInit(ScrnInfoPtr pScrn) {
	return xf86CrtcCreate(pScrn, &wiz_crtc_funcs) != NULL;
}

static void
WizCrtcDPMS(xf86CrtcPtr crtc, int mode) {
	fbdevHWDPMSSet(crtc->scrn, mode, 0);
}

static void WizCrtcGammaSet(xf86CrtcPtr crtc,  CARD16 *red, CARD16 *green,
		CARD16 *blue, int size) {
}

static void WizCrtcDestroy(xf86CrtcPtr crtc) {
}

#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)

static Bool
WizSetModeMajor(xf86CrtcPtr crtc, DisplayModePtr mode,
		Rotation rotation, int x, int y) {
	ScrnInfoPtr scrn = crtc->scrn;
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
	DisplayModeRec saved_mode;
	int saved_x, saved_y;
	Rotation saved_rotation;
	WizPtr pWiz = WizPTR(crtc->scrn);
	Bool ret = FALSE;
	int i;

	struct fb_var_screeninfo var = pWiz->fb_var;

	crtc->enabled = xf86CrtcInUse (crtc);

	if (!crtc->enabled)
		return TRUE;

	saved_mode = crtc->mode;
	saved_x = crtc->x;
	saved_y = crtc->y;
	saved_rotation = crtc->rotation;

	crtc->mode = *mode;
	crtc->x = x;
	crtc->y = y;
	crtc->rotation = rotation;

	crtc->funcs->dpms(crtc, DPMSModeOff);
	for (i = 0; i < xf86_config->num_output; i++) {
		xf86OutputPtr output = xf86_config->output[i];

		if (output->crtc != crtc)
			continue;

		output->funcs->prepare(output);
	}

	ConvertModeXfreeToFb(mode, &rotation, &var);
	if (rotation == RR_Rotate_90 || rotation == RR_Rotate_270) {
		var.pixclock *= 2;
	}

	if (ioctl(pWiz->fb_fd, FBIOPUT_VSCREENINFO, (void*)&var) != 0) {
		goto done;
	}

	crtc->funcs->dpms (crtc, DPMSModeOn);
	for (i = 0; i < xf86_config->num_output; i++)
	{
		xf86OutputPtr output = xf86_config->output[i];
		if (output->crtc == crtc)
		{
			output->funcs->commit(output);
#ifdef RANDR_12_INTERFACE
			if (output->randr_output)
				RRPostPendingProperties (output->randr_output);
#endif
		}
	}

	ret = TRUE;
	if (scrn->pScreen)
		xf86CrtcSetScreenSubpixelOrder (scrn->pScreen);

done:
	if (!ret) {
		crtc->x = saved_x;
		crtc->y = saved_y;
		crtc->rotation = saved_rotation;
		crtc->mode = saved_mode;
	}

	return ret;
}

#else

static void
WizModeSet(xf86CrtcPtr crtc, DisplayModePtr mode,
		DisplayModePtr adjusted_mode, int x, int y) {
	WizPtr pWiz = WizPTR(crtc->scrn);

	struct fb_var_screeninfo var = pWiz->fb_var;

	ConvertModeXfreeToFb(adjusted_mode, NULL, &var);

	ioctl(pWiz->fb_fd, FBIOPUT_VSCREENINFO, (void*)&var);
}

static Bool
WizLock(xf86CrtcPtr crtc) {
	return FALSE;
}

static Bool
WizModeFixup(xf86CrtcPtr crtc, DisplayModePtr mode,
		DisplayModePtr adjusted_mode) {
	return TRUE;
}

static void
WizPrepare(xf86CrtcPtr crtc) {
	(*crtc->funcs->dpms)(crtc, DPMSModeOff);
}

static void
WizCommit(xf86CrtcPtr crtc) {
	(*crtc->funcs->dpms)(crtc, DPMSModeOn);
}

#endif
