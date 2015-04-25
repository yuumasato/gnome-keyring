/*
 * gnome-keyring
 *
 * Copyright (C) 2009 Stefan Walter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __GKD_SECRET_SECRET_H__
#define __GKD_SECRET_SECRET_H__

#include "gkd-secret-types.h"

#include <glib.h>

struct _GkdSecretSecret {
	GkdSecretSession *session;

	gpointer parameter;
	gsize n_parameter;
	gpointer value;
	gsize n_value;

	GDestroyNotify destroy_func;
	gpointer destroy_data;
};

GkdSecretSecret *      gkd_secret_secret_new                      (GkdSecretSession *session,
                                                                   gconstpointer parameter,
                                                                   gsize n_parameter,
                                                                   gconstpointer value,
                                                                   gsize n_value);

GkdSecretSecret*       gkd_secret_secret_new_take_memory          (GkdSecretSession *session,
                                                                   gpointer parameter,
                                                                   gsize n_parameter,
                                                                   gpointer value,
                                                                   gsize n_value);

GkdSecretSecret*       gkd_secret_secret_parse                    (GkdSecretService *service,
                                                                   const char *sender,
                                                                   GVariant *variant,
                                                                   GError **error);

GVariant *             gkd_secret_secret_append                   (GkdSecretSecret *secret);

void                   gkd_secret_secret_free                     (gpointer data);

#endif /* __GKD_SECRET_PROPERTY_H__ */
