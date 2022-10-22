// gtklock-playerctl-module
// Copyright (c) 2022 Jovan Lanik

// Playerctl module

#include <playerctl.h>
#include <libsoup/soup.h>

#include "gtklock-module.h"

#define MODULE_DATA(x) (x->module_data[self_id])
#define PLAYERCTL(x) ((struct playerctl *)MODULE_DATA(x))

struct playerctl {
	GtkWidget *revealer;
	GtkWidget *album_art;
	GtkWidget *label_box;
	GtkWidget *previous_button;
	GtkWidget *play_pause_button;
	GtkWidget *next_button;
};

const gchar module_name[] = "playerctl";
const guint module_major_version = 2;
const guint module_minor_version = 0;

static int self_id;

PlayerctlPlayerManager *player_manager = NULL;
PlayerctlPlayer *current_player = NULL;
SoupSession *soup_session = NULL;

static int art_size = 64;
static gchar *position = "top-center";
static gboolean show_hidden = FALSE;

GOptionEntry module_entries[] = {
	{ "art-size", 0, 0, G_OPTION_ARG_INT, &art_size, "Album art size in pixels", NULL },
	{ "position", 0, 0, G_OPTION_ARG_STRING, &position, "Position of media player controls", NULL },
	{ "show-hidden", 0, 0, G_OPTION_ARG_NONE, &show_hidden, "Show media controls when hidden", NULL },
	{ NULL },
};

static void setup_album_art_placeholder(struct Window *ctx) {
	gtk_image_set_from_icon_name(GTK_IMAGE(PLAYERCTL(ctx)->album_art) , "audio-x-generic-symbolic", GTK_ICON_SIZE_BUTTON);
	return;
}

static void request_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	struct Window *ctx = user_data;
	GError *error = NULL;

	GInputStream *stream = soup_request_send_finish(SOUP_REQUEST(source_object), res, &error);
	if(error != NULL) {
		g_warning("Failed loading album art: %s", error->message);
		g_error_free(error);
		error = NULL;

		setup_album_art_placeholder(ctx);
		return;
	}

	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, -1, art_size, TRUE, NULL, &error);
	if(error != NULL) {
		g_warning("Failed loading album art: %s", error->message);
		g_error_free(error);

		setup_album_art_placeholder(ctx);
		return;
	}

	gtk_image_set_from_pixbuf(GTK_IMAGE(PLAYERCTL(ctx)->album_art), pixbuf);
}

static void setup_album_art(struct Window *ctx) {
	GError *error = NULL;
	gchar *uri = playerctl_player_print_metadata_prop(current_player, "mpris:artUrl", NULL);
	if(error != NULL) {
		g_warning("Failed loading album art: %s", error->message);
		g_error_free(error);
		error = NULL;

		setup_album_art_placeholder(ctx);
		return;
	}

	if(!uri || uri[0] == '\0') {
		setup_album_art_placeholder(ctx);
		return;
	}

	SoupRequest *request = soup_session_request(soup_session, uri, &error);
	if(error != NULL) {
		g_warning("Failed loading album art: %s", error->message);
		g_error_free(error);

		setup_album_art_placeholder(ctx);
		return;
	}
	soup_request_send_async(request, NULL, request_callback, ctx);
}

static void play_pause(GtkButton *self, gpointer user_data) {
	GError *error = NULL;
	playerctl_player_play_pause(current_player, &error);
	if(error != NULL) {
		g_warning("Failed play_pause: %s", error->message);
		g_error_free(error);
	}
}

static void next(GtkButton *self, gpointer user_data) {
	GError *error = NULL;
	playerctl_player_next(current_player, NULL);
	if(error != NULL) {
		g_warning("Failed go_next: %s", error->message);
		g_error_free(error);
	}
}

static void previous(GtkButton *self, gpointer user_data) {
	GError *error = NULL;
	playerctl_player_previous(current_player, NULL);
	if(error != NULL) {
		g_warning("Failed go_previous: %s", error->message);
		g_error_free(error);
	}
}

