INSTALL_DIR := .

# We use boost 1.67.
BOOST_DIR := /wdc/apps/utilities/boost-1.67
BOOST_INC := $(BOOST_DIR)/include

# These boost libraries must be compiled with: "g++ -std=c++14" or "g++ -std=c++17"
BOOST_LIB_DIR := $(BOOST_DIR)/lib
BOOST_LIBS := $(BOOST_LIB_DIR)/libboost_program_options.a
BOOST_LIBS += $(BOOST_LIB_DIR)/libboost_system.a

# Optimization flags.  Use -g for debug.
OFLAGS := -O3

# Include paths.
IFLAGS := -I$(BOOST_INC) -I.

# Command to compile .cpp files.
CPPC := $(CXX) -std=c++17 $(OFLAGS) $(IFLAGS)

# Rule to make a .o from a .cpp file.
%.o:  %.cpp
	$(CPPC) -pedantic -Wall -c -o $@ $<

# Rule to make a .o from a .c file.
%.o:  %.c
	$(CPPC) -Wall -c -o $@ $<

# Main target.
whisper: whisper.o linenoise.o librvcore.a
	$(CPPC) -o $@ $^ $(BOOST_LIBS) -lpthread

# Object files needed for librvcore.a
OBJS := IntRegs.o CsRegs.o instforms.o Memory.o Core.o InstInfo.o \
	 Triggers.o PerfRegs.o gdb.o CoreConfig.o Server.o

librvcore.a: $(OBJS)
	ar r $@ $^

install: whisper
	@if test "." -ef "$(INSTALL_DIR)" -o "" == "$(INSTALL_DIR)" ; \
         then echo "INSTALL_DIR is not set or is same as current dir" ; \
         else echo cp $^ $(INSTALL_DIR); cp $^ $(INSTALL_DIR); \
         fi

clean:
	$(RM) whisper $(OBJS) librvcore.a whisper.o linenoise.o

extraclean: clean
	$(RM) *.d

help:
	@echo "Possible targets: whisper install clean extraclean"
	@echo "To compile for debug: make OFLAGS=-g"
	@echo "To install: make INSTALL_DIR=<target> install"

.PHONY: install clean extraclean help

# The rest of the files is for automatically generating/maintaining
# dependencies.
%.d: %.cpp
	@set -e; rm -f $@; \
	 $(CPPC) -M $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

%.d: %.c
	@set -e; rm -f $@; \
	 $(CC) -M $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

CPP_SOURCES := $(OBJS:.o=.cpp) whisper.cpp
C_SOURCES := linenoise.c

include $(CPP_SOURCES:.cpp=.d) $(C_SOURCES:.c=.d)
