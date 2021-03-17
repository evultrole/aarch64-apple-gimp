/* GTK+ Integration for the Mac OS X Menubar.
 *
 * Copyright (C) 2007 Pioneer Research Center USA, Inc.
 * Copyright (C) 2007, 2008 Imendio AB
 * Copyright © 2009, 2010 John Ralls
 *
 * For further information, see:
 * http://sourceforge.net/apps/trac/gtk-osx/wiki/Integrate
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
#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkquartz.h>
#include <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include "gtk-mac-menu.h"
#include "gtk-mac-private.h"

/* TODO
 *
 * - Adding a standard Window menu (Minimize etc)?
 * - Sync reordering items? Does that work now?
 * - Create on demand? (can this be done with gtk+? ie fill in menu
     items when the menu is opened)
 *
 * - Deleting a menu item that is not the last one in a menu doesn't work
 */

#define GTK_QUARTZ_MENU_CREATOR 'GTKC'
#define GTK_QUARTZ_ITEM_WIDGET  'IWID'

#define GTK_MAC_KEY_HANDLER     "gtk-mac-key-handler"

#define DEBUG FALSE
#define DEBUG_SET FALSE
#define DEBUG_SYNC FALSE
#define DEBUG_SIGNAL FALSE
#define DEBUG_CARBON FALSE

static MenuID   last_menu_id;
static gboolean global_key_handler_enabled = TRUE;

static void   sync_menu_shell (GtkMenuShell *menu_shell, MenuRef carbon_menu,
                               gboolean toplevel, gboolean debug);

/* A category that exposes the protected carbon event for an NSEvent. */
@interface NSEvent (GdkQuartzNSEvent)
- (void *)gdk_quartz_event_ref;
@end

@implementation NSEvent (GdkQuartzNSEvent)
- (void *)gdk_quartz_event_ref
{
  return _eventRef;
}
@end

static gboolean
menu_flash_off_cb (gpointer data)
{
  /* Disable flash by flashing a non-existing menu id. */
  FlashMenuBar (last_menu_id + 1);
  return FALSE;
}

/*
 * utility functions
 */

static GtkWidget *
find_menu_label (GtkWidget *widget)
{
  GtkWidget *label = NULL;
  if (GTK_IS_LABEL (widget))
    return widget;
  if (GTK_IS_CONTAINER (widget))
    {
      GList *children;
      GList *l;
      children = gtk_container_get_children (GTK_CONTAINER (widget));
      for (l = children; l; l = l->next)
        {
          label = find_menu_label (l->data);
          if (label)
            break;
        }
      g_list_free (children);
    }
  return label;
}

static const gchar *
get_menu_label_text (GtkWidget  *menu_item, GtkWidget **label)
{
  GtkWidget *my_label;
  my_label = find_menu_label (menu_item);
  if (label)
    *label = my_label;
  if (my_label)
    return gtk_label_get_text (GTK_LABEL (my_label));
  return NULL;
}

static gboolean
accel_find_func (GtkAccelKey *key, GClosure *closure, gpointer data)
{
  return (GClosure *) data == closure;
}

static GClosure *
_gtk_accel_label_get_closure (GtkAccelLabel *label)
{
  g_return_val_if_fail (GTK_IS_ACCEL_LABEL (label), NULL);

  GClosure *closure = NULL;
  g_object_get (G_OBJECT (label), "accel-closure", &closure, NULL);
  return closure;
}

/*
 * CarbonMenu functions
 *
 * A CarbonMenu contains a reference to the OSX menu; connect attaches
 * it to the GtkMenu so that sync_menu will know which OSX menu to
 * synchronise as it recurses through the GtkMenu tree. There is no
 * back-pointer from the OSX Menu to the GtkMenu. Note that Gtk
 * doesn't have an "App" menu (the one named after the application),
 * so no MenuRef points to it.
 */

typedef struct
{
  MenuRef menu;
  guint   toplevel : 1;
} CarbonMenu;

static GQuark carbon_menu_quark = 0;

static CarbonMenu *
carbon_menu_new (void)
{
  return g_slice_new0 (CarbonMenu);
}

static void
carbon_menu_free (CarbonMenu *menu)
{
  DisposeMenu (menu->menu);
  g_slice_free (CarbonMenu, menu);
}

static CarbonMenu *
carbon_menu_get (GtkWidget *widget)
{
  return g_object_get_qdata (G_OBJECT (widget), carbon_menu_quark);
}

static void
carbon_menu_connect (GtkWidget *menu, MenuRef menuRef, gboolean toplevel)
{
  CarbonMenu *carbon_menu = carbon_menu_get (menu);
  if (!carbon_menu)
    {
      carbon_menu = carbon_menu_new ();
      g_object_set_qdata_full (G_OBJECT (menu), carbon_menu_quark,
                               carbon_menu,
                               (GDestroyNotify) carbon_menu_free);
    }
  carbon_menu->menu     = menuRef;
  carbon_menu->toplevel = toplevel;
}


/*
 * CarbonMenuItem functions
 *
 * Like CarbonMenu, the CarbonMenuItem contains a reference to the OSX
 * Menu which contains it and the index the menu item in that menu
 * (there aren't pointers directly to menu items in Carbon like there
 * are in Cocoa). If the item has a submenu, there's a reference for
 * that object as well, and there's a pointer to the accelerator for
 * connecting signals from. This structure is inserted into the
 * GtkMenuItem, and pointer to the GtkMenuItem is attached to the OSX
 * Menu at the indicated index. Much effort goes into ensuring that
 * the indices stay synchronized, as interesting behavior will result
 * if they get out of sync.
 */

typedef struct
{
  MenuRef        menu;
  MenuItemIndex  index;
  MenuRef        submenu;
  GClosure      *accel_closure;
} CarbonMenuItem;

static GQuark carbon_menu_item_quark = 0;

static CarbonMenuItem *
carbon_menu_item_new (void)
{
  return g_slice_new0 (CarbonMenuItem);
}

