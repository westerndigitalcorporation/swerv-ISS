OFLAGS := -O3
BOOST_INC := /wdc/apps/utilities/boost-1.67/include
IFLAGS := -I$(BOOST_INC) -I/home/joseph.rahmeh/local/include

# These boost libraries are compiled stih g++ -std=gnu++14
#BOOST_LIB_DIR := /home/joseph.rahmeh/local/lib
BOOST_LIB_DIR := /wdc/apps/utilities/boost-1.67/lib
BOOST_OPTS := $(BOOST_LIB_DIR)/libboost_program_options.a
BOOST_SYS := $(BOOST_LIB_DIR)/libboost_system.a

# Command to compile .cpp files.
CPPC := $(CXX) $(OFLAGS) $(IFLAGS)

%.o:  %.cpp
	$(CPPC) -pedantic -Wall -c -o $@ $^

# Command to compile .c files.
%.o:  %.c
	$(CC) -Wall -c -o $@ $^


RISCV := IntRegs.o CsRegs.o instforms.o Memory.o Core.o InstInfo.o

whisper: whisper.o linenoise.o librvcore.a
	$(CPPC) -o $@ $^ $(BOOST_OPTS) $(BOOST_SYS) -lpthread

gen16codes: gen16codes.o librvcore.a
	$(CPPC) -o $@ $^ $(BOOST_OPTS)

trace-compare: trace-compare.o
	$(CPPC) -o $@ $^ $(BOOST_OPTS)

adjust-spike-log: adjust-spike-log.o
	$(CPPC) -o $@ $^ $(BOOST_OPTS)

librvcore.a: $(RISCV)
	ar r $@ $^

all: whisper gen16codes trace-compare adjust-spike-log

RELEASE_DIR := /home/joseph.rahmeh/bin

release: all
	cp whisper gen16codes trace-compare adjust-spike-log $(RELEASE_DIR)

clean:
	$(RM) whisper gen16codes trace-compare $(RISCV) librvcore.a \
	whisper.o linenoise.o \
	gen16codes.o \
	trace-compare.o \
	adjust-spike-log.o

.PHONY: all clean release

