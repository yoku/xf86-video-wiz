/*
 * Copyright © 2009 Yogish Kulkarni <yogishkulkarni@gmail.com>
 *
 * This driver is based on Xati,
 * Copyright  2003 Eric Anholt
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
#include <config.h>
#endif
#include "wiz-log.h"
#include "wiz.h"
#include "wiz-draw.h"

static const CARD8 WIZSolidRop[16] = {
	/* GXclear      */      0x00,         /* 0 */
	/* GXand        */      0xa0,         /* src AND dst */
	/* GXandReverse */      0x50,         /* src AND NOT dst */
	/* GXcopy       */      0xf0,         /* src */
	/* GXandInverted*/      0x0a,         /* NOT src AND dst */
	/* GXnoop       */      0xaa,         /* dst */
	/* GXxor        */      0x5a,         /* src XOR dst */
	/* GXor         */      0xfa,         /* src OR dst */
	/* GXnor        */      0x05,         /* NOT src AND NOT dst */
	/* GXequiv      */      0xa5,         /* NOT src XOR dst */
	/* GXinvert     */      0x55,         /* NOT dst */
	/* GXorReverse  */      0xf5,         /* src OR NOT dst */
	/* GXcopyInverted*/     0x0f,         /* NOT src */
	/* GXorInverted */      0xaf,         /* NOT src OR dst */
	/* GXnand       */      0x5f,         /* NOT src OR NOT dst */
	/* GXset        */      0xff,         /* 1 */
};

static const CARD8 WIZBltRop[16] = {
	/* GXclear      */      0x00,         /* 0 */
	/* GXand        */      0x88,         /* src AND dst */
	/* GXandReverse */      0x44,         /* src AND NOT dst */
	/* GXcopy       */      0xcc,         /* src */
	/* GXandInverted*/      0x22,         /* NOT src AND dst */
	/* GXnoop       */      0xaa,         /* dst */
	/* GXxor        */      0x66,         /* src XOR dst */
	/* GXor         */      0xee,         /* src OR dst */
	/* GXnor        */      0x11,         /* NOT src AND NOT dst */
	/* GXequiv      */      0x99,         /* NOT src XOR dst */
	/* GXinvert     */      0x55,         /* NOT dst */
	/* GXorReverse  */      0xdd,         /* src OR NOT dst */
	/* GXcopyInverted*/     0x33,         /* NOT src */
	/* GXorInverted */      0xbb,         /* NOT src OR dst */
	/* GXnand       */      0x77,         /* NOT src OR NOT dst */
	/* GXset        */      0xff,         /* 1 */
};

/********************************
 * exa entry points declarations
 ********************************/

Bool
WIZExaPrepareSolid(PixmapPtr      pPixmap,
		int            alu,
		Pixel          planemask,
		Pixel          fg);

void
WIZExaSolid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2);

void
WIZExaDoneSolid(PixmapPtr pPixmap);

void
WIZExaCopy(PixmapPtr pDstPixmap,
		int    srcX,
		int    srcY,
		int    dstX,
		int    dstY,
		int    width,
		int    height);

void
WIZExaDoneCopy(PixmapPtr pDstPixmap);

Bool
WIZExaCheckComposite(int op,
		PicturePtr   pSrcPicture,
		PicturePtr   pMaskPicture,
		PicturePtr   pDstPicture);


Bool
WIZExaPrepareComposite(int                op,
		PicturePtr         pSrcPicture,
		PicturePtr         pMaskPicture,
		PicturePtr         pDstPicture,
		PixmapPtr          pSrc,
		PixmapPtr          pMask,
		PixmapPtr          pDst);

void
WIZExaComposite(PixmapPtr pDst,
		int srcX,
		int srcY,
		int maskX,
		int maskY,
		int dstX,
		int dstY,
		int width,
		int height);

Bool
WIZExaPrepareCopy(PixmapPtr       pSrcPixmap,
		PixmapPtr       pDstPixmap,
		int             dx,
		int             dy,
		int             alu,
		Pixel           planemask);

void
WIZExaDoneComposite(PixmapPtr pDst);


Bool
WIZExaUploadToScreen(PixmapPtr pDst,
		int x,
		int y,
		int w,
		int h,
		char *src,
		int src_pitch);
Bool
WIZExaDownloadFromScreen(PixmapPtr pSrc,
		int x,  int y,
		int w,  int h,
		char *dst,
		int dst_pitch);

