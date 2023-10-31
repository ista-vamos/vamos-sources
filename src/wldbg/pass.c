/*
 * Copyright (c) 2015 Marek Chalupa
 * Copyright (c) ISTA Austria
 *
 * Code is based on the example pass from wldbg
 *
 * compile with:
 *
 * cc -Wall -fPIC -shared -o vamos.so vamos.c
 * (maybe will need to add -I../src)
 *
 * run with (in directory with vamos.so):
 *
 * wldbg vamos -- wayland-client
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client-protocol.h>

#include "wldbg-parse-message.h"
#include "wldbg-pass.h"
#include <wldbg.h>

#include "vamos-buffers/core/event.h"
#include "vamos-buffers/core/signatures.h"
#include "vamos-buffers/core/source.h"
#include "vamos-buffers/shmbuf/buffer.h"
#include "vamos-buffers/shmbuf/client.h"

static void send_event(struct wldbg_message *message);

struct pass_data {
  unsigned int incoming_number;
  unsigned int outcoming_number;
  unsigned int in_trasfered;
  unsigned int out_trasfered;
};

struct event {
  vms_event base;
  unsigned char args[];
};

vms_shm_buffer *top_buffer;
vms_kind client_new_kind;
vms_kind client_exit_kind;
vms_eventid top_buffer_eventid;

static size_t waiting_for_buffer = 0;

enum vamos_event_idx {
  POINTER_MOTION = 0,
  KEYBOARD_KEY = 1,
};

struct kind_mapping {
  const char *name;
  const char *sig;
  vms_kind kind;
} vamos_events[] = {
    /* the order of events must match the indices
     * in the enum vamos_event_idx */
    {"pointer_motion", "iii", 0},
    {"keyboard_key", "iiii", 0},
};

struct connection_data {
  vms_eventid next_id;
  vms_shm_buffer *buffer;
  size_t waiting_for_buffer;

  struct kind_mapping events[sizeof vamos_events / sizeof vamos_events[0]];
};

static void handle_keyboard_message(struct wldbg_resolved_message *rm,
                                    struct connection_data *data);

static void handle_pointer_message(struct wldbg_resolved_message *rm,
                                   struct connection_data *data);

static unsigned char *data_ptr(struct connection_data *data) {
  unsigned char *addr;
  while (!(addr = vms_shm_buffer_start_push(data->buffer))) {
    ++data->waiting_for_buffer;
  }

  return addr;
}

struct vms_source_control *sub_control;

static vms_shm_buffer *init_vamos(const char *shmkey) {

  struct vms_source_control *top_control =
      vms_source_control_define(2, "client_new", "i", "client_exit", "i");
  if (!top_control) {
    fprintf(stderr, "%s:%d: Failed defining source control\n", __FILE__,
            __LINE__);
    return NULL;
  }

  sub_control = vms_source_control_define(
      sizeof vamos_events / sizeof vamos_events[0],
      vamos_events[POINTER_MOTION].name, vamos_events[POINTER_MOTION].sig,
      vamos_events[KEYBOARD_KEY].name, vamos_events[KEYBOARD_KEY].sig);
  if (!sub_control) {
    fprintf(stderr, "%s:%d: Failed defining source control\n", __FILE__,
            __LINE__);
    free(top_control);
    return NULL;
  }

  const size_t capacity = 128;
  top_buffer = vms_shm_buffer_create(shmkey, capacity, top_control);
  if (!top_buffer) {
    fprintf(stderr, "Failed creating shm buffer\n");
    free(top_control);
    free(sub_control);
    return NULL;
  }

  fprintf(stderr, "info: waiting for the monitor to attach... ");
  if (vms_shm_buffer_wait_for_reader(top_buffer) < 0) {
    fprintf(stderr, "Failed waiting for the monitor to attach...\n");
    vms_shm_buffer_destroy(top_buffer);
    free(top_control);
    free(sub_control);
    return NULL;
  }
  fprintf(stderr, "done\n");

  size_t events_num;
  struct vms_event_record *events =
      vms_shm_buffer_get_avail_events(top_buffer, &events_num);

  struct vms_event_record *event = events;
  for (int i = 0; i < events_num; ++i) {
    printf("event-kind kind: %lu\n", event->kind);
    if (strcmp(event->name, "client_new") == 0) {
      client_new_kind = event->kind;
    } else if (strcmp(event->name, "client_exit") == 0) {
      client_exit_kind = event->kind;
    }
    ++event;
  }

  return top_buffer;
}

