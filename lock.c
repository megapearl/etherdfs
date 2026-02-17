/*
 * part of ethersrv
 * http://etherdfs.sourceforge.net
 *
 * Copyright (C) 2017 Mateusz Viste
 * Copyright (c) 2025-2026 D. Flissinger (megapearl)
 */

#include <fcntl.h>      /* open(), O_CREAT, O_EXCL */
#include <unistd.h>     /* close(), unlink() */
#include <sys/stat.h>   /* 0644 mode */
#include <stdio.h>

#include "lock.h"

/* acquire the lock file - Atomic implementation */
int lockme(char *lockfile) {
  int fd;

  /* Try to create the file atomically.
     O_EXCL ensures that this call fails if the file already exists. 
     This prevents race conditions. */
  fd = open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0644);

  if (fd < 0) {
    return -1; /* Failed to acquire lock (already exists or permission error) */
  }

  close(fd);
  return 0;
}

/* release the lock file */
void unlockme(char *lockfile) {
  unlink(lockfile);
}
