/*
 * Google Invisibility Tracker Plugin
 *  Copyright (C) 2010, Federico Zanco <federico.zanco ( at ) gmail.com>
 * 
 *
 * License:
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02111-1301, USA.
 *
 */
 
#define PLUGIN_ID			"google-invisibility-tracker"
#define PLUGIN_NAME			"Google Invisibility Tracker Plugin"
#define PLUGIN_VERSION		"0.2.2"
#define PLUGIN_STATIC_NAME	"google-invisibility-tracker"
#define PLUGIN_SUMMARY		"Alerts you when a buddy goes invisible"
#define PLUGIN_DESCRIPTION	"Alerts you when a buddy goes invisible"
#define PLUGIN_AUTHOR		"Federico Zanco <federico.zanco ( at ) gmail.com>"
#define PLUGIN_WEBSITE		"http://www.siorarina.net/google-invisibility-tracker/"

#define GMAIL_DOMAIN		"gmail.com"

#define GMAIL_RESOURCE		"gmail"
#define GTALK_RESOURCE		"TalkGadget"
#define IGOOGLE_RESOURCE	"iGoogle"

#define PREF_TIMEOUT								PREF_PREFIX "/timeout"
#define	PREF_TIMEOUT_DEFAULT						5
#define PREF_PREFIX									"/plugins/core/" PLUGIN_ID
#define PREF_ALERT_POPUP							PREF_PREFIX "/alert_popup"
#define PREF_ALERT_CONVERSATIONS					PREF_PREFIX "/alert_conversations"
#define PREF_ALERT_LOG								PREF_PREFIX "/alert_log"

#define TIMEOUT_MIN									1
#define TIMEOUT_MAX									3600

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* config.h may define PURPLE_PLUGINS; protect the definition here so that we
 * don't get complaints about redefinition when it's not necessary. */
#ifndef PURPLE_PLUGINS
# define PURPLE_PLUGINS
#endif

#ifdef GLIB_H
# include <glib.h>
#endif

/* This will prevent compiler errors in some instances and is better explained 
 * in the how-to documents on the wiki */
#ifndef G_GNUC_NULL_TERMINATED
# if __GNUC__ >= 4
#  define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
# else
#  define G_GNUC_NULL_TERMINATED
# endif
#endif

#include <blist.h>
#include <notify.h>
#include <debug.h>
#include <plugin.h>
#include <version.h>
#include <prefs.h>
#include <string.h>


static PurplePlugin *this_plugin = NULL;
static guint timer = 0;


static gboolean
is_google_account(PurpleAccount *account)
{
/*	// this shoud be the right way but JabberStream...
	return ((JabberStream *) 
				((PurpleConnection *) 
					(purple_account_get_connection(account)))
						->proto_data)
							->googletalk;	*/
	
	return g_strcmp0(purple_account_get_protocol_id(account), "prpl-jabber") == 0
		&& g_strstr_len(purple_account_get_username(account), -1, GMAIL_DOMAIN);
}


static char *
get_resource(const char *full_jid)
{
	gchar **name = NULL;
	
	name = g_strsplit(full_jid, "/", 2);
	
	g_free(name[0]);
	
	return name[1];
}


static void
set_timeout(GSourceFunc callback, gpointer data)
{
	if (!timer) 
	{
		purple_debug_info(PLUGIN_STATIC_NAME, "set_timeout\n");
		timer = purple_timeout_add_seconds(
					purple_prefs_get_int(PREF_TIMEOUT),
					callback,
					data);
	}
}


static void
unset_timeout()
{
	if (timer)
	{
		purple_debug_info(PLUGIN_STATIC_NAME, "unset_timeout\n");
		purple_timeout_remove(timer);
		timer = 0;
	}
}


static gboolean
recheck_status_cb(gpointer buddy)
{
	char *m = NULL;
	GList *conv = NULL;
	time_t t;
	
	purple_debug_info(PLUGIN_STATIC_NAME, "recheck_status_cb\n");
	
	unset_timeout();
	
	if (purple_status_type_get_primitive(
			purple_status_get_type(
				purple_presence_get_active_status(
					purple_buddy_get_presence(buddy)))) == PURPLE_STATUS_OFFLINE)
	{
		// track in debug window
		purple_debug_info(PLUGIN_STATIC_NAME,
			"caught! %s has just gone invisible!\n",
			purple_buddy_get_alias(buddy));
			
		m = g_strdup_printf(
			"Your buddy %s is now Invisible!",
			purple_buddy_get_alias(buddy));
		
		t = time(NULL);	
		
		// alert in conversations
		if (purple_prefs_get_bool(PREF_ALERT_CONVERSATIONS))
		{
			for (conv = purple_get_conversations(); conv; conv = g_list_next(conv))
				purple_conversation_write(
					(PurpleConversation *) conv->data,
					NULL,
					m,
					PURPLE_MESSAGE_NOTIFY | PURPLE_MESSAGE_SYSTEM,
					t);
		}
		
		// write in status log
		if (purple_prefs_get_bool(PREF_ALERT_LOG))
		{
			purple_log_write(
				purple_account_get_log(purple_buddy_get_account(buddy), TRUE),
				PURPLE_MESSAGE_SYSTEM,
				purple_account_get_username(purple_buddy_get_account(buddy)),
				t,
				m);
		}
		
		// alert with a popup
		if (purple_prefs_get_bool(PREF_ALERT_POPUP))
		{
			g_free(m);
		
			m =	g_strdup_printf(
				"%s\nYour buddy %s (%s) is now Invisible!\n\nAccount: %s (%s)",
				asctime(localtime(&t)),
				purple_buddy_get_alias(buddy),
				purple_buddy_get_name(buddy),
				purple_account_get_alias(purple_buddy_get_account(buddy)),
				purple_account_get_username(purple_buddy_get_account(buddy)));
			
			purple_notify_info(
				this_plugin,
				PLUGIN_NAME,
			"HEY!",
			m);
		}
			
		g_free(m);
	}
	
	return FALSE;
}


