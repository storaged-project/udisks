/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 David Zeuthen <zeuthen@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#if !defined (__STORAGED_INSIDE_STORAGED_H__) && !defined (STORAGED_COMPILATION)
#error "Only <storaged/storaged.h> can be included directly."
#endif

#ifndef __STORAGED_ERROR_H__
#define __STORAGED_ERROR_H__

#include <storaged/storagedtypes.h>

G_BEGIN_DECLS

/**
 * STORAGED_ERROR:
 *
 * Error domain for Storaged. Errors in this domain will be form the
 * #StoragedError enumeration. See #GError for more information on error
 * domains.
 */
#define STORAGED_ERROR (storaged_error_quark ())

GQuark storaged_error_quark (void);

G_END_DECLS

#endif /* __STORAGED_ERROR_H__ */
