#
# ethersrv makefile for FreeBSD and Linux (GCC and Clang)
# http://etherdfs.sourceforge.net
#
# Copyright (C) 2017, 2018 Mateusz Viste
# Copyright (C) 2020 Michael Ortmann
# Copyright (C) 2023-2025 E. Voirin (oerg866)
# Copyright (c) 2025-2026 D. Flissinger (megapearl)
#

# --- OPTIMIZATION EXPLANATION ---
# -O3           : Max speed optimization (loop unrolling, inlining, vectorization)
# -flto         : Link Time Optimization (optimizes across object files)
# -static       : Static linking (no external dependencies, robust for Docker)
# -s            : Strip symbols (smaller binary size)
# -Wall         : Show all warnings (good practice)
# --------------------------------

CFLAGS := -O3 -flto -static -Wall -std=gnu89 -pedantic -Wextra -s -Wno-long-long -Wno-variadic-macros -Wformat-security

CC ?= gcc

# The default target
all: ethersrv

ethersrv: ethersrv.c fs.c fs.h lock.c lock.h
	$(CC) $(CFLAGS) ethersrv.c fs.c lock.c -o ethersrv

clean:
	rm -f ethersrv *.o
