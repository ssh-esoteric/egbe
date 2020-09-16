// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "egbe_plugin_api.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <string.h>
#include <sys/param.h>
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

struct egbe_curl_context {
	CURL *handle;
	char api_url[MAX_URL];

	long registration_code;
	struct link_state link_state;
};

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

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *context)
{
	size *= nmemb;
	size = MIN(size, MAX_BODY - 1);
	strncpy(context, contents, size);

	return size;
}

static int http_request(struct egbe_gameboy *self, const char *req_body)
{
	struct egbe_curl_context *cc = self->link_context;
	char buf[MAX_BODY] = { 0 };

	curl_easy_setopt(cc->handle, CURLOPT_URL, cc->api_url);

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Accept: application/json");

	if (req_body) {
		curl_easy_setopt(cc->handle, CURLOPT_CUSTOMREQUEST, "PATCH");
		curl_easy_setopt(cc->handle, CURLOPT_POSTFIELDS, req_body);

		headers = curl_slist_append(headers, "Content-Type: application/json");
	} else {
		curl_easy_setopt(cc->handle, CURLOPT_CUSTOMREQUEST, "GET");
		curl_easy_setopt(cc->handle, CURLOPT_POSTFIELDS, "");
	}

	curl_easy_setopt(cc->handle, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(cc->handle, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(cc->handle, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(cc->handle, CURLOPT_WRITEDATA, buf);

	CURLcode rc = curl_easy_perform(cc->handle);
	if (rc) {
		GBLOG("Error making curl request: %s", curl_easy_strerror(rc));
	}
	curl_slist_free_all(headers);

	long code;
	curl_easy_getinfo(cc->handle, CURLINFO_RESPONSE_CODE, &code);

	if (code >= 200 && code <= 299) {
		struct json_object *rsp_body = json_tokener_parse(buf);
		if (rsp_body) {
			parse_json_into_state(rsp_body, &cc->link_state);
			json_object_put(rsp_body);
		}

		return 0;
	} else {
		GBLOG("Unexpected HTTP response code: %ld", code);
		return 1;
	}
}

static int api_register(struct egbe_gameboy *self, int link_flags)
{
	struct egbe_curl_context *cc = self->link_context;
	long my_id = cc->registration_code;

	int rc = http_request(self, NULL);
	if (rc)
		return rc;

	link_flags &= (EGBE_LINK_HOST | EGBE_LINK_GUEST);

	if (cc->link_state.host.id)
		link_flags &= ~EGBE_LINK_HOST;

	if (cc->link_state.guest.id)
		link_flags &= ~EGBE_LINK_GUEST;

	if (!link_flags) {
		GBLOG("Failed to register: Link is already full");
		return 1;
	}

	struct json_object *body = json_object_new_object();

	// Note: The server API allows us to send a PATCH with both host and
	//       guest fields populated when registering, which indicates that
	//       we're fine being either peer.
	if (link_flags & EGBE_LINK_HOST) {
		struct json_object *peer = json_object_new_object();
		json_object_object_add(peer, "id", json_object_new_int64(my_id));

		json_object_object_add(body, "host", peer);
	}
	if (link_flags & EGBE_LINK_GUEST) {
		struct json_object *peer = json_object_new_object();
		json_object_object_add(peer, "id", json_object_new_int64(my_id));

		json_object_object_add(body, "guest", peer);
	}

	const char *json = json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN);
	http_request(self, json);
	json_object_put(body);

	if (link_flags & EGBE_LINK_HOST && cc->link_state.host.id == my_id) {
		GBLOG("Registered as a host");

		self->link_status = EGBE_LINK_HOST;
		return 0;
	}
	else if (link_flags & EGBE_LINK_GUEST && cc->link_state.guest.id == my_id) {
		GBLOG("Registered as a guest");

		self->link_status = EGBE_LINK_GUEST;
		return 0;
	}
	else {
		GBLOG("Failed to register");
		return 1;
	}
}

static void interrupt_serial_curl(struct gameboy *gb, void *context)
{
	struct egbe_gameboy *self = context;

	self->till = gb->cycles;
	self->xfer_pending = true;
}

static int link_connect(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->link_context;

	// TODO: The registration type could be configurable
	int rc = api_register(self, EGBE_LINK_HOST | EGBE_LINK_GUEST);
	if (rc)
		return rc;

	self->start = self->gb->cycles;
	self->till = self->gb->cycles;

	return 0;
}

static void link_cleanup(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->link_context;

	if (cc->handle) {
		curl_easy_cleanup(cc->handle);
		cc->handle = NULL;
	}
}

static void update_link_status(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->link_context;
	struct link_peer *host = &cc->link_state.host;
	struct link_peer *guest = &cc->link_state.guest;

	if (self->link_status & EGBE_LINK_HOST) {
		if (host->cycles <= guest->cycles) {
			self->link_status &= ~EGBE_LINK_WAITING;
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

static void link_update_self(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->link_context;

	struct json_object *body = json_object_new_object();
	struct json_object *peer = json_object_new_object();

	char *key = (self->link_status & EGBE_LINK_HOST) ? "host" : "guest";
	json_object_object_add(body, key, peer);

	json_object_object_add(peer, "id", json_object_new_int64(cc->registration_code));
	json_object_object_add(peer, "cycles", json_object_new_int64(self->till - self->start));
	json_object_object_add(peer, "serial", json_object_new_int64(self->xfer_pending ? self->gb->sb : -1));

	// TODO
	json_object_object_add(peer, "infrared", json_object_new_int64(0));

	const char *json = json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN);
	http_request(self, json);
	json_object_put(body);
}

static void link_read_self(struct egbe_gameboy *self)
{
	http_request(self, NULL);
}

static void link_tick(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->link_context;
	struct link_peer *host = &cc->link_state.host;
	struct link_peer *guest = &cc->link_state.guest;

	switch (self->link_status) {
	case EGBE_LINK_HOST | EGBE_LINK_WAITING:
		link_read_self(self);
		update_link_status(self);

		if (self->link_status & EGBE_LINK_WAITING)
			break;

		if (guest->serial >= 0)
			gameboy_start_serial(self->gb, guest->serial);

		; // fallthrough
	case EGBE_LINK_HOST:
		// TODO: Try using guest cycles instead of GB cycles?
		self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);

		link_update_self(self);
		update_link_status(self);
		self->xfer_pending = false;

		// TODO: Refactor; duplicated above
		if (self->link_status & EGBE_LINK_WAITING)
			break;
		if (guest->serial >= 0)
			gameboy_start_serial(self->gb, guest->serial);

		break;

	case EGBE_LINK_GUEST | EGBE_LINK_WAITING:
		link_read_self(self);
		update_link_status(self);

		if (self->link_status & EGBE_LINK_WAITING)
			break;

		; // fallthrough
	case EGBE_LINK_GUEST:
		self->till = host->cycles + self->start;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);

		self->xfer_pending = (host->serial >= 0);
		if (self->xfer_pending)
			gameboy_start_serial(self->gb, host->serial);

		link_update_self(self);
		update_link_status(self);
		self->xfer_pending = false;
		break;

	default:
		GBLOG("Unknown link status: %d", self->link_status);
		self->link_status = EGBE_LINK_DISCONNECTED;
		; // fallthrough
	case EGBE_LINK_DISCONNECTED:
		self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);
	}
}

static int start_link_client(struct egbe_gameboy *self, char *api_url)
{
	if (!api_url) {
		GBLOG("API URL is required to initialize curl handler");
		return EINVAL;
	}

	size_t len = strnlen(api_url, MAX_URL);
	if (len >= MAX_URL) {
		GBLOG("API URL is too large: %ld bytes", len);
		return EINVAL;
	}

	struct egbe_curl_context *cc = calloc(1, sizeof(*cc));
	if (!cc) {
		GBLOG("Failed to allocate curl context");
		return ENOMEM;
	}
	strncpy(cc->api_url, api_url, len);

	cc->handle = curl_easy_init();
	if (!cc->handle) {
		GBLOG("Failed to allocate curl handle");
		return 1;
	}

	int rc = getrandom(&cc->registration_code, sizeof(cc->registration_code), GRND_NONBLOCK);
	if (rc == sizeof(cc->registration_code)) {
		// Positive IDs for simplicity
		if (cc->registration_code < 0)
			cc->registration_code *= -1;
	} else {
		GBLOG("Error creating random ID");
	}

	self->link_context = cc;
	self->link_cleanup = link_cleanup;
	self->link_connect = link_connect;
	self->tick = link_tick;

	self->gb->on_serial_start.callback = interrupt_serial_curl;
	self->gb->on_serial_start.context = self;

	return 0;
}

static int plugin_init(struct egbe_application *app, struct egbe_plugin *plugin)
{
	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
		GBLOG("Failed to initialize curl");
		return 1;
	}

	return 0;
}

static void plugin_exit(struct egbe_application *app, struct egbe_plugin *plugin)
{
	curl_global_cleanup();
}

PLUGIN_NAME("curl");
PLUGIN_DESCRIPTION("Uses cURL to communicate with the EGBE Link Hub HTTP spec");
PLUGIN_WEBSITE("https://github.com/ssh-esoteric/egbe");

PLUGIN_AUTHOR("EGBE");
PLUGIN_VERSION("0.0.1");

PLUGIN_INIT(plugin_init);
PLUGIN_EXIT(plugin_exit);

PLUGIN_START_LINK_CLIENT(start_link_client);
