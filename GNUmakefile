OFLAGS := -O3
IFLAGS := -I.

%.o:  %.cpp
	$(CXX) -std=gnu++14 $(OFLAGS) $(IFLAGS) -c -o $@ $^

OBJ := CsRegs.o Core.o sim.o Inst.o Memory.o

sim: $(OBJ)
	$(CXX) -std=gnu++14 $(OFLAGS) -o $@ $^

gen16codes: gen16codes.o CsRegs.o Core.o Inst.o Memory.o
	$(CXX) -std=gnu++14 $(OFLAGS) -o $@ $^

gen16codesb: gen16codesb.o CsRegs.o Core.o Inst.o Memory.o
	$(CXX) -std=gnu++14 $(OFLAGS) -o $@ $^

clean:
	$(RM) sim gen16codes $(OBJ) gen16codes.o
