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

#include <gtk/gtk.h>
#include "rs-dir-selector.h"

enum {
	COL_NAME = 0,
	COL_PATH,
	NUM_COLS
};

struct _RSDirSelector {
	GtkScrolledWindow parent;
	GtkWidget *view;
	gchar *root;
};

G_DEFINE_TYPE (RSDirSelector, rs_dir_selector, GTK_TYPE_SCROLLED_WINDOW);

static void realize(GtkWidget *widget, gpointer data);
static void dir_selector_add_element(GtkTreeStore *treestore, GtkTreeIter *iter, const gchar *name, const gchar *path);
static void row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data);
static void row_expanded(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data);
static void row_collapsed(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data);
static void move_cursor(GtkTreeView *view, GtkMovementStep movement, gint direction, gpointer user_data);
static GtkTreeModel *create_and_fill_model(RSDirSelector *selector, const gchar *root);
static gboolean directory_contains_directories(const gchar *filepath);
static gint dir_sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data);

#define UP_ENTRY "↩"

enum {
	DIRECTORY_ACTIVATED_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * Class initializer
 */
static void
rs_dir_selector_class_init(RSDirSelectorClass *klass)
{
	signals[DIRECTORY_ACTIVATED_SIGNAL] = g_signal_new ("directory-activated",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST,
		0,
		NULL, 
		NULL,                
		g_cclosure_marshal_VOID__STRING,
		G_TYPE_NONE,
		1,
		G_TYPE_STRING);
}

/**
 * Instance initialization
 */
static void
rs_dir_selector_init(RSDirSelector *selector)
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkScrolledWindow *scroller = GTK_SCROLLED_WINDOW(selector);

	g_object_set (G_OBJECT (selector), "hadjustment", NULL, "vadjustment", NULL, NULL);
	gtk_scrolled_window_set_policy (scroller, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* Un GtkScrolledWindow peut se reduire a zero (il scrolle) : sans
	   plancher de hauteur, l'arbre s'ecrase en bande fine quand il a peu
	   de lignes. On force une hauteur minimale et on reclame l'espace
	   vertical disponible dans l'expander parent. */
	gtk_scrolled_window_set_min_content_height (scroller, 200);
	gtk_widget_set_vexpand (GTK_WIDGET (selector), TRUE);

	selector->view = gtk_tree_view_new();
	col = gtk_tree_view_column_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(selector->view), col);
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute (col, renderer, "text",
										COL_NAME);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(selector->view), FALSE);

	g_signal_connect(selector->view, "row-activated", G_CALLBACK(row_activated), selector);
	g_signal_connect(selector->view, "row-expanded", G_CALLBACK(row_expanded), NULL);
	g_signal_connect(selector->view, "row-collapsed", G_CALLBACK(row_collapsed), NULL);
	g_signal_connect(selector->view, "move-cursor", G_CALLBACK(move_cursor), NULL);
	
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(selector->view)),
								GTK_SELECTION_SINGLE);

	/* Ouvre par défaut sur ~/Images, avec fallback sur ~ */
	const gchar *pictures = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	if (!pictures || !g_file_test(pictures, G_FILE_TEST_IS_DIR))
		pictures = g_get_home_dir();
	rs_dir_selector_set_root(selector, pictures);

	g_signal_connect(selector, "realize", G_CALLBACK(realize), NULL);

	gtk_container_add (GTK_CONTAINER (scroller), selector->view);
	return;
}

static void
realize(GtkWidget *widget, gpointer data)
{
	RSDirSelector *selector = RS_DIR_SELECTOR(widget);
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(selector->view));

	if (!selection)
		return;

	if (gtk_tree_selection_count_selected_rows(selection) == 1)
	{
		GList *selected = gtk_tree_selection_get_selected_rows(selection, NULL);

		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(selector->view), g_list_nth_data(selected, 0), NULL, TRUE, 0.2, 0.0);

		g_list_foreach (selected, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (selected);
	}
}

/**
 * Adds an element to treestore
 * @param treestore A GtkTreeStore
 * @param iter A GtkTreeIter
 * @param name A gchar
 * @param path A gchar
 */
static void
dir_selector_add_element(GtkTreeStore *treestore, GtkTreeIter *iter, const gchar *name, const gchar *path)
{
	GtkTreeIter this, child;

	gtk_tree_store_append(treestore, &this, iter);
	gtk_tree_store_set(treestore, &this,
					   COL_NAME, name,
					   COL_PATH, path,
					   -1);
	if (path && directory_contains_directories(path))
	{
		gtk_tree_store_append(treestore, &child, &this);
		gtk_tree_store_set(treestore, &child,
		-1);
	}
}

/**
 * Callback after activating a row
 * @param view A GtkTreeView
 * @param path A GtkTreePath
 * @param col A GtkTreeViewColumn
 * @param user_data A gpointer
 */
