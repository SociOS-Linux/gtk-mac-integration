#include <gtk/gtk.h>

#include <Carbon/Carbon.h>

#include "sync-menu.h"

#define CARBON_MENU 1

#ifndef CARBON_MENU
#import <AppKit/AppKit.h>
#endif

/* TODO
 *
 * - Setup shortcuts, possibly transforming ctrl->cmd
 * - Sync menus
 * - Create on demand? (can this be done with gtk+? ie fill in menu items when the menu is opened)
 * - Figure out what to do per app/window...
 * - Toggle/radio items
 *
 */

#define GTK_QUARTZ_MENU_CREATOR 'GTKC'
#define GTK_QUARTZ_ITEM_WIDGET  'GWID'


static void   sync_menu_shell (GtkMenuShell *menu_shell,
			       MenuRef       carbon_menu);


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
get_menu_label_text (GtkWidget  *menu_item,
		     GtkWidget **label)
{
  *label = find_menu_label (menu_item);
  if (!*label)
    return NULL;

  return gtk_label_get_text (GTK_LABEL (*label));
}

static gboolean
accel_find_func (GtkAccelKey *key,
		 GClosure    *closure,
		 gpointer     data)
{
  return (GClosure *) data == closure;
}


/*
 * CarbonMenu functions
 */

typedef struct
{
  MenuRef menu;
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
  g_slice_free (CarbonMenu, menu);
}

static CarbonMenu *
carbon_menu_get (GtkWidget *widget)
{
  return g_object_get_qdata (G_OBJECT (widget), carbon_menu_quark);
}

static void
carbon_menu_connect (GtkWidget *menu,
		     MenuRef    menuRef)
{
  CarbonMenu *carbon_menu = carbon_menu_get (menu);

  if (!carbon_menu)
    {
      carbon_menu = carbon_menu_new ();

      g_object_set_qdata_full (G_OBJECT (menu), carbon_menu_quark,
			       carbon_menu,
			       (GDestroyNotify) carbon_menu_free);
    }

  carbon_menu->menu = menuRef;
}


/*
 * CarbonMenuItem functions
 */

typedef struct
{
  MenuRef       menu;
  MenuItemIndex index;
  MenuRef       submenu;
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
  g_slice_free (CarbonMenuItem, menu_item);
}

static CarbonMenuItem *
carbon_menu_item_get (GtkWidget *widget)
{
  return g_object_get_qdata (G_OBJECT (widget), carbon_menu_item_quark);
}

static void
carbon_menu_item_update_state (CarbonMenuItem *carbon_item,
			       GtkWidget      *widget)
{
  gboolean sensitive;
  gboolean visible;
  UInt32   set_attrs = 0;
  UInt32   clear_attrs = 0;

  g_object_get (widget,
                "sensitive", &sensitive,
                "visible",   &visible,
                NULL);

  if (!sensitive)
    set_attrs |= kMenuItemAttrDisabled;
  else
    clear_attrs |= kMenuItemAttrDisabled;

  if (!visible)
    set_attrs |= kMenuItemAttrHidden;
  else
    clear_attrs |= kMenuItemAttrHidden;

  ChangeMenuItemAttributes (carbon_item->menu, carbon_item->index,
                            set_attrs, clear_attrs);
}

static void
carbon_menu_item_update_active (CarbonMenuItem *carbon_item,
				GtkWidget      *widget)
{
  gboolean active;

  g_object_get (widget,
                "active", &active,
                NULL);

  CheckMenuItem (carbon_item->menu, carbon_item->index,
		 active);
}

