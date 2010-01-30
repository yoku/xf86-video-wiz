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

#include <string.h>

/* all driver need this */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "mipointer.h"
#include "mibstore.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"

/* for visuals */
#include "fb.h"

#include "fbdevhw.h"

#include "xf86xv.h"

#include "xf86i2c.h"
#include "xf86Modes.h"
#include "xf86Crtc.h"
#include "xf86RandR12.h"

#include "wiz.h"
// #include "wiz-kms-driver.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <sys/mman.h>


static Bool debug = 0;

#define TRACE_ENTER(str) \
    do { if (debug) ErrorF("Wiz: " str " %d\n",pScrn->scrnIndex); } while (0)
#define TRACE_EXIT(str) \
    do { if (debug) ErrorF("Wiz: " str " done\n"); } while (0)
#define TRACE(str) \
    do { if (debug) ErrorF("Wiz trace: " str "\n"); } while (0)

/* -------------------------------------------------------------------- */
/* prototypes                                                           */
static const OptionInfoRec *
WizAvailableOptions(int chipid, int busid);

static void
WizIdentify(int flags);

static Bool
WizProbe(DriverPtr drv, int flags);

static Bool
WizPreInit(ScrnInfoPtr pScrn, int flags);

static Bool
WizScreenInit(int Index, ScreenPtr pScreen, int argc, char **argv);

static Bool
WizCloseScreen(int scrnIndex, ScreenPtr pScreen);

static Bool
WizCrtcResize(ScrnInfoPtr scrn, int width, int height);

static Bool
WizInitFramebufferDevice(ScrnInfoPtr scrn, const char *fb_device);

static void
WizSaveHW(ScrnInfoPtr pScrn);

static void
WizRestoreHW(ScrnInfoPtr pScren);

static Bool
WizEnterVT(int scrnIndex, int flags);

static void
WizLeaveVT(int scrnIndex, int flags);

static void
WizLoadColormap(ScrnInfoPtr pScrn, int numColors, int *indices,
        LOCO *colors, VisualPtr pVisual);
 /* -------------------------------------------------------------------- */

static const xf86CrtcConfigFuncsRec wiz_crtc_config_funcs = {
    .resize = WizCrtcResize
};

#define WIZ_VERSION		1000
#define WIZ_NAME		"Wiz"
#define WIZ_DRIVER_NAME	        "Wiz"

_X_EXPORT DriverRec Wiz = {
	WIZ_VERSION,
	WIZ_DRIVER_NAME,
	WizIdentify,
	WizProbe,
	WizAvailableOptions,
	NULL,
	0,
	NULL
};

/* Supported "chipsets" */
static SymTabRec WizChipsets[] = {
    { 0, "Wiz" },
    {-1, NULL }
};

/* Supported options */
typedef enum {
	OPTION_SHADOW_FB,
	OPTION_DEVICE,
	OPTION_DEBUG,
} WizOpts;

static const OptionInfoRec WizOptions[] = {
	{ OPTION_SHADOW_FB,	"ShadowFB",	OPTV_BOOLEAN,	{0},	FALSE },
	{ OPTION_DEBUG,		"debug",	OPTV_BOOLEAN,	{0},	FALSE },
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

#ifdef XFree86LOADER

MODULESETUPPROTO(WizSetup);

static XF86ModuleVersionInfo WizVersRec =
{
	"Wiz",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	NULL,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData wizModuleData = { &WizVersRec, WizSetup, NULL };

pointer
WizSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&Wiz, module, 0);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}

#endif /* XFree86LOADER */

Bool
WizGetRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;

	pScrn->driverPrivate = xnfcalloc(sizeof(WizRec), 1);
	return TRUE;
}

