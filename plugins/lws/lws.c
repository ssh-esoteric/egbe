// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "egbe_plugin_api.h"
#include <json-c/json.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/random.h>

#define MAX_URL 1024
#define MAX_BODY 2048

struct link_peer {
	long id;
	long cycles;
	int serial;
	int infrared;
};

struct link_state {
	struct link_peer host;
	struct link_peer guest;
};

struct egbe_lws_context {
	struct lws_context *context;
	struct lws *wsi;

	struct json_object *id_obj;
	const char *id_str;

	long registration_code;
	struct link_state link_state;

	char *api_proto;
	char *api_host;
	char *api_origin;
	char *api_path;
	int api_port;

	char buf[MAX_BODY];
	size_t len;
};

static void interrupt_serial(struct gameboy *gb, void *context)
{
	struct egbe_gameboy *self = context;

	self->till = gb->cycles;
	self->xfer_pending = true;
}

static int parse_json_into_state(struct json_object *body, struct link_state *state)
{
	struct json_object *host = json_object_object_get(body, "host");
	if (!json_object_is_type(host, json_type_object)) {
		GBLOG("Couldn't find host in response");
		return 1;
	}

	struct json_object *guest = json_object_object_get(body, "guest");
	if (!json_object_is_type(guest, json_type_object)) {
		GBLOG("Couldn't find guest in response");
		return 1;
	}

	state->host.id       = json_object_get_int64(json_object_object_get(host, "id"));
	state->host.cycles   = json_object_get_int64(json_object_object_get(host, "cycles"));
	state->host.serial   = json_object_get_int64(json_object_object_get(host, "serial"));
	state->host.infrared = json_object_get_int64(json_object_object_get(host, "infrared"));

	state->guest.id       = json_object_get_int64(json_object_object_get(guest, "id"));
	state->guest.cycles   = json_object_get_int64(json_object_object_get(guest, "cycles"));
	state->guest.serial   = json_object_get_int64(json_object_object_get(guest, "serial"));
	state->guest.infrared = json_object_get_int64(json_object_object_get(guest, "infrared"));

	return 0;
}

static void update_link_status(struct egbe_gameboy *self)
{
	struct egbe_lws_context *cc = self->link_context;
	struct link_peer *host = &cc->link_state.host;
	struct link_peer *guest = &cc->link_state.guest;

	if (self->link_status == EGBE_LINK_DISCONNECTED) {
		if (cc->registration_code == host->id) {
			self->link_status |= EGBE_LINK_HOST;
			GBLOG("Registered as HOST");

			self->gb->on_serial_start.callback = interrupt_serial;
			self->gb->on_serial_start.context = self;
		}
		else if (cc->registration_code == guest->id) {
			self->link_status |= EGBE_LINK_GUEST;
			GBLOG("Registered as GUEST");
		}
	}

	if (self->link_status & EGBE_LINK_HOST) {
		if (host->cycles <= guest->cycles) {
			self->link_status &= ~EGBE_LINK_WAITING;

			if (guest->serial >= 0)
				gameboy_start_serial(self->gb, guest->serial);

		} else {
			self->link_status |= EGBE_LINK_WAITING;
		}
	} else {
		if (host->cycles > guest->cycles) {
			self->link_status &= ~EGBE_LINK_WAITING;
		} else {
			self->link_status |= EGBE_LINK_WAITING;
		}
	}
}

static void cb_protocol_init(struct egbe_gameboy *self)
{
	struct egbe_lws_context *wsc = self->link_context;

	struct lws_client_connect_info i = {
		.context = wsc->context,
		.port = wsc->api_port,
		.address = wsc->api_host,
		.path = wsc->api_path,
		.host = wsc->api_host,
		.origin = wsc->api_origin,
		.ssl_connection = 0,

		.protocol = "egbe-link-hub-protocol",
	};
	if (strcmp(wsc->api_proto, "https") == 0)
		i.ssl_connection = LCCSCF_USE_SSL;

	wsc->wsi = lws_client_connect_via_info(&i);
	if (!wsc->wsi) {
		GBLOG("Failed to connect to LWS client");
	}
}

