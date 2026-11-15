ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

# Needs sqlite3.c + sqlite3.h (SQLite "amalgamation") in this folder.
# vda_probe.elf is a temporary read-only diagnostic (see vda-probe/vda_probe.c); remove it
# once the Ghostpad VDA-signature/Mbus-NID investigation is done.
ELFS := dashboard.elf vda_probe.elf
CFLAGS := -O2 -Wall -I. \
	-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
	-DSQLITE_OMIT_SHARED_CACHE -DSQLITE_TEMP_STORE=3

all: $(ELFS)

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) -c -o $@ sqlite3.c

dashboard.elf: dashboard/dashboard.c dashboard/dashboard_page.h dashboard/hub_patch_files.h dashboard/wh_inspector_files.h appdb.h sqlite3.o
	$(CC) $(CFLAGS) -Idashboard -o $@ dashboard/dashboard.c sqlite3.o

vda_probe.elf: vda-probe/vda_probe.c vda-probe/pt/pt.c vda-probe/pt/pt.h vda-probe/pt/log.h
	$(CC) -Wall -O2 -Ivda-probe -o $@ vda-probe/vda_probe.c vda-probe/pt/pt.c

clean:
	rm -f $(ELFS) sqlite3.o