void
WizFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	xfree(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/* -------------------------------------------------------------------- */

/* Map the mmio registers of the wiz. We can not use xf86MapVidMem since it
 * will open /dev/mem without O_SYNC. */
static Bool
WizMapMMIO(ScrnInfoPtr pScrn) {
    WizPtr pWiz = WizPTR(pScrn);
    off_t base =  0xc0000000;
    size_t length = 0x20000;
    int fd;
    off_t page_base = base & ~(getpagesize() - 1);
    off_t base_offset = base - page_base;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to open \"/dev/mem\": %s\n",
                   strerror(errno));
        return FALSE;
    }
    pWiz->reg_base = (char *)mmap(NULL, length, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, page_base);

    close(fd);

    if (pWiz->reg_base == MAP_FAILED) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to mmap mmio registers: %s\n",
                   strerror(errno));
        return FALSE;
    }

    pWiz->reg_base += base_offset;

    return TRUE;
}

static void
WizUnmapMMIO(ScrnInfoPtr pScrn) {
    WizPtr pWiz = WizPTR(pScrn);
    size_t length = 0x20000;
    char *page_base = (char *)((off_t)pWiz->reg_base & ~(getpagesize() - 1));
    size_t base_offset = page_base - pWiz->reg_base;

   if (pWiz->reg_base != MAP_FAILED)
        munmap(page_base, length + base_offset);
}

static Bool
WizSwitchMode(int scrnIndex, DisplayModePtr mode, int flags) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR (pScrn);
    xf86OutputPtr output = config->output[config->compat_output];
    Rotation rotation;

    if (output && output->crtc)
        rotation = output->crtc->rotation;
    else
        rotation = RR_Rotate_0;

    return xf86SetSingleMode(pScrn, mode, rotation);
}

static const OptionInfoRec *
WizAvailableOptions(int chipid, int busid)
{
	return WizOptions;
}

static void
WizIdentify(int flags)
{
	xf86PrintChipsets(WIZ_NAME, "driver for wiz", WizChipsets);
}

static Bool
WizFbdevProbe(DriverPtr drv, GDevPtr *devSections, int numDevSections)
{
	char *dev;
	Bool foundScreen = FALSE;
	int i;
	ScrnInfoPtr pScrn;

	if (!xf86LoadDrvSubModule(drv, "fbdevhw")) return FALSE;

	for (i = 0; i < numDevSections; i++) {

		dev = xf86FindOptionValue(devSections[i]->options, "Device");
		if (fbdevHWProbe(NULL, dev, NULL)) {
			int entity;
			pScrn = NULL;

			entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
			pScrn = xf86ConfigFbEntity(pScrn,0,entity, NULL, NULL,
				                   NULL, NULL);

			if (pScrn) {

				foundScreen = TRUE;
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				           "Not using KMS\n");

				pScrn->driverVersion = WIZ_VERSION;
				pScrn->driverName    = WIZ_DRIVER_NAME;
				pScrn->name          = WIZ_NAME;
				pScrn->Probe         = WizProbe;
				pScrn->PreInit       = WizPreInit;
				pScrn->ScreenInit    = WizScreenInit;
				pScrn->SwitchMode    = WizSwitchMode;
				pScrn->AdjustFrame   = fbdevHWAdjustFrameWeak();
				pScrn->EnterVT       = WizEnterVT;
				pScrn->LeaveVT       = WizLeaveVT;
				pScrn->ValidMode     = fbdevHWValidModeWeak();

				xf86DrvMsg(pScrn->scrnIndex, X_INFO,
					   "using %s\n",
					   dev ? dev : "default device\n");

			}
		}

	}

	return foundScreen;
}

static Bool
WizProbe(DriverPtr drv, int flags)
{
	GDevPtr *devSections;
	int numDevSections;
	Bool foundScreen = FALSE;

	TRACE("probe start");

	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

	numDevSections = xf86MatchDevice(WIZ_DRIVER_NAME, &devSections);
	if (numDevSections <= 0) return FALSE;

	foundScreen = WizFbdevProbe(drv, devSections, numDevSections);

	xfree(devSections);
	TRACE("probe done");
	return foundScreen;
}