static void
carbon_menu_item_free (CarbonMenuItem *menu_item)
{
  DeleteMenuItem (menu_item->menu, menu_item->index); //Clean up the Carbon Menu
  if (menu_item->accel_closure)
    g_closure_unref (menu_item->accel_closure);
  g_slice_free (CarbonMenuItem, menu_item);
}

static const gchar *
carbon_menu_error_string (OSStatus err)
{
  switch (err)
    {
    case 0:
      return "No Error";
    case -50:
      return "User Parameter List";
    case -5603:
      return "Menu Property Reserved Creator Type";
    case -5604:
      return "Menu Property Not Found";
    case -5620:
      return "Menu Not Found";
    case -5621:
      return "Menu uses system definition";
    case -5622:
      return "Menu Item Not Found";
    case -5623:
      return "Menu Reference Invalid";
    case -9860:
      return "Event Already Posted";
    case -9861:
      return "Event Target Busy";
    case -9862:
      return "Invalid Event Class";
    case -9864:
      return "Incorrect Event Class";
    case -9866:
      return "Event Handler Already Installed";
    case -9868:
      return "Internal Event Error";
    case -9869:
      return "Incorrect Event Kind";
    case -9870:
      return "Event Parameter Not Found";
    case -9874:
      return "Event Not Handled";
    case -9875:
      return "Event Loop Timeout";
    case -9876:
      return "Event Loop Quit";
    case -9877:
      return  "Event Not In Queue";
    case -9878:
      return "Hot Key Exists";
    case -9879:
      return "Invalid Hot Key";
    default:
      return "Invalid Error Code";
    }
  return "System Error: Unreachable";
}

#define carbon_menu_warn(err, msg) \
    if (err && DEBUG_CARBON) \
	g_printerr("%s: %s %s\n", G_STRFUNC, msg, carbon_menu_error_string(err));

#define carbon_menu_warn_label(err, label, msg) \
    if (err && DEBUG_CARBON) \
	g_printerr("%s: %s %s %s\n", G_STRFUNC, label, msg, carbon_menu_error_string(err));

#define carbon_menu_err_return(err, msg) \
    if (err) { \
    	if (DEBUG_CARBON) \
	    g_printerr("%s: %s %s\n", G_STRFUNC, msg, carbon_menu_error_string(err)); \
	return;\
    }

#define carbon_menu_err_return_val(err, msg, val) \
    if (err) { \
    	if (DEBUG_CARBON) \
	    g_printerr("%s: %s %s\n", G_STRFUNC, msg, carbon_menu_error_string(err)); \
	return val;\
    }

#define carbon_menu_err_return_label(err, label, msg)	\
    if (err) { \
    	if (DEBUG_CARBON) \
	    g_printerr("%s: %s %s %s\n", G_STRFUNC, label, msg, carbon_menu_error_string(err)); \
	return;\
    }

#define carbon_menu_err_return_label_val(err, label, msg, val)	\
    if (err) { \
    	if (DEBUG_CARBON) \
	    g_printerr("%s: %s %s %s\n", G_STRFUNC, label, msg, carbon_menu_error_string(err)); \
	return val;\
    }

static CarbonMenuItem *
carbon_menu_item_get (GtkWidget *widget)
{
  return g_object_get_qdata (G_OBJECT (widget), carbon_menu_item_quark);
}

static CarbonMenuItem *
carbon_menu_item_get_checked (GtkWidget *widget)
{
  CarbonMenuItem * carbon_item = carbon_menu_item_get (widget);
  GtkWidget *checkWidget = NULL;
  OSStatus  err;
  const gchar *label = get_menu_label_text (GTK_WIDGET (widget), NULL);
  const gchar *name = gtk_widget_get_name (widget);

  if (!carbon_item)
    return NULL;

  /* Get any GtkWidget associated with the item. */
  err = GetMenuItemProperty (carbon_item->menu, carbon_item->index,
                             GTK_QUARTZ_MENU_CREATOR,
                             GTK_QUARTZ_ITEM_WIDGET,
                             sizeof (checkWidget), 0, &checkWidget);
  if (err)
    {
      if (DEBUG_CARBON)
        g_printerr ("%s: Widget %s %s Cross-check error %s\n", G_STRFUNC,
                    name, label, carbon_menu_error_string (err));
      return NULL;
    }
  /* This could check the checkWidget, but that could turn into a
   * recursion nightmare, so worry about it when it we run
   * carbon_menu_item_get on it.
   */
  if (widget != checkWidget)
    {
      const gchar *clabel = get_menu_label_text (GTK_WIDGET (checkWidget),
                            NULL);
      const gchar *cname = gtk_widget_get_name (checkWidget);
      if (DEBUG_CARBON)
        g_printerr ("%s: Widget mismatch, expected %s %s got %s %s\n",
                    G_STRFUNC, name, label, cname, clabel);
      return NULL;
    }
  return carbon_item;
}

static void
carbon_menu_item_update_state (CarbonMenuItem *carbon_item, GtkWidget *widget)
{
  gboolean sensitive;
  gboolean visible;
  UInt32   set_attrs = 0;
  UInt32   clear_attrs = 0;
  OSStatus err;

  g_object_get (widget, "sensitive", &sensitive, "visible",   &visible, NULL);
  if (!sensitive)
    set_attrs |= kMenuItemAttrDisabled;
  else
    clear_attrs |= kMenuItemAttrDisabled;
  if (!visible)
    set_attrs |= kMenuItemAttrHidden;
  else
    clear_attrs |= kMenuItemAttrHidden;
  err = ChangeMenuItemAttributes (carbon_item->menu, carbon_item->index,
                                  set_attrs, clear_attrs);
  if (!err && carbon_item->submenu)
    err = ChangeMenuAttributes (carbon_item->submenu,
                                set_attrs, clear_attrs);
  carbon_menu_warn (err, "Failed to update state");
}

