.PHONY: all install clean distclean

ifeq ($(PREFIX_DIR),)
PREFIX_DIR=~/.local
endif

cf_common:=-std=c99 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-parameter -Wno-unused-function -ffunction-sections -fdata-sections 
cf_release:=-Ofast -fno-stack-protector -fomit-frame-pointer -DNDEBUG
cf_checked:=-Ofast -fomit-frame-pointer -DNDEBUG -DO71_CHECKED
cf_debug:=-O0 -D_DEBUG
all: o71 o71c o71d

distclean: clean

clean:
	rm -f o71 o71c o71d

install: all
	install -t $(PREFIX_DIR)/bin o71 o71c o71d

o71: o71.c o71.h
	gcc -o$@ $(cf_common) $(cf_release) -DO71_STATIC -DO71_MAIN $<
	strip $@

o71c: o71.c o71.h
	gcc -o$@ $(cf_common) $(cf_checked) -DO71_STATIC -DO71_MAIN $<
	strip $@

o71d: o71.c o71.h
	gcc -o$@ $(cf_common) $(cf_debug) -DO71_STATIC -DO71_MAIN $<