static void cb_client_receive(struct egbe_gameboy *self, void *in, size_t len)
{
	if (!in || !len) {
		GBLOG("Received without a body..?");
		return;
	}

	struct egbe_lws_context *wsc = self->link_context;
	struct json_object *body = json_tokener_parse(in);

	const char *blob = json_object_to_json_string_ext(body, JSON_C_TO_STRING_PRETTY);

	struct json_object *message_obj = json_object_object_get(body, "message");

	struct json_object *type_obj = json_object_object_get(body, "type");
	const char *type = json_object_get_string(type_obj);

	if (!type_obj) {
		const char *message = json_object_get_string(message_obj);
		struct json_object *embedded = json_tokener_parse(message);

		parse_json_into_state(embedded, &wsc->link_state);
		update_link_status(self);

		json_object_put(embedded);
		return;
	}
	else if (strcmp("ping", type) == 0) {
		int64_t ts = json_object_get_int64(message_obj);

		// GBLOG("Ping: %ld", ts);
	}
	else if (strcmp("welcome", type) == 0) {
		GBLOG("Welcomed.  Attempting to subscribe %ld", wsc->registration_code);

		struct json_object *token = json_object_new_object();
		json_object_object_add(token, "channel", json_object_new_string("LinkChannel"));
		json_object_object_add(token, "id", json_object_new_int64(wsc->registration_code));

		wsc->id_obj = token;
		wsc->id_str = json_object_to_json_string(token);

		struct json_object *sub = json_object_new_object();
		json_object_object_add(sub, "command", json_object_new_string("subscribe"));
		json_object_object_add(sub, "identifier", json_object_new_string(wsc->id_str));
		const char *msg = json_object_to_json_string_length(sub, 0, &wsc->len);

		strncpy(wsc->buf + LWS_PRE, msg, wsc->len);
		lws_callback_on_writable(wsc->wsi);

		json_object_put(body);
	}
	else if (strcmp("confirm_subscription", type) == 0) {
		GBLOG("Subscription confirmed");

		self->start = self->gb->cycles;
		self->till = self->gb->cycles;
		self->link_status &= ~EGBE_LINK_WAITING;
	}
	else if (strcmp("reject_subscription", type) == 0) {
		GBLOG("Subscription rejected (link is full?)");
	}
}

static void cb_client_writeable(struct egbe_gameboy *self)
{
	struct egbe_lws_context *wsc = self->link_context;

	char *buf = wsc->buf + LWS_PRE;
	if (*buf)
		lws_write(wsc->wsi, (uint8_t *)buf, wsc->len, LWS_WRITE_TEXT);

	*buf = '\0';
}

static int cb_protocol(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
	struct lws_context *ctx = lws_get_context(wsi);
	struct egbe_gameboy *self = lws_context_user(ctx);

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		cb_protocol_init(self);
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
		cb_client_receive(self, in, len);
		break;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
		cb_client_writeable(self);
		break;

	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols protocols[] = {
	{ .name = "egbe-link-hub-protocol", .callback = cb_protocol, },
	{ 0 },
};

static int link_update_self(struct egbe_gameboy *self)
{
	struct egbe_lws_context *wsc = self->link_context;

	struct json_object *req = json_object_new_object();
	json_object_object_add(req, "command", json_object_new_string("message"));
	json_object_object_add(req, "identifier", json_object_new_string(wsc->id_str));

	struct json_object *body = json_object_new_object();
	struct json_object *peer = json_object_new_object();

	char *key = (self->link_status & EGBE_LINK_HOST) ? "host" : "guest";
	json_object_object_add(body, key, peer);

	json_object_object_add(peer, "id",     json_object_new_int64(wsc->registration_code));
	json_object_object_add(peer, "cycles", json_object_new_int64(self->till - self->start));
	json_object_object_add(peer, "serial",   json_object_new_int64(self->xfer_pending ? self->gb->sb : -1));
	// TODO
	json_object_object_add(peer, "infrared", json_object_new_int64(0));

	if (self->xfer_pending) {
		// GBLOG("SENT %02X @%ld", self->gb->sb, self->till - self->start);
		self->xfer_pending = false;
	}

	const char *blob = json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN);
	json_object_object_add(req, "data", json_object_new_string(blob));

	const char *msg = json_object_to_json_string_length(req, JSON_C_TO_STRING_PLAIN, &wsc->len);

	char *buf = wsc->buf + LWS_PRE;
	if (*buf) {
		GBLOG("Already queued a message...");
	} else {
		strncpy(wsc->buf + LWS_PRE, msg, wsc->len);
		lws_callback_on_writable(wsc->wsi);
	}

	json_object_put(body);
	json_object_put(req);

	return 0;
}