static void
carbon_menu_item_update_active (CarbonMenuItem *carbon_item,
                                GtkWidget *widget)
{
  gboolean active;
  g_object_get (widget, "active", &active, NULL);
  CheckMenuItem (carbon_item->menu, carbon_item->index, active);
}

static void
carbon_menu_item_update_submenu (CarbonMenuItem *carbon_item,
                                 GtkWidget *widget, bool debug)
{
  GtkWidget *submenu;
  const gchar *label_text;
  CFStringRef  cfstr = NULL;
  OSStatus err;

  label_text = get_menu_label_text (widget, NULL);
  submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (widget));
  if (!submenu)
    {
      err = SetMenuItemHierarchicalMenu (carbon_item->menu,
                                         carbon_item->index, NULL);
      carbon_menu_warn_label (err, label_text, "Failed to clear submenu");
      carbon_item->submenu = NULL;
      return;
    }
  err = CreateNewMenu (++last_menu_id, 0, &carbon_item->submenu);
  carbon_menu_err_return_label (err, label_text, "Failed to create new menu");
  if (label_text)
    cfstr = CFStringCreateWithCString (NULL, label_text,
                                       kCFStringEncodingUTF8);

  err = SetMenuTitleWithCFString (carbon_item->submenu, cfstr);
  if (cfstr)
    CFRelease (cfstr);
  carbon_menu_err_return_label (err, label_text, "Failed to set menu title");
  err = SetMenuItemHierarchicalMenu (carbon_item->menu, carbon_item->index,
                                     carbon_item->submenu);
  carbon_menu_err_return_label (err, label_text, "Failed to set menu");
  sync_menu_shell (GTK_MENU_SHELL (submenu), carbon_item->submenu,
                   FALSE, debug);
}

static void
carbon_menu_item_update_label (CarbonMenuItem *carbon_item, GtkWidget *widget)
{
  const gchar *label_text;
  CFStringRef  cfstr = NULL;
  OSStatus err;

  label_text = get_menu_label_text (widget, NULL);
  if (label_text)
    cfstr = CFStringCreateWithCString (NULL, label_text,
                                       kCFStringEncodingUTF8);
  err = SetMenuItemTextWithCFString (carbon_item->menu, carbon_item->index,
                                     cfstr);
  carbon_menu_warn (err, "Failed to set menu text");
  if (cfstr)
    CFRelease (cfstr);
}

static void
carbon_menu_item_update_accelerator (CarbonMenuItem *carbon_item,
                                     GtkWidget *widget)
{
  GtkAccelKey *key;
  GtkWidget *label;
  GdkDisplay *display = NULL;
  GdkKeymap *keymap = NULL;
  GdkKeymapKey *keys = NULL;
  gint n_keys = 0;
  UInt8 modifiers = 0;
  OSStatus err;

  const gchar *label_txt = get_menu_label_text (widget, &label);
  if (! (GTK_IS_ACCEL_LABEL (label)
         && _gtk_accel_label_get_closure (GTK_ACCEL_LABEL (label))))
    {
// Clear the menu shortcut
      err = SetMenuItemModifiers (carbon_item->menu, carbon_item->index,
                                  kMenuNoModifiers | kMenuNoCommandModifier);
      carbon_menu_warn_label (err, label_txt, "Failed to set modifiers");
      err = ChangeMenuItemAttributes (carbon_item->menu, carbon_item->index,
                                      0, kMenuItemAttrUseVirtualKey);
      carbon_menu_warn_label (err, label_txt, "Failed to change attributes");
      err = SetMenuItemCommandKey (carbon_item->menu, carbon_item->index,
                                   false, 0);
      carbon_menu_warn_label (err, label_txt, "Failed to clear command key");
      return;
    }
  GClosure *closure = _gtk_accel_label_get_closure (GTK_ACCEL_LABEL (label));
  key = gtk_accel_group_find (gtk_accel_group_from_accel_closure (closure),
                              accel_find_func,
                              closure);
  if (! (key && key->accel_key && key->accel_flags & GTK_ACCEL_VISIBLE))
    return;
  display = gtk_widget_get_display (widget);
  keymap  = gdk_keymap_get_for_display (display);

  if (!gdk_keymap_get_entries_for_keyval (keymap, key->accel_key,
                                          &keys, &n_keys))
    return;

  err = SetMenuItemCommandKey (carbon_item->menu, carbon_item->index,
                               true, keys[0].keycode);
  carbon_menu_warn_label (err, label_txt, "Set Command Key Failed");
  g_free (keys);
  if (key->accel_mods)
    {
      if (key->accel_mods & GDK_SHIFT_MASK)
        modifiers |= kMenuShiftModifier;
      if (key->accel_mods & GDK_MOD1_MASK)
        modifiers |= kMenuOptionModifier;
    }
  if (! (key->accel_mods & GDK_CONTROL_MASK))
    {
      modifiers |= kMenuNoCommandModifier;
    }
  err = SetMenuItemModifiers (carbon_item->menu, carbon_item->index,
                              modifiers);
  carbon_menu_warn_label (err, label_txt, "Set Item Modifiers Failed");
  return;
}

static void
carbon_menu_item_accel_changed (GtkAccelGroup *accel_group, guint keyval,
                                GdkModifierType  modifier,
                                GClosure *accel_closure, GtkWidget *widget)
{
  CarbonMenuItem *carbon_item = carbon_menu_item_get (widget);
  GtkWidget      *label;

  const gchar *label_text = get_menu_label_text (widget, &label);
  if (!carbon_item)
    {
      if (DEBUG_CARBON)
        g_printerr ("%s: Bad carbon item for %s\n", G_STRFUNC, label_text);
      return;
    }
  if (gtk_accel_group_from_accel_closure (accel_closure) != accel_group)
    return;
  if (GTK_IS_ACCEL_LABEL (label) &&
      _gtk_accel_label_get_closure (GTK_ACCEL_LABEL (label)) == accel_closure)
    carbon_menu_item_update_accelerator (carbon_item, widget);
}

