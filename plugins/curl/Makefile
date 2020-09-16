curl.so: curl.c
	$(CC) $(PLUGIN_CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs libcurl json-c)
