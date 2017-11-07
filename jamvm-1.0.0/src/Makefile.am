SUBDIRS = @arch@
DIST_SUBDIRS = powerpc i386

libexec_PROGRAMS = jamvm
include_HEADERS = jni.h

jamvm_SOURCES = alloc.c alloc.h cast.c class.c dll.c excep.c execute.c frame.h hash.c \
                hash.h interp.c jam.c jam.h jni.c lock.c lock.h natives.c reflect.c \
                resolve.c sig.h string.c thread.c thread.h utf8.c

LDADD = -lpthread -ldl -lm @arch@/libnative.a