static void tick_lws(struct egbe_gameboy *self)
{
	struct egbe_lws_context *wsc = self->link_context;
	struct link_peer *host = &wsc->link_state.host;
	struct link_peer *guest = &wsc->link_state.guest;

	lws_service(wsc->context, 4);
	if (self->link_status & EGBE_LINK_WAITING)
		return;

	switch (self->link_status & EGBE_LINK_MASK) {
	case EGBE_LINK_HOST | EGBE_LINK_WAITING:
		; // fallthrough
	case EGBE_LINK_HOST:
		self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);

		link_update_self(self);
		self->link_status |= EGBE_LINK_WAITING;
		self->xfer_pending = false;

		break;

	case EGBE_LINK_GUEST | EGBE_LINK_WAITING:
		; // fallthrough
	case EGBE_LINK_GUEST:
		self->till = host->cycles + self->start;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);

		self->xfer_pending = (host->serial >= 0);
		if (self->xfer_pending)
			gameboy_start_serial(self->gb, host->serial);

		link_update_self(self);
		self->link_status |= EGBE_LINK_WAITING;
		self->xfer_pending = false;

		break;

	default:
		GBLOG("Bad link_status: %d", self->link_status);
		self->link_status = EGBE_LINK_DISCONNECTED;
		; // Fallthrough
	case EGBE_LINK_DISCONNECTED:
		self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);
		break;
	}
}

static int connect_lws(struct egbe_gameboy *self)
{
	struct egbe_lws_context *wsc = self->link_context;

	lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, NULL);

	struct lws_context_creation_info info = {
		.port = CONTEXT_PORT_NO_LISTEN,
		.protocols = protocols,
		.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT
		         | LWS_SERVER_OPTION_JUST_USE_RAW_ORIGIN
		         | LWS_SERVER_OPTION_VALIDATE_UTF8,
		.user = self,
	};

	wsc->context = lws_create_context(&info);
	if (!wsc->context) {
		GBLOG("Failed to create LWS context");
		return 1;
	}

	return 0;
}

static void cleanup_lws(struct egbe_gameboy *self)
{
	struct egbe_lws_context *wsc = self->link_context;

	free(wsc->api_proto);
	free(wsc->api_host);
	free(wsc->api_path);
	free(wsc->api_origin);

	lws_context_destroy(wsc->context);
}

static int start_link_client(struct egbe_gameboy *self, char *api_url)
{
	int rc;

	if (!api_url) {
		GBLOG("API URL is required to initialize LWS handler");
		return EINVAL;
	}

	struct egbe_lws_context *wsc = calloc(1, sizeof(*wsc));
	if (!wsc) {
		GBLOG("Failed to allocate LWS data");
		return ENOMEM;
	}

	// Prepare a duplicate; lws_parse_uri will mangle the original string
	char buf[MAX_URL] = { 0 };
	strncpy(buf, api_url, sizeof(buf));

	const char *proto, *host, *path;
	rc = lws_parse_uri(buf, &proto, &host, &wsc->api_port, &path);
	if (rc) {
		GBLOG("Failed to parse API URL: %d", rc);
		return EINVAL;
	}
	size_t protolen, hostlen, pathlen;
	protolen = strlen(proto);
	hostlen = strlen(host);
	pathlen = strlen(path);

	wsc->api_proto = strdup(proto);
	wsc->api_host = strdup(host);

	// The first '/' in the path is mangled to tokenize the host; restore it
	wsc->api_path = calloc(1, 1 + pathlen + 1);
	strcat(wsc->api_path, "/");
	strcat(wsc->api_path + 1, path);

	// As is the '://' for the protocol
	wsc->api_origin = calloc(1, protolen + 3 + hostlen + 1);
	strcat(wsc->api_origin, proto);
	strcat(wsc->api_origin + protolen, "://");
	strcat(wsc->api_origin + protolen + 3, host);

	rc = getrandom(&wsc->registration_code, sizeof(wsc->registration_code), GRND_NONBLOCK);
	if (rc == sizeof(wsc->registration_code)) {
		// Positive IDs for simplicity
		if (wsc->registration_code < 0)
			wsc->registration_code *= -1;
	} else {
		GBLOG("Error creating random ID");
	}

	self->link_context = wsc;

	self->link_cleanup = cleanup_lws;
	self->link_connect = connect_lws;
	self->tick = tick_lws;

	return 0;
}

PLUGIN_NAME("lws");
PLUGIN_DESCRIPTION("Uses libwebsockets to communicate with the EGBE Link Hub HTTP+WebSocket spec");
PLUGIN_WEBSITE("https://github.com/ssh-esoteric/egbe");

PLUGIN_AUTHOR("EGBE");
PLUGIN_VERSION("0.0.1");

PLUGIN_START_LINK_CLIENT(start_link_client);