static void
carbon_menu_item_update_submenu (CarbonMenuItem *carbon_item,
				 GtkWidget      *widget)
{
  GtkWidget *submenu;

  submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (widget));

  if (submenu)
    {
      GtkWidget   *label = NULL;
      const gchar *label_text;
      CFStringRef  cfstr = NULL;

      label_text = get_menu_label_text (widget, &label);
      if (label_text)
        cfstr = CFStringCreateWithCString (NULL, label_text,
					   kCFStringEncodingUTF8);

      CreateNewMenu (0, 0, &carbon_item->submenu);
      SetMenuTitleWithCFString (carbon_item->submenu, cfstr);
      SetMenuItemHierarchicalMenu (carbon_item->menu, carbon_item->index,
				   carbon_item->submenu);

      sync_menu_shell (GTK_MENU_SHELL (submenu), carbon_item->submenu);

      if (cfstr)
	CFRelease (cfstr);
    }
  else
    {
      SetMenuItemHierarchicalMenu (carbon_item->menu, carbon_item->index,
				   NULL);
      carbon_item->submenu = NULL;
    }
}

static void
carbon_menu_item_update_label (CarbonMenuItem *carbon_item,
			       GtkWidget      *widget)
{
  GtkWidget   *label;
  const gchar *label_text;
  CFStringRef  cfstr = NULL;

  label_text = get_menu_label_text (widget, &label);
  if (label_text)
    cfstr = CFStringCreateWithCString (NULL, label_text,
				       kCFStringEncodingUTF8);

  SetMenuItemTextWithCFString (carbon_item->menu, carbon_item->index,
			       cfstr);

  if (cfstr)
    CFRelease (cfstr);
}

static guint
accel_key_to_command_key (guint in, gboolean *is_glyph )
{
  const struct { guint keyval; guint commandkey; } keys[] = {
    { GDK_F1, kMenuF1Glyph },
    { GDK_F2, kMenuF2Glyph },
    { GDK_F3, kMenuF3Glyph },
    { GDK_F4, kMenuF4Glyph },
    { GDK_F5, kMenuF5Glyph },
    { GDK_F6, kMenuF6Glyph },
    { GDK_F7, kMenuF7Glyph },
    { GDK_F8, kMenuF8Glyph },
    { GDK_F9, kMenuF9Glyph },
    { GDK_F10, kMenuF10Glyph },
    { GDK_F11, kMenuF11Glyph },
    { GDK_F12, kMenuF12Glyph }
  };
  gint i;

  for (i = 0; i < G_N_ELEMENTS (keys); i++)
    if (keys[i].keyval == in)
      {
        *is_glyph = TRUE;
        return keys[i].commandkey;
      }

  *is_glyph = FALSE;
  return g_ascii_toupper (in);
}

static void
carbon_menu_item_update_accel_closure (CarbonMenuItem *carbon_item,
				       GtkWidget      *widget)
{
  GtkWidget *label;

  get_menu_label_text (widget, &label);

  if (GTK_IS_ACCEL_LABEL (label) &&
      GTK_ACCEL_LABEL (label)->accel_closure)
    {
      GtkAccelKey *key;

      key = gtk_accel_group_find (GTK_ACCEL_LABEL (label)->accel_group,
				  accel_find_func,
				  GTK_ACCEL_LABEL (label)->accel_closure);

      if (key            &&
	  key->accel_key &&
	  key->accel_flags & GTK_ACCEL_VISIBLE)
	{
	  UInt8    modifiers = 0;
          gboolean is_glyph;
          guint    val;

          val = accel_key_to_command_key (key->accel_key, &is_glyph);
          if (!is_glyph)
            SetMenuItemCommandKey (carbon_item->menu, carbon_item->index,
                                   false, val);
	  else
            SetMenuItemKeyGlyph (carbon_item->menu, carbon_item->index, val);

	  if (key->accel_mods)
	    {
	      if (key->accel_mods & GDK_SHIFT_MASK)
		modifiers |= kMenuShiftModifier;

	      if (key->accel_mods & GDK_MOD1_MASK)
		modifiers |= kMenuOptionModifier;
	    }
	  else
	    {
	      modifiers |= kMenuNoCommandModifier;
	    }

	  SetMenuItemModifiers (carbon_item->menu, carbon_item->index,
				modifiers);
	}
    }
}