static void
row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data)
{
	RSDirSelector *selector = RS_DIR_SELECTOR(user_data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *name = NULL;
	gchar *filepath = NULL;

	model = gtk_tree_view_get_model(view);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter,
	                   COL_NAME, &name,
	                   COL_PATH, &filepath,
	                   -1);

	if (g_strcmp0(name, UP_ENTRY) == 0)
	{
		/* Remonte d'un niveau : change la racine affichée */
		rs_dir_selector_set_root(selector, filepath);
	}

	g_signal_emit(G_OBJECT(selector), signals[DIRECTORY_ACTIVATED_SIGNAL], 0, filepath);
	g_free(name);
	g_free(filepath);
}

/**
 * Callback after expanding a row
 * @param view A GtkTreeView
 * @param iter A GtkTreeIter
 * @param path A GtkTreePath
 * @param user_data A gpointer
 */
static void
row_expanded(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter empty;
	gchar *filepath;
	gchar *file;
	GDir *dir;
	GString *gs = NULL;

	/* Set busy cursor */	
	if (GDK_IS_WINDOW(gtk_widget_get_window(GTK_WIDGET(view))))
	{
		GdkCursor* cursor = gdk_cursor_new(GDK_WATCH);
		gdk_window_set_cursor(gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(view))), cursor);
		gdk_cursor_unref(cursor);
		gdk_flush();
	}

	model = gtk_tree_view_get_model(view);
	gtk_tree_model_iter_children(GTK_TREE_MODEL(model),
								 &empty, iter);
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter,
					   COL_PATH, &filepath,
					   -1);

	dir = g_dir_open(filepath, 0, NULL);

	if (dir)
	{
		while ((file = (gchar *) g_dir_read_name(dir)))
		{
			gs = g_string_new(filepath);
			g_string_append(gs, file);
			g_string_append(gs, "/");
			if (g_file_test(gs->str, G_FILE_TEST_IS_DIR)) 
			{
				if (file[0] == '.') 
				{
					/* Fixme: If hidden files should be shown too */
				} 
				else
				{
					dir_selector_add_element(GTK_TREE_STORE(model), iter, file, gs->str);
				}
			}
			g_string_free(gs, TRUE);	
		}
		g_dir_close(dir);
		g_free(filepath);
	}
	if (GDK_IS_WINDOW(gtk_widget_get_window(GTK_WIDGET(view))))
		gdk_window_set_cursor(gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(view))), NULL);
	gtk_tree_store_remove(GTK_TREE_STORE(model), &empty);
}

/**
 * Callback after collapsing a row
 * @param view A GtkTreeView
 * @param iter A GtkTreeIter
 * @param path A GtkTreePath
 * @param user_data A gpointer
 */
static void
row_collapsed(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter child;

	model = gtk_tree_view_get_model(view);
	while (gtk_tree_model_iter_children(GTK_TREE_MODEL(model), &child, iter)) 
	{
		gtk_tree_store_remove(GTK_TREE_STORE(model), &child);
	}
	dir_selector_add_element(GTK_TREE_STORE(model), iter, NULL, NULL);
}

/**
 * Callback after moving cursor
 * @param view A GtkTreeView
 * @param iter A GtkMovementStep
 * @param path A gint
 * @param user_data A gpointer
 */
static void
move_cursor(GtkTreeView *view, GtkMovementStep movement, gint direction, gpointer user_data)
{
	if (movement == GTK_MOVEMENT_VISUAL_POSITIONS)
	{
		GtkTreePath *path;
		gtk_tree_view_get_cursor(view, &path, NULL);
		if (direction == 1) /* RIGHT */
		{
			gtk_tree_view_expand_row(view, path, FALSE);
		}
		else if (direction == -1) /* LEFT */
		{
			gtk_tree_view_collapse_row(view, path);
		}
		gtk_tree_path_free(path);
	}
}

/* Tri : ↩ toujours en premier, sinon alphabétique */
static gint
dir_sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gchar *na = NULL, *nb = NULL;
	gint result;

	gtk_tree_model_get(model, a, COL_NAME, &na, -1);
	gtk_tree_model_get(model, b, COL_NAME, &nb, -1);

	if (g_strcmp0(na, UP_ENTRY) == 0)      { result = -1; goto done; }
	if (g_strcmp0(nb, UP_ENTRY) == 0)      { result =  1; goto done; }
	result = g_utf8_collate(na ? na : "", nb ? nb : "");
done:
	g_free(na);
	g_free(nb);
	return result;
}

/**
 * Creates a GtkTreeStore showing the CONTENTS of root directly,
 * with a ↩ entry at top to navigate to the parent directory.
 */