static Bool
WizPreInit(ScrnInfoPtr pScrn, int flags)
{
    WizPtr pWiz;
    int default_depth, fbbpp;
    rgb weight_defaults = {0, 0, 0};
    Gamma gamma_defaults = {0.0, 0.0, 0.0};
    char *fb_device;

    if (flags & PROBE_DETECT)
        return FALSE;

    TRACE_ENTER("PreInit");

    /* Check the number of entities, and fail if it isn't one. */
    if (pScrn->numEntities != 1)
        return FALSE;

    pScrn->monitor = pScrn->confScreen->monitor;

    WizGetRec(pScrn);
    pWiz = WizPTR(pScrn);

    pWiz->accel = FALSE;

    pWiz->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

    fb_device = xf86FindOptionValue(pWiz->pEnt->device->options, "Device");

    /* open device */
    if (!fbdevHWInit(pScrn, NULL, fb_device))
            return FALSE;

    /* FIXME: Replace all fbdev functionality with our own code, so we only have
     * to open the fb devic only once. */
    if (!WizInitFramebufferDevice(pScrn, fb_device))
        return FALSE;

    default_depth = fbdevHWGetDepth(pScrn, &fbbpp);

    if (!xf86SetDepthBpp(pScrn, default_depth, default_depth, fbbpp, 0))
        return FALSE;

    xf86PrintDepthBpp(pScrn);

    /* color weight */
    if (!xf86SetWeight(pScrn, weight_defaults, weight_defaults))
        return FALSE;

    /* visual init */
    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    /* We don't currently support DirectColor at > 8bpp */
    if (pScrn->defaultVisual != TrueColor) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "requested default visual"
			   " (%s) is not supported at depth %d\n",
			   xf86GetVisualName(pScrn->defaultVisual), pScrn->depth);
        return FALSE;
    }

    if (!xf86SetGamma(pScrn, gamma_defaults)) {
        return FALSE;
    }

    xf86CrtcConfigInit(pScrn, &wiz_crtc_config_funcs);
    xf86CrtcSetSizeRange(pScrn, 240, 320, 480, 640);
    WizCrtcInit(pScrn);
    WizOutputInit(pScrn);

    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes.\n");
        return FALSE;
    }

    pScrn->progClock = TRUE;
    pScrn->chipset   = "Wiz";
    pScrn->videoRam  = fbdevHWGetVidmem(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware: %s (video memory:"
		   " %dkB)\n", fbdevHWGetName(pScrn), pScrn->videoRam/1024);

    /* handle options */
    xf86CollectOptions(pScrn, NULL);
    if (!(pWiz->Options = xalloc(sizeof(WizOptions))))
        return FALSE;
    memcpy(pWiz->Options, WizOptions, sizeof(WizOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pWiz->pEnt->device->options, pWiz->Options);

    /* use shadow framebuffer by default */
    pWiz->shadowFB = xf86ReturnOptValBool(pWiz->Options, OPTION_SHADOW_FB, TRUE);

    debug = xf86ReturnOptValBool(pWiz->Options, OPTION_DEBUG, FALSE);

    /* First approximation, may be refined in ScreenInit */
    pScrn->displayWidth = pScrn->virtualX;

    xf86PrintModes(pScrn);

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);

    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
        WizFreeRec(pScrn);
        return FALSE;
    }

    TRACE_EXIT("PreInit");
    return TRUE;
}


static Bool
WizScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    WizPtr pWiz = WizPTR(pScrn);
    VisualPtr visual;
    int ret, flags;

    TRACE_ENTER("WizScreenInit");

#if DEBUG
	ErrorF("\tbitsPerPixel=%d, depth=%d, defaultVisual=%s\n"
		   "\tmask: %x,%x,%x, offset: %d,%d,%d\n",
		   pScrn->bitsPerPixel,
		   pScrn->depth,
		   xf86GetVisualName(pScrn->defaultVisual),
		   pScrn->mask.red,pScrn->mask.green,pScrn->mask.blue,
		   pScrn->offset.red,pScrn->offset.green,pScrn->offset.blue);
