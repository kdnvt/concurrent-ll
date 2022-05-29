# Configurable options
# MODE = release | debug (default: release)

# Management PC specific settings
OS_NAME := $(shell uname -s)
ifeq ($(OS_NAME),Darwin) # for OS X, use build in tools
CORE_NUM := $(shell sysctl -n hw.ncpu)
else # Linux and other
CORE_NUM := $(shell nproc)
endif

ifneq ($(CORE_SPEED_KHz), )
CFLAGS += -DCORE_NUM=${CORE_NUM}
else
CFLAGS += -DCORE_NUM=4
endif
$(info *** Using as a default number of cores: $(CORE_NUM) on 1 socket)
$(info ***)

# Generic configurations
CFLAGS += --std=gnu99 -pedantic -Wall
CFLAGS += -fno-strict-aliasing
CFLAGS += -D_GNU_SOURCE
CFLAGS += -D_REENTRANT
CFLAGS += -I include
LDFLAGS += -lpthread

ifneq ($(MODE),debug)
	CFLAGS += -O3 -DNDEBUG
else
	CFLAGS += -g
endif

OUT = out
EXEC = $(OUT)/test-lock $(OUT)/test-lockfree $(OUT)/test-lockfree2
all: $(EXEC)

deps =

LOCK_OBJS =
LOCK_OBJS += src/lock/list.o
LOCK_OBJS += src/main.o
LOCK_OBJS += src/hp.o
deps += $(LOCK_OBJS:%.o=%.o.d)

$(OUT)/test-lock: $(LOCK_OBJS)
	@mkdir -p $(OUT)
	$(CC) -o $@ $^ $(LDFLAGS)
src/lock/%.o: src/lock/%.c
	$(CC) $(CFLAGS) -DLOCK_BASED -o $@ -MMD -MF $@.d -c $<

LOCKFREE_OBJS =
LOCKFREE_OBJS += src/lockfree/list.o
LOCKFREE_OBJS += src/main.o
LOCKFREE_OBJS += src/hp.o
deps += $(LOCKFREE_OBJS:%.o=%.o.d)

$(OUT)/test-lockfree: $(LOCKFREE_OBJS)
	@mkdir -p $(OUT)
	$(CC) -o $@ $^ $(LDFLAGS)
src/lockfree/%.o: src/lockfree/%.c
	$(CC) $(CFLAGS) -DLOCKFREE -o $@ -MMD -MF $@.d -c $<

LOCKFREE2_OBJS =
LOCKFREE2_OBJS += src/lockfree2/list.o
LOCKFREE2_OBJS += src/main.o
LOCKFREE2_OBJS += src/hp.o
deps += $(LOCKFREE2_OBJS:%.o=%.o.d)

$(OUT)/test-lockfree2: $(LOCKFREE2_OBJS)
	@mkdir -p $(OUT)
	$(CC) -o $@ $^ $(LDFLAGS)
src/lockfree2/%.o: src/lockfree2/%.c
	$(CC) $(CFLAGS) -DLOCKFREE2 -o $@ -MMD -MF $@.d -c $<

src/hp.o: src/hp.c
	$(CC) $(CFLAGS) -o $@ -MMD -MF $@.d -c $<

check: $(EXEC)
	bash scripts/test_correctness.sh

bench: $(EXEC)
	bash scripts/run_ll.sh
	bash scripts/create_plots_ll.sh >/dev/null
	@echo Check the plots generated in directory 'out/plots'.

bench2: $(EXEC)
	bash scripts/run_ll_2.sh
	bash scripts/create_plots_ll_2.sh >/dev/null
	@echo Check the plots generated in directory 'out/plots'.

bench3: $(EXEC)
	bash scripts/run_ll_3.sh
	bash scripts/create_plots_ll_3.sh >/dev/null
	@echo Check the plots generated in directory 'out/plots'.

clean:
	$(RM) -f $(EXEC)
	$(RM) -f $(LOCK_OBJS) $(LOCKFREE_OBJS) $(LOCK_FREE2_OBJS) $(deps)

distclean: clean
	$(RM) -rf out

.PHONY: all check clean distclean

-include $(deps)