static void destroy_data(struct wldbg_connection *conn, void *data) {
  struct connection_data *cdata = (struct connection_data *)data;
  vms_shm_buffer_destroy_sub_buffer(cdata->buffer);
  free(data);
}

struct connection_data *get_connection_data(struct wldbg_message *msg) {
  struct connection_data *data =
      wldbg_connection_get_user_data(msg->connection);
  if (data) {
    return data;
  }

  printf("CREATING SUBBUFFER\n");

  /* TODO new client found */
  assert(top_buffer && "No top-level SHM buffer");
  assert(sub_control);
  const size_t capacity = 100;
  vms_shm_buffer *buffer =
      vms_shm_buffer_create_sub_buffer(top_buffer, capacity, sub_control);

  if (!buffer) {
    fprintf(stderr, "Failed creating shm buffer\n");
    return NULL;
  }

  data = calloc(1, sizeof *data);
  if (!data) {
    fprintf(stderr, "Failed memory allocation");
    abort();
  }

  data->buffer = buffer;


  int pid = wldbg_connection_get_client_pid(msg->connection);

  /* Notify VAMOS about a new client (it must be done before waiting for the monitor to attach) */
  unsigned char *addr;
  while (!(addr = vms_shm_buffer_start_push(top_buffer))) {
    ++waiting_for_buffer;
  }

  ++top_buffer_eventid;
  addr = vms_shm_buffer_partial_push(top_buffer, addr,
                                     &client_new_kind,
                                     sizeof(vms_kind));
  addr = vms_shm_buffer_partial_push(top_buffer, addr, &top_buffer_eventid,
                                     sizeof(vms_eventid));
  addr = vms_shm_buffer_partial_push(top_buffer, addr, &pid,
                                     sizeof(pid));
  vms_shm_buffer_finish_push(top_buffer);

  fprintf(stderr, "[%d] info: waiting for the monitor to attach... ", pid);
  if (vms_shm_buffer_wait_for_reader(buffer) < 0) {
    fprintf(stderr, "Failed waiting for the monitor to attach...\n");
    vms_shm_buffer_destroy(buffer);
    return NULL;
  }
  fprintf(stderr, "done\n");

  size_t events_num;
  struct vms_event_record *events =
      vms_shm_buffer_get_avail_events(buffer, &events_num);

  struct vms_event_record *event = events;
  for (int i = 0; i < events_num; ++i) {
    printf("event-kind kind: %lu\n", event->kind);
    if (strcmp(event->name, vamos_events[POINTER_MOTION].name) == 0) {
      data->events[POINTER_MOTION].kind = event->kind;
      printf("POINTER_MOTION kind: %lu\n", event->kind);
    } else if (strcmp(event->name, vamos_events[KEYBOARD_KEY].name) == 0) {
      data->events[KEYBOARD_KEY].kind = event->kind;
      printf("KEYBOARD_KEY kind: %lu\n", event->kind);
    }
    ++event;
  }

  wldbg_connection_set_user_data(msg->connection, data, destroy_data);

  return data;
}

static void help(void *user_data) { (void)user_data; }

static int init(struct wldbg *wldbg, struct wldbg_pass *pass, int argc,
                const char *argv[]) {
  (void)wldbg;

  printf("-- Initializing VAMOS pass --\n\n");
  for (int i = 0; i < argc; ++i)
    printf("\targument[%d]: %s\n", i, argv[i]);

  printf("\n\n");

  if (!init_vamos(argv[1])) {
    fprintf(stderr, "Failed initializing VAMOS");
    return -1;
  }

  struct pass_data *data = calloc(1, sizeof *data);
  if (!data) {
    fprintf(stderr, "Memory allocation failed");
    return -1;
  }

  pass->user_data = data;

  return 0;
}