void
WIZExaWaitMarker (ScreenPtr pScreen, int marker);

static void
WIZBlockHandler(pointer blockData, OSTimePtr timeout, pointer readmask)
{
	ScreenPtr pScreen = (ScreenPtr) blockData;

	exaWaitSync(pScreen);
}

static void
WIZWakeupHandler(pointer blockData, int result, pointer readmask)
{
}

void
WIZDrawFini(ScrnInfoPtr pScrn) {
	WizPtr pWiz = WizPTR(pScrn);

	if (pWiz->exa) {
		exaDriverFini(pWiz->pScreen);
		xfree(pWiz->exa);
		pWiz->exa = NULL;
	}
}

Bool
WIZDrawEnable(ScrnInfoPtr pScrn)
{
	//WizPtr pWiz = WizPTR(pScrn);

	return TRUE;
}

void
WIZDrawDisable(ScrnInfoPtr pScrn) {
	//WizPtr pWiz = WizPTR(pScrn);

}

Bool
WIZDrawExaInit(ScrnInfoPtr pScrn)
{
	WizPtr pWiz = WizPTR(pScrn);

	Bool success = FALSE;
	ExaDriverPtr exa;

	xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			"EXA hardware acceleration initialising\n");

	exa = pWiz->exa = exaDriverAlloc();
	if(!exa)
		return FALSE;

	pWiz->exa->memoryBase = pWiz->fbstart;
	pWiz->exa->memorySize = pWiz->fb_fix.smem_len;
	pWiz->exa->offScreenBase = (pWiz->fb_fix.line_length * pWiz->fb_var.yres);

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->PrepareSolid = WIZExaPrepareSolid;
	exa->Solid = WIZExaSolid;
	exa->DoneSolid = WIZExaDoneSolid;

	exa->PrepareCopy = WIZExaPrepareCopy;
	exa->Copy = WIZExaCopy;
	exa->DoneCopy = WIZExaDoneCopy;

	exa->CheckComposite = WIZExaCheckComposite;
	exa->PrepareComposite = WIZExaPrepareComposite;
	exa->Composite = WIZExaComposite;
	exa->DoneComposite = WIZExaDoneComposite;

	exa->DownloadFromScreen = WIZExaDownloadFromScreen;
	exa->UploadToScreen = WIZExaUploadToScreen;

	/*wizs->exa.MarkSync = WIZExaMarkSync;*/
	exa->WaitMarker = WIZExaWaitMarker;

	exa->pixmapOffsetAlign = 2;
	exa->pixmapPitchAlign = 2;

	exa->maxX = 320;
	exa->maxY = 240;

	exa->flags = EXA_OFFSCREEN_PIXMAPS;

	RegisterBlockAndWakeupHandlers(WIZBlockHandler,
			WIZWakeupHandler,
			pWiz->pScreen);

	success = exaDriverInit(pWiz->pScreen, exa);
	if (success) {
		ErrorF("Initialized EXA acceleration\n");
	} else {
		ErrorF("Failed to initialize EXA acceleration\n");
		xfree(pWiz->exa);
		pWiz->exa = NULL;
	}

	return success;
}

Bool
WIZExaPrepareSolid(PixmapPtr      pPix,
		int            alu,
		Pixel          pm,
		Pixel          fg)
{
	//ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	//WizPtr pWiz = WizPTR(pScrn);

	CARD32 offset;
	CARD16 op, pitch;
	FbBits mask;

	if (pPix->drawable.bitsPerPixel != 16)
		WIZ_FALLBACK(("Only 16bpp is supported\n"));

	mask = FbFullMask(16);
	if ((pm & mask) != mask)
		WIZ_FALLBACK(("Can't do planemask 0x%08x\n",
					(unsigned int) pm));
	op = WIZSolidRop[alu] << 8;
	offset = exaGetPixmapOffset(pPix);
	pitch = exaGetPixmapPitch(pPix);

	return FALSE;
}

void
WIZExaSolid(PixmapPtr pPix, int x1, int y1, int x2, int y2)
{
	//ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	//WizPtr pWiz = WizPTR(pScrn);

}

void
WIZExaDoneSolid(PixmapPtr pPix)
{
	ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
	WizPtr pWiz = WizPTR(pScrn);
	exaMarkSync(pWiz->pScreen);
}

