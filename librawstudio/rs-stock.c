/*
 * * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>,
 * * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <rawstudio.h>
#include <config.h>

typedef struct _RSCursorItem RSCursorItem;

struct _RSCursorItem {
	const gchar *filename;
	const gint x_hot, y_hot;
};

static RSCursorItem rs_cursor_items[] = {
	{ "cursor-crop.png", 8, 8},
	{ "cursor-rotate.png", 8, 8},
	{ "cursor-color-picker.png", 8, 8},
};

static void
icon_theme_append_path(GtkIconTheme *theme, const gchar *new_path)
{
	gchar **paths = NULL;
	gint n = 0;
	gchar **extended;
	gint i;

	gtk_icon_theme_get_search_path(theme, &paths, &n);

	extended = g_new(gchar *, n + 2);
	for (i = 0; i < n; i++)
		extended[i] = paths[i];
	extended[n]     = (gchar *) new_path;
	extended[n + 1] = NULL;

	gtk_icon_theme_set_search_path(theme, (const gchar **) extended, n + 1);

	g_free(extended);
	if (paths)
		g_strfreev(paths);
}

void
rs_stock_init(void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();

	/* Installed path: $(datadir)/carastudio/icons */
	icon_theme_append_path(theme,
		PACKAGE_DATA_DIR G_DIR_SEPARATOR_S PACKAGE G_DIR_SEPARATOR_S "icons");

	/* Dev path: running from source tree without install */
	if (g_file_test("data/icons", G_FILE_TEST_IS_DIR))
		icon_theme_append_path(theme, "data/icons");
}

GdkCursor*
rs_cursor_new(GdkDisplay *display, RSCursorType cursor_type)
{
	RSCursorItem *cursor = &rs_cursor_items[cursor_type];
	GdkPixbuf *pixbuf = NULL;

	pixbuf = gdk_pixbuf_new_from_file(g_build_filename(
		PACKAGE_DATA_DIR G_DIR_SEPARATOR_S "pixmaps" G_DIR_SEPARATOR_S PACKAGE,
		cursor->filename, NULL), NULL);

	return gdk_cursor_new_from_pixbuf(display, pixbuf, cursor->x_hot, cursor->y_hot);
}
