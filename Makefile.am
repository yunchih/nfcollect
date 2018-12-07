bin_PROGRAMS = nfcollect nfextract

AM_CFLAGS = \
			-I$(top_srcdir)/include \
			-Werror -Wall -Wno-address-of-packed-member

nfcollect_SOURCES = lib/util.c lib/sql.c lib/extract.c lib/commit.c lib/collect.c bin/nfcollect.c
nfextract_SOURCES = lib/util.c lib/sql.c lib/extract.c lib/commit.c lib/collect.c bin/nfextract.c
