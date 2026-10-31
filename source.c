// gtklock-playerctl-module
// Copyright (c) 2024 Jovan Lanik

// Playerctl module

#include <playerctl.h>
#include <libsoup/soup.h>

#include "gtklock-module.h"

struct playerctl {
	GtkWidget *revealer;
	GtkWidget *album_art;
	GtkWidget *label_box;
	GtkWidget *previous_button;
	GtkWidget *play_pause_button;
	GtkWidget *next_button;
	gboolean destroyed;
};

const gchar module_name[] = "playerctl";
const guint module_major_version = 5;
const guint module_minor_version = 0;

static int self_id;

static PlayerctlPlayerManager *player_manager = NULL;
static PlayerctlPlayer *current_player = NULL;
static SoupSession *soup_session = NULL;

static int art_size = 64;
static gchar *position = "top-center";
static gboolean show_hidden = FALSE;

GOptionEntry module_entries[] = {
	{ "art-size", 0, 0, G_OPTION_ARG_INT, &art_size, "Album art size in pixels", NULL },
	{ "position", 0, 0, G_OPTION_ARG_STRING, &position, "Position of media player controls", NULL },
	{ "show-hidden", 0, 0, G_OPTION_ARG_NONE, &show_hidden, "Show media controls when hidden", NULL },
	{ 0 },
};

static void **module_data_from_window(struct Window *ctx) {
	g_assert(ctx != NULL);
	return &ctx->module_data[self_id];
}

static void set_art_from_stream(GInputStream *stream, struct playerctl *widget) {
	g_assert(widget);
	if(widget->destroyed) return;

	GError *error = NULL;
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, -1, art_size, TRUE, NULL, &error);
	if(error != NULL) {
		g_warning("Failed loading album art (gdk_pixbuf_new_from_stream_at_scale): %s", error->message);
		g_error_free(error);
		return;
	}
	gtk_image_set_from_pixbuf(GTK_IMAGE(widget->album_art), pixbuf);
}

static void file_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	GError *error = NULL;
	GFileInputStream *stream = g_file_read_finish(G_FILE(source_object), res, &error);
	if(error != NULL) {
		g_warning("Failed loading album art (g_file_read_finish): %s", error->message);
		g_error_free(error);
		return;
	}
	set_art_from_stream(G_INPUT_STREAM(stream), user_data);
}

static void request_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
	GError *error = NULL;
	GInputStream *stream = soup_session_send_finish(SOUP_SESSION(source_object), res, &error);
	if(error != NULL) {
		g_warning("Failed loading album art (soup_request_send_finish): %s", error->message);
		g_error_free(error);
		return;
	}
	set_art_from_stream(stream, user_data);
}

static void setup_album_art(struct playerctl *widget) {
	g_assert(widget);
	g_assert(!widget->destroyed);

	gtk_image_set_from_icon_name(GTK_IMAGE(widget->album_art), "audio-x-generic-symbolic", GTK_ICON_SIZE_BUTTON);

	GError *error = NULL;
	gchar *uri = playerctl_player_print_metadata_prop(current_player, "mpris:artUrl", NULL);
	if(error != NULL) {
		g_warning("Failed loading album art (playerctl_player_print_metadata_prop): %s", error->message);
		g_error_free(error);
		return;
	}

	if(!uri || uri[0] == '\0') return;

	const char *scheme = g_uri_peek_scheme(uri);
	if(g_strcmp0("file", scheme) == 0) {
		GFile *file = g_file_new_for_uri(uri);
		g_file_read_async(file, G_PRIORITY_DEFAULT, NULL, file_callback, widget);
	} else if(g_strcmp0("http", scheme) == 0 || g_strcmp0("https", scheme) == 0) {
		SoupMessage *msg = soup_message_new(SOUP_METHOD_GET, uri);
		soup_session_send_async(soup_session, msg, G_PRIORITY_DEFAULT, NULL, request_callback, widget);
	} else {
		g_warning("Failed loading album art (g_uri_peek_scheme): Unknown scheme");
	}
}

static void play_pause(G_GNUC_UNUSED GtkButton *self, G_GNUC_UNUSED gpointer user_data) {
	GError *error = NULL;
	playerctl_player_play_pause(current_player, &error);
	if(error != NULL) {
		g_warning("Failed play_pause: %s", error->message);
		g_error_free(error);
	}
}