#endif

    if (NULL == (pWiz->fbmem = fbdevHWMapVidmem(pScrn))) {
        xf86DrvMsg(scrnIndex, X_ERROR, "mapping of video memory failed\n");
        return FALSE;
    }

    pWiz->fboff = fbdevHWLinearOffset(pScrn);

    fbdevHWSaveScreen(pScreen, SCREEN_SAVER_ON);

    /* mi layer */
    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, TrueColorMask, pScrn->rgbBits, TrueColor)) {
        xf86DrvMsg(scrnIndex, X_ERROR,
                   "visual type setup failed for %d bits per pixel [1]\n",
                   pScrn->bitsPerPixel);
        return FALSE;
    }
    if (!miSetPixmapDepths()) {
      xf86DrvMsg(scrnIndex, X_ERROR, "pixmap depth setup failed\n");
      return FALSE;
    }

    pScrn->displayWidth = fbdevHWGetLineLength(pScrn) /
					  (pScrn->bitsPerPixel / 8);

    pWiz->fbstart = pWiz->fbmem + pWiz->fboff;

    ret = fbScreenInit(pScreen, pWiz->fbstart, pScrn->virtualX,
                       pScrn->virtualY, pScrn->xDpi, pScrn->yDpi,
                       pScrn->displayWidth,  pScrn->bitsPerPixel);
    if (!ret)
        return FALSE;

    /* Fixup RGB ordering */
    visual = pScreen->visuals + pScreen->numVisuals;
    while (--visual >= pScreen->visuals) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed   = pScrn->offset.red;
            visual->offsetGreen = pScrn->offset.green;
            visual->offsetBlue  = pScrn->offset.blue;
            visual->redMask     = pScrn->mask.red;
            visual->greenMask   = pScrn->mask.green;
            visual->blueMask    = pScrn->mask.blue;
        }
    }

    /* must be after RGB ordering fixed */
    if (!fbPictureInit(pScreen, NULL, 0))
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Render extension initialisation failed\n");

    pWiz->pScreen = pScreen;

    /* map in the registers */
    if (WizMapMMIO(pScrn)) {

        xf86LoadSubModule(pScrn, "exa");

    	if (!WIZDrawExaInit(pScrn)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "EXA hardware acceleration initialization failed\n");
        } else {
            pWiz->accel = TRUE;
        }
    }

    xf86SetBlackWhitePixels(pScreen);
    miInitializeBackingStore(pScreen);
    xf86SetBackingStore(pScreen);

    /* software cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    WizEnterVT(scrnIndex, 0);

    xf86CrtcScreenInit(pScreen);
#if XORG_VERSION_CURRENT >= XORG_VERSION_NUMERIC(1,5,0,0,0)
    xf86RandR12SetRotations(pScreen, RR_Rotate_0 | RR_Rotate_90 |
                                     RR_Rotate_180 | RR_Rotate_270);
#endif
    /* colormap */
    pWiz->colormap = NULL;
    if (!miCreateDefColormap(pScreen)) {
        xf86DrvMsg(scrnIndex, X_ERROR,
                   "internal error: miCreateDefColormap failed "
                   "in WizScreenInit()\n");
        return FALSE;
    }

    flags = CMAP_PALETTED_TRUECOLOR;
    if (!xf86HandleColormaps(pScreen, 256, 8, WizLoadColormap,
                             NULL, flags))
        return FALSE;

    xf86DPMSInit(pScreen, xf86DPMSSet, 0);

    pScreen->SaveScreen = xf86SaveScreen;

    /* Wrap the current CloseScreen function */
    pWiz->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = WizCloseScreen;

    TRACE_EXIT("WizScreenInit");

    return TRUE;
}

static Bool
WizCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    WizPtr pWiz = WizPTR(pScrn);

    if (pWiz->accel)
        WIZDrawFini(pScrn);

    if (pScrn->vtSema)
        WizRestoreHW(pScrn);

    fbdevHWUnmapVidmem(pScrn);
    WizUnmapMMIO(pScrn);

    if (pWiz->colormap) {
        xfree(pWiz->colormap);
        pWiz->colormap = NULL;
    }

    pScrn->vtSema = FALSE;

    pScreen->CreateScreenResources = pWiz->CreateScreenResources;
    pScreen->CloseScreen = pWiz->CloseScreen;
    return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

static Bool
WizCrtcResize(ScrnInfoPtr pScrn, int width, int height) {
    pScrn->virtualX = width;
    pScrn->virtualY = height;
    pScrn->displayWidth = width * (pScrn->bitsPerPixel / 8);
    pScrn->pScreen->GetScreenPixmap(pScrn->pScreen)->devKind = pScrn->displayWidth;

    return TRUE;
}


