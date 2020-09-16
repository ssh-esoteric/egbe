lws.so: lws.c
	$(CC) $(PLUGIN_CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs libwebsockets json-c)
