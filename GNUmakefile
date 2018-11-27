INSTALL_DIR := /home/joseph.rahmeh/bin

# We use boost 1.67.
BOOST_DIR := /wdc/apps/utilities/boost-1.67
BOOST_INC := $(BOOST_DIR)/include

# These boost libraries must be compiled with g++ -std=gnu++14
BOOST_LIB_DIR := $(BOOST_DIR)/lib
BOOST_LIBS := $(BOOST_LIB_DIR)/libboost_program_options.a
BOOST_LIBS += $(BOOST_LIB_DIR)/libboost_system.a

OFLAGS := -O3

IFLAGS := -I$(BOOST_INC) -I.

# Command to compile .cpp files.
CPPC := $(CXX) -std=c++17 $(OFLAGS) $(IFLAGS)

# Rule to make a .o from a .cpp file.
%.o:  %.cpp
	$(CPPC) -pedantic -Wall -c -o $@ $<

# Rule to make a .o from a .c file.
%.o:  %.c
	$(CPPC) -Wall -c -o $@ $<

whisper: whisper.o linenoise.o librvcore.a
	$(CPPC) -o $@ $^ $(BOOST_LIBS) -lpthread

RISCV := IntRegs.o CsRegs.o instforms.o Memory.o Core.o InstInfo.o \
	 Triggers.o PerfRegs.o gdb.o CoreConfig.o

librvcore.a: $(RISCV)
	ar r $@ $^

install: whisper
	cp $^ $(INSTALL_DIR)

clean:
	$(RM) whisper $(RISCV) librvcore.a whisper.o linenoise.o

extraclean: clean
	$(RM) *.d

help:
	@echo Possible targets: whisper install clean extraclean
	@echo To compile for debug: make OFLAGS=-g

.PHONY: install clean extraclean help

# Rule for generating dependency files
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

CPP_SOURCES := $(RISCV:.o=.cpp) whisper.cpp
C_SOURCES := linenoise.c

include $(CPP_SOURCES:.cpp=.d) $(C_SOURCES:.c=.d)