static Bool
WizInitFramebufferDevice(ScrnInfoPtr pScrn, const char *fb_device) {
    WizPtr pWiz = WizPTR(pScrn);

    if (fb_device) {
        pWiz->fb_fd = open(fb_device, O_RDWR, 0);
        if (pWiz->fb_fd == -1) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to open framebuffer device \"%s\": %s\n",
                       fb_device, strerror(errno));
            goto fail2;
        }
    } else {
        fb_device = getenv("FRAMEBUFFER");
        if (fb_device != NULL) {
            pWiz->fb_fd = open(fb_device, O_RDWR, 0);
        if (pWiz->fb_fd != -1)
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "Failed to open framebuffer device \"%s\": %s\n",
                       fb_device, strerror(errno));
             fb_device = NULL;
        }
        if (fb_device == NULL) {
            fb_device = "/dev/fb0";
            pWiz->fb_fd = open(fb_device, O_RDWR, 0);
            if (pWiz->fb_fd == -1) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                          "Failed to open framebuffer device \"%s\": %s",
                           fb_device, strerror(errno));
                goto fail2;
            }
        }
    }

    /* retrive current setting */
    if (ioctl(pWiz->fb_fd, FBIOGET_FSCREENINFO, (void*)(&pWiz->fb_fix)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOGET_FSCREENINFO failed: %s",
                   strerror(errno));
        goto fail1;
    }

    if (ioctl(pWiz->fb_fd, FBIOGET_VSCREENINFO, (void*)(&pWiz->fb_var)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOGET_FSCREENINFO failed: %s",
                   strerror(errno));
        goto fail1;
    }
    return TRUE;

fail1:
    close(pWiz->fb_fd);
    pWiz->fb_fd = -1;
fail2:
    return FALSE;
}

/* Save framebuffer setup and all the wiz registers we are going to touch */
static void
WizSaveHW(ScrnInfoPtr pScrn) {
    WizPtr pWiz = WizPTR(pScrn);

    if (ioctl(pWiz->fb_fd, FBIOGET_VSCREENINFO, (void*)(&pWiz->fb_saved_var)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOGET_FSCREENINFO failed: %s",
                   strerror(errno));
    }
}

static void
WizRestoreHW(ScrnInfoPtr pScrn) {
    WizPtr pWiz = WizPTR(pScrn);

    if (ioctl(pWiz->fb_fd, FBIOPUT_VSCREENINFO, (void*)(&pWiz->fb_saved_var)) == -1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Framebuffer ioctl FBIOSET_FSCREENINFO failed: %s",
                   strerror(errno));
    }
}

static Bool
WizEnterVT(int scrnIndex, int flags) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    WizPtr pWiz = WizPTR(pScrn);

    WizSaveHW(pScrn);

    if (pWiz->accel)
        pWiz->accel = WIZDrawEnable(pScrn);

    if (!xf86SetDesiredModes(pScrn))
        return FALSE;

    return TRUE;
}

static void
WizLeaveVT(int scrnIndex, int flags) {
    ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
    WizPtr pWiz = WizPTR(pScrn);

    if (pWiz->accel)
        WIZDrawDisable(pScrn);

    WizRestoreHW(pScrn);
}

static void
WizLoadColormap(ScrnInfoPtr pScrn, int numColors, int *indices,
        LOCO *colors, VisualPtr pVisual) {
    WizPtr pWiz = WizPTR(pScrn);
    int i;
    ErrorF("%s:%s[%d]\n", __FILE__, __func__, __LINE__);

    if (pWiz->colormap) {
        xfree (pWiz->colormap);
    }

    pWiz->colormap = xalloc (sizeof(uint16_t) * numColors);

    for (i = 0; i < numColors; ++i) {
        pWiz->colormap[i] =
            ((colors[indices[i]].red << 8) & 0xf700) |
            ((colors[indices[i]].green << 3) & 0x7e0) |
            (colors[indices[i]].blue >> 3);
    }
}