static void
carbon_menu_item_notify (GObject        *object,
			 GParamSpec     *pspec,
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
      carbon_menu_item_update_submenu (carbon_item, GTK_WIDGET (object));
    }
}

static void
carbon_menu_item_notify_label (GObject    *object,
			       GParamSpec *pspec,
			       gpointer    data)
{
  CarbonMenuItem *carbon_item = carbon_menu_item_get (GTK_WIDGET (object));

  if (!strcmp (pspec->name, "label"))
    {
      carbon_menu_item_update_label (carbon_item,
				     GTK_WIDGET (object));
    }
  else if (!strcmp (pspec->name, "accel-closure"))
    {
      carbon_menu_item_update_accel_closure (carbon_item,
					     GTK_WIDGET (object));
    }
}

static CarbonMenuItem *
carbon_menu_item_connect (GtkWidget     *menu_item,
			  GtkWidget     *label,
			  MenuRef        menu,
			  MenuItemIndex  index,
			  MenuRef        submenu)
{
  CarbonMenuItem *carbon_item = carbon_menu_item_get (menu_item);

  if (!carbon_item)
    {
      carbon_item = carbon_menu_item_new ();

      g_object_set_qdata_full (G_OBJECT (menu_item), carbon_menu_item_quark,
			       carbon_item,
			       (GDestroyNotify) carbon_menu_item_free);

      g_signal_connect (menu_item, "notify",
                        G_CALLBACK (carbon_menu_item_notify),
                        carbon_item);

      if (label)
	g_signal_connect_swapped (label, "notify::label",
				  G_CALLBACK (carbon_menu_item_notify_label),
				  menu_item);
    }

  carbon_item->menu    = menu;
  carbon_item->index   = index;
  carbon_item->submenu = submenu;

  return carbon_item;
}


/*
 * carbon event handler
 */

static OSStatus
menu_event_handler_func (EventHandlerCallRef  event_handler_call_ref, 
			 EventRef             event_ref, 
			 void                *data)
{
  UInt32 event_class = GetEventClass (event_ref);
  UInt32 event_kind = GetEventKind (event_ref);
  MenuRef menu_ref;

  switch (event_class) 
    {
    case kEventClassCommand:
      /* This is called when activating (is that the right GTK+ term?)
       * a menu item.
       */
      if (event_kind == kEventCommandProcess)
	{
	  HICommand command;
	  OSStatus  err;

	  //g_print ("Menu: kEventClassCommand/kEventCommandProcess\n");

	  err = GetEventParameter (event_ref, kEventParamDirectObject, 
				   typeHICommand, 0, 
				   sizeof (command), 0, &command);

	  if (err == noErr)
	    {
	      GtkWidget *widget = NULL;
              
	      if (command.commandID == kHICommandQuit)
		{
		  gtk_main_quit (); /* Just testing... */
		  return noErr;
		}
	      
	      /* Get any GtkWidget associated with the item. */
	      err = GetMenuItemProperty (command.menu.menuRef, 
					 command.menu.menuItemIndex, 
					 GTK_QUARTZ_MENU_CREATOR, 
					 GTK_QUARTZ_ITEM_WIDGET,
					 sizeof (widget), 0, &widget);
	      if (err == noErr && widget)
		{
		  gtk_menu_item_activate (GTK_MENU_ITEM (widget));
		  return noErr;
		}
	    }
	}
      break;

    case kEventClassMenu: 
      GetEventParameter (event_ref, 
			 kEventParamDirectObject, 
			 typeMenuRef, 
			 NULL, 
			 sizeof (menu_ref), 
			 NULL, 
			 &menu_ref);

      switch (event_kind)
	{
	case kEventMenuTargetItem:
	  /* This is called when an item is selected (what is the
	   * GTK+ term? prelight?)
	   */
	  //g_print ("kEventClassMenu/kEventMenuTargetItem\n");
	  break;

	case kEventMenuOpening:
	  /* Is it possible to dynamically build the menu here? We
	   * can at least set visibility/sensitivity. 
	   */
	  //g_print ("kEventClassMenu/kEventMenuOpening\n");
	  break;
	    
	case kEventMenuClosed:
	  //g_print ("kEventClassMenu/kEventMenuClosed\n");
	  break;

	default:
	  break;
	}

      break;
	
    default:
      break;
    }

  return CallNextEventHandler (event_handler_call_ref, event_ref);
}

