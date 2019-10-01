#include <stdio.h>
#include <glib.h>
#include <gst/gst.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#define DEFAULT_RES_BUFFER 1024

char* HOST = "127.0.0.1";
char* GST_PORT = "6601";
int MPD_PORT = 6600;

char* help_msg = "usage: %s [options]\n"
                 "  -host   host ip address (127.0.0.1)\n"
                 "  -m     mpd port (6600)\n"
                 "  -g     gstreamer port (6601)\n";

typedef struct {
  char* title;
  char* artist;
} song;

song current;

int mpd_sock;
bool mpd_is_playing = false;

pthread_t thread;
GstElement *pipeline;
GstBus *bus;

static void create_pipeline();
static void destroy_pipeline();
static void* audio_bus_thread();
static int mpd_make_connection();
static void mpd_get_state();
static void wait_for_audio();

int main(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "-h")) {
      printf(help_msg, argv[0]);
    } else if (!strcmp(argv[i], "-host")) {
      HOST = argv[i + 1];
    } else if (!strcmp(argv[i], "-m")) {
      MPD_PORT = atoi(argv[i + 1]);
    } else if (!strcmp(argv[i], "-g")) {
      GST_PORT = argv[i + 1];
    }
  }

  gst_init(&argc, &argv);
  
  if (mpd_make_connection() < 0) {
    exit(1);
  }

  current.artist = (char *)malloc(256);
  current.title = (char *)malloc(256);

  mpd_get_state();

  wait_for_audio();

  return 0;
}

int mpd_make_connection() {
  printf("[info] atempting connection to mpd\n");
  struct sockaddr_in serv_addr;

  if ((mpd_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("err: socket failed\n");
    return -1;
  }

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(MPD_PORT);

  if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0) {
    printf("err: invalid addr\n");
    return -1;
  }

  if (connect(mpd_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("err: connection failed\n");
    return -1;
  }

  char *buf = (char *)malloc(DEFAULT_RES_BUFFER);
  read(mpd_sock, buf, DEFAULT_RES_BUFFER);

  char ok[3];
  memcpy(ok, buf, 2);
  ok[2] = '\0';

  if (strcmp(ok, "OK") != 0) {
    return -1;
  }

  free(buf);

  printf("[info] connected to mpd\n");

  return 0;
}

void wait_for_audio() {
  printf("[info] waiting for audio to start\n");
  do { // wait until there is something to play
    mpd_get_state();
    usleep(800000);
  } while (!mpd_is_playing);
  create_pipeline();
}

void* audio_bus_thread(void* v) {
  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessageType msg_type = (GstMessageType)(
      GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  while (1) {
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, msg_type);
    if (msg != NULL) {
      switch (GST_MESSAGE_TYPE(msg)) {
      case GST_MESSAGE_EOS:
        destroy_pipeline();
        wait_for_audio();
        return 0;
      default: continue;
      }
    }
  }
}

void mpd_current_song() {
  const char *cmd = "currentsong\n";
  write(mpd_sock, cmd, strlen(cmd));

  char *buf = (char *)malloc(DEFAULT_RES_BUFFER);
  read(mpd_sock, buf, DEFAULT_RES_BUFFER);

  char *line = strtok(buf, "\n");
  while (line != NULL) {
    char key[128]; char value[128];
    char *index = strchr(line, ':');
    if (index) {
      snprintf(key, (index - line) + 1, "%s", line);
      sprintf(value, "%s", index + 2);
      if (!strcmp(key, "Artist")) {
        strcpy(current.artist, value);
      } else if (!strcmp(key, "Title")) {
        strcpy(current.title, value);
      }
    }
    line = strtok(NULL, "\n");
  }
  
  free(buf);
}

void mpd_get_state() {
  const char *cmd = "status\n";
  write(mpd_sock, cmd, strlen(cmd));

  char *buf = (char *)malloc(DEFAULT_RES_BUFFER);
  read(mpd_sock, buf, DEFAULT_RES_BUFFER);

  char *line = strtok(buf, "\n");
  while (line != NULL) {
    char key[128]; char value[128];
    char *index = strchr(line, ':');
    if (index) {
      snprintf(key, (index - line) + 1, "%s", line);
      sprintf(value, "%s", index + 2);
      if (!strcmp(key, "state")) {
        if (!strcmp(value, "play")) {
          mpd_is_playing = true; 
        } else {
          mpd_is_playing = false;
        }
      }
    }
    line = strtok(NULL, "\n");
  }

  free(buf);
}

void create_pipeline() {
  const char *launch_cmd =
      "tcpclientsrc host=%s port=%s ! queue min-threshold-buffers=6 ! "
      "mpegaudioparse ! mpg123audiodec ! audioconvert ! audioresample ! "
      "pulsesink sync=false buffer-time=800000";

  char buf[256];
  sprintf(buf, launch_cmd, HOST, GST_PORT);

  GError *error = NULL;
  pipeline = gst_parse_launch(buf, &error);
  if (error) {
    printf("err: %s\n",  error->message);
    return;
  }

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  printf("[info] playing\n");

  printf("[info] setting song\n");

  mpd_current_song();

  pthread_create(&thread, NULL, audio_bus_thread, NULL);
  pthread_join(thread, NULL);
}

void destroy_pipeline() {
  if (pipeline == NULL) return;
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
}