static void widget_destroy(GtkWidget *widget, gpointer data) {
	gtk_widget_destroy(widget);
}

static void setup_playback(struct Window *ctx, PlayerctlPlaybackStatus status) {
	const gchar *icon = status == PLAYERCTL_PLAYBACK_STATUS_PLAYING ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
	GtkWidget *image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(PLAYERCTL(ctx)->play_pause_button), image);
}

static void setup_metadata(struct Window *ctx) {
	if(!current_player) {
		gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(ctx)->revealer), FALSE);
		return;
	}

	PlayerctlPlaybackStatus status;
	g_object_get(current_player, "playback-status", &status, NULL);
	setup_playback(ctx, status);

	setup_album_art(ctx);
	gtk_container_foreach(GTK_CONTAINER(PLAYERCTL(ctx)->label_box), widget_destroy, NULL);

	gchar *title = playerctl_player_get_title(current_player, NULL);
	if(title && title[0] != '\0') {
		GtkWidget *title_label = gtk_label_new(NULL);
		gtk_widget_set_name(title_label, "title-label");
		gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(title_label), 1);
		gchar *title_bold = g_markup_printf_escaped("<b>%s</b>", title);
		gtk_label_set_markup(GTK_LABEL(title_label), title_bold);
		gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->label_box), title_label);
	}

	gchar *album = playerctl_player_get_album(current_player, NULL);
	if(album && album[0] != '\0') {
		GtkWidget *album_label = gtk_label_new(album);
		gtk_widget_set_name(album_label, "album-label");
		gtk_label_set_xalign(GTK_LABEL(album_label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(album_label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(album_label), 1);
		gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->label_box), album_label);
	}

	gchar *artist = playerctl_player_get_artist(current_player, NULL);
	if(artist && artist[0] != '\0') {
		GtkWidget *artist_label = gtk_label_new(artist);
		gtk_widget_set_name(artist_label, "artist-label");
		gtk_label_set_xalign(GTK_LABEL(artist_label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(artist_label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(artist_label), 1);
		gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->label_box), artist_label);
	}

	gboolean can_go_next, can_go_previous, can_pause;
	g_object_get(current_player,
		"can-go-next", &can_go_next,
		"can-go-previous", &can_go_previous,
		"can-pause", &can_pause,
		NULL
	);

	gtk_widget_set_sensitive(PLAYERCTL(ctx)->previous_button, can_go_previous);
	gtk_widget_set_sensitive(PLAYERCTL(ctx)->play_pause_button, can_pause);
	gtk_widget_set_sensitive(PLAYERCTL(ctx)->next_button, can_go_next);

	gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(ctx)->revealer), TRUE);
	gtk_widget_show_all(PLAYERCTL(ctx)->revealer);
}

