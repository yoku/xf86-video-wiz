/*
 * Copyright © 2009 Yogish Kulkarni <yogishkulkarni@gmail.com>
 *
 * This driver is based on Xati,
 * Copyright © 2004 Eric Anholt
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
 *
 */
#ifndef _WIZ_LOG_H_
#define _WIZ_LOG_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include "os.h"

#ifdef NDEBUG
/*we are not in debug mode*/
#define WIZ_LOG(...) do {} while(0)
#define WIZ_LOG_ERROR(...) do {} while(0)

#else /*NDEBUG*/
#define ERROR_LOG_LEVEL 3
#define INFO_LOG_LEVEL 4

#ifndef WIZ_LOG
#define WIZ_LOG(...) \
LogMessageVerb(X_NOTICE, INFO_LOG_LEVEL, "in %s:%d:%s: ",\
               __FILE__, __LINE__, __func__) ; \
LogMessageVerb(X_NOTICE, INFO_LOG_LEVEL, __VA_ARGS__)
#endif /*WIZ_LOG*/

#ifndef WIZ_LOG_ERROR
#define WIZ_LOG_ERROR(...) \
LogMessageVerb(X_NOTICE, ERROR_LOG_LEVEL, "Error:in %s:%d:%s: ",\
               __FILE__, __LINE__, __func__) ; \
LogMessageVerb(X_NOTICE, ERROR_LOG_LEVEL, __VA_ARGS__)
#endif /*WIZ_LOG_ERROR*/


#endif /*NDEBUG*/

#ifndef WIZ_RETURN_IF_FAIL
#define WIZ_RETURN_IF_FAIL(cond) \
if (!(cond)) {\
	WIZ_LOG_ERROR("contion failed:%s\n",#cond);\
	return; \
}
#endif /*WIZ_RETURN_IF_FAIL*/

#ifndef WIZ_RETURN_VAL_IF_FAIL
#define WIZ_RETURN_VAL_IF_FAIL(cond, val) \
if (!(cond)) {\
	WIZ_LOG_ERROR("contion failed:%s\n",#cond);\
	return val; \
}
#endif /*WIZ_RETURN_VAL_IF_FAIL*/

#endif /*_WIZ_LOG_H_*/

