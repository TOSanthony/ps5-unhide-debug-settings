ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

# Needs sqlite3.c + sqlite3.h (SQLite "amalgamation") in this folder.
ELF := unhide_debug.elf
CFLAGS := -O2 -Wall -I. \
	-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_OMIT_SHARED_CACHE -DSQLITE_TEMP_STORE=3

all: $(ELF)

$(ELF): main.c sqlite3.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)