static void next(G_GNUC_UNUSED GtkButton *self, G_GNUC_UNUSED gpointer user_data) {
	GError *error = NULL;
	playerctl_player_next(current_player, NULL);
	if(error != NULL) {
		g_warning("Failed go_next: %s", error->message);
		g_error_free(error);
	}
}

static void previous(G_GNUC_UNUSED GtkButton *self, G_GNUC_UNUSED gpointer user_data) {
	GError *error = NULL;
	playerctl_player_previous(current_player, NULL);
	if(error != NULL) {
		g_warning("Failed go_previous: %s", error->message);
		g_error_free(error);
	}
}

static void widget_destroy(GtkWidget *widget, G_GNUC_UNUSED gpointer data) {
	g_assert(widget);

	gtk_widget_destroy(widget);
}

static void setup_playback(struct playerctl *widget, PlayerctlPlaybackStatus status) {
	g_assert(widget);
	g_assert(!widget->destroyed);

	const gchar *icon = status == PLAYERCTL_PLAYBACK_STATUS_PLAYING ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
	GtkWidget *image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(widget->play_pause_button), image);
}

static gboolean setup_button_sensitive_handler(gpointer user_data) {
	if(current_player) {
		struct playerctl *widget = user_data;
		g_assert(widget);
		if(widget->destroyed) return G_SOURCE_REMOVE;

		gboolean can_go_next, can_go_previous, can_pause;
		g_object_get(current_player, "can-go-next", &can_go_next, "can-go-previous", &can_go_previous, "can-pause", &can_pause, NULL);

		gtk_widget_set_sensitive(widget->previous_button, can_go_previous);
		gtk_widget_set_sensitive(widget->play_pause_button, can_pause);
		gtk_widget_set_sensitive(widget->next_button, can_go_next);
	}
	return G_SOURCE_REMOVE;
}

static void setup_button_sensitive(struct playerctl *widget) {
	g_assert(widget);

	g_timeout_add_seconds(1, setup_button_sensitive_handler, widget);
	setup_button_sensitive_handler(widget);
}

