# Diagnostic smbman IOP module (vendored PS2SDK smbman + mass:/SMBDEBUG.LOG probe).
# Built locally and bin2s-embedded in place of the stock SDK smbman.irx.

IOP_BIN = smbman_dbg.irx
IOP_OBJS = smbman.o smb_fio.o smb.o poll.o auth.o des.o md4.o smbdbg.o imports.o

IOP_CFLAGS += -Wall

# The vendored smbman uses 64-bit division in FileTimeToDate() (time/10000000),
# which compiles to a call to __udivdi3. The stock SDK smbman Makefile links
# libgcc for exactly this reason; without it the IRX link fails with
# "undefined reference to __udivdi3".
IOP_LIBS += -lgcc

# Headers pulled in by the vendored sources and by irx_imports.h.
IOP_IMPORT_INCS += \
	system/intrman \
	system/ioman \
	system/sifman \
	system/stdio \
	system/sysclib \
	system/threadman \
	tcpip/tcpip

IOP_INCS += -I.

all: $(IOP_BIN)

clean:
	-rm -f $(IOP_OBJS) $(IOP_BIN) $(IOP_BIN:.irx=.notiopmod.elf) $(IOP_BIN:.irx=.notiopmod.stripped.elf)

include $(PS2SDK)/Defs.make
include ../Rules.make
