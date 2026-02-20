# AlphaBoy - Go for Game Boy (DMG-01)
# Build system using GBDK-2020

# ---- Platform detection ----
ifeq ($(OS),Windows_NT)
    MKDIR  = cmd /c mkdir
    RMDIR  = cmd /c rmdir /S /Q
else
    MKDIR  = mkdir -p
    RMDIR  = rm -rf
endif

# ---- Paths ----
GBDK_HOME  = gbdk_release/
LCC        = $(GBDK_HOME)bin/lcc
PNG2ASSET  = $(GBDK_HOME)bin/png2asset

# ---- Project ----
PROJECTNAME = alphaboy

SRCDIR   = src
RESDIR   = res

# Debug is the default; pass RELEASE=1 for an optimized build.
ifdef RELEASE
    VARIANT  = release
else
    VARIANT  = debug
endif

OBJDIR   = obj/$(VARIANT)
BUILDDIR = build/$(VARIANT)

BINS       = $(BUILDDIR)/$(PROJECTNAME).gb

CSOURCES   = $(foreach dir,$(SRCDIR),$(notdir $(wildcard $(dir)/*.c)))
RESSOURCES = $(foreach dir,$(RESDIR),$(notdir $(wildcard $(dir)/*.c)))
ASMSOURCES = $(foreach dir,$(SRCDIR),$(notdir $(wildcard $(dir)/*.s)))

OBJS  = $(CSOURCES:%.c=$(OBJDIR)/%.o)
OBJS += $(RESSOURCES:%.c=$(OBJDIR)/%.o)
OBJS += $(ASMSOURCES:%.s=$(OBJDIR)/%.o)

# ---- Flags ----
# MBC5 + RAM + Battery (cart type 0x1B), 1 RAM bank (8 KB)
LCCFLAGS = -Wm-yt0x1B -Wm-ya1

ifdef RELEASE
	LCCFLAGS += -DNDEBUG
else
	LCCFLAGS += -debug -v
endif

# ---- Targets ----

all: dirs $(BINS)

# Link object files into the final ROM
$(BINS): $(OBJS)
	$(LCC) $(LCCFLAGS) -o $@ $^

# Compile src/*.c
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(LCC) $(LCCFLAGS) -c -o $@ $<

# Compile res/*.c
$(OBJDIR)/%.o: $(RESDIR)/%.c
	$(LCC) $(LCCFLAGS) -c -o $@ $<

# Compile src/*.s
$(OBJDIR)/%.o: $(SRCDIR)/%.s
	$(LCC) $(LCCFLAGS) -c -o $@ $<

# ---- Asset conversion ----

assets: $(RESDIR)/tiles.c

$(RESDIR)/tiles.c: assets/tiles.png
	$(PNG2ASSET) $< -o $@ -map -keep_palette_order -noflip

# ---- Utility targets ----

dirs:
ifeq ($(OS),Windows_NT)
	-$(MKDIR) $(subst /,\,$(OBJDIR))
	-$(MKDIR) $(subst /,\,$(BUILDDIR))
else
	-$(MKDIR) $(OBJDIR)
	-$(MKDIR) $(BUILDDIR)
endif

# ---- Emulator ----
ifeq ($(OS),Windows_NT)
    EMULATOR = Emulicious-with-Java64\Emulicious.exe
else
    EMULATOR = Emulicious-with-Java64/Emulicious.exe
endif

EMUFLAGS = -set WindowDebuggerOpen=true -set DebuggerSuspendOnOpen=false

run: all
ifeq ($(OS),Windows_NT)
	$(EMULATOR) $(EMUFLAGS) $(subst /,\,$(BINS))
else
	$(EMULATOR) $(EMUFLAGS) $(BINS)
endif

# ---- Formatting ----

format:
	clang-format -i $(wildcard $(SRCDIR)/*.c $(SRCDIR)/*.h $(RESDIR)/*.c $(RESDIR)/*.h)

clean:
	-$(RMDIR) obj
	-$(RMDIR) build

.PHONY: all dirs assets run format clean