static void setup_metadata(struct playerctl *widget) {
	g_assert(widget);
	g_assert(!widget->destroyed);

	if(!current_player) {
		gtk_revealer_set_reveal_child(GTK_REVEALER(widget->revealer), FALSE);
		return;
	}

	PlayerctlPlaybackStatus status;
	g_object_get(current_player, "playback-status", &status, NULL);
	setup_playback(widget, status);

	setup_album_art(widget);
	gtk_container_foreach(GTK_CONTAINER(widget->label_box), widget_destroy, NULL);

	gchar *title = playerctl_player_get_title(current_player, NULL);
	if(title && title[0] != '\0') {
		GtkWidget *title_label = gtk_label_new(NULL);
		gtk_widget_set_name(title_label, "title-label");
		gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(title_label), 1);
		gchar *title_bold = g_markup_printf_escaped("<b>%s</b>", title);
		gtk_label_set_markup(GTK_LABEL(title_label), title_bold);
		gtk_container_add(GTK_CONTAINER(widget->label_box), title_label);
	}

	gchar *album = playerctl_player_get_album(current_player, NULL);
	if(album && album[0] != '\0') {
		GtkWidget *album_label = gtk_label_new(album);
		gtk_widget_set_name(album_label, "album-label");
		gtk_label_set_xalign(GTK_LABEL(album_label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(album_label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(album_label), 1);
		gtk_container_add(GTK_CONTAINER(widget->label_box), album_label);
	}

	gchar *artist = playerctl_player_get_artist(current_player, NULL);
	if(artist && artist[0] != '\0') {
		GtkWidget *artist_label = gtk_label_new(artist);
		gtk_widget_set_name(artist_label, "artist-label");
		gtk_label_set_xalign(GTK_LABEL(artist_label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(artist_label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(artist_label), 1);
		gtk_container_add(GTK_CONTAINER(widget->label_box), artist_label);
	}

	setup_button_sensitive(widget);

	gtk_revealer_set_reveal_child(GTK_REVEALER(widget->revealer), TRUE);
	gtk_widget_show_all(widget->revealer);
}

static struct playerctl *setup_playerctl(struct Window *ctx) {
	g_assert(ctx);
	void **module_data = module_data_from_window(ctx);
	struct playerctl *widget = *module_data;
	if(widget) return widget;
	widget = g_malloc(sizeof(struct playerctl));
	g_assert(widget);

	widget->destroyed = FALSE;

	widget->revealer = gtk_revealer_new();
	g_object_set(widget->revealer, "margin", 5, NULL);
	gtk_widget_set_name(widget->revealer, "playerctl-revealer");
	gtk_revealer_set_transition_type(GTK_REVEALER(widget->revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
	gtk_revealer_set_reveal_child(GTK_REVEALER(widget->revealer), FALSE);

	if(g_strcmp0(position, "top-left") == 0 || g_strcmp0(position, "bottom-left") == 0)
		gtk_widget_set_halign(widget->revealer, GTK_ALIGN_START);
	else if(g_strcmp0(position, "top-right") == 0 || g_strcmp0(position, "bottom-right") == 0)
		gtk_widget_set_halign(widget->revealer, GTK_ALIGN_END);
	else gtk_widget_set_halign(widget->revealer, GTK_ALIGN_CENTER);

	if(g_strcmp0(position, "top-left") == 0 || g_strcmp0(position, "top-right") == 0 || g_strcmp0(position, "top-center") == 0)
		gtk_widget_set_valign(widget->revealer, GTK_ALIGN_START);
	else if(g_strcmp0(position, "bottom-left") == 0 || g_strcmp0(position, "bottom-right") == 0 || g_strcmp0(position, "bottom-center") == 0)
		gtk_widget_set_valign(widget->revealer, GTK_ALIGN_END);
	else if(g_strcmp0(position, "above-clock") != 0 && g_strcmp0(position, "under-clock") != 0) {
		g_warning("%s: Unknown position", module_name);
		gtk_widget_set_valign(widget->revealer, GTK_ALIGN_START);
		gtk_widget_set_halign(widget->revealer, GTK_ALIGN_CENTER);
	}

	gboolean above = g_strcmp0(position, "above-clock") == 0;
	if(above || g_strcmp0(position, "under-clock") == 0) {
		gtk_container_add(GTK_CONTAINER(ctx->info_box), widget->revealer);
		if(above) gtk_box_reorder_child(GTK_BOX(ctx->info_box), widget->revealer, 0);
	} else gtk_overlay_add_overlay(GTK_OVERLAY(ctx->overlay), widget->revealer);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
	gtk_widget_set_name(box, "playerctl-box");
	gtk_container_add(GTK_CONTAINER(widget->revealer), box);

	if(art_size) {
		widget->album_art = gtk_image_new_from_icon_name("audio-x-generic-symbolic", GTK_ICON_SIZE_BUTTON);
		gtk_widget_set_halign(widget->album_art, GTK_ALIGN_CENTER);
		gtk_widget_set_name(widget->album_art, "album-art");
		gtk_widget_set_size_request(widget->album_art, art_size, art_size);
		gtk_container_add(GTK_CONTAINER(box), widget->album_art);
	}

	widget->label_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(widget->label_box, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(widget->label_box, 180, -1);
	gtk_container_add(GTK_CONTAINER(box), widget->label_box);

	GtkWidget *control_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_valign(control_box, GTK_ALIGN_CENTER);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(control_box), GTK_BUTTONBOX_EXPAND);
	gtk_container_add(GTK_CONTAINER(box), control_box);

	widget->previous_button = gtk_button_new_from_icon_name("media-skip-backward-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(widget->previous_button, "clicked", G_CALLBACK(previous), NULL);
	gtk_widget_set_name(widget->previous_button, "previous-button");
	gtk_container_add(GTK_CONTAINER(control_box), widget->previous_button);

	widget->play_pause_button = gtk_button_new();
	g_signal_connect(widget->play_pause_button, "clicked", G_CALLBACK(play_pause), NULL);
	gtk_widget_set_name(widget->play_pause_button, "play-pause-button");
	gtk_container_add(GTK_CONTAINER(control_box), widget->play_pause_button);

	widget->next_button = gtk_button_new_from_icon_name("media-skip-forward-symbolic", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(widget->next_button, "clicked", G_CALLBACK(next), NULL);
	gtk_widget_set_name(widget->next_button, "next-button");
	gtk_container_add(GTK_CONTAINER(control_box), widget->next_button);

	setup_metadata(widget);

	*module_data = widget;
	return widget;
}

void g_module_unload(G_GNUC_UNUSED GModule *m) {
	g_object_unref(player_manager);
	g_object_unref(soup_session);
}

static void name_appeared(G_GNUC_UNUSED PlayerctlPlayerManager *self, PlayerctlPlayerName *name, G_GNUC_UNUSED gpointer user_data) {
	if(current_player) return;

	current_player = playerctl_player_new_from_name(name, NULL);
	playerctl_player_manager_manage_player(player_manager, current_player);
	g_object_unref(current_player);
}

static void metadata(G_GNUC_UNUSED PlayerctlPlayer *player, G_GNUC_UNUSED GVariant *metadata, struct GtkLock *gtklock) {
	struct Window *window = gtklock->focused_window;
	if(!window) return;
	void **module_data = module_data_from_window(window);

	struct playerctl *widget = *module_data;

	if(widget) setup_metadata(widget);
	else widget = setup_playerctl(window);
}

static void playback_status(G_GNUC_UNUSED PlayerctlPlayer *player, PlayerctlPlaybackStatus status, struct GtkLock *gtklock) {
	struct Window *window = gtklock->focused_window;
	if(!window) return;
	void **module_data = module_data_from_window(window);

	struct playerctl *widget = *module_data;

	if(widget) setup_metadata(widget);
	else widget = setup_playerctl(window);

	setup_playback(widget, status);
}

static void player_appeared(G_GNUC_UNUSED PlayerctlPlayerManager *self, PlayerctlPlayer *player, struct GtkLock *gtklock) {
	struct Window *window = gtklock->focused_window;

	if(window) setup_playerctl(window);

	g_signal_connect(player, "metadata", G_CALLBACK(metadata), gtklock);
	g_signal_connect(player, "playback-status", G_CALLBACK(playback_status), gtklock);
}

static void player_vanished(G_GNUC_UNUSED PlayerctlPlayerManager *self, G_GNUC_UNUSED PlayerctlPlayer *player, struct GtkLock *gtklock) {
	current_player = NULL;

	if(!gtklock->focused_window) return; // FIXME: wont delete if window is none (unplug monitor)
	void **module_data = module_data_from_window(gtklock->focused_window);
	struct playerctl *widget = *module_data;

	if(gtklock->focused_window && widget) {
		gtk_widget_destroy(widget->revealer);
		// FIXME: leak, otherwise use-after-free
		// g_free(widget);
		widget->destroyed = TRUE;
		widget = *module_data = NULL;
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
	g_assert(win);
	void **module_data = module_data_from_window(win);
	struct playerctl *widget = *module_data;

	if(widget) setup_metadata(widget);
	else widget = setup_playerctl(win);

	gtk_revealer_set_reveal_child(GTK_REVEALER(widget->revealer), !gtklock->hidden || show_hidden);
	if(old != NULL && win != old) {
		struct playerctl *old_widget = *module_data_from_window(old);
		g_assert(old_widget);
		gtk_revealer_set_reveal_child(GTK_REVEALER(old_widget->revealer), FALSE);
	}
}

void on_window_destroy(G_GNUC_UNUSED struct GtkLock *gtklock, struct Window *ctx) {
	g_assert(ctx);
	void **module_data = module_data_from_window(ctx);
	struct playerctl *widget = *module_data;
	if(widget) {
		gtk_widget_destroy(widget->revealer);
		g_free(widget);
		widget = *module_data = NULL;
	}
}

void on_idle_hide(struct GtkLock *gtklock) {
	if(gtklock->focused_window) {
		void **module_data = module_data_from_window(gtklock->focused_window);
		struct playerctl *widget = *module_data;
		if(widget) gtk_revealer_set_reveal_child(GTK_REVEALER(widget->revealer), show_hidden);
	}
}

void on_idle_show(struct GtkLock *gtklock) {
	if(gtklock->focused_window) {
		void **module_data = module_data_from_window(gtklock->focused_window);
		struct playerctl *widget = *module_data;
		if(widget) gtk_revealer_set_reveal_child(GTK_REVEALER(widget->revealer), TRUE);
	}
}
