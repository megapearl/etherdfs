#
# ethersrv makefile for FreeBSD and Linux (GCC and Clang)
# http://etherdfs.sourceforge.net
#
# Copyright (C) 2017, 2018 Mateusz Viste
# Copyright (C) 2020 Michael Ortmann
# Copyright (C) 2023-2025 E. Voirin (oerg866)
#

# --- OPTIMALISATIE UITLEG ---
# -DDEBUG=0     : Schakelt trage debug logging uit (CRUCIAAL)
# -O3           : Maximale snelheid optimalisatie (loop unrolling, inlining)
# -flto         : Link Time Optimization (optimaliseert over bestanden heen)
# -static       : Statische linking (geen dependencies, robuust voor Docker)
# -s            : Strip symbols (kleinere binary)
# ----------------------------

CFLAGS := -DDEBUG=0 -O3 -flto -static -Wall -std=gnu89 -pedantic -Wextra -s -Wno-long-long -Wno-variadic-macros -Wformat-security -D_FORTIFY_SOURCE=1

CC ?= gcc

# De default target
all: ethersrv

ethersrv: ethersrv.c fs.c fs.h lock.c lock.h debug.h
	$(CC) $(CFLAGS) ethersrv.c fs.c lock.c -o ethersrv

clean:
	rm -f ethersrv *.o
