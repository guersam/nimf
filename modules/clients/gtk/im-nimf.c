/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*-  */
/*
 * im-nimf.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2015 Hodong Kim <cogniti@gmail.com>
 *
 * Nimf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nimf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>
#include <glib/gi18n.h>
#include <nimf.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>

#define NIMF_GTK_TYPE_IM_CONTEXT  (nimf_gtk_im_context_get_type ())
#define NIMF_GTK_IM_CONTEXT(obj)  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NIMF_GTK_TYPE_IM_CONTEXT, NimfGtkIMContext))

typedef struct _NimfGtkIMContext      NimfGtkIMContext;
typedef struct _NimfGtkIMContextClass NimfGtkIMContextClass;

struct _NimfGtkIMContext
{
  GtkIMContext  parent_instance;

  NimfIM       *im;
  GdkWindow    *client_window;
  GdkRectangle  cursor_area;
  GSettings    *settings;
  gboolean      is_reset_on_gdk_button_press_event;
  gboolean      is_hook_gdk_event_key;
  gboolean      has_focus;
  gboolean      has_event_filter;
};

struct _NimfGtkIMContextClass
{
  GtkIMContextClass parent_class;
};

G_DEFINE_DYNAMIC_TYPE (NimfGtkIMContext, nimf_gtk_im_context, GTK_TYPE_IM_CONTEXT);

static NimfEvent *
translate_gdk_event_key (GdkEventKey *event)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfEventType type;

  if (event->type == GDK_KEY_PRESS)
    type = NIMF_EVENT_KEY_PRESS;
  else
    type = NIMF_EVENT_KEY_RELEASE;

  NimfEvent *nimf_event = nimf_event_new (type);
  nimf_event->key.state = event->state;
  nimf_event->key.keyval = event->keyval;
  nimf_event->key.hardware_keycode = event->hardware_keycode;

  return nimf_event;
}

static NimfEvent *
translate_xkey_event (XEvent *xevent)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfEventType type = NIMF_EVENT_NOTHING;

  if (xevent->type == KeyPress)
    type = NIMF_EVENT_KEY_PRESS;
  else
    type = NIMF_EVENT_KEY_RELEASE;

  NimfEvent *nimf_event = nimf_event_new (type);
  nimf_event->key.state  = xevent->xkey.state;
  nimf_event->key.keyval = XLookupKeysym (&xevent->xkey,
                             (!(xevent->xkey.state & ShiftMask) !=
                              !(xevent->xkey.state & LockMask)) ? 1 : 0);
  nimf_event->key.hardware_keycode = xevent->xkey.keycode;

  return nimf_event;
}

static gboolean
nimf_gtk_im_context_filter_keypress (GtkIMContext *context,
                                     GdkEventKey  *event)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  gboolean retval = FALSE;
  NimfEvent *nimf_event = translate_gdk_event_key (event);

  retval = nimf_im_filter_event (NIMF_GTK_IM_CONTEXT (context)->im, nimf_event);
  nimf_event_free (nimf_event);

  return retval;
}

static void
nimf_gtk_im_context_reset (GtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_im_reset (NIMF_GTK_IM_CONTEXT (context)->im);
}

static GdkFilterReturn
on_gdk_x_event (XEvent           *xevent,
                GdkEvent         *event,
                NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s: %p, %" G_GINT64_FORMAT, G_STRFUNC, context,
           g_get_real_time ());

  gboolean retval = FALSE;

  if (context->has_focus == FALSE || context->client_window == NULL)
    return GDK_FILTER_CONTINUE;

  switch (xevent->type)
  {
    case KeyPress:
    case KeyRelease:
      if (context->is_hook_gdk_event_key)
      {
        NimfEvent *d_event = translate_xkey_event (xevent);
        retval = nimf_im_filter_event (context->im, d_event);
        nimf_event_free (d_event);
      }
      break;
    case ButtonPress:
      if (context->is_reset_on_gdk_button_press_event)
        nimf_im_reset (context->im);
      break;
    default:
      break;
  }

  if (retval == FALSE)
    return GDK_FILTER_CONTINUE;
  else
    return GDK_FILTER_REMOVE;
}

static void
nimf_gtk_im_context_set_client_window (GtkIMContext *context,
                                       GdkWindow    *window)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfGtkIMContext *a_context = NIMF_GTK_IM_CONTEXT (context);

  if (a_context->client_window)
  {
    g_object_unref (a_context->client_window);
    a_context->client_window = NULL;
  }

  if (window)
    a_context->client_window = g_object_ref (window);
}

static void
nimf_gtk_im_context_get_preedit_string (GtkIMContext   *context,
                                        gchar         **str,
                                        PangoAttrList **attrs,
                                        gint           *cursor_pos)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  PangoAttribute *attr;

  nimf_im_get_preedit_string (NIMF_GTK_IM_CONTEXT (context)->im,
                              str, cursor_pos);

  if (attrs)
  {
    *attrs = pango_attr_list_new ();

    attr = pango_attr_underline_new (PANGO_UNDERLINE_SINGLE);

    if (str)
    {
      attr->start_index = 0;
      attr->end_index   = strlen (*str);
    }

    pango_attr_list_change (*attrs, attr);
  }
}

static void
nimf_gtk_im_context_focus_in (GtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfGtkIMContext *a_context = NIMF_GTK_IM_CONTEXT (context);
  a_context->has_focus = TRUE;
  nimf_im_focus_in (a_context->im);
}

static void
nimf_gtk_im_context_focus_out (GtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfGtkIMContext *a_context = NIMF_GTK_IM_CONTEXT (context);
  nimf_im_focus_out (a_context->im);
  a_context->has_focus = FALSE;
}

static void
nimf_gtk_im_context_set_cursor_location (GtkIMContext *context,
                                         GdkRectangle *area)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfGtkIMContext *nimf_context = NIMF_GTK_IM_CONTEXT (context);

  if (memcmp (&nimf_context->cursor_area, area, sizeof (GdkRectangle)) == 0)
    return;

  nimf_context->cursor_area = *area;
  GdkRectangle root_area = *area;

  if (nimf_context->client_window)
  {
    gdk_window_get_root_coords (nimf_context->client_window,
                                area->x,
                                area->y,
                                &root_area.x,
                                &root_area.y);

    nimf_im_set_cursor_location (NIMF_GTK_IM_CONTEXT (context)->im,
                                 (const NimfRectangle *) &root_area);
  }
}

static void
nimf_gtk_im_context_set_use_preedit (GtkIMContext *context,
                                     gboolean      use_preedit)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_im_set_use_preedit (NIMF_GTK_IM_CONTEXT (context)->im, use_preedit);
}

static gboolean
nimf_gtk_im_context_get_surrounding (GtkIMContext  *context,
                                     gchar        **text,
                                     gint          *cursor_index)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return nimf_im_get_surrounding (NIMF_GTK_IM_CONTEXT (context)->im,
                                  text, cursor_index);
}

static void
nimf_gtk_im_context_set_surrounding (GtkIMContext *context,
                                     const char   *text,
                                     gint          len,
                                     gint          cursor_index)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_im_set_surrounding (NIMF_GTK_IM_CONTEXT (context)->im,
                           text, len, cursor_index);
}

GtkIMContext *
nimf_gtk_im_context_new (void)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return g_object_new (NIMF_GTK_TYPE_IM_CONTEXT, NULL);
}

static void
on_commit (NimfIM           *im,
           const gchar      *text,
           NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_signal_emit_by_name (context, "commit", text);
}

static gboolean
on_delete_surrounding (NimfIM           *im,
                       gint              offset,
                       gint              n_chars,
                       NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  gboolean retval;
  g_signal_emit_by_name (context,
                         "delete-surrounding", offset, n_chars, &retval);
  return retval;
}

static void
on_preedit_changed (NimfIM           *im,
                    NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
  g_signal_emit_by_name (context, "preedit-changed");
}

static void
on_preedit_end (NimfIM           *im,
                NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
  g_signal_emit_by_name (context, "preedit-end");
}

static void
on_preedit_start (NimfIM           *im,
                  NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
  g_signal_emit_by_name (context, "preedit-start");
}

static gboolean
on_retrieve_surrounding (NimfIM           *im,
                         NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  gboolean retval;
  g_signal_emit_by_name (context, "retrieve-surrounding", &retval);

  return retval;
}

static void
nimf_gtk_im_context_update_event_filter (NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  if (context->is_reset_on_gdk_button_press_event ||
      context->is_hook_gdk_event_key)
  {
    if (context->has_event_filter == FALSE)
    {
      context->has_event_filter = TRUE;
      gdk_window_add_filter (NULL, (GdkFilterFunc) on_gdk_x_event, context);
    }
  }
  else
  {
    if (context->has_event_filter == TRUE)
    {
      context->has_event_filter = FALSE;
      gdk_window_remove_filter (NULL, (GdkFilterFunc) on_gdk_x_event, context);
    }
  }
}

static void
on_changed_reset_on_gdk_button_press_event (GSettings        *settings,
                                            gchar            *key,
                                            NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  context->is_reset_on_gdk_button_press_event =
    g_settings_get_boolean (context->settings, key);

  nimf_gtk_im_context_update_event_filter (context);
}

static void
on_changed_hook_gdk_event_key (GSettings        *settings,
                               gchar            *key,
                               NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  context->is_hook_gdk_event_key =
    g_settings_get_boolean (context->settings, key);

  nimf_gtk_im_context_update_event_filter (context);
}

static void
nimf_gtk_im_context_init (NimfGtkIMContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  context->im = nimf_im_new ();

  g_signal_connect (context->im, "commit",
                    G_CALLBACK (on_commit), context);
  g_signal_connect (context->im, "delete-surrounding",
                    G_CALLBACK (on_delete_surrounding), context);
  g_signal_connect (context->im, "preedit-changed",
                    G_CALLBACK (on_preedit_changed), context);
  g_signal_connect (context->im, "preedit-end",
                    G_CALLBACK (on_preedit_end), context);
  g_signal_connect (context->im, "preedit-start",
                    G_CALLBACK (on_preedit_start), context);
  g_signal_connect (context->im, "retrieve-surrounding",
                    G_CALLBACK (on_retrieve_surrounding), context);

  context->settings = g_settings_new ("org.nimf.clients.gtk");

  context->is_reset_on_gdk_button_press_event =
    g_settings_get_boolean (context->settings,
                            "reset-on-gdk-button-press-event");

  context->is_hook_gdk_event_key =
    g_settings_get_boolean (context->settings, "hook-gdk-event-key");

  nimf_gtk_im_context_update_event_filter (context);

  g_signal_connect (context->settings,
                    "changed::reset-on-gdk-button-press-event",
                    G_CALLBACK (on_changed_reset_on_gdk_button_press_event),
                    context);
  g_signal_connect (context->settings, "changed::hook-gdk-event-key",
                    G_CALLBACK (on_changed_hook_gdk_event_key), context);
}

static void
nimf_gtk_im_context_finalize (GObject *object)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfGtkIMContext *context = NIMF_GTK_IM_CONTEXT (object);

  if (context->has_event_filter)
    gdk_window_remove_filter (NULL, (GdkFilterFunc) on_gdk_x_event, context);

  g_object_unref (context->im);
  g_object_unref (context->settings);

  if (context->client_window)
    g_object_unref (context->client_window);

  G_OBJECT_CLASS (nimf_gtk_im_context_parent_class)->finalize (object);
}

static void
nimf_gtk_im_context_class_init (NimfGtkIMContextClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkIMContextClass *im_context_class = GTK_IM_CONTEXT_CLASS (class);

  im_context_class->set_client_window   = nimf_gtk_im_context_set_client_window;
  im_context_class->get_preedit_string  = nimf_gtk_im_context_get_preedit_string;
  im_context_class->filter_keypress     = nimf_gtk_im_context_filter_keypress;
  im_context_class->focus_in            = nimf_gtk_im_context_focus_in;
  im_context_class->focus_out           = nimf_gtk_im_context_focus_out;
  im_context_class->reset               = nimf_gtk_im_context_reset;
  im_context_class->set_cursor_location = nimf_gtk_im_context_set_cursor_location;
  im_context_class->set_use_preedit     = nimf_gtk_im_context_set_use_preedit;
  im_context_class->set_surrounding     = nimf_gtk_im_context_set_surrounding;
  im_context_class->get_surrounding     = nimf_gtk_im_context_get_surrounding;

  object_class->finalize = nimf_gtk_im_context_finalize;
}

static void
nimf_gtk_im_context_class_finalize (NimfGtkIMContextClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static const GtkIMContextInfo nimf_info = {
  PACKAGE,          /* ID */
  N_("Nimf"),       /* Human readable name */
  GETTEXT_PACKAGE,  /* Translation domain */
  NIMF_LOCALE_DIR,  /* Directory for bindtextdomain */
  "ko:ja:zh"        /* Languages for which this module is the default */
};

static const GtkIMContextInfo *info_list[] = {
  &nimf_info
};

G_MODULE_EXPORT void im_module_init (GTypeModule *type_module)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_gtk_im_context_register_type (type_module);
}

G_MODULE_EXPORT void im_module_exit (void)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

G_MODULE_EXPORT void im_module_list (const GtkIMContextInfo ***contexts,
                                     int                      *n_contexts)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  *contexts = info_list;
  *n_contexts = G_N_ELEMENTS (info_list);
}

G_MODULE_EXPORT GtkIMContext *im_module_create (const gchar *context_id)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  if (g_strcmp0 (context_id, PACKAGE) == 0)
    return nimf_gtk_im_context_new ();
  else
    return NULL;
}