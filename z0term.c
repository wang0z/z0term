/*
  Lance Wang <Lance.w19@gmail.com>
  Redistributed under the terms of GPLv3 or any later version.
 */

#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pty.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <asm/ioctl.h>
#include <asm/ioctls.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <vte/vte.h>
#include <vte/vtepty.h>

/* tty */
static int master_fd;
static int slave_fd;
static pid_t slave_pid;
static VtePty *vte_pty;

static gboolean
create_tty(void)
{
  char pty_dev[PATH_MAX];
  int ret;

  ret = openpty(&master_fd, &slave_fd, pty_dev, NULL, NULL);
  if (ret < 0) {
    perror("");
    exit(ret);
  }
  printf("z0term/ pty is %s\n", pty_dev);

  vte_pty = vte_pty_new_foreign_sync(master_fd, NULL, NULL);
  if (NULL == vte_pty) {
    fprintf(stderr, "Failed to create vte_pty\n");
    exit(-1);
  }
}

void
reap_child(int not_used)
{
  int stat;
  pid_t p;

  if ((p = waitpid(slave_pid, &stat, WNOHANG)) < 0)
    fprintf(stderr, "Waiting for pid %hd failed: %s\n", slave_pid, strerror(errno));

  if (slave_pid != p)
    return;

  if (!WIFEXITED(stat) || WEXITSTATUS(stat))
    fprintf(stderr, "child finished with error '%d'\n", stat);
  gtk_main_quit();
}

static gboolean
create_child(void)
{
  char spawn_err[1] = {1};
  int pp[2];
  int pid;

  pipe2(pp, O_CLOEXEC);
  pid = fork();
  if (!pid) {//child
    close(pp[0]);
    setsid();
    if (ioctl(slave_fd, TIOCSCTTY, (char *)0) < 0) {
      write(pp[1], spawn_err, 1);
    } else {
      char * child_argv[] = {"bash", NULL};
      dup2(slave_fd, 0); dup2(slave_fd, 1); dup2(slave_fd, 2);
      if (slave_fd > 2) close(slave_fd);
      setenv("TERM", "xterm-256color", 1);
      close(pp[1]);
      execv("/bin/bash", child_argv);
    }
    close(pp[0]);
    fprintf(stderr, "Failed to execv\n");
    exit(-1);
  }
  close(pp[1]);
  if (read(pp[0], spawn_err, 1) > 0) {
    int ch_s;
    waitpid(pid, &ch_s, 0);
    fprintf(stderr, "Failed child\n");
  } else {
    slave_pid = pid;
    signal(SIGCHLD, reap_child);
  }
  close(pp[0]);

  return TRUE;
}

/* term */
#define COLOR(r, g, b) { .red = (r) / 255.0, .green = (g) / 255.0, .blue = (b) / 255.0, .alpha = 1.0 }
#define C_BASE03    COLOR(0x00, 0x2b, 0x36)
#define C_BASE02    COLOR(0x07, 0x36, 0x42)
#define C_BASE01    COLOR(0x58, 0x6e, 0x75)
#define C_BASE00    COLOR(0x65, 0x7b, 0x83)
#define C_BASE3     COLOR (0xfd, 0xf6, 0xe3)
#define C_BASE2     COLOR (0xee, 0xe8, 0xd5)
#define C_BASE1     COLOR(0x93, 0xa1, 0xa1)
#define C_BASE0     COLOR(0x83, 0x94, 0x96)
#define C_YELLOW    COLOR(0xb5, 0x89, 0x00)
#define C_ORANGE    COLOR(0xcb, 0x4b, 0x16)
#define C_RED       COLOR(0xdc, 0x32, 0x2f)
#define C_MAGENTA   COLOR(0xd3, 0x36, 0x82)
#define C_VIOLET    COLOR(0x6c, 0x71, 0xc4)
#define C_BLUE      COLOR(0x26, 0x8b, 0xd2)
#define C_CYAN      COLOR(0x2a, 0xa1, 0x98)
#define C_GREEN     COLOR(0x85, 0x99, 0x00)
const GdkRGBA solarized[16] = {
C_BASE02, C_RED, C_GREEN, C_YELLOW, C_BLUE, C_MAGENTA, C_CYAN, C_BASE2,
C_BASE03, C_ORANGE, C_BASE01, C_BASE00, C_BASE0, C_VIOLET, C_BASE1, C_BASE3,
};
// ligth
// const GdkRGBA solarized_fg = C_BASE01;
// const GdkRGBA solarized_bg = C_BASE3;