static void
setup_menu_event_handler (void)
{
  EventHandlerUPP menu_event_handler_upp;
  EventHandlerRef menu_event_handler_ref;
  const EventTypeSpec menu_events[] = {
    { kEventClassCommand, kEventCommandProcess },
    { kEventClassMenu, kEventMenuTargetItem },
    { kEventClassMenu, kEventMenuOpening },
    { kEventClassMenu, kEventMenuClosed }
  };

  /* FIXME: We might have to install one per window? */

  menu_event_handler_upp = NewEventHandlerUPP (menu_event_handler_func);
  InstallEventHandler (GetApplicationEventTarget (), menu_event_handler_upp,
		       GetEventTypeCount (menu_events), menu_events, 0,
		       &menu_event_handler_ref);
  
#if 0
  /* FIXME: Remove the handler with: */
  RemoveEventHandler(menu_event_handler_ref);
  DisposeEventHandlerUPP(menu_event_handler_upp);
#endif
}

#ifdef CARBON_MENU

static void
sync_menu_shell (GtkMenuShell *menu_shell,
                 MenuRef       carbon_menu)
{
  GList         *children;
  GList         *l;
  MenuItemIndex  carbon_index = 1;

  carbon_menu_connect (GTK_WIDGET (menu_shell), carbon_menu);

  children = gtk_container_get_children (GTK_CONTAINER (menu_shell));

  for (l = children; l; l = l->next)
    {
      GtkWidget      *menu_item = l->data;
      CarbonMenuItem *carbon_item;

      if (GTK_IS_TEAROFF_MENU_ITEM (menu_item))
	continue;

      carbon_item = carbon_menu_item_get (menu_item);

      if (carbon_item && carbon_item->index != carbon_index)
	{
	  DeleteMenuItem (carbon_item->menu,
			  carbon_item->index);
	  carbon_item = NULL;
	}

      if (!carbon_item)
	{
	  GtkWidget          *label = NULL;
	  const gchar        *label_text;
	  CFStringRef         cfstr = NULL;
	  MenuItemAttributes  attributes = 0;
	  MenuRef             carbon_submenu = NULL;

	  label_text = get_menu_label_text (menu_item, &label);
	  if (label_text)
	    cfstr = CFStringCreateWithCString (NULL, label_text,
					       kCFStringEncodingUTF8);

	  if (GTK_IS_SEPARATOR_MENU_ITEM (menu_item))
	    attributes |= kMenuItemAttrSeparator;

	  if (!GTK_WIDGET_IS_SENSITIVE (menu_item))
	    attributes |= kMenuItemAttrDisabled;

	  if (!GTK_WIDGET_VISIBLE (menu_item))
	    attributes |= kMenuItemAttrHidden;

	  InsertMenuItemTextWithCFString (carbon_menu, cfstr,
					  carbon_index + 1,
					  attributes, 0);
	  SetMenuItemProperty (carbon_menu, carbon_index,
			       GTK_QUARTZ_MENU_CREATOR,
			       GTK_QUARTZ_ITEM_WIDGET,
			       sizeof (menu_item), &menu_item);

	  if (cfstr)
	    CFRelease (cfstr);

	  carbon_item = carbon_menu_item_connect (menu_item, label,
						  carbon_menu,
						  carbon_index,
						  carbon_submenu);

	  if (GTK_IS_CHECK_MENU_ITEM (menu_item))
	    carbon_menu_item_update_active (carbon_item, menu_item);

	  carbon_menu_item_update_accel_closure (carbon_item, menu_item);

	  if (gtk_menu_item_get_submenu (GTK_MENU_ITEM (menu_item)))
	    carbon_menu_item_update_submenu (carbon_item, menu_item);
	}

      carbon_index++;
    }

  g_list_free (children);
}