static void
carbon_menu_item_update_accel_closure (CarbonMenuItem *carbon_item,
                                       GtkWidget *widget)
{
  GtkAccelGroup *group;
  GtkWidget     *label;
  get_menu_label_text (widget, &label);
  if (carbon_item->accel_closure)
    {
      group = gtk_accel_group_from_accel_closure (carbon_item->accel_closure);
      g_signal_handlers_disconnect_by_func (group,
                                            carbon_menu_item_accel_changed,
                                            widget);
      g_closure_unref (carbon_item->accel_closure);
      carbon_item->accel_closure = NULL;
    }
  if (GTK_IS_ACCEL_LABEL (label))
    carbon_item->accel_closure = _gtk_accel_label_get_closure (GTK_ACCEL_LABEL (label));
  if (carbon_item->accel_closure)
    {
      g_closure_ref (carbon_item->accel_closure);
      group = gtk_accel_group_from_accel_closure (carbon_item->accel_closure);
      g_signal_connect_object (group, "accel-changed",
                               G_CALLBACK (carbon_menu_item_accel_changed),
                               widget, 0);
    }
  carbon_menu_item_update_accelerator (carbon_item, widget);
}

static void
carbon_menu_item_notify (GObject *object, GParamSpec *pspec,
                         CarbonMenuItem *carbon_item)
{
  if (!strcmp (pspec->name, "sensitive") ||
      !strcmp (pspec->name, "visible"))
    {
      carbon_menu_item_update_state (carbon_item, GTK_WIDGET (object));
    }
  else if (!strcmp (pspec->name, "active"))
    {
      carbon_menu_item_update_active (carbon_item, GTK_WIDGET (object));
    }
  else if (!strcmp (pspec->name, "submenu"))
    {
      carbon_menu_item_update_submenu (carbon_item, GTK_WIDGET (object),
                                       DEBUG_SIGNAL);
    }
  else if (DEBUG)
    g_printerr ("%s: Invalid parameter specification %s\n", G_STRFUNC,
                pspec->name);
}

static void
carbon_menu_item_notify_label (GObject *object, GParamSpec *pspec,
                               gpointer data)
{
  CarbonMenuItem *carbon_item =
    carbon_menu_item_get_checked (GTK_WIDGET (object));
  const gchar *label_text = get_menu_label_text (GTK_WIDGET (object), NULL);

  if (!carbon_item)
    {
      if (DEBUG_CARBON)
        g_printerr ("%s: Bad carbon item for %s\n", G_STRFUNC, label_text);
      return;
    }
  if (!strcmp (pspec->name, "label"))
    {
      carbon_menu_item_update_label (carbon_item, GTK_WIDGET (object));
    }
  else if (!strcmp (pspec->name, "accel-closure"))
    {
      carbon_menu_item_update_accel_closure (carbon_item,
                                             GTK_WIDGET (object));
    }
}

static CarbonMenuItem *
carbon_menu_item_connect (GtkWidget *menu_item, GtkWidget *label,
                          MenuRef menu, MenuItemIndex index)
{
  CarbonMenuItem *carbon_item =
    carbon_menu_item_get_checked (menu_item);

  if (!carbon_item)
    {
      carbon_item = carbon_menu_item_new ();
      g_object_set_qdata_full (G_OBJECT (menu_item), carbon_menu_item_quark,
                               carbon_item,
                               (GDestroyNotify) carbon_menu_item_free);
      g_signal_connect (menu_item, "notify",
                        G_CALLBACK (carbon_menu_item_notify), carbon_item);
      if (label)
        g_signal_connect_swapped (label, "notify::label",
                                  G_CALLBACK (carbon_menu_item_notify_label),
                                  menu_item);
    }
  carbon_item->menu  = menu;
  carbon_item->index = index;
  return carbon_item;
}

static CarbonMenuItem *
carbon_menu_item_create (GtkWidget *menu_item, MenuRef carbon_menu,
                         MenuItemIndex index, bool debug)
{
  GtkWidget          *label      = NULL;
  const gchar        *label_text;
  CFStringRef         cfstr      = NULL;
  MenuItemAttributes  attributes = 0;
  CarbonMenuItem *carbon_item;
  OSStatus err;

  label_text = get_menu_label_text (menu_item, &label);
  if (debug)
    g_printerr ("%s:   -> creating new %s\n", G_STRFUNC, label_text);
  if (label_text)
    cfstr = CFStringCreateWithCString (NULL, label_text,
                                       kCFStringEncodingUTF8);
  if (GTK_IS_SEPARATOR_MENU_ITEM (menu_item))
    attributes |= kMenuItemAttrSeparator;
  if (!gtk_widget_get_sensitive (menu_item))
    attributes |= kMenuItemAttrDisabled;
  if (!gtk_widget_get_visible (menu_item))
    attributes |= kMenuItemAttrHidden;
  err = InsertMenuItemTextWithCFString (carbon_menu, cfstr, index - 1,
                                        attributes, 0);
  carbon_menu_err_return_label_val (err, label_text,
                                    "Failed to insert menu item", NULL);
  err = SetMenuItemProperty (carbon_menu, index,
                             GTK_QUARTZ_MENU_CREATOR,
                             GTK_QUARTZ_ITEM_WIDGET,
                             sizeof (menu_item), &menu_item);

  if (cfstr)
    CFRelease (cfstr);
  if (err)
    {
      carbon_menu_warn_label (err, label_text,
                              "Failed to set menu property");
      DeleteMenuItem (carbon_menu, index); //Clean up the extra menu item
      return NULL;
    }
  carbon_item = carbon_menu_item_connect (menu_item, label,
                                          carbon_menu,
                                          index);
  if (!carbon_item)   //Got a bad carbon_item, bail out
    {
      DeleteMenuItem (carbon_menu, index); //Clean up the extra menu item
      return carbon_item;
    }
  return carbon_item;
}


typedef struct
{
  GtkWidget *widget;
} ActivateIdleData;