// dark
const GdkRGBA solarized_fg = C_BASE1;
const GdkRGBA solarized_bg = C_BASE03;

static VteTerminal *term;

int font_size = 14;

static gboolean
set_vte_terminal(void)
{
  PangoFontDescription *font;
  int size;
  vte_terminal_set_encoding(term, "Default Encoding", NULL);
  vte_terminal_set_colors(term,
                          &solarized_fg,
                          &solarized_bg,
                          solarized, 16);
  font = pango_font_description_from_string("monospace 14");
  size = pango_font_description_get_size(font);
  pango_font_description_set_size(font, font_size * size / 14);
  vte_terminal_set_font(term, font);
  vte_terminal_set_font_scale(term, 1.0);
  vte_terminal_set_allow_bold(term, FALSE);
  vte_terminal_set_audible_bell(term, FALSE);
}

/* menu */
static GtkWidget *vte;
static GtkWidget *menu;

static void menu_copy(GtkMenuItem *item, VteTerminal *term)
{
  vte_terminal_copy_clipboard(term);
}

static void menu_paste(GtkMenuItem *item, VteTerminal *term)
{
  vte_terminal_paste_clipboard(term);
}

static gboolean menu_popup(GtkWidget *widget, GdkEventButton *event)
{
  if (event->button == 3) {
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
  }
  return FALSE;
}

static gboolean create_menu(void)
{
  GtkWidget *copy_item;
  GtkWidget *paste_item;

  menu = gtk_menu_new();

  copy_item = gtk_menu_item_new_with_label("copy");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_item);
  g_signal_connect(copy_item, "activate", G_CALLBACK(menu_copy), term);

  paste_item = gtk_menu_item_new_with_label("paste");
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste_item);
  g_signal_connect(paste_item, "activate", G_CALLBACK(menu_paste), term);

  g_signal_connect(vte, "button-press-event", G_CALLBACK(menu_popup), NULL);
  gtk_widget_show_all(menu);
  return TRUE;
}

static gboolean delete_event(void) {
  gtk_main_quit();
  return TRUE;
}


int main(int argc, char **argv)
{
  int gtk_argc;
  char **gtk_argv;
  int c;

  GtkWidget *main_window;

  gtk_argc = argc;
  gtk_argv = argv;
  gtk_init(&gtk_argc, &gtk_argv);

  while (1) {
      long i;
      c = getopt(argc, argv, "s:");
      if (c == -1) break;
      switch (c) {
      case 's':
          i = strtol(optarg, NULL, 10);
          if (i > 8 && i < 64) font_size = (int)i;
          break;
      case '?':
          break;
      default:
          break;
      }
  }
  create_tty();
  create_child();

  vte = vte_terminal_new();
  term = VTE_TERMINAL(vte);
  set_vte_terminal();
  gtk_widget_set_hexpand(vte, TRUE);
  gtk_widget_set_vexpand(vte, TRUE);
  create_menu();

  vte_terminal_set_pty(term, vte_pty);

  main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_container_add(GTK_CONTAINER(GTK_WINDOW(main_window)), vte);
  g_signal_connect(main_window, "delete_event", G_CALLBACK(delete_event), NULL);
  gtk_widget_show_all(main_window);

  gtk_main();
}
