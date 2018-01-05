OFLAGS := -O3
IFLAGS := -I/usr/local/include
LFLAGS := /usr/local/lib/libboost_program_options.a

COMP := $(CXX) -std=gnu++14 $(OFLAGS) $(IFLAGS)

%.o:  %.cpp
	$(COMP) -c -o $@ $^

OBJ := CsRegs.o Core.o sim.o Inst.o Memory.o

sim: $(OBJ)
	$(COMP) -o $@ $^ $(LFLAGS)

gen16codes: gen16codes.o CsRegs.o Core.o Inst.o Memory.o
	$(COMP) -o $@ $^ $(LFLAGS)

clean:
	$(RM) sim gen16codes $(OBJ) gen16codes.o