static gboolean
jabber_presence_received_cb(PurpleConnection *gc, const char *type, const char *from, xmlnode *presence, gpointer data)
{
	PurpleBuddy *buddy = NULL;
	PurpleAccount *account = NULL;

	purple_debug_info(PLUGIN_STATIC_NAME, "jabber_presence_received_cb\n");	
	purple_debug_info(PLUGIN_STATIC_NAME, "presence TYPE: %s\n", type);

	// we're looking only for the unavailable presences
	if (g_strcmp0(type, "unavailable"))
		return FALSE;

	account = purple_connection_get_account(gc);
	
	// only google accounts...
	if (!is_google_account(account))
		return FALSE;
		
	buddy =  purple_find_buddy(account, from);
	
	// this happens when you have a contact in server but not in libpurple blist.
	// I.e. when you remove a buddy from pidgin, the contact will not be removed
	// on server
	if (!buddy)
		return FALSE;
	
	purple_debug_info(PLUGIN_STATIC_NAME, "STATUS ID: %s\n", 
		purple_status_get_id(
			purple_presence_get_active_status(
				purple_buddy_get_presence(buddy))));

	// we have to analyze only buddies that goes offline
	// (from any other status type obviously)
	if (purple_status_type_get_primitive(
			purple_status_get_type(
				purple_presence_get_active_status(
					purple_buddy_get_presence(buddy)))) == PURPLE_STATUS_OFFLINE)
		return FALSE;

	// these are the only clients that send vcard when going unavailable	
	if (g_str_has_prefix(get_resource(from), GTALK_RESOURCE) ||
		g_str_has_prefix(get_resource(from), IGOOGLE_RESOURCE) ||
		g_str_has_prefix(get_resource(from), GMAIL_RESOURCE))
	{
		// here's the trick! If the presence stanza has not a vcard node then
		// we're sure the buddy has gone invisible but we wait a few seconds
		// to be sure the buddy has really gone offline to avoid a bit of
		// false positives
		if (!xmlnode_get_child_with_namespace(presence, "x", "vcard-temp:x:update"))
			set_timeout(recheck_status_cb, (gpointer ) buddy);
	}
	
	return FALSE;
}


static gboolean
plugin_load (PurplePlugin *plugin)
{
	purple_debug_info(PLUGIN_STATIC_NAME, "plugin_load\n");
	
	this_plugin = plugin;
	
	// add plugin pref
	purple_prefs_add_none(PREF_PREFIX);
	
	// check timeout pref (and set it)
	if (!purple_prefs_exists(PREF_TIMEOUT))
		purple_prefs_add_int(PREF_TIMEOUT, PREF_TIMEOUT_DEFAULT);
	
	// check alert popup pref
	if (!purple_prefs_exists(PREF_ALERT_POPUP))
		purple_prefs_add_bool(PREF_ALERT_POPUP, FALSE);
		
	// check alert conversations pref
	if (!purple_prefs_exists(PREF_ALERT_CONVERSATIONS))
		purple_prefs_add_bool(PREF_ALERT_CONVERSATIONS, FALSE);	
		
	// check alert log pref
	if (!purple_prefs_exists(PREF_ALERT_LOG))
		purple_prefs_add_bool(PREF_ALERT_LOG, FALSE);
			
	// jabber receiving presence
	purple_signal_connect(
		purple_find_prpl("prpl-jabber"),
		"jabber-receiving-presence",
		this_plugin,
		PURPLE_CALLBACK(jabber_presence_received_cb),
		NULL);
	
	return TRUE;
}


static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *pref;
	
	frame = purple_plugin_pref_frame_new();
	
	// timeout
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_TIMEOUT,
				"Time out for a single scan (in seconds)");	
	purple_plugin_pref_set_bounds(pref, TIMEOUT_MIN, TIMEOUT_MAX);
	purple_plugin_pref_frame_add(frame, pref);
	
	// popup message
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_ALERT_POPUP,
				"Show a popup message when a buddy goes invisible");
	purple_plugin_pref_frame_add(frame, pref);
	
	// show a message in every conversation
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_ALERT_CONVERSATIONS,
				"Show a message in every conversation when a buddy goes invisible");
	purple_plugin_pref_frame_add(frame, pref);
	
	// write the event in system log
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_ALERT_LOG,
				"Keep track of invisibile status changes in system log");
	purple_plugin_pref_frame_add(frame, pref);
	
	return frame;
}


static PurplePluginUiInfo prefs_info = {
	get_plugin_pref_frame,
	0,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};


/* For specific notes on the meanings of each of these members, consult the C Plugin Howto
 * on the website. */
static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,
	PLUGIN_ID,
	PLUGIN_NAME,
	PLUGIN_VERSION,
	PLUGIN_SUMMARY,
	PLUGIN_DESCRIPTION,
	PLUGIN_AUTHOR,
	PLUGIN_WEBSITE,
	plugin_load,
	NULL,
	NULL,
	NULL,
	NULL,
	&prefs_info,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};


static void
init_plugin (PurplePlugin * plugin)
{
}


PURPLE_INIT_PLUGIN (PLUGIN_ID, init_plugin, info)
