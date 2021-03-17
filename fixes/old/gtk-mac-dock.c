/* GTK+ Integration for the Mac OS X Dock.
 *
 * Copyright (C) 2007, 2008 Imendio AB
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __x86_64__
/* FIXME: Add example like this to docs for the open documents stuff:

    <key>CFBundleDocumentTypes</key>
    <array>
      <dict>
        <key>CFBundleTypeExtensions</key>
        <array>
          <string>txt</string>
        </array>
      </dict>
    </array>

*/

#include <config.h>
#include <Carbon/Carbon.h>
#include <sys/param.h>
#include <gtk/gtk.h>

#include "gtk-mac-dock.h"
#include "gtk-mac-bundle.h"
#include "gtk-mac-image-utils.h"
#include "gtk-mac-private.h"

enum
{
  CLICKED,
  QUIT_ACTIVATE,
  OPEN_DOCUMENTS,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct GtkMacDockPriv GtkMacDockPriv;

struct GtkMacDockPriv
{
  glong id;
};

static void  mac_dock_finalize                  (GObject          *object);
static OSErr mac_dock_handle_quit               (const AppleEvent *inAppleEvent,
    AppleEvent       *outAppleEvent,
    long              inHandlerRefcon);
static OSErr mac_dock_handle_open_documents     (const AppleEvent *inAppleEvent,
    AppleEvent       *outAppleEvent,
    long              inHandlerRefcon);
static OSErr mac_dock_handle_open_application   (const AppleEvent *inAppleEvent,
    AppleEvent       *outAppleEvent,
    long              inHandlerRefcon);
static OSErr mac_dock_handle_reopen_application (const AppleEvent *inAppleEvent,
    AppleEvent       *outAppleEvent,
    long              inHandlerRefcon);

G_DEFINE_TYPE (GtkMacDock, gtk_mac_dock, G_TYPE_OBJECT)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GTK_TYPE_MAC_DOCK, GtkMacDockPriv))

static GList      *handlers;
static GtkMacDock *global_dock;