static void destroy(void *user_data) {
  struct pass_data *data = user_data;

  printf(" -- Destroying vamos pass --\n\n");
  printf("Totally trasfered %u bytes from client to server\n"
         "and %u bytes from server to client\n",
         data->out_trasfered, data->in_trasfered);

  free(data);
  free(sub_control);

  vms_shm_buffer_destroy(top_buffer);
}

static int message_in(void *user_data, struct wldbg_message *message) {
  struct pass_data *data = user_data;

  data->incoming_number++;
  data->in_trasfered += message->size;

  /*
  printf("GOT incoming message: %u, size: %lu bytes\n",
         data->incoming_number, message->size);
         */

  send_event(message);

  return PASS_NEXT;
}

static int message_out(void *user_data, struct wldbg_message *message) {
  struct pass_data *data = user_data;

  data->outcoming_number++;
  data->out_trasfered += message->size;

  /*
  printf("GOT outcoming message: %u, size: %lu bytes\n",
         data->outcoming_number, message->size);
         */

  send_event(message);

  return PASS_NEXT;
}

struct wldbg_pass wldbg_pass = {.init = init,
                                .destroy = destroy,
                                .server_pass = message_in,
                                .client_pass = message_out,
                                .help = help,
                                .description =
                                    "Wldbg pass that sends data into VAMOS"};

void send_event(struct wldbg_message *message) {
  int is_buggy = 0;
  uint32_t pos;
  struct wldbg_connection *conn = message->connection;
  struct wldbg_resolved_message rm;

  struct connection_data *data = get_connection_data(message);

  if (!wldbg_resolve_message(message, &rm)) {
    // fprintf(stderr, "Failed resolving message, event lost...\n");
    return;
  }

  if (rm.wl_interface == &wl_pointer_interface) {
    printf("%c: ", message->from == SERVER ? 'S' : 'C');
    wldbg_message_print(message);
    handle_pointer_message(&rm, data);
  }

  if (rm.wl_interface == &wl_keyboard_interface) {
    printf("%c: ", message->from == SERVER ? 'S' : 'C');
    wldbg_message_print(message);
    handle_keyboard_message(&rm, data);
  }

  return;
}

void handle_keyboard_message(struct wldbg_resolved_message *rm,
                             struct connection_data *data) {
  unsigned int pos = 0;
  const struct wl_message *wl_message = rm->wl_message;
  struct wldbg_resolved_arg *arg;

  /*
  printf("event/request: %s\n", rm->wl_message->name);
  while((arg = wldbg_resolved_message_next_argument(rm))) {
          assert(arg != NULL);
          printf("  pos=%u, p=%u\n", pos, arg->data ? *((uint32_t *)arg->data) :
  0);
          ++pos;
  }
  */
}

void handle_pointer_message(struct wldbg_resolved_message *rm,
                            struct connection_data *data) {
  unsigned int pos = 0;
  const struct wl_message *wl_message = rm->wl_message;
  struct wldbg_resolved_arg *arg;

  if (strcmp(wl_message->name, "motion") == 0) {
    unsigned char *addr = data_ptr(data);
    ++data->next_id;
    printf("SEND: %lu of %lu\n", data->next_id, data->events[POINTER_MOTION].kind);
    addr = vms_shm_buffer_partial_push(data->buffer, addr,
                                       &data->events[POINTER_MOTION].kind,
                                       sizeof(vms_kind));
    addr = vms_shm_buffer_partial_push(data->buffer, addr, &data->next_id,
                                       sizeof(vms_eventid));

#ifndef NDEBUG
    size_t n = 0;
#endif
    while((arg = wldbg_resolved_message_next_argument(rm))) {
      assert(n == 0 || arg->type == 'f');
      assert(n != 0 || arg->type == 'u');
      addr = vms_shm_buffer_partial_push(data->buffer, addr,
		      			 arg->data, sizeof(uint32_t));
      assert(n++ <= 3);
    }
    assert(n == 3);

    vms_shm_buffer_finish_push(data->buffer);
  }
}
