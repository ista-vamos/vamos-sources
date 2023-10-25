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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <wayland-client-protocol.h>

#include <wldbg.h>
#include "wldbg-pass.h"
#include "wldbg-parse-message.h"

static void
send_event(struct wldbg_message *message);

static void
handle_keyboard_message(struct wldbg_resolved_message *rm);

static void
handle_pointer_message(struct wldbg_resolved_message *rm);

struct vamos_data {
	unsigned int incoming_number;
	unsigned int outcoming_number;
	unsigned int in_trasfered;
	unsigned int out_trasfered;
};

static int
vamos_in(void *user_data, struct wldbg_message *message)
{
	struct vamos_data *data = user_data;

	data->incoming_number++;
	data->in_trasfered += message->size;

	/*
	printf("GOT incoming message: %u, size: %lu bytes\n",
	       data->incoming_number, message->size);
	       */

	send_event(message);

	return PASS_NEXT;
}

static int
vamos_out(void *user_data, struct wldbg_message *message)
{
	struct vamos_data *data = user_data;

	data->outcoming_number++;
	data->out_trasfered += message->size;

	/*
	printf("GOT outcoming message: %u, size: %lu bytes\n",
	       data->outcoming_number, message->size);
	       */

	send_event(message);

	return PASS_NEXT;
}

static void
vamos_help(void *user_data)
{
	(void) user_data;
}

static int
vamos_init(struct wldbg *wldbg, struct wldbg_pass *pass,
	     int argc, const char *argv[])
{
	int i;
	struct vamos_data *data = calloc(1, sizeof *data);
	if (!data)
		return -1;

	(void) wldbg;

	printf("-- Initializing VAMOS pass --\n\n");
	for (i = 0; i < argc; ++i)
		printf("\targument[%d]: %s\n", i, argv[i]);

	printf("\n\n");

	pass->user_data = data;

	return 0;
}

static void
vamos_destroy(void *user_data)
{
	struct vamos_data *data = user_data;

	printf(" -- Destroying vamos pass --\n\n");
	printf("Totally trasfered %u bytes from client to server\n"
	       "and %u bytes from server to client\n",
	       data->out_trasfered, data->in_trasfered);

	free(data);
}

struct wldbg_pass wldbg_pass = {
	.init = vamos_init,
	.destroy = vamos_destroy,
	.server_pass = vamos_in,
	.client_pass = vamos_out,
	.help = vamos_help,
	.description = "Wldbg pass that forwards data into VAMOS"
};

void
send_event(struct wldbg_message *message)
{
	int is_buggy = 0;
	uint32_t pos;
	struct wldbg_connection *conn = message->connection;
	struct wldbg_resolved_message rm;

	if (!wldbg_resolve_message(message, &rm)) {
		fprintf(stderr, "Failed resolving message, event lost...\n");
		return;
	}

	if (rm.wl_interface == &wl_pointer_interface) {
		printf("%c: ", message->from == SERVER ? 'S' : 'C');
		wldbg_message_print(message);
		handle_pointer_message(&rm);
	}

	if (rm.wl_interface == &wl_keyboard_interface) {
		printf("%c: ", message->from == SERVER ? 'S' : 'C');
		wldbg_message_print(message);
		handle_keyboard_message(&rm);
	}

	return;

}

void
handle_keyboard_message(struct wldbg_resolved_message *rm)
{
	unsigned int pos = 0;
	const struct wl_message *wl_message = rm->wl_message;
	struct wldbg_resolved_arg *arg;

	printf("event/request: %s\n", rm->wl_message->name);
	while((arg = wldbg_resolved_message_next_argument(rm))) {
		assert(arg != NULL);
		printf("  pos=%u, p=%u\n", pos, arg->data ? *((uint32_t *)arg->data) : 0);
		++pos;
	}
}

void
handle_pointer_message(struct wldbg_resolved_message *rm)
{
	unsigned int pos = 0;
	const struct wl_message *wl_message = rm->wl_message;
	struct wldbg_resolved_arg *arg;

	printf("event/request: %s\n", rm->wl_message->name);
	while((arg = wldbg_resolved_message_next_argument(rm))) {
		assert(arg != NULL);
		printf("  pos=%u, p=%u\n", pos, arg->data ? *((uint32_t *)arg->data) : 0);
		++pos;
	}
}