static void setup_playerctl(struct Window *ctx) {
	if(MODULE_DATA(ctx) != NULL) return;
	MODULE_DATA(ctx) = g_malloc(sizeof(struct playerctl));

	PLAYERCTL(ctx)->revealer = gtk_revealer_new();
	g_object_set(PLAYERCTL(ctx)->revealer, "margin", 5, NULL);
	gtk_widget_set_name(PLAYERCTL(ctx)->revealer, "playerctl-revealer");
	gtk_revealer_set_transition_type(GTK_REVEALER(PLAYERCTL(ctx)->revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
	gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(ctx)->revealer), FALSE);

	if(
		g_strcmp0(position, "top-left") == 0 ||
		g_strcmp0(position, "bottom-left") == 0
	) gtk_widget_set_halign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_START);
	else if(
		g_strcmp0(position, "top-right") == 0 ||
		g_strcmp0(position, "bottom-right") == 0
	) gtk_widget_set_halign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_END);
	else gtk_widget_set_halign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_CENTER);

	if(
		g_strcmp0(position, "top-left") == 0 ||
		g_strcmp0(position, "top-right") == 0 ||
		g_strcmp0(position, "top-center") == 0
	) gtk_widget_set_valign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_START);
	else if(
		g_strcmp0(position, "bottom-left") == 0 ||
		g_strcmp0(position, "bottom-right") == 0 ||
		g_strcmp0(position, "bottom-center") == 0
	) gtk_widget_set_valign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_END);
	else if(g_strcmp0(position, "above-clock") != 0 && g_strcmp0(position, "under-clock") != 0) {
		g_warning("%s: Unknown position", module_name);
		gtk_widget_set_valign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_START);
		gtk_widget_set_halign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_CENTER);
	} 

	gboolean under = g_strcmp0(position, "under-clock") == 0;
	if(under || g_strcmp0(position, "above-clock") == 0) {
		GValue val = G_VALUE_INIT;
		g_value_init(&val, G_TYPE_INT);
		gtk_container_child_get_property(GTK_CONTAINER(ctx->window_box), ctx->clock_label, "position", &val);
		gint pos = g_value_get_int(&val);

		gtk_container_add(GTK_CONTAINER(ctx->window_box), PLAYERCTL(ctx)->revealer);
		gtk_box_reorder_child(GTK_BOX(ctx->window_box), PLAYERCTL(ctx)->revealer, pos + under);
	}
	else gtk_overlay_add_overlay(GTK_OVERLAY(ctx->overlay), PLAYERCTL(ctx)->revealer);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
	gtk_widget_set_name(box, "playerctl-box");
	gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->revealer), box);

	if(art_size) {
		PLAYERCTL(ctx)->album_art = gtk_image_new_from_icon_name("audio-x-generic-symbolic", GTK_ICON_SIZE_BUTTON);
		gtk_widget_set_halign(PLAYERCTL(ctx)->album_art, GTK_ALIGN_CENTER);
		gtk_widget_set_name(PLAYERCTL(ctx)->album_art, "album-art");
		gtk_widget_set_size_request(PLAYERCTL(ctx)->album_art, art_size, art_size);
		gtk_container_add(GTK_CONTAINER(box), PLAYERCTL(ctx)->album_art);
	}

	PLAYERCTL(ctx)->label_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(PLAYERCTL(ctx)->label_box, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(PLAYERCTL(ctx)->label_box, 180, -1);
	gtk_container_add(GTK_CONTAINER(box), PLAYERCTL(ctx)->label_box);

	GtkWidget *control_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_valign(control_box, GTK_ALIGN_CENTER);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(control_box), GTK_BUTTONBOX_EXPAND);
	gtk_container_add(GTK_CONTAINER(box), control_box);

	PLAYERCTL(ctx)->previous_button = gtk_button_new_from_icon_name("media-skip-backward-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(PLAYERCTL(ctx)->previous_button, "clicked", G_CALLBACK(previous), ctx);
	gtk_widget_set_name(PLAYERCTL(ctx)->previous_button, "previous-button");
	gtk_container_add(GTK_CONTAINER(control_box), PLAYERCTL(ctx)->previous_button);

	PLAYERCTL(ctx)->play_pause_button = gtk_button_new();
	g_signal_connect(PLAYERCTL(ctx)->play_pause_button, "clicked", G_CALLBACK(play_pause), ctx);
	gtk_widget_set_name(PLAYERCTL(ctx)->play_pause_button, "play-pause-button");
	gtk_container_add(GTK_CONTAINER(control_box), PLAYERCTL(ctx)->play_pause_button);

	PLAYERCTL(ctx)->next_button = gtk_button_new_from_icon_name("media-skip-forward-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(PLAYERCTL(ctx)->next_button, "clicked", G_CALLBACK(next), ctx);
	gtk_widget_set_name(PLAYERCTL(ctx)->next_button, "next-button");
	gtk_container_add(GTK_CONTAINER(control_box), PLAYERCTL(ctx)->next_button);

	setup_metadata(ctx);
}

void g_module_unload(GModule *m) {
	g_object_unref(player_manager);
	g_object_unref(soup_session);
}

static void name_appeared(PlayerctlPlayerManager *self, PlayerctlPlayerName *name, gpointer user_data) {
	if(current_player) return;
	
	current_player = playerctl_player_new_from_name(name, NULL);
	playerctl_player_manager_manage_player(player_manager, current_player);
	g_object_unref(current_player);
}

static void metadata(PlayerctlPlayer *player, GVariant *metadata, gpointer user_data) {
	struct GtkLock *gtklock = user_data;
	if(!gtklock->focused_window) return;

	if(MODULE_DATA(gtklock->focused_window)) setup_metadata(gtklock->focused_window);
	else setup_playerctl(gtklock->focused_window);
}

static void playback_status(PlayerctlPlayer *player, PlayerctlPlaybackStatus status, gpointer user_data) {
	struct GtkLock *gtklock = user_data;
	if(!gtklock->focused_window) return;

	if(MODULE_DATA(gtklock->focused_window)) setup_metadata(gtklock->focused_window);

	setup_playback(gtklock->focused_window, status);
}

static void player_appeared(PlayerctlPlayerManager *self, PlayerctlPlayer *player, gpointer user_data) {
	struct GtkLock *gtklock = user_data;
	if(gtklock->focused_window) setup_playerctl(gtklock->focused_window);
	g_signal_connect(player, "metadata", G_CALLBACK(metadata), user_data);
	g_signal_connect(player, "playback-status", G_CALLBACK(playback_status), user_data);
}

static void player_vanished(PlayerctlPlayerManager *self, PlayerctlPlayer *player, gpointer user_data) {
	struct GtkLock *gtklock = user_data;
	current_player = NULL;
	if(gtklock->focused_window && MODULE_DATA(gtklock->focused_window)) {
		gtk_widget_destroy(PLAYERCTL(gtklock->focused_window)->revealer);
		g_free(MODULE_DATA(gtklock->focused_window));
		MODULE_DATA(gtklock->focused_window) = NULL;
	}
}

void on_activation(struct GtkLock *gtklock, int id) {
	self_id = id;

	GError *error = NULL;
	player_manager = playerctl_player_manager_new(&error);
	if(error != NULL) {
		g_warning("Playerctl failed: %s", error->message);
		g_error_free(error);
	} else {
		g_signal_connect(player_manager, "player-appeared", G_CALLBACK(player_appeared), gtklock);
		g_signal_connect(player_manager, "player-vanished", G_CALLBACK(player_vanished), gtklock);

		GList *available_players = NULL;
		g_object_get(player_manager, "player-names", &available_players, NULL);
		if(available_players) {
			PlayerctlPlayerName *name = available_players->data;
			current_player = playerctl_player_new_from_name(name, NULL);
			playerctl_player_manager_manage_player(player_manager, current_player);
			g_object_unref(current_player);
		}
		g_signal_connect(player_manager, "name-appeared", G_CALLBACK(name_appeared), NULL);
	}

	soup_session = soup_session_new();
}

void on_focus_change(struct GtkLock *gtklock, struct Window *win, struct Window *old) {
	if(MODULE_DATA(win)) setup_metadata(win);
	else setup_playerctl(win);

	gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(win)->revealer), !gtklock->hidden || show_hidden);
	if(old != NULL && win != old)
		gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(old)->revealer), FALSE);
}

void on_window_destroy(struct GtkLock *gtklock, struct Window *ctx) {
	if(MODULE_DATA(ctx) != NULL) {
		g_free(MODULE_DATA(ctx));
		MODULE_DATA(ctx) = NULL;
	}
}

void on_idle_hide(struct GtkLock *gtklock) {
	if(gtklock->focused_window) {
		GtkRevealer *revealer = GTK_REVEALER(PLAYERCTL(gtklock->focused_window)->revealer);	
		gtk_revealer_set_reveal_child(revealer, show_hidden);
	}
}

void on_idle_show(struct GtkLock *gtklock) {
	if(gtklock->focused_window) {
		GtkRevealer *revealer = GTK_REVEALER(PLAYERCTL(gtklock->focused_window)->revealer);	
		gtk_revealer_set_reveal_child(revealer, TRUE);
	}
}

