.SUFFIXES: .o .c
#
# Makefile for PC Xeros to compile on a PC running Linux.
#

CCPREFIX =


# Things that need not be changed, usually
OS      = LINUX
DEFS	= -DBSDURG  -DVERBOSE -DPRINTERR
INCLUDE = -I../h
CFLAGS	= -Wall -Wstrict-prototypes -fno-builtin -c  ${DEFS} ${INCLUDE}
SDEFS	= -D${OS} -I../h -DLOCORE -DSTANDALONE -DAT386
LIB     = ../lib
AS      = $(CCPREFIX)as --32
AR	= $(CCPREFIX)ar
XEROS   = ./xeros
GCC     = $(CCPREFIX)gcc -m32 -march=i386 -std=gnu99
CC      = $(GCC) -D__KERNEL__ -D__ASSEMBLY__
LD      = $(CCPREFIX)ld -m elf_i386
CPP     = $(CC) -E
AWK     = awk


# Use the following line if you want to boot Xeros from floppy diskette
#
BRELOC = 0x100000
TEXTSPOT= 0x000000
BOOTPLOC= 0x150000

# Ignore these
#BRELOC = 0x150000
#TEXTSPOT= 0x150000

# Linker line, do not modify this, please.
LDSTR	= -e start -Ttext ${TEXTSPOT}


#Do NOT modify these lines
SOBJ = startup.o intr.o
IOBJ = init.o i386.o evec.o kprintf.o
UOBJ = mem.o disp.o ctsw.o syscall.o create.o user.o msg.o sleep.o signal.o

#Add your sources here
MY_OBJ = util.o queue.o deltalist.o di_calls.o kbd.o
MY_TEST = memtest.o queuetest.o createtest.o syscalltest.o deltalisttest.o msgtest.o preemptiontest.o signaltest.o devicetest.o


# Don't modify any of this unless you are really sure
all: xeros

xeros: Makefile ${SOBJ} ${IOBJ} ${UOBJ} ${MY_OBJ} ${MY_TEST} ${LIB}/libxc.a
	$(LD) ${LDSTR} ${SOBJ} ${IOBJ} ${UOBJ} ${MY_OBJ} ${MY_TEST} ${LIB}/libxc.a -o ${XEROS}

clean:
	rm -rf *.o *.bak *.a core errs ${XEROS} ${XEROS}.boot

cleanall:
	rm -rf *.o *.bak *.a core errs ${XEROS} ${XEROS}.boot
	(cd ${LIB}/libxc; make clean)

${LIB}/libxc.a:
	(cd ${LIB}/libxc; make install)

intr.o: ../c/intr.S ../c/xint.s
	${CPP} ${SDEFS} ../c/intr.S | ${AS} -o intr.o

startup.o: ../c/startup.S Makefile
	${CPP} ${SDEFS} -DBRELOC=${BRELOC} -DBOOTPLOC=${BOOTPLOC} -DLINUX_XINU ../c/startup.S | ${AS} -o startup.o

${IOBJ}:
	${CC} ${CFLAGS} ../c/`basename $@ .o`.[c]

${UOBJ}:
	${CC} ${CFLAGS} ../c/`basename $@ .o`.[c]

${MY_OBJ}:
	${CC} ${CFLAGS} ../c/`basename $@ .o`.[c]

${MY_TEST}:
	${CC} ${CFLAGS} ../c/test/`basename $@ .o`.[c]

init.o: ../c/init.c ../h/i386.h ../h/xeroskernel.h ../h/xeroslib.h
i386.o: ../c/i386.c ../h/i386.h ../h/icu.h ../h/xeroskernel.h ../h/xeroslib.h
evec.o: ../c/evec.c ../h/i386.h ../h/xeroskernel.h ../h/xeroslib.h
kprintf.o: ../c/kprintf.c ../h/i386.h ../h/xeroskernel.h ../h/xeroslib.h
mem.o: ../c/mem.c ../h/xeroskernel.h ../h/xeroslib.h
disp.o: ../c/disp.c ../h/xeroskernel.h ../h/xeroslib.h
ctsw.o: ../c/ctsw.c ../h/xeroskernel.h ../h/xeroslib.h
syscall.o: ../c/syscall.c ../h/xeroskernel.h ../h/xeroslib.h
create.o: ../c/create.c ../h/xeroskernel.h ../h/xeroslib.h
user.o: ../c/user.c ../h/xeroskernel.h ../h/xeroslib.h
msg.o: ../c/msg.c ../h/xeroskernel.h ../h/xeroslib.h
sleep.o: ../c/sleep.c ../h/xeroskernel.h ../h/xeroslib.h
signal.o: ../c/signal.c ../h/xeroskernel.h ../h/xeroslib.h
util.o: ../c/util.c ../h/xeroskernel.h
queue.o: ../c/queue.c ../h/xeroskernel.h
deltalist.o: ../c/deltalist.c ../h/xeroskernel.h
kbd.o: ../c/kbd.c ../h/xeroskernel.h
di_calls.o: ../c/di_calls.c ../h/xeroskernel.h ../h/kbd.h

# Tests
memtest.o: ../c/test/memtest.c ../h/xeroskernel.h
queuetest.o: ../c/queue.c ../c/test/queuetest.c ../h/xeroskernel.h
createtest.o: ../c/test/createtest.c ../h/xeroskernel.h
syscalltest.o: ../c/test/syscalltest.c ../h/xeroskernel.h
deltalisttest.o: ../c/deltalist.c ../c/test/deltalisttest.c ../h/xeroskernel.h
msgtest.o: ../c/test/msgtest.c ../h/xeroskernel.h
preemptiontest.o: ../c/test/preemptiontest.c ../h/xeroskernel.h
signaltest.o: ../c/test/signaltest.c ../h/xeroskernel.h
devicetest.o: ../c/test/devicetest.c ../h/xeroskernel.h ../h/kbd.h