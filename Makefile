CFLAGS  += -std=c99 -Wall -g -D_REENTRANT
LIBS    := -lpthread -lm -lssl -lcrypto

TARGET  := $(shell uname -s | tr '[A-Z]' '[a-z]' 2>/dev/null || echo unknown)

ifeq ($(TARGET), sunos)
	CFLAGS += -D_PTHREADS -D_POSIX_C_SOURCE=200112L
	LIBS   += -lsocket
else ifeq ($(TARGET), darwin)
	LDFLAGS += -pagezero_size 10000 -image_base 100000000
else ifeq ($(TARGET), linux)
	CFLAGS  += -D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE -D_DEFAULT_SOURCE
	LIBS    += -ldl
	LDFLAGS += -Wl,-E
else ifeq ($(TARGET), freebsd)
	CFLAGS  += -D_DECLARE_C99_LDBL_MATH
	LDFLAGS += -Wl,-E
endif

SRC  := wrk.c net.c ssl.c aprintf.c stats.c script.c units.c \
		ae.c zmalloc.c http_parser.c server_perf.c
BIN  := visual-wrk
VER  ?= $(shell git describe --tags --always --dirty)

ODIR := obj
OBJ  := $(patsubst %.c,$(ODIR)/%.o,$(SRC)) $(ODIR)/bytecode.o $(ODIR)/version.o
LIBS := -lluajit-5.1 $(LIBS)
LIBS := -ljansson $(LIBS)
LIBS := -lcurl $(LIBS)

DEPS    :=
CFLAGS  += -I$(ODIR)/include
LDFLAGS += -L$(ODIR)/lib

ifneq ($(WITH_LUAJIT),)
	CFLAGS  += -I$(WITH_LUAJIT)/include
	LDFLAGS += -L$(WITH_LUAJIT)/lib
else
	CFLAGS  += -I$(ODIR)/include/luajit-2.1
	DEPS    += $(ODIR)/lib/libluajit-5.1.a
endif

ifneq ($(WITH_OPENSSL),)
	CFLAGS  += -I$(WITH_OPENSSL)/include
	LDFLAGS += -L$(WITH_OPENSSL)/lib
else
	DEPS += $(ODIR)/lib/libssl.a
endif

ifneq ($(WITH_JANSSON),)
	CFLAGS  += -I$(WITH_JANSSON)/include
	LDFLAGS += -L$(WITH_JANSSON)/lib
else
	CFLAGS  += -I$(ODIR)/include/
	DEPS += $(ODIR)/lib/libjansson.a
endif

ifneq ($(WITH_CURL),)
	CFLAGS  += -I$(WITH_CURL)/include
	LDFLAGS += -L$(WITH_CURL)/lib
else
	CFLAGS  += -I$(ODIR)/include/
	DEPS += $(ODIR)/lib/libcurl.a
endif

ifeq ($(PREFIX),)
	INSTALL_PREFIX = /usr/local
endif

all: $(BIN)

clean:
	$(RM) -rf $(BIN) obj/*

install: ${BIN}
	install -d ${INSTALL_PREFIX}/bin
	install -m 755 ${BIN} ${INSTALL_PREFIX}/bin/
	install -d ${INSTALL_PREFIX}/lib/visual_wrk/
	install -m 755 lib/* ${INSTALL_PREFIX}/lib/visual_wrk/
	cp template/ ${INSTALL_PREFIX}/lib/visual_wrk/ -R
	chmod 755 ${INSTALL_PREFIX}/lib/visual_wrk/template -R

uninstall:
	rm ${INSTALL_PREFIX}/bin/${BIN} ${INSTALL_PREFIX}/lib/visual_wrk/ -rf

$(BIN): $(OBJ)
	@echo LINK $(BIN)
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJ): config.h Makefile $(DEPS) | $(ODIR)

$(ODIR):
	@mkdir -p $@

$(ODIR)/bytecode.o: src/wrk.lua
	@echo LUAJIT $<
	@$(SHELL) -c 'PATH=obj/bin:$(PATH) luajit -b $(CURDIR)/$< $(CURDIR)/$@'

$(ODIR)/version.o:
	@echo 'const char *VERSION="$(VER)";' | $(CC) -xc -c -o $@ -

$(ODIR)/%.o : %.c
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<

# Dependencies

LUAJIT  := $(notdir $(patsubst %.tar.gz,%,$(wildcard deps/LuaJIT*.tar.gz)))
OPENSSL := $(notdir $(patsubst %.tar.gz,%,$(wildcard deps/openssl*.tar.gz)))
JANSSON := $(notdir $(patsubst %.tar.gz,%,$(wildcard deps/jansson*.tar.gz)))
CURL := $(notdir $(patsubst %.tar.gz,%,$(wildcard deps/curl*.tar.gz)))

OPENSSL_OPTS = no-shared no-psk no-srp no-dtls no-idea --prefix=$(abspath $(ODIR))

$(ODIR)/$(LUAJIT):  deps/$(LUAJIT).tar.gz  | $(ODIR)
	@tar -C $(ODIR) -xf $<

$(ODIR)/$(OPENSSL): deps/$(OPENSSL).tar.gz | $(ODIR)
	@tar -C $(ODIR) -xf $<

$(ODIR)/$(CURL): deps/$(CURL).tar.gz | $(ODIR)
	@tar -C $(ODIR) -xf $<

$(ODIR)/$(JANSSON): deps/$(JANSSON).tar.gz | $(ODIR)
	@tar -C $(ODIR) -xf $<

$(ODIR)/lib/libcurl.a: $(ODIR)/$(CURL)
	@echo Building curl...
ifeq ($(TARGET), darwin)
	@$(SHELL) -c "cd $< && ./Configure darwin64-x86_64-cc"
else
	@$(SHELL) -c "cd $< && ./configure --prefix=$(abspath $(ODIR))"
endif
	@$(MAKE) -C $< BUILDMODE=static install
	
$(ODIR)/lib/libjansson.a: $(ODIR)/$(JANSSON)
	@echo Building Jansson...
ifeq ($(TARGET), darwin)
	@$(SHELL) -c "cd $< && ./Configure darwin64-x86_64-cc"
else
	@$(SHELL) -c "cd $< && ./configure --prefix=$(abspath $(ODIR))"
endif
	@$(MAKE) -C $< BUILDMODE=static install
	@cd $(ODIR)/lib && rm libjansson*.so

$(ODIR)/lib/libluajit-5.1.a: $(ODIR)/$(LUAJIT)
	@echo Building LuaJIT...
	@$(MAKE) -C $< PREFIX=$(abspath $(ODIR)) BUILDMODE=static install
	@cd $(ODIR)/bin && ln -s luajit-2.1.0-beta3 luajit

$(ODIR)/lib/libssl.a: $(ODIR)/$(OPENSSL)
	@echo Building OpenSSL...
ifeq ($(TARGET), darwin)
	@$(SHELL) -c "cd $< && ./Configure $(OPENSSL_OPTS) darwin64-x86_64-cc"
else
	@$(SHELL) -c "cd $< && ./config $(OPENSSL_OPTS)"
endif
	@$(MAKE) -C $< depend
	@$(MAKE) -C $<
	@$(MAKE) -C $< install_sw
	@touch $@

# ------------

.PHONY: all clean
.PHONY: $(ODIR)/version.o

.SUFFIXES:
.SUFFIXES: .c .o .lua

vpath %.c   src
vpath %.h   src
vpath %.lua scripts


