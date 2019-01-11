INSTALL_DIR := .

PROJECT := whisper

# For Static Linking to Boost Library
# STATIC_LINK := 1
# For Dynamic linking to Boost Library change the following line to
# STATIC_LINK := 0
# or
# make STATIC_LINK=0
STATIC_LINK := 1

# For non-default compiler toolchain uncomment and change the following variables
#CC := gcc-8
#CXX := g++-8
#AR := gcc-ar-8
# Or run make with these options
# $ make CC=gcc-8 CXX=g++-8 AR=gcc-ar-8

# We use boost 1.67.
# Set the BOOST_ROOT environment variable to point to the base install
# location of the Boost Libraries
BOOST_DIR := $(BOOST_ROOT)
# For Various Installation types of Boost Library
BOOST_INC := $(BOOST_DIR) $(BOOST_DIR)/include

# These boost libraries must be compiled with: "g++ -std=c++14" or "g++ -std=c++17"
# For Various Installation types of Boost Library
BOOST_LIB_DIR := $(BOOST_DIR)/stage/lib $(BOOST_DIR)/lib
# Specify only the basename of the Boost libraries
BOOST_LIBS := boost_program_options \
              boost_system

# Add extra dependency libraries here
EXTRA_LIBS := -lpthread

# Add External Library location paths here
LINK_DIRS := $(addprefix -L,$(BOOST_LIB_DIR))

# Generating the Linker options for dependent libraries
ifeq ($(STATIC_LINK), 1)
  LINK_LIBS := $(addprefix -l:lib, $(addsuffix .a, $(BOOST_LIBS))) $(EXTRA_LIBS)
else
  COMMA := ,
  LINK_DIRS += $(addprefix -Wl$(COMMA)-rpath=, $(BOOST_LIB_DIR))
  LINK_LIBS := $(addprefix -l, $(BOOST_LIBS)) $(EXTRA_LIBS)
endif

# Optimization flags.  Use -g for debug.
OFLAGS := -O3

# Include paths.
IFLAGS := $(addprefix -I,$(BOOST_INC)) -I.

# Command to compile .cpp files.
override CXXFLAGS += -MMD -MP -std=c++17 $(OFLAGS) $(IFLAGS) -pedantic -Wall
# Command to compile .c files
override CFLAGS += -MMD -MP $(OFLAGS) $(IFLAGS) -pedantic -Wall

# Rule to make a .o from a .cpp file.
%.o:  %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Rule to make a .o from a .c file.
%.o:  %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Main target.(only linking)
$(PROJECT): whisper.o linenoise.o librvcore.a
	$(CXX) -o $@ $^ $(LINK_DIRS) $(LINK_LIBS)

# List of All CPP Sources for the project
SRCS_CXX := whisper.cpp IntRegs.cpp CsRegs.cpp instforms.cpp Memory.cpp Core.cpp InstInfo.cpp \
	Triggers.cpp PerfRegs.cpp gdb.cpp CoreConfig.cpp
# List of All C Sources for the project
SRCS_C := linenoise.c

# List of all object files for the project
OBJS_GEN := $(SRCS_CXX:.cpp=.o) $(SRCS_C:.c=.o)
# List of all auto-genreated dependency files.
DEPS_FILES := $(OBJS_GEN:.o=.d)

# Include Generated Dependency files if available.
-include $(DEPS_FILES)

# Object files needed for librvcore.a
OBJS := IntRegs.o CsRegs.o instforms.o Memory.o Core.o InstInfo.o \
	 Triggers.o PerfRegs.o gdb.o CoreConfig.o

librvcore.a: $(OBJS)
	$(AR) cr $@ $^

install: $(PROJECT)
	@if test "." -ef "$(INSTALL_DIR)" -o "" == "$(INSTALL_DIR)" ; \
         then echo "INSTALL_DIR is not set or is same as current dir" ; \
         else echo cp $^ $(INSTALL_DIR); cp $^ $(INSTALL_DIR); \
         fi

clean:
	$(RM) $(PROJECT) $(OBJS_GEN) librvcore.a $(DEPS_FILES)

help:
	@echo "Possible targets: $(PROJECT) install clean"
	@echo "To compile for debug: make OFLAGS=-g"
	@echo "To install: make INSTALL_DIR=<target> install"

.PHONY: install clean help

