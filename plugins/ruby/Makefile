# Ruby has non-strict prototypes in its headers
PLUGIN_CFLAGS += -Wno-strict-prototypes

ruby.so: ruby.c
	$(CC) $(PLUGIN_CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs ruby)