static void
activate_destroy_cb (gpointer user_data)
{
  ActivateIdleData *data = user_data;

  if (data->widget)
    g_object_remove_weak_pointer (G_OBJECT (data->widget),
                                  (gpointer) &data->widget);
  g_free (data);
}

static gboolean
activate_idle_cb (gpointer user_data)
{
  ActivateIdleData *data = user_data;

  if (data->widget)
    gtk_menu_item_activate (GTK_MENU_ITEM (data->widget));
  return FALSE;
}

/*
 * carbon event handler
 */

static OSStatus
menu_event_handler_func (EventHandlerCallRef  event_handler_call_ref,
                         EventRef event_ref, void *data)
{
  UInt32 event_class = GetEventClass (event_ref);
  UInt32 event_kind = GetEventKind (event_ref);
  HICommand command;
  OSStatus  err;
  GtkWidget *widget = NULL;
  ActivateIdleData *idleData;

  switch (event_class)
    {
    case kEventClassCommand:
      /* This is called when activating (is that the right GTK+ term?)
       * a menu item.
       */
      if (event_kind != kEventCommandProcess)
        break;

#if DEBUG
      g_printerr ("Menu: kEventClassCommand/kEventCommandProcess\n");
#endif
      err = GetEventParameter (event_ref, kEventParamDirectObject,
                               typeHICommand, 0,
                               sizeof (command), 0, &command);
      if (err != noErr)
        {
          carbon_menu_warn (err, "Get Event Returned Error");
          break;
        }
      /* Get any GtkWidget associated with the item. */
      err = GetMenuItemProperty (command.menu.menuRef,
                                 command.menu.menuItemIndex,
                                 GTK_QUARTZ_MENU_CREATOR,
                                 GTK_QUARTZ_ITEM_WIDGET,
                                 sizeof (widget), 0, &widget);
      if (err != noErr)
        {
          carbon_menu_warn (err, "Failed to retrieve the widget associated with the menu item");
          break;
        }
      if (! GTK_IS_WIDGET (widget))
        {
          g_printerr ("The item associated with the menu item isn't a widget\n");
          break;
        }
      /* Activate from an idle handler so that the event is
       * emitted from the main loop instead of in the middle of
       * handling quartz events.
       */
      idleData = g_new0 (ActivateIdleData, 1);
      idleData->widget = widget;
      g_object_add_weak_pointer (G_OBJECT (widget),
                                 (gpointer) &idleData->widget);
      g_idle_add_full (G_PRIORITY_HIGH, activate_idle_cb,
                       idleData, activate_destroy_cb);
      return noErr;
      break;
    case kEventClassMenu:
      if (event_kind == kEventMenuEndTracking)
        g_idle_add (menu_flash_off_cb, NULL);
      break;
    default:
      break;
    }
  return eventNotHandledErr;
}

static gboolean
nsevent_handle_menu_key (NSEvent *nsevent)
{
  EventRef      event_ref;
  MenuRef       menu_ref;
  MenuItemIndex index;
  MenuCommand menu_command;
  HICommand   hi_command;
  OSStatus err;

  if ([nsevent type] != NSKeyDown)
    return FALSE;
  event_ref = [nsevent gdk_quartz_event_ref];
  if (!IsMenuKeyEvent (NULL, event_ref, kMenuEventQueryOnly,
                       &menu_ref, &index))
    return FALSE;
  err = GetMenuItemCommandID (menu_ref, index, &menu_command);
  carbon_menu_err_return_val (err, "Failed to get command id", FALSE);
  hi_command.commandID = menu_command;
  hi_command.menu.menuRef = menu_ref;
  hi_command.menu.menuItemIndex = index;
  err = CreateEvent (NULL, kEventClassCommand, kEventCommandProcess,
                     0, kEventAttributeUserEvent, &event_ref);
  carbon_menu_err_return_val (err, "Failed to create event", FALSE);
  err = SetEventParameter (event_ref, kEventParamDirectObject, typeHICommand,
                           sizeof (HICommand), &hi_command);
  if (err != noErr)
    ReleaseEvent (event_ref); //We're about to bail, don't want to leak
  carbon_menu_err_return_val (err, "Failed to set event parm", FALSE);
  FlashMenuBar (GetMenuID (menu_ref));
  g_timeout_add (30, menu_flash_off_cb, NULL);
  err = SendEventToEventTarget (event_ref, GetMenuEventTarget (menu_ref));
  ReleaseEvent (event_ref);
  carbon_menu_err_return_val (err, "Failed to send event", FALSE);
  return TRUE;
}

gboolean
gtk_mac_menu_handle_menu_event (GdkEventKey *event)
{
  NSEvent *nsevent;

  /* FIXME: If the event here is unallocated, we crash. */
  nsevent = gdk_quartz_event_get_nsevent ((GdkEvent *) event);
  if (nsevent)
    return nsevent_handle_menu_key (nsevent);
  return FALSE;
}

static GdkFilterReturn
global_event_filter_func (gpointer  windowing_event, GdkEvent *event,
                          gpointer  user_data)
{
  NSEvent *nsevent = windowing_event;

  /* Handle menu events with no window, since they won't go through the
   * regular event processing.
   */
  if ([nsevent window] == nil)
    {
      if (nsevent_handle_menu_key (nsevent))
        return GDK_FILTER_REMOVE;
    }
  else if (global_key_handler_enabled && [nsevent type] == NSKeyDown)
    {
      GList *toplevels, *l;
      GtkWindow *focus = NULL;

      toplevels = gtk_window_list_toplevels ();
      for (l = toplevels; l; l = l->next)
        {
          if (gtk_window_has_toplevel_focus (l->data))
            {
              focus = l->data;
              break;
            }
        }
      g_list_free (toplevels);

      /* FIXME: We could do something to skip menu events if there is a
       * modal dialog...
       */
      if (!focus
          || !g_object_get_data (G_OBJECT (focus), GTK_MAC_KEY_HANDLER))
        {
          if (nsevent_handle_menu_key (nsevent))
            return GDK_FILTER_REMOVE;
        }
    }
  return GDK_FILTER_CONTINUE;
}

