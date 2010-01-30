/*
 * Copyright © 2009 Yogish Kulkarni <yogishkulkarni@gmail.com>
 *
 * This driver is based on Xati,
 * Copyright  2004 Eric Anholt
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

#ifndef _WIZ_DRAW_H_
#define _WIZ_DRAW_H_

void WIZWaitIdle(WizPtr *pWiz);

#define WIZ_TRACE_FALL 0
#define WIZ_TRACE_DRAW 1

#if WIZ_TRACE_FALL
#define WIZ_FALLBACK(x)			\
do {					\
	ErrorF("%s: ", __FUNCTION__);	\
	ErrorF x;			\
	return FALSE;			\
} while (0)
#else
#define WIZ_FALLBACK(x) return FALSE
#endif

#if WIZ_TRACE_DRAW
#define ENTER_DRAW(pix) WIZEnterDraw(pix, __FUNCTION__)
#define LEAVE_DRAW(pix) WIZLeaveDraw(pix, __FUNCTION__)

void
WIZEnterDraw (PixmapPtr pPixmap, const char *function);

void
WIZLeaveDraw (PixmapPtr pPixmap, const char *function);
#else /* WIZ_TRACE */
#define ENTER_DRAW(pix)
#define LEAVE_DRAW(pix)
#endif /* !WIZ_TRACE */

#endif /* _WIZ_DRAW_H_ */