static void
gtk_mac_dock_class_init (GtkMacDockClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = mac_dock_finalize;

  signals[CLICKED] =
    g_signal_new ("clicked",
                  GTK_TYPE_MAC_DOCK,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /* FIXME: Need marshaller. */
  signals[OPEN_DOCUMENTS] =
    g_signal_new ("open-documents",
                  GTK_TYPE_MAC_DOCK,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[QUIT_ACTIVATE] =
    g_signal_new ("quit-activate",
                  GTK_TYPE_MAC_DOCK,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_type_class_add_private (object_class, sizeof (GtkMacDockPriv));

  /* FIXME: Just testing with triggering Carbon to take control over
   * the dock menu events instead of Cocoa (which happens when the
   * sharedApplication is created) to get custom dock menu working
   * with carbon menu code. However, doing this makes the dock icon
   * not get a "running triangle".
   */
#if 0
  EventTypeSpec kFakeEventList[] = { { INT_MAX, INT_MAX } };
  EventRef event;

  ReceiveNextEvent (GetEventTypeCount (kFakeEventList),
                    kFakeEventList,
                    kEventDurationNoWait, false,
                    &event);
#endif
}

static void
gtk_mac_dock_init (GtkMacDock *dock)
{
  GtkMacDockPriv *priv = GET_PRIV (dock);
  static glong    id;

  priv->id = ++id;

  handlers = g_list_prepend (handlers, dock);

  AEInstallEventHandler (kCoreEventClass, kAEQuitApplication,
                         mac_dock_handle_quit,
                         priv->id, true);
  AEInstallEventHandler (kCoreEventClass, kAEOpenApplication,
                         mac_dock_handle_open_application,
                         priv->id, true);
  AEInstallEventHandler (kCoreEventClass, kAEReopenApplication,
                         mac_dock_handle_reopen_application,
                         priv->id, true);
  AEInstallEventHandler (kCoreEventClass, kAEOpenDocuments,
                         mac_dock_handle_open_documents,
                         priv->id, true);
}

static void
mac_dock_finalize (GObject *object)
{
  GtkMacDockPriv *priv;

  priv = GET_PRIV (object);

  AERemoveEventHandler (kCoreEventClass, kAEQuitApplication,
                        mac_dock_handle_quit, false);
  AERemoveEventHandler (kCoreEventClass, kAEReopenApplication,
                        mac_dock_handle_reopen_application, false);
  AERemoveEventHandler (kCoreEventClass, kAEOpenApplication,
                        mac_dock_handle_open_application, false);
  AERemoveEventHandler (kCoreEventClass, kAEOpenDocuments,
                        mac_dock_handle_open_documents, false);

  handlers = g_list_remove (handlers, object);

  G_OBJECT_CLASS (gtk_mac_dock_parent_class)->finalize (object);
}

GtkMacDock *
gtk_mac_dock_new (void)
{
  return g_object_new (GTK_TYPE_MAC_DOCK, NULL);
}

GtkMacDock *
gtk_mac_dock_get_default (void)
{
  if (!global_dock)
    global_dock = g_object_new (GTK_TYPE_MAC_DOCK, NULL);

  return global_dock;
}

/* For internal use only. Returns TRUE if there is a handled setup for the
 * Quit dock menu item (i.e. if there is a dock instance alive).
 */
gboolean
_gtk_mac_dock_is_quit_menu_item_handled (void)
{
  return handlers != NULL;
}

static GtkMacDock *
mac_dock_get_from_id (gulong id)
{
  GList      *l;
  GtkMacDock *dock = NULL;

  for (l = handlers; l; l = l->next)
    {
      dock = l->data;
      if (GET_PRIV (dock)->id == id)
        break;

      dock = NULL;
    }

  return dock;
}

static OSErr
mac_dock_handle_quit (const AppleEvent *inAppleEvent,
                      AppleEvent       *outAppleEvent,
                      long              inHandlerRefcon)
{
  GtkMacDock *dock;

  dock = mac_dock_get_from_id (inHandlerRefcon);

  if (dock)
    g_signal_emit (dock, signals[QUIT_ACTIVATE], 0);

  return noErr;
}

static OSErr
mac_dock_handle_open_application (const AppleEvent *inAppleEvent,
                                  AppleEvent       *outAppleEvent,
                                  long              inHandlerRefCon)
{
  /*g_print ("FIXME: mac_dock_handle_open_application\n");*/

  return noErr;
}

static OSErr
mac_dock_handle_reopen_application (const AppleEvent *inAppleEvent,
                                    AppleEvent       *outAppleEvent,
                                    long              inHandlerRefcon)
{
  GtkMacDock *dock;

  dock = mac_dock_get_from_id (inHandlerRefcon);

  if (dock)
    g_signal_emit (dock, signals[CLICKED], 0);

  return noErr;
}

static OSErr
mac_dock_handle_open_documents (const AppleEvent *inAppleEvent,
                                AppleEvent       *outAppleEvent,
                                long              inHandlerRefCon)
{
  GtkMacDock *dock;
  OSStatus    status;
  AEDescList  documents;
  gchar       path[MAXPATHLEN];

  /*g_print ("FIXME: mac_dock_handle_open_documents\n");*/

  dock = mac_dock_get_from_id (inHandlerRefCon);

  status = AEGetParamDesc (inAppleEvent,
                           keyDirectObject, typeAEList,
                           &documents);
  if (status == noErr)
    {
      long count = 0;
      int  i;

      AECountItems (&documents, &count);

      for (i = 0; i < count; i++)
        {
          FSRef ref;

          status = AEGetNthPtr (&documents, i + 1, typeFSRef,
                                0, 0, &ref, sizeof (ref),
                                0);
          if (status != noErr)
            continue;

          FSRefMakePath (&ref, (unsigned char *) path, MAXPATHLEN);

          /* FIXME: Add to a list, then emit the open-documents
           * signal.
           */
          /*g_print ("  %s\n", path);*/
        }
    }

  return status;
}

void
gtk_mac_dock_set_icon_from_pixbuf (GtkMacDock *dock,
                                   GdkPixbuf  *pixbuf)
{
  if (!pixbuf)
    RestoreApplicationDockTileImage ();
  else
    {
      CGImageRef image;

      image = gtkosx_create_cgimage_from_pixbuf (pixbuf);
      SetApplicationDockTileImage (image);
      CGImageRelease (image);
    }
}

void
gtk_mac_dock_set_icon_from_resource (GtkMacDock   *dock,
                                     GtkMacBundle *bundle,
                                     const gchar  *name,
                                     const gchar  *type,
                                     const gchar  *subdir)
{
  gchar *path;

  g_return_if_fail (GTK_IS_MAC_DOCK (dock));
  g_return_if_fail (name != NULL);

  path = gtk_mac_bundle_get_resource_path (bundle, name, type, subdir);
  if (path)
    {
      GdkPixbuf *pixbuf;

      pixbuf = gdk_pixbuf_new_from_file (path, NULL);
      if (pixbuf)
        {
          gtk_mac_dock_set_icon_from_pixbuf (dock, pixbuf);
          g_object_unref (pixbuf);
        }

      g_free (path);
    }
}

void
gtk_mac_dock_set_overlay_from_pixbuf (GtkMacDock  *dock,
                                      GdkPixbuf   *pixbuf)
{
  CGImageRef image;

  g_return_if_fail (GTK_IS_MAC_DOCK (dock));
  g_return_if_fail (pixbuf == NULL || GDK_IS_PIXBUF (pixbuf));

  if (pixbuf)
    {
      image = gtkosx_create_cgimage_from_pixbuf (pixbuf);
      OverlayApplicationDockTileImage (image);
      CGImageRelease (image);
    }
  else
    RestoreApplicationDockTileImage ();
}

void
gtk_mac_dock_set_overlay_from_resource (GtkMacDock   *dock,
                                        GtkMacBundle *bundle,
                                        const gchar  *name,
                                        const gchar  *type,
                                        const gchar  *subdir)
{
  gchar *path;

  g_return_if_fail (GTK_IS_MAC_DOCK (dock));
  g_return_if_fail (name != NULL);

  path = gtk_mac_bundle_get_resource_path (bundle, name, type, subdir);
  if (path)
    {
      GdkPixbuf *pixbuf;

      pixbuf = gdk_pixbuf_new_from_file (path, NULL);
      if (pixbuf)
        {
          gtk_mac_dock_set_overlay_from_pixbuf (dock, pixbuf);
          g_object_unref (pixbuf);
        }

      g_free (path);
    }
}

struct _GtkMacAttentionRequest
{
  NMRec    nm_request;
  guint    timeout_id;
  gboolean is_cancelled;
};

static gboolean
mac_dock_attention_cb (GtkMacAttentionRequest *request)
{
  request->timeout_id = 0;
  request->is_cancelled = TRUE;

  NMRemove (&request->nm_request);

  return FALSE;
}


/* FIXME: Add listener for "application activated" and cancel any
 * requests.
 */
/**
 * gtk_mac_doc_attention_request:
 * @dock: The dock object
 * @type: The attention type flag
 */
GtkMacAttentionRequest *
gtk_mac_dock_attention_request (GtkMacDock          *dock,
                                GtkMacAttentionType  type)
{
  GtkMacAttentionRequest *request;

  request = g_new0 (GtkMacAttentionRequest, 1);

  request->nm_request.nmMark = 1;
  request->nm_request.qType = nmType;

  if (NMInstall (&request->nm_request) != noErr)
    {
      g_free (request);
      return NULL;
    }

  if (type == GTK_MAC_ATTENTION_INFO)
    request->timeout_id = gdk_threads_add_timeout (
                            1000,
                            (GSourceFunc) mac_dock_attention_cb,
                            request);

  return request;
}

/**
 * gtk_mac_dock_attention_cancel:
 * @dock: The dock object
 * @request: The request id we want to cancel.
 */
void
gtk_mac_dock_attention_cancel (GtkMacDock             *dock,
                               GtkMacAttentionRequest *request)
{
  if (request->timeout_id)
    g_source_remove (request->timeout_id);

  if (!request->is_cancelled)
    NMRemove (&request->nm_request);

  g_free (request);
}

GType
gtk_mac_attention_type_get_type (void)
{
  /* FIXME */
  return 0;
}

#endif // __x86_64__
