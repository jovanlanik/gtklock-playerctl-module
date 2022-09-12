// gtklock-playerctl-module
// Copyright (c) 2022 Jovan Lanik

// Playerctl module

#include <playerctl.h>

#include "gtklock-module.h"

#define MODULE_DATA(x) (x->module_data[self_id])
#define PLAYERCTL(x) ((struct playerctl *)MODULE_DATA(x))

extern void config_load(const char *path, const char *group, GOptionEntry entries[]);

struct playerctl {
	GtkWidget *revealer;
	GtkWidget *box;
};

const gchar module_version[] = "v1.3.6";

static int self_id;

static gboolean show_labels = FALSE;

static GOptionEntry playerctl_entries[] = {
	{ "show-labels", 0, 0, G_OPTION_ARG_NONE, &show_labels, NULL, NULL },
	{ NULL },
};

static void setup_playerctl(struct Window *ctx) {
	if(MODULE_DATA(ctx) != NULL) {
		gtk_widget_destroy(PLAYERCTL(ctx)->revealer);
		g_free(MODULE_DATA(ctx));
		MODULE_DATA(ctx) = NULL;
	}
	MODULE_DATA(ctx) = g_malloc(sizeof(struct playerctl));

	PLAYERCTL(ctx)->revealer = gtk_revealer_new();
	g_object_set(PLAYERCTL(ctx)->revealer, "margin", 5, NULL);
	gtk_widget_set_halign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(PLAYERCTL(ctx)->revealer, GTK_ALIGN_START);
	gtk_widget_set_name(PLAYERCTL(ctx)->revealer, "playerctl-revealer");
	gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(ctx)->revealer), TRUE);
	gtk_revealer_set_transition_type(GTK_REVEALER(PLAYERCTL(ctx)->revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
	gtk_overlay_add_overlay(GTK_OVERLAY(ctx->overlay), PLAYERCTL(ctx)->revealer);

	PLAYERCTL(ctx)->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_widget_set_name(PLAYERCTL(ctx)->box, "playerctl-box");
	gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->revealer), PLAYERCTL(ctx)->box);

	GtkWidget *topbar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->box),  topbar_box);

	GtkWidget *topbar_label = gtk_label_new("player name");
	gtk_container_add(GTK_CONTAINER(topbar_box), topbar_label);

	GtkWidget *topbar_button = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(topbar_button), GTK_RELIEF_NONE);
	gtk_box_pack_end(GTK_BOX(topbar_box), topbar_button, FALSE, FALSE, 0);

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
	gtk_container_add(GTK_CONTAINER(PLAYERCTL(ctx)->box), grid);

	// TODO: controls

	gtk_widget_show_all(PLAYERCTL(ctx)->revealer);
}

void g_module_unload(GModule *m) {
}

void on_activation(struct GtkLock *gtklock, int id) {
	self_id = id;
	config_load(gtklock->config_path, "playerctl", playerctl_entries);
}

void on_focus_change(struct GtkLock *gtklock, struct Window *win, struct Window *old) {
	setup_playerctl(win);
	if(gtklock->hidden)
		gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(win)->revealer), FALSE);
	if(old != NULL && win != old)
		gtk_revealer_set_reveal_child(GTK_REVEALER(PLAYERCTL(old)->revealer), FALSE);
}

void on_window_empty(struct GtkLock *gtklock, struct Window *ctx) {
	if(MODULE_DATA(ctx) != NULL) {
		g_free(MODULE_DATA(ctx));
		MODULE_DATA(ctx) = NULL;
	}
}

void on_idle_hide(struct GtkLock *gtklock) {
	if(gtklock->focused_window) {
		GtkRevealer *revealer = GTK_REVEALER(PLAYERCTL(gtklock->focused_window)->revealer);	
		gtk_revealer_set_reveal_child(revealer, FALSE);
	}
}

void on_idle_show(struct GtkLock *gtklock) {
	if(gtklock->focused_window) {
		GtkRevealer *revealer = GTK_REVEALER(PLAYERCTL(gtklock->focused_window)->revealer);	
		gtk_revealer_set_reveal_child(revealer, TRUE);
	}
}

