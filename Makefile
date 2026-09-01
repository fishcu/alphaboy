# AlphaBoy - Go for Game Boy (DMG-01)
# Build system using GBDK-2020

# ---- Platform detection ----
ifeq ($(OS),Windows_NT)
    MKDIR  = cmd /c if not exist
    RMDIR  = cmd /c if exist
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

SRCDIR     = src
INCDIR     = include
RESDIR     = res
PROFILEDIR = examples/profile

# ---- Build configuration ----
# BUILD = debug          (default) debug symbols, verbose, no optimisation
# BUILD = release        optimised, no debug symbols
# BUILD = relwithdebinfo optimised + debug symbols (best for profiling)
# BUILD = profile        relwithdebinfo + demo replay (used by `make flamegraph`)
BUILD ?= debug

OBJDIR   = obj/$(BUILD)
BUILDDIR = build/$(BUILD)

BINS       = $(BUILDDIR)/$(PROJECTNAME).gb

CSOURCES   = $(foreach dir,$(SRCDIR),$(notdir $(wildcard $(dir)/*.c)))
RESSOURCES = $(foreach dir,$(RESDIR),$(notdir $(wildcard $(dir)/*.c)))
ASMSOURCES = $(foreach dir,$(SRCDIR),$(notdir $(wildcard $(dir)/*.s)))

ifeq ($(BUILD),profile)
    PROFILESOURCES = $(notdir $(wildcard $(PROFILEDIR)/*.c))
    CSOURCES := $(filter-out main.c,$(CSOURCES))
endif

OBJS  = $(CSOURCES:%.c=$(OBJDIR)/%.o)
OBJS += $(RESSOURCES:%.c=$(OBJDIR)/%.o)
OBJS += $(ASMSOURCES:%.s=$(OBJDIR)/%.o)
ifeq ($(BUILD),profile)
    OBJS += $(PROFILESOURCES:%.c=$(OBJDIR)/%.o)
endif

# ---- Flags ----
# MBC5 + RAM + Battery (cart type 0x1B), 1 RAM bank (8 KB)
LCCFLAGS = -Wm-yt0x1B -Wm-ya1 -I$(INCDIR) -I$(RESDIR)

ifeq ($(BUILD),release)
	LCCFLAGS += -DNDEBUG -Wf--opt-code-speed -Wf--max-allocs-per-node50000
else ifeq ($(BUILD),relwithdebinfo)
	LCCFLAGS += -debug -DNDEBUG -Wf--opt-code-speed -Wf--max-allocs-per-node50000
else ifeq ($(BUILD),profile)
	LCCFLAGS += -debug -DNDEBUG -Wf--opt-code-speed -Wf--max-allocs-per-node50000
else
	LCCFLAGS += -debug -v
endif

# VBlank must meet a hardware deadline in every configuration.  Keep its
# implementation and direct helpers optimized even in the debug build.
ifeq ($(BUILD),debug)
    REALTIME_OBJS = $(OBJDIR)/cursor.o $(OBJDIR)/input.o $(OBJDIR)/interrupts.o
    $(REALTIME_OBJS): LCCFLAGS += -Wf--opt-code-speed -Wf--max-allocs-per-node50000
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

# Compile examples/profile/*.c (profile build only)
$(OBJDIR)/%.o: $(PROFILEDIR)/%.c
	$(LCC) $(LCCFLAGS) -c -o $@ $<

# ---- Asset conversion ----

assets: $(RESDIR)/tiles.c

$(RESDIR)/tiles.c: assets/tiles.png
	$(PNG2ASSET) $< -o $@ -map -keep_palette_order -noflip

# ---- Utility targets ----

dirs:
ifeq ($(OS),Windows_NT)
	@$(MKDIR) "$(subst /,\,$(OBJDIR))" mkdir "$(subst /,\,$(OBJDIR))"
	@$(MKDIR) "$(subst /,\,$(BUILDDIR))" mkdir "$(subst /,\,$(BUILDDIR))"
else
	-$(MKDIR) $(OBJDIR)
	-$(MKDIR) $(BUILDDIR)
endif

# ---- Emulator ----
# EMU = mesen        (default) Mesen emulator
# EMU = emulicious   Emulicious with debugger/profiler windows
# EMU = bgb          BGB emulator
# Override on the command line: make run EMU=bgb
# Or set EMULATOR / EMUFLAGS directly for unlisted emulators.
EMU ?= mesen

ifeq ($(EMU),mesen)
    EMULATOR ?= Mesen_2.1.1_Windows\Mesen.exe
    EMUFLAGS ?=

else ifeq ($(EMU),emulicious)
    EMULATOR ?= Emulicious-with-Java64\Emulicious.exe
    EMUFLAGS ?= -set WindowDebuggerOpen=true -set DebuggerSuspendOnOpen=false \
                -set WindowProfilerWindowOpen=true -set WindowProfilerWindowProcedureProfiler=true

else ifeq ($(EMU),bgb)
    EMULATOR ?= bgbw64\bgb64.exe
    EMUFLAGS ?=

else
    $(error Unknown EMU value '$(EMU)'. Use mesen, emulicious, or bgb (or set EMULATOR directly))
endif

run: all
ifeq ($(OS),Windows_NT)
	$(EMULATOR) $(EMUFLAGS) $(subst /,\,$(BINS))
else
	$(EMULATOR) $(EMUFLAGS) $(BINS)
endif

# ---- Flamegraph profiling ----
# Builds the profile ROM, runs gb-flamegraph, outputs to build/flamegraph/.
# The replay plays and then undoes one move per frame, blocking as needed.
# The default window records the complete 318-move round trip.
# FLAME_START = first frame to record
# FLAME_FRAMES = number of frames to record

GB_FLAMEGRAPH_PACKAGE = https://github.com/chrismaltby/gb-flamegraph.git
GB_FLAMEGRAPH = npx --yes --package=$(GB_FLAMEGRAPH_PACKAGE) \
	node --stack-size=32768 $(PROFILEDIR)/run_flamegraph.js
FLAMEGRAPH_DIR = build/flamegraph
PROFILE_ROM    = build/profile/$(PROJECTNAME).gb
FLAME_START   ?= 0
FLAME_FRAMES  ?= 845

flamegraph:
	$(MAKE) BUILD=profile all
	$(GB_FLAMEGRAPH) -r $(PROFILE_ROM) \
		-s $(FLAME_START) -f $(FLAME_FRAMES) -c all -e $(FLAMEGRAPH_DIR)
	@echo Flamegraph written to $(FLAMEGRAPH_DIR)/
	@echo Open $(FLAMEGRAPH_DIR)/index.html in a browser.

# ---- Formatting ----

format:
	clang-format -i $(wildcard $(SRCDIR)/*.c $(INCDIR)/*.h $(RESDIR)/*.c $(RESDIR)/*.h examples/*/*.c examples/*/*.h)

clean:
ifeq ($(OS),Windows_NT)
	@$(RMDIR) "obj" rmdir /S /Q "obj"
	@$(RMDIR) "build" rmdir /S /Q "build"
else
	-$(RMDIR) obj
	-$(RMDIR) build
endif

.PHONY: all dirs assets run format flamegraph clean