Bool
WIZExaPrepareCopy(PixmapPtr       pSrc,
		PixmapPtr       pDst,
		int             dx,
		int             dy,
		int             alu,
		Pixel           pm)
{
	return FALSE;
	//ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
	//WizPtr pWiz = WizPTR(pScrn);

	FbBits mask;

	CARD32 src_offset, dst_offset;
	CARD16 src_pitch, dst_pitch;
	CARD16 op;

	if (pSrc->drawable.bitsPerPixel != 16 ||
			pDst->drawable.bitsPerPixel != 16)
		WIZ_FALLBACK(("Only 16bpp is supported"));

	mask = FbFullMask(16);
	if ((pm & mask) != mask) {
		WIZ_FALLBACK(("Can't do planemask 0x%08x",
					(unsigned int) pm));
	}

	src_offset = exaGetPixmapOffset(pSrc);
	src_pitch = exaGetPixmapPitch(pSrc);

	dst_offset = exaGetPixmapOffset(pDst);
	dst_pitch = exaGetPixmapPitch(pDst);

	op = WIZBltRop[alu] << 8;
}

void
WIZExaCopy(PixmapPtr       pDst,
		int    srcX,
		int    srcY,
		int    dstX,
		int    dstY,
		int    width,
		int    height)
{
	//ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	//WizPtr pWiz = WizPTR(pScrn);
}

void
WIZExaDoneCopy(PixmapPtr pDst)
{
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	WizPtr pWiz = WizPTR(pScrn);
	exaMarkSync(pWiz->pScreen);
}

Bool
WIZExaCheckComposite(int op,
		PicturePtr   pSrcPicture,
		PicturePtr   pMaskPicture,
		PicturePtr   pDstPicture)
{
	return FALSE;
}

Bool
WIZExaPrepareComposite(int                op,
		PicturePtr         pSrcPicture,
		PicturePtr         pMaskPicture,
		PicturePtr         pDstPicture,
		PixmapPtr          pSrc,
		PixmapPtr          pMask,
		PixmapPtr          pDst)
{
	return FALSE;
}

void
WIZExaComposite(PixmapPtr pDst,
		int srcX,
		int srcY,
		int maskX,
		int maskY,
		int dstX,
		int dstY,
		int width,
		int height)
{
}

void
WIZExaDoneComposite(PixmapPtr pDst)
{
}

Bool
WIZExaUploadToScreen(PixmapPtr pDst,
		int x,
		int y,
		int w,
		int h,
		char *src,
		int src_pitch)
{
	return FALSE;
	
	ScrnInfoPtr pScrn = xf86Screens[pDst->drawable.pScreen->myNum];
	WizPtr pWiz = WizPTR(pScrn);
	int bpp, i;
	CARD8 *dst_offset;
	int dst_pitch;

	exaWaitSync(pScrn->pScreen);

	bpp = pDst->drawable.bitsPerPixel / 8;
	dst_pitch = exaGetPixmapPitch(pDst);
	dst_offset = pWiz->exa->memoryBase + exaGetPixmapOffset(pDst)
		+ x*bpp + y*dst_pitch;

	for (i = 0; i < h; i++) {
		memcpy(dst_offset, src, w*bpp);
		dst_offset += dst_pitch;
		src += src_pitch;
	}
}

Bool
WIZExaDownloadFromScreen(PixmapPtr pSrc,
		int x,  int y,
		int w,  int h,
		char *dst,
		int dst_pitch)
{
	return FALSE;
	
	ScrnInfoPtr pScrn = xf86Screens[pSrc->drawable.pScreen->myNum];
	WizPtr pWiz = WizPTR(pScrn);
	int bpp, i;
	CARD8 *dst_offset, *src;
	int src_pitch;

	exaWaitSync(pScrn->pScreen);

	bpp = pSrc->drawable.bitsPerPixel / 8;
	src_pitch = exaGetPixmapPitch(pSrc);
	src = pWiz->exa->memoryBase + exaGetPixmapOffset(pSrc) +
		x*bpp + y*src_pitch;
	dst_offset = (unsigned char*)dst;

	for (i = 0; i < h; i++) {
		memcpy(dst_offset, src, w*bpp);
		dst_offset += dst_pitch;
		src += src_pitch;
	}
}

void
WIZExaWaitMarker (ScreenPtr pScreen, int marker)
{
	//ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	//WizPtr pWiz = WizPTR(pScrn);
}

