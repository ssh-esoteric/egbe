// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "egbe.h"
#include "common.h"
#include <curl/curl.h>
#include <sys/param.h>
#include <string.h>

#define MAX_URL 1024
#define MAX_BODY 2048

static size_t egbe_curl_initialized = 0;

struct egbe_curl_context {
	CURL *handle;

	char api_url[MAX_URL];
};

struct egbe_msg {
	long offset;
	char code;
	uint8_t xfer;
};

static int parse_msg(struct egbe_msg *msg, char *buf)
{
	int xfer;
	int rc = sscanf(buf, "%c %ld %d", &msg->code, &msg->offset, &xfer);
	msg->xfer = xfer;

	return rc;
}

static void render_msg(struct egbe_msg *msg, char *buf)
{
	sprintf(buf, "%c %ld %d", msg->code, msg->offset, msg->xfer);
}

static bool check_for_disconnect(struct egbe_gameboy *self, struct egbe_msg *msg)
{
	if (msg->code != 'd')
		return false;

	GBLOG("Curl client disconnected");
	self->status = EGBE_LINK_DISCONNECTED;
	self->start = 0;
	self->till = self->gb->cycles;

	self->gb->on_serial_start.callback = NULL;
	self->gb->on_serial_start.context = NULL;

	return true;
}

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *context)
{
	size *= nmemb;
	size = MIN(size, MAX_BODY - 1);
	strncpy(context, contents, size);

	return size;
}

static long perform_curl(struct egbe_curl_context *cc,
                         enum egbe_link_status status,
                         struct egbe_msg *req, struct egbe_msg *rsp)
{
	char buf[MAX_BODY];

	if (req) {
		render_msg(req, buf);
		curl_easy_setopt(cc->handle, CURLOPT_CUSTOMREQUEST, "PUT");
		curl_easy_setopt(cc->handle, CURLOPT_POSTFIELDS, buf);
	} else {
		curl_easy_setopt(cc->handle, CURLOPT_CUSTOMREQUEST, "GET");
		curl_easy_setopt(cc->handle, CURLOPT_POSTFIELDS, "");
	}

	curl_easy_setopt(cc->handle, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(cc->handle, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(cc->handle, CURLOPT_WRITEDATA, buf);

	struct curl_slist *headers = NULL;
	switch (status) {
	case EGBE_LINK_DISCONNECTED:
		headers = curl_slist_append(headers, "Accept: application/prs.egbe.msg-v0.sync");
		break;
	case EGBE_LINK_GUEST:
		headers = curl_slist_append(headers, "Accept: application/prs.egbe.msg-v0.guest");
		break;
	case EGBE_LINK_HOST:
		headers = curl_slist_append(headers, "Accept: application/prs.egbe.msg-v0.host");
		break;
	}
	curl_easy_setopt(cc->handle, CURLOPT_HTTPHEADER, headers);

	CURLcode rc = curl_easy_perform(cc->handle);
	if (rc) {
		GBLOG("Error making curl request: %s", curl_easy_strerror(rc));
		strcpy(buf, "d 0 0");
	}
	curl_slist_free_all(headers);

	long code;
	curl_easy_getinfo(cc->handle, CURLINFO_RESPONSE_CODE, &code);

	if (rsp)
		parse_msg(rsp, buf);

	return rc ? -1 : code;
}

static void tick_curl(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->context;
	struct egbe_msg req, rsp;

	switch (self->status) {
	case EGBE_LINK_HOST:
		self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);

		req.code = self->xfer_pending ? 'x' : 'i';
		req.offset = self->till - self->start;
		req.xfer = self->gb->sb;

		perform_curl(cc, self->status, &req, &rsp);
		if (check_for_disconnect(self, &rsp))
			return;

		if (self->xfer_pending) {
			perform_curl(cc, self->status, NULL, &rsp);
			if (check_for_disconnect(self, &rsp))
				return;

			gameboy_start_serial(self->gb, rsp.xfer);

			self->xfer_pending = false;
		}
		break;

	case EGBE_LINK_GUEST:
		perform_curl(cc, self->status, NULL, &rsp);
		if (check_for_disconnect(self, &rsp))
			return;

		self->till = rsp.offset + self->start;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);

		if (rsp.code == 'x') {
			gameboy_start_serial(self->gb, rsp.xfer);

			req.code = 'x';
			req.offset = rsp.offset;
			req.xfer = self->gb->sb;

			perform_curl(cc, self->status, &req, &rsp);
			if (check_for_disconnect(self, &rsp))
				return;
		}
		break;

	case EGBE_LINK_DISCONNECTED:
	default:
		self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
		while (self->gb->cycles < self->till)
			gameboy_tick(self->gb);
		break;
	}
}

static void interrupt_serial_curl(struct gameboy *gb, void *context)
{
	struct egbe_gameboy *self = context;

	self->till = gb->cycles;
	self->xfer_pending = true;
}

static int connect_curl(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->context;

	if (self->status) {
		GBLOG("GB already registered as a %s",
		      self->status == EGBE_LINK_HOST ? "host" : "guest");
		return 0;
	}

	struct egbe_msg req, rsp;

	// TODO: Use actual randomness; this is probably good enough for now
	long registration_code = (long)self->gb ^ (long)cc;

	curl_easy_setopt(cc->handle, CURLOPT_URL, cc->api_url);

	req.code = 'c';
	req.offset = registration_code;
	req.xfer = 0;

	perform_curl(cc, self->status, &req, &rsp);
	if (check_for_disconnect(self, &rsp))
		return 1;

	if (rsp.code != 'c') {
		GBLOG("Bad response code while registering GB: %c", rsp.code);
		return 1;
	}

	if (rsp.offset == registration_code) {
		GBLOG("Registered as a host");

		self->status = EGBE_LINK_HOST;
		self->start = self->gb->cycles;
		self->till = self->gb->cycles;

		self->gb->on_serial_start.callback = interrupt_serial_curl;
		self->gb->on_serial_start.context = self;

	} else {
		GBLOG("Registered as a guest");

		self->status = EGBE_LINK_GUEST;
		self->start = self->gb->cycles;
		self->till = self->gb->cycles;
	}

	return 0;
}

static void cleanup_curl(struct egbe_gameboy *self)
{
	struct egbe_curl_context *cc = self->context;

	if (cc->handle) {
		curl_easy_cleanup(cc->handle);
		cc->handle = NULL;
	}

	if (egbe_curl_initialized && !--egbe_curl_initialized)
		curl_global_cleanup();
}

int egbe_gameboy_init_curl(struct egbe_gameboy *self, char *api_url)
{
	if (!api_url) {
		GBLOG("API URL is required to initialize curl handler");
		return EINVAL;
	}

	if (!egbe_curl_initialized++) {
		if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
			egbe_curl_initialized = 0;
			GBLOG("Failed to initialize curl");
			return 1;
		}
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

	self->context = cc;

	self->cleanup = cleanup_curl;
	self->connect = connect_curl;
	self->tick = tick_curl;

	return 0;
}