static GtkTreeModel *
create_and_fill_model(RSDirSelector *selector, const gchar *root)
{
	GtkTreeStore *treestore;
	GtkTreeIter iter;
	GDir *dir;
	gchar *file;

	treestore = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING);

	/* Entrée ↩ (sauf si on est déjà à la racine filesystem) */
	if (g_strcmp0(root, "/") != 0)
	{
		/* Normalise : retire le slash final pour que g_path_get_dirname marche */
		gchar *tmp = g_strdup(root);
		gsize len = strlen(tmp);
		if (len > 1 && tmp[len-1] == G_DIR_SEPARATOR)
			tmp[len-1] = '\0';
		gchar *parent = g_path_get_dirname(tmp);
		g_free(tmp);
		gchar *parent_slash = g_strconcat(parent, "/", NULL);
		g_free(parent);

		gtk_tree_store_append(treestore, &iter, NULL);
		gtk_tree_store_set(treestore, &iter,
		                   COL_NAME, UP_ENTRY,
		                   COL_PATH, parent_slash,
		                   -1);
		g_free(parent_slash);
	}

	/* Contenu du répertoire courant */
	dir = g_dir_open(root, 0, NULL);
	if (dir)
	{
		while ((file = (gchar *) g_dir_read_name(dir)))
		{
			if (file[0] == '.')
				continue;
			gchar *path = g_strconcat(root, file, "/", NULL);
			if (g_file_test(path, G_FILE_TEST_IS_DIR))
				dir_selector_add_element(treestore, NULL, file, path);
			g_free(path);
		}
		g_dir_close(dir);
	}

	return GTK_TREE_MODEL(treestore);
}

/**
 * Creates a GtkWidget
 * @return A GtkWidget
 */
GtkWidget *
rs_dir_selector_new()
{
	return g_object_new (RS_DIR_SELECTOR_TYPE_WIDGET, NULL);
}

/**
 * Sets root
 * @param root A gchar
 */
void
rs_dir_selector_set_root(RSDirSelector *selector, const gchar *root)
{
	/* Normalise : s'assure que root se termine par '/' */
	gchar *root_norm;
	if (root && !g_str_has_suffix(root, "/"))
		root_norm = g_strconcat(root, "/", NULL);
	else
		root_norm = g_strdup(root ? root : "/");

	g_free(selector->root);
	selector->root = root_norm;

	GtkTreeModel *model = create_and_fill_model(selector, root_norm);

	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(model);
	gtk_tree_sortable_set_sort_func(sortable, COL_NAME, dir_sort_func, NULL, NULL);
	gtk_tree_sortable_set_sort_column_id(sortable, COL_NAME, GTK_SORT_ASCENDING);

	gtk_tree_view_set_model(GTK_TREE_VIEW(selector->view), model);
	g_object_unref(model);
}

/**
 * Expands to path
 * @param path to expand
 */
void
rs_dir_selector_expand_path(RSDirSelector *selector, const gchar *expand)
{
	GtkTreeView *view = GTK_TREE_VIEW(selector->view);
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	gchar *filepath = NULL;
	GString *gs;

	if (g_path_is_absolute(expand))
		gs = g_string_new(expand);
	else
	{
		gs = g_string_new(g_get_current_dir());
		g_string_append(gs, G_DIR_SEPARATOR_S);
		g_string_append(gs, expand);
	}
	if (!g_str_has_suffix(gs->str, G_DIR_SEPARATOR_S))
		g_string_append(gs, G_DIR_SEPARATOR_S);

	/* Si le répertoire cible est hors de la racine courante, on ne change pas
	   la racine : le sélecteur reste sur ~/Images (ou le dernier répertoire
	   défini). L'expansion échouera silencieusement et l'utilisateur verra
	   la racine par défaut. */

	model = gtk_tree_view_get_model(view);
	path = gtk_tree_path_new_first();

	while (gtk_tree_model_get_iter(model, &iter, path))
	{
		gtk_tree_model_get(model, &iter, COL_PATH, &filepath, -1);

		if (filepath && g_str_has_prefix(gs->str, filepath))
		{
			gtk_tree_view_expand_row(view, path, FALSE);
			gtk_tree_path_down(path);
		}
		else
		{
			gtk_tree_path_next(path);
		}
		g_free(filepath);
		filepath = NULL;
	}

	g_string_free(gs, TRUE);

	if (gtk_widget_get_realized(GTK_WIDGET(selector)))
		gtk_tree_view_scroll_to_cell(view, path, NULL, TRUE, 0.2, 0.0);
	else
	{
		GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
		if (gtk_tree_model_get_iter(model, &iter, path))
			gtk_tree_selection_select_iter(selection, &iter);
	}

	gtk_tree_path_free(path);
}

static gboolean
directory_contains_directories(const gchar *filepath)
{
	GDir *dir;
	GString *gs = NULL;
	gchar *file;

	dir = g_dir_open(filepath, 0, NULL);

	if (dir)
	{
		while ((file = (gchar *) g_dir_read_name(dir)))
		{
			gs = g_string_new(filepath);
			g_string_append(gs, file);
			g_string_append(gs, "/");

			if (file[0] == '.') 
			{
				/* Fixme: If hidden files should be shown too */
			} 
			else
			{
				/* FIXME: This will cause RS to hang on dead NFS mount */
				if (g_file_test(gs->str, G_FILE_TEST_IS_DIR)) 
				{
					g_dir_close(dir);
					g_string_free(gs, TRUE);
					return TRUE;
				}
			}
			g_string_free(gs, TRUE);
		}
		g_dir_close(dir);
	}

	return FALSE;
}
