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

#if !defined (__UDISKS_INSIDE_UDISKS_H__) && !defined (UDISKS_COMPILATION)
#error "Only <udisks/udisks.h> can be included directly."
#endif

#ifndef __UDISKS_ERROR_H__
#define __UDISKS_ERROR_H__

#include <udisks/udiskstypes.h>

G_BEGIN_DECLS

/**
 * UDISKS_ERROR:
 *
 * Error domain for UDisks. Errors in this domain will be form the
 * #UDisksError enumeration. See #GError for more information on error
 * domains.
 */
#define UDISKS_ERROR (udisks_error_quark ())

GQuark udisks_error_quark (void);

G_END_DECLS

#endif /* __UDISKS_ERROR_H__ */