void
sync_menu_takeover_menu (GtkMenuShell *menu_shell)
{
  MenuRef carbon_menubar;

  g_return_if_fail (GTK_IS_MENU_SHELL (menu_shell));

  if (carbon_menu_quark == 0)
    carbon_menu_quark = g_quark_from_static_string ("CarbonMenu");

  if (carbon_menu_item_quark == 0)
    carbon_menu_item_quark = g_quark_from_static_string ("CarbonMenuItem");

  CreateNewMenu (0 /*id*/, 0 /*options*/, &carbon_menubar);
  SetRootMenu (carbon_menubar);

  setup_menu_event_handler ();
  
  sync_menu_shell (menu_shell, carbon_menubar);
}

#else /* !CARBON_MENU */

static void
nsmenu_from_menushell (GtkMenuShell *menu_shell,
		       NSMenu       *nsMenu)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  GList             *children;
  GList             *list;

  children = gtk_container_get_children (GTK_CONTAINER (menu_shell));

  for (list = children; list; list = list->next)
    {
      GtkMenuItem *menu_item = list->data;
      GtkLabel    *label;
      NSMenuItem  *menuItem;
      const gchar *menu_label;
      NSString    *menuLabel;
      GtkWidget   *submenu;

      if (GTK_IS_TEAROFF_MENU_ITEM (menu_item))
	continue;

      menu_label = get_menu_label_text (menu_item, &label);
      if (menu_label)
	menuLabel = [NSString stringWithUTF8String:menu_label];
      else
	menuLabel = NULL;

      if (GTK_IS_SEPARATOR_MENU_ITEM (menu_item))
	menuItem = [NSMenuItem separatorItem];
      else
	menuItem = [[NSMenuItem allocWithZone:[NSMenu menuZone]] init];

      if (menuLabel)
	[menuItem setTitle:menuLabel];

      if (!GTK_WIDGET_IS_SENSITIVE (menu_item))
        [menuItem setEnabled:NO];

#if 0
      /* FIXME ??? */
      if (!GTK_WIDGET_VISIBLE (menu_item))
	/* ??? */;
#endif

      [nsMenu addItem:menuItem];

      submenu = gtk_menu_item_get_submenu (menu_item);
      if (submenu)
        {
	  NSMenu *subMenu = [[NSMenu allocWithZone:[NSMenu menuZone]] initWithTitle:menuLabel];

	  [menuItem setSubmenu:subMenu];

	  nsmenu_from_menushell (GTK_MENU_SHELL (submenu), subMenu);
        }

#if 0
      g_signal_connect (menu_item, "notify",
                        G_CALLBACK (menu_item_notify_cb),
                        NULL);
#endif
    }

  g_list_free (children);

  [pool release];
}

void
sync_menu_takeover_menu (GtkMenuShell *menu_shell)
{
  NSMenu *mainMenu;

  g_return_if_fail (GTK_IS_MENU_SHELL (menu_shell));

  mainMenu = [[NSMenu alloc] initWithTitle:@""];

  // [mainMenu initWithTitle:[NSString stringWithUTF8String:"Foo"]];
  // [mainMenu setAutoenablesItems:NO];

  // mainMenu = [NSApp mainMenu];

  nsmenu_from_menushell (menu_shell, mainMenu);

  // [NSApp setMainMenu:mainMenu];
}

#endif /* CARBON_MENU */
