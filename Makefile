ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

# Needs sqlite3.c + sqlite3.h (SQLite "amalgamation") in this folder.
ELFS := dashboard.elf
CFLAGS := -O2 -Wall -I. \
	-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_OMIT_SHARED_CACHE -DSQLITE_TEMP_STORE=3

all: $(ELFS)

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) -c -o $@ sqlite3.c

dashboard.elf: dashboard/dashboard.c dashboard/dashboard_page.h dashboard/hub_patch_files.h appdb.h sqlite3.o
	$(CC) $(CFLAGS) -Idashboard -o $@ dashboard/dashboard.c sqlite3.o

clean:
	rm -f $(ELFS) sqlite3.o
