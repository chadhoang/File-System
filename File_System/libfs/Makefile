# Target library
lib 	:= libfs.a
objs	:= disk.o fs.o

CC		:=	gcc
CFLAGS	:=	-Wall -Wextra -Werror -MMD
CFLAGS	+=	-g
ARRCS	:=	ar rcs

ifneq ($(V),1)
Q = @
endif
 
all: $(lib)

deps	:=$(patsubst %.o, %.d, $(objs))
-include $(deps)

libfs.a: $(objs)
	@echo "ARRCS $@"
	$(Q)$(ARRCS) $@ $^

%.o: %.c
	@echo "CC $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(Q)rm -f $(lib) $(objs) $(deps)

## TODO: Phase 1
