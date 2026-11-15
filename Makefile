ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

# Needs sqlite3.c + sqlite3.h (SQLite "amalgamation") in this folder.
ELFS := welcome_hub.elf toggle_store.elf icon_menu.elf dashboard.elf pad_probe.elf pad_rpc.elf
CFLAGS := -O2 -Wall -I. \
	-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_OMIT_SHARED_CACHE -DSQLITE_TEMP_STORE=3

all: $(ELFS)

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) -c -o $@ sqlite3.c

welcome_hub.elf: welcome_hub.c appdb.h sqlite3.o
	$(CC) $(CFLAGS) -o $@ welcome_hub.c sqlite3.o

toggle_store.elf: toggle_store.c appdb.h sqlite3.o
	$(CC) $(CFLAGS) -o $@ toggle_store.c sqlite3.o

icon_menu.elf: icon_menu.c appdb.h sqlite3.o
	$(CC) $(CFLAGS) -o $@ icon_menu.c sqlite3.o

dashboard.elf: dashboard/dashboard.c dashboard/dashboard_page.h dashboard/hub_patch_files.h appdb.h sqlite3.o
	$(CC) $(CFLAGS) -Idashboard -o $@ dashboard/dashboard.c sqlite3.o

pad_probe.elf: pad_probe.c
	$(CC) $(CFLAGS) -o $@ pad_probe.c -lScePad -lSceUserService

pad_rpc.elf: pad_rpc/pad_rpc.c pad_rpc/pt/pt.c
	$(CC) -Wall -O2 -Ipad_rpc -o $@ pad_rpc/pad_rpc.c pad_rpc/pt/pt.c -lSceNotification

clean:
	rm -f $(ELFS) sqlite3.o