static gboolean
key_press_event (GtkWidget   *widget, GdkEventKey *event, gpointer user_data)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWidget *focus = gtk_window_get_focus (window);
  gboolean handled = FALSE;

  /* Text widgets get all key events first. */
  if (GTK_IS_EDITABLE (focus) || GTK_IS_TEXT_VIEW (focus))
    handled = gtk_window_propagate_key_event (window, event);

  if (!handled)
    handled = gtk_mac_menu_handle_menu_event (event);

  /* Invoke control/alt accelerators. */
  if (!handled && event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))
    handled = gtk_window_activate_key (window, event);

  /* Invoke focus widget handlers. */
  if (!handled)
    handled = gtk_window_propagate_key_event (window, event);

  /* Invoke non-(control/alt) accelerators. */
  if (!handled && ! (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    handled = gtk_window_activate_key (window, event);

  return handled;
}

static void
setup_menu_event_handler (void)
{
  static gboolean is_setup = FALSE;

  EventHandlerUPP menu_event_handler_upp;
  EventHandlerRef menu_event_handler_ref;
  OSStatus err;
  const EventTypeSpec menu_events[] =
  {
    { kEventClassCommand, kEventCommandProcess },
    { kEventClassMenu, kEventMenuEndTracking }
  };

  if (is_setup)
    return;
  gdk_window_add_filter (NULL, global_event_filter_func, NULL);
  menu_event_handler_upp = NewEventHandlerUPP (menu_event_handler_func);
  err = InstallEventHandler (GetApplicationEventTarget (),
                             menu_event_handler_upp,
                             GetEventTypeCount (menu_events), menu_events, 0,
                             &menu_event_handler_ref);
  carbon_menu_err_return (err, "Failed to install event handler");
#if 0
  /* Note: If we want to supporting shutting down, remove the handler
   * with:
   */
  err = RemoveEventHandler (menu_event_handler_ref);
  carbon_menu_warn (err, "Failed to remove handler");
  err = DisposeEventHandlerUPP (menu_event_handler_upp);
  carbon_menu_warn (err, "Failed to elete menu handler UPP");
#endif
  is_setup = TRUE;
}

static void
sync_menu_shell (GtkMenuShell *menu_shell, MenuRef carbon_menu,
                 gboolean toplevel, gboolean debug)
{
  GList         *children;
  GList         *l;
  MenuItemIndex  carbon_index = 1;
  OSStatus err;

  if (debug)
    g_printerr ("%s: syncing shell %s (%p)\n", G_STRFUNC,
                get_menu_label_text (GTK_WIDGET (menu_shell), NULL),
                menu_shell);
  carbon_menu_connect (GTK_WIDGET (menu_shell), carbon_menu, toplevel);
  children = gtk_container_get_children (GTK_CONTAINER (menu_shell));
  for (l = children; l; l = l->next)
    {
      GtkWidget      *menu_item = l->data;
      CarbonMenuItem *carbon_item;
      MenuAttributes attrs;
      const gchar *label = get_menu_label_text (menu_item, NULL);

#if ! GTK_CHECK_VERSION (3, 10, 0)
      if (GTK_IS_TEAROFF_MENU_ITEM (menu_item))
        continue;
#endif
      if (toplevel && (g_object_get_data (G_OBJECT (menu_item),
                                          "gtk-empty-menu-item")
                       || GTK_IS_SEPARATOR_MENU_ITEM (menu_item)))
        continue;
      carbon_item = carbon_menu_item_get (menu_item);
      if (debug)
        g_printerr ("%s: carbon_item %d for menu_item %d (%s, %s)\n",
                    G_STRFUNC, carbon_item ? carbon_item->index : -1,
                    carbon_index, label,
                    g_type_name (G_TYPE_FROM_INSTANCE (menu_item)));

      if (carbon_item && carbon_item->index != carbon_index)
        {
          if (carbon_item->index == carbon_index - 1)
            {
              if (debug)
                g_printerr ("%s: %s incrementing index\n", G_STRFUNC, label);
              ++carbon_item->index;
            }
          else if (carbon_item->index == carbon_index + 1)
            {
              if (debug)
                g_printerr ("%s: %s decrementing index\n", G_STRFUNC, label);
              --carbon_item->index;
            }
          else
            {
              if (debug)
                g_printerr ("%s: %s -> not matching, deleting\n",
                            G_STRFUNC, label);
              DeleteMenuItem (carbon_item->menu, carbon_index);
              carbon_item = NULL;
            }
        }
      if (!carbon_item)
        carbon_item = carbon_menu_item_create (menu_item, carbon_menu,
                                               carbon_index, debug);
      if (!carbon_item) //Bad carbon item, give up
        continue;
      if (GTK_IS_CHECK_MENU_ITEM (menu_item))
        carbon_menu_item_update_active (carbon_item, menu_item);
      carbon_menu_item_update_accel_closure (carbon_item, menu_item);
      if (gtk_menu_item_get_submenu (GTK_MENU_ITEM (menu_item)))
        carbon_menu_item_update_submenu (carbon_item, menu_item, debug);
      else
        {
          carbon_index++;
          continue;
        }

      /*The rest only applies to submenus, not to items which should have
       * been fixed up in carbon_menu_item_create
       */
      err = GetMenuAttributes ( carbon_item->submenu, &attrs);
      carbon_menu_warn (err, "Failed to get menu attributes");
      if (!gtk_widget_get_visible (menu_item))
        {
          if ((attrs & kMenuAttrHidden) == 0)
            {
              if (debug)
                g_printerr ("Hiding menu %s\n", label);
              err = ChangeMenuAttributes (carbon_item->submenu,
                                          kMenuAttrHidden, 0);
              carbon_menu_warn_label (err, label, "Failed to set visible");
            }
        }
      else if ((attrs & kMenuAttrHidden) != 0)
        {
          if (debug)
            g_printerr ("Revealing menu %s\n", label);
          err = ChangeMenuAttributes (carbon_item->submenu, 0,
                                      kMenuAttrHidden);
          carbon_menu_warn_label (err, label, "Failed to set Hidden");
        }
      carbon_index++;
    }
  g_list_free (children);
}

static gulong emission_hook_id    = 0;
static gint   emission_hook_count = 0;

static gboolean
parent_set_emission_hook (GSignalInvocationHint *ihint, guint n_param_values,
                          const GValue *param_values, gpointer data)
{
  GtkWidget *instance = g_value_get_object (param_values);
  CarbonMenu *carbon_menu;
  GtkWidget *previous_parent  = NULL;
  GtkWidget *menu_shell = NULL;

  if (!GTK_IS_MENU_ITEM (instance))
    return TRUE;
  previous_parent = g_value_get_object (param_values + 1);
  if (GTK_IS_MENU_SHELL (previous_parent))
    {
      menu_shell = previous_parent;
    }
  else if (GTK_IS_MENU_SHELL (gtk_widget_get_parent (instance)))
    {
      menu_shell = gtk_widget_get_parent (instance);
    }
  if (!menu_shell)
    return TRUE;
  carbon_menu = carbon_menu_get (menu_shell);

  if (!carbon_menu)
    return TRUE;
#if DEBUG
  g_printerr ("%s: item %s (%s) %s %s (%p)\n", G_STRFUNC,
              get_menu_label_text (instance, NULL),
              g_type_name (G_TYPE_FROM_INSTANCE (instance)),
              previous_parent ? "removed from" : "added to",
              get_menu_label_text (menu_shell, NULL),
              menu_shell);
#endif
  sync_menu_shell (GTK_MENU_SHELL (menu_shell), carbon_menu->menu,
                   carbon_menu->toplevel, DEBUG_SIGNAL);
  return TRUE;
}

static void
parent_set_emission_hook_remove (GtkWidget *widget, gpointer data)
{
  CarbonMenu *carbon_menu = carbon_menu_get (widget);
  if (carbon_menu)
    {
      MenuID id = GetMenuID (carbon_menu->menu);
      ClearMenuBar ();
      DeleteMenu (id);
    }
  emission_hook_count--;
  if (emission_hook_count > 0)
    return;
  g_signal_remove_emission_hook (
    g_signal_lookup ("parent-set", GTK_TYPE_WIDGET), emission_hook_id);
  emission_hook_id = 0;
}

static gboolean
window_focus (GtkWindow *window, GdkEventFocus *event, CarbonMenu *menu)
{
  OSStatus err = SetRootMenu (menu->menu);
  if (err)
    {
      carbon_menu_warn (err, "Failed to transfer menu");
    }
  else if (DEBUG)
    {
      g_printerr ("%s: Switched Menu\n", G_STRFUNC);
    }
  return FALSE;
}


/*
 * public functions
 */

void
gtk_mac_menu_set_menu_bar (GtkMenuShell *menu_shell)
{
  CarbonMenu *current_menu;
  MenuRef     carbon_menubar;
  OSStatus    err;
  GtkWidget *parent = gtk_widget_get_toplevel (GTK_WIDGET (menu_shell));

  g_return_if_fail (GTK_IS_MENU_SHELL (menu_shell));
  if (carbon_menu_quark == 0)
    carbon_menu_quark = g_quark_from_static_string ("CarbonMenu");
  if (carbon_menu_item_quark == 0)
    carbon_menu_item_quark = g_quark_from_static_string ("CarbonMenuItem");
  current_menu = carbon_menu_get (GTK_WIDGET (menu_shell));
  if (current_menu)
    {
      err = SetRootMenu (current_menu->menu);
      carbon_menu_warn (err, "Failed to set root menu");
      return;
    }
  err = CreateNewMenu (++last_menu_id /*id*/, 0 /*options*/, &carbon_menubar);
  carbon_menu_err_return (err, "Failed to create menu");
  err = SetRootMenu (carbon_menubar);
  carbon_menu_err_return (err, "Failed to set root menu");
  setup_menu_event_handler ();
  if (emission_hook_id == 0)
    {
      emission_hook_id =
        g_signal_add_emission_hook (
          g_signal_lookup ("parent-set", GTK_TYPE_WIDGET), 0,
          parent_set_emission_hook, NULL, NULL);
    }
  emission_hook_count++;
  g_signal_connect (menu_shell, "destroy",
                    G_CALLBACK (parent_set_emission_hook_remove), NULL);

#if DEBUG_SET
  g_printerr ("%s: syncing menubar\n", G_STRFUNC);
#endif
  sync_menu_shell (menu_shell, carbon_menubar, TRUE, DEBUG_SET);
  if (parent)
    g_signal_connect (parent, "focus-in-event",
                      G_CALLBACK (window_focus),
                      carbon_menu_get (GTK_WIDGET (menu_shell)));

}

void
gtk_mac_menu_set_quit_menu_item (GtkMenuItem *menu_item)
{
  MenuRef       appmenu;
  MenuItemIndex index;
  OSStatus err;

  g_return_if_fail (GTK_IS_MENU_ITEM (menu_item));
  setup_menu_event_handler ();
  err = GetIndMenuItemWithCommandID (NULL, kHICommandQuit, 1,
                                     &appmenu, &index);
  carbon_menu_err_return (err, "Failed to obtain Quit Menu");
  err = SetMenuItemCommandID (appmenu, index, 0);
  carbon_menu_err_return (err,
                          "Failed to set Quit menu command id");
  err = SetMenuItemProperty (appmenu, index, GTK_QUARTZ_MENU_CREATOR,
                             GTK_QUARTZ_ITEM_WIDGET, sizeof (menu_item),
                             &menu_item);
  carbon_menu_err_return (err,
                          "Failed to associate Quit menu item");
  gtk_widget_hide (GTK_WIDGET (menu_item));
  return;
}

void
gtk_mac_menu_connect_window_key_handler (GtkWindow *window)
{
  if (g_object_get_data (G_OBJECT (window), GTK_MAC_KEY_HANDLER))
    {
      g_warning ("Window %p is already connected", window);
      return;
    }

  g_signal_connect (window, "key-press-event",
                    G_CALLBACK (key_press_event), NULL);
  g_object_set_data (G_OBJECT (window), GTK_MAC_KEY_HANDLER,
                     GINT_TO_POINTER (1));
}

/* Most applications will want to have this enabled (which is the
 * defalt). For apps that need to deal with the events themselves, the
 * global handling can be disabled.
 */
void
gtk_mac_menu_set_global_key_handler_enabled (gboolean enabled)
{
  global_key_handler_enabled = enabled;
}

/* For testing use only. Returns TRUE if there is a GtkMenuItem assigned to
 * the Quit menu item.
 */
gboolean
_gtk_mac_menu_is_quit_menu_item_handled (void)
{
  MenuRef       appmenu;
  MenuItemIndex index;
  OSStatus err = GetIndMenuItemWithCommandID (NULL, kHICommandQuit, 1,
                 &appmenu, &index);
  carbon_menu_warn (err, "failed with");
  return (err == noErr);
}

/* Application Menu Functions
 *
 * The "application" menu (the one named the same as the application),
 * is special because there isn't a corresponding Gtk menu, but OSX
 * practice puts the About, Preferences, and Quit menu items in it, so
 * we need to provide a way for app developers to move those items
 * (and others, if they want) from their Gtk locations to the app
 * menu.
 */
struct _GtkMacMenuGroup
{
  GList *items;
};
/* app_menu_groups is a list of GtkMacMenuGroups, itself a list of
 * menu_items. They're provided to insert separators into the app
 * menu, grouping the items.
*/
static GList *app_menu_groups = NULL;

GtkMacMenuGroup *
gtk_mac_menu_add_app_menu_group (void)
{
  GtkMacMenuGroup *group = g_slice_new0 (GtkMacMenuGroup);

  app_menu_groups = g_list_append (app_menu_groups, group);
  return group;
}

/**
 * gtk_mac_menu_add_app_menu_item:
 * @group: Application menu group; helps place separators
 * @menu_item: Menu Item to add to app menu
 * @label: The label name for the menu item
 *
 * Move a menu item to the App menu (the one named with the
 * application's name). This has issues with multiple window menubars,
 * because the menu items are tied to a particular window's menu and
 * because there's only one App menu in Carbon regardless of how many
 * menubars there are. Don't use this for Quit, it has its own
 * function.
 */
void
gtk_mac_menu_add_app_menu_item (GtkMacMenuGroup *group, GtkMenuItem *menu_item,
                                const gchar *label)
{
  MenuRef  appmenu;
  GList   *list;
  gint     index = 0;
  CFStringRef cfstr;
  OSStatus err;

  g_return_if_fail (group != NULL);
  g_return_if_fail (GTK_IS_MENU_ITEM (menu_item));
  setup_menu_event_handler ();
  err = GetIndMenuItemWithCommandID (NULL, kHICommandHide, 1,
                                     &appmenu, NULL);
  carbon_menu_err_return (err, "retrieving app menu failed");
  for (list = app_menu_groups; list; list = g_list_next (list))
    {
      GtkMacMenuGroup *list_group = list->data;

      index += g_list_length (list_group->items);
      /*  adjust index for the separator between groups, but not
       *  before the first group
       */
      if (list_group->items && list->prev)
        index++;
      if (group != list_group)
        continue;

      /*  add a separator before adding the first item, but not
       *  for the first group
       */
      if (!group->items && list->prev)
        {
          err = InsertMenuItemTextWithCFString (appmenu, NULL, index,
                                                kMenuItemAttrSeparator, 0);
          carbon_menu_err_return (err, "Failed to add separator");
          index++;
        }
      if (!label)
        label = get_menu_label_text (GTK_WIDGET (menu_item), NULL);
      cfstr = CFStringCreateWithCString (NULL, label,
                                         kCFStringEncodingUTF8);
//Add a new menu item and associate it with the GtkMenuItem.
      err = InsertMenuItemTextWithCFString (appmenu, cfstr, index, 0, 0);
      carbon_menu_err_return (err, "Failed to add menu item");
      err = SetMenuItemProperty (appmenu, index + 1,
                                 GTK_QUARTZ_MENU_CREATOR,
                                 GTK_QUARTZ_ITEM_WIDGET,
                                 sizeof (menu_item), &menu_item);
      CFRelease (cfstr);
      carbon_menu_err_return (err, "Failed to associate Gtk Widget");
      gtk_widget_hide (GTK_WIDGET (menu_item));
//Finally add the item to the group; this is really just for tracking the count.
      group->items = g_list_append (group->items, menu_item);
//Bail out: The rest of the menu doesn't matter.
      return;

    }
  if (!list)
    g_warning ("%s: app menu group %p does not exist", G_STRFUNC, group);
}
/**
 * gtk_mac_menu_sync:
 * @menu_shell: The menubar to sync to the main menu.
 * Syncronize changes in the GtkMenuBar to an already-created Mac
 * MenuBar. You must have already run gtk_mac_menu_set_menu_bar on the
 * GtkMenuBar to be synced.
 */
void
gtk_mac_menu_sync (GtkMenuShell *menu_shell)
{
  CarbonMenu *carbon_menu = carbon_menu_get (GTK_WIDGET (menu_shell));
  g_return_if_fail (carbon_menu != NULL);
#if DEBUG_SYNC
  g_printerr ("%s: syncing menubar\n", G_STRFUNC);
#endif
  sync_menu_shell (menu_shell, carbon_menu->menu,
                   carbon_menu->toplevel, DEBUG_SYNC);
}

#endif /* __x86_64__ */
