# directory containing the source files
SRCDIR := src
# directory to output the object files
OBJDIR := obj
# directory to output the executable
OUTDIR := .

SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

# name of the executable to output
BIN := emulator
ifeq ($(OS),Windows_NT)
    BIN := $(BIN).exe
endif

TARGET := $(OUTDIR)/$(BIN)

# set up some vars for utils (some of these are defined already by make)
CC ?= gcc
LD := $(CC)
_CC := $(TOOLCHAIN)$(CC)
_LD := $(TOOLCHAIN)$(LD)

# flags for the C compiler (change the -std=... if you want)
CFLAGS += -std=c11 -Wall -Wextra -Wuninitialized -Wundef
# flags for the C preprocessor (put things like -DMYMACRO=... here)
CPPFLAGS += -D_DEFAULT_SOURCE
# flags for the linker
LDFLAGS += 
# libraries to link to (put things like -lmylib here)
LDLIBS += 

# add some more flags depending on if a debug build is wanted or not
ifeq ($(DEBUG),y)
    CFLAGS += -g -Og -fsanitize=address -Wdouble-promotion
    #CFLAGS += -Wconversion
    LDFLAGS += -fsanitize=address
else
    CFLAGS += -O2
    CPPFLAGS += -DNDEBUG
endif

# the rest of this file is for compiling and building the executable

# enables some GNU make specific features to help with some things
.SECONDEXPANSION:

# file management helper funcs
define mkdir
if [ ! -d '$(1)' ]; then echo 'Creating $(1)/...'; mkdir -p '$(1)'; fi; true
endef
define rm
if [ -f '$(1)' ]; then echo 'Removing $(1)/...'; rm -f '$(1)'; fi; true
endef
define rmdir
if [ -d '$(1)' ]; then echo 'Removing $(1)/...'; rm -rf '$(1)'; fi; true
endef

# recompile a .c file if a .h it includes changes
deps.filter := %.c %.h
deps.option := -MM
define deps
$$(filter $$(deps.filter),,$$(shell $(_CC) $(CFLAGS) $(CPPFLAGS) -E $(deps.option) $(1)))
endef

# default rule to run
default: build

# create the output dir if it doesn't exist
$(OUTDIR):
	@$(call mkdir,$@)

# create the object dir if it doesn't exist
$(OBJDIR):
	@$(call mkdir,$@)

# compile the .c files into .o object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(call deps,$(SRCDIR)/%.c) | $(OBJDIR) $(OUTDIR)
	@echo Compiling $<...
	@$(_CC) $(CFLAGS) -Wall -Wextra -I$(PSRCDIR) -DPSRC_REUSABLE $(CPPFLAGS) $< -c -o $@
	@echo Compiled $<

# link the .o object files into an executable
$(TARGET): $(OBJECTS) | $(OUTDIR)
	@echo Linking $@...
	@$(_LD) $(LDFLAGS) $^ $(LDLIBS) -o $@
	@echo Linked $@

# phony rule as a shortcut to build the executable
build: $(TARGET)
	@:

# phony rule to clean up the object files
clean:
	@$(call rmdir,$(OBJDIR))

# phony rule to clean up the object files and executable
distclean: clean
	@$(call rm,$(TARGET))

# specify the phony rules (this means they don't correlate to actual files like real rules)
.PHONY: build clean distclean
