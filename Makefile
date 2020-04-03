TARGET_MODULE := khttpd

KDIR=/lib/modules/$(shell uname -r)/build

CFLAGS_user = -std=gnu99 -Wall -Wextra -Werror
LDFLAGS_user = -lpthread
ccflags-y := -std=gnu99 -Wno-declaration-after-statement

obj-m += khttpd.o
khttpd-objs := \
	fib.o \
	http_parser.o \
	http_server.o \
	main.o		  

PORT ?= 3999

GIT_HOOKS := .git/hooks/applied
all: $(GIT_HOOKS) http_parser.c bignum.h htstress
	make -C $(KDIR) M=$(PWD) modules

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

htstress: htstress.c
	$(CC) $(CFLAGS_user) -o $@ $< $(LDFLAGS_user)

check: all
	@scripts/test.sh

load:
	sudo insmod $(TARGET_MODULE).ko port=$(PORT)
unload:
	sudo rmmod $(TARGET_MODULE) || true >/dev/null

verify: all
	$(MAKE) unload
	$(MAKE) load
	@python3 scripts/verify.py $(PORT) 1 -b base16
	@python3 scripts/verify.py $(PORT) 100 -b base16
	@python3 scripts/verify.py $(PORT) 1000 -b base16
	@python3 scripts/verify.py $(PORT) 10000 -b base16
	@python3 scripts/verify.py $(PORT) 100000 -b base16
	@python3 scripts/verify.py $(PORT) 1000000 -b base16
	$(MAKE) unload

clean:
	make -C $(KDIR) M=$(PWD) clean
	$(RM) htstress

# Download http_parser.[ch] from nodejs/http-parser repository
# the inclusion of standard header files such as <string.h> will be replaced
# with "compat/string.h", which is just a wrapper to Linux kernel headers.
# TODO: rewrite with elegant scripts
http_parser.c:
	wget -q https://raw.githubusercontent.com/nodejs/http-parser/master/http_parser.c
	@sed -i 's/#include <assert.h>/#include "compat\/assert.h"/' $@
	@sed -i 's/#include <stddef.h>/#include "compat\/stddef.h"/' $@
	@sed -i 's/#include <ctype.h>/#include "compat\/ctype.h"/' $@
	@sed -i 's/#include <string.h>/#include "compat\/string.h"/' $@
	@sed -i 's/#include <limits.h>/#include "compat\/limits.h"/' $@
	@echo "File $@ was patched."
	wget -q https://raw.githubusercontent.com/nodejs/http-parser/master/http_parser.h
	@sed -i 's/#include <stddef.h>/#include "compat\/stddef.h"/' http_parser.h
	@sed -i 's/#include <stdint.h>/#include "compat\/stdint.h"/' http_parser.h
	@echo "File http_parser.h was patched."

bignum.h:
	wget -q https://raw.githubusercontent.com/colinyoyo26/fibdrv/master/bignum.h

distclean: clean
	$(RM) http_parser.c http_parser.h bignum.h
