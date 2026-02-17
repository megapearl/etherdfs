/*
 * This file is part of the ethersrv project
 * Copyright (C) 2017 Mateusz Viste
 * Copyright (C) 2020 Michael Ortmann
 * Copyright (C) 2023-2025 E. Voirin (oerg866)
 * Copyright (c) 2025-2026 D. Flissinger (megapearl)
 */

#define _GNU_SOURCE /* Needed for strdup and other extensions */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/statvfs.h> /* statvfs() for diskfree calls */
#include <sys/stat.h>    /* stat() */
#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>      /* free() */
#include <string.h>
#include <time.h>        /* time_t, struct tm... */
#include <unistd.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
  #include <sys/mount.h> /* statfs() */
#else
  #include <linux/msdos_fs.h>
  #include <sys/vfs.h>   /* struct statfs */
#endif
#include <sys/ioctl.h>
#include <ctype.h>

/* NOTE: Use DBG macro from main file context if linked properly, 
   but since this is a separate unit, we rely on stderr prints guarded by #ifdef DEBUG
   or we re-implement a weak link to the debug flag. 
   For now, we strip heavy logging unless explicitly requested. */

/* External debug flag from ethersrv.c if we wanted to link it, 
   but to keep it simple we just comment out the heavy hitters */
/* #define DBG(...) fprintf(stderr, __VA_ARGS__) */
#define DBG(...) /* disabled by default for speed */

/* macOS doesn't have all the FreeBSD file flags, define missing ones */
#ifdef __APPLE__
  #ifndef UF_READONLY
    #define UF_READONLY UF_IMMUTABLE  /* Use immutable flag for readonly */
  #endif
  #ifndef UF_SYSTEM
    #define UF_SYSTEM 0  /* Not supported on macOS */
  #endif
  #ifndef UF_ARCHIVE
    #define UF_ARCHIVE 0  /* Not supported on macOS */
  #endif
#endif

#include "fs.h" 

/* database containing file/dir identifiers */
static struct sfsdb {
  char *name;
  time_t lastused;
  struct sdirlist { /* pointer to dir listing */
    struct fileprops fprops;
    struct sdirlist *next;
  } *dirlist;
} fsdb[65536];

/* frees a sdirlist linked list */
static void freedirlist(struct sdirlist *d) {
  while (d != NULL) {
    struct sdirlist *victim = d;
    d = d->next;
    free(victim);
  }
}

/* returns the "start sector" of a filesystem item */
unsigned short getitemss(char *f) {
  unsigned short i, firstfree = 0xffffu, oldest = 0;
  time_t now = time(NULL);
  
  /* see if not already in cache */
  for (i = 0; i < 0xffffu; i++) {
    /* is it what I am looking after? */
    if ((fsdb[i].name != NULL) && (strcmp(fsdb[i].name, f) == 0)) {
      fsdb[i].lastused = now;
      return(i);
    }
    /* check last usage and remove if older than one hour */
    if ((fsdb[i].name != NULL) && ((now - fsdb[i].lastused) > 3600)) {
      free(fsdb[i].name);
      freedirlist(fsdb[i].dirlist);
      memset(&(fsdb[i]), 0, sizeof(struct sfsdb));
    }
    /* remember free slot */
    if ((firstfree == 0xffffu) && (fsdb[i].name == NULL)) {
      firstfree = i;
    } else if (fsdb[oldest].lastused > fsdb[i].lastused) { 
      oldest = i;
    }
  }
  
  /* not found - if no free slot, replace oldest */
  if (firstfree == 0xffffu) {
    firstfree = oldest;
    free(fsdb[oldest].name);
    freedirlist(fsdb[oldest].dirlist);
    memset(&(fsdb[oldest]), 0, sizeof(struct sfsdb));
  }
  
  /* register it */
  fsdb[firstfree].name = strdup(f);

  if (fsdb[firstfree].name == NULL) {
    fprintf(stderr, "ERROR: OUT OF MEM!\n");
    return(0xffffu);
  }
  fsdb[firstfree].lastused = now;
  return(firstfree);
}

char *sstoitem(unsigned short ss) {
  return(fsdb[ss].name);
}

/* turns a character c into its upper-case variant */
char upchar(char c) {
  if ((c >= 'a') && (c <= 'z')) c -= ('a' - 'A');
  return(c);
}

/* translates a filename string into a fcb-style block ("FILE0001TXT") */
void filename2fcb(char *d, char *s) {
  int i;
  int j;
  /* fill the FCB block with spaces */
  for (i = 0; i < 11; i++) d[i] = ' ';

  /* cover '.' and '..' entries */
  for (i = 0; i < 8; i++) {
    if (s[i] != '.') break;
    d[i] = '.';
  }

  /* fill in the filename */
  j = i;

  for (; i < 8; i++) {
    if ((s[j] == '.') || (s[j] == 0)) break;
    while ((s[j]) == ' ') {
      j++;
    }
    d[i] = upchar(s[j]);
    j++;
  }

  s += i;
  /* fast forward to either the first dot or NULL-terminator */
  for (; ((*s != '.') && (*s != 0)); s++);
  if (*s == 0) return;
  s++; /* skip the dot */
  /* fill in the extension */
  d += 8;
  for (j = 0; j < 3; j++) {
    if ((s[j] == '.') || (s[j] == 0) || (s[j] == ' ')) break;
    *d = upchar(s[j]);
    d++;
  }
}

/* converts a time_t into a DWORD with DOS (FAT-style) timestamp bits */
static unsigned long time2dos(time_t t) {
  unsigned long res;
  struct tm *ltime;
  ltime = localtime(&t);
  if (ltime == NULL) return 0; /* Safety check */
  
  res = ltime->tm_year - 80; /* tm_year is years from 1900 */
  res <<= 4;
  res |= ltime->tm_mon + 1; 
  res <<= 5;
  res |= ltime->tm_mday;
  res <<= 5;
  res |= ltime->tm_hour;
  res <<= 6;
  res |= ltime->tm_min;
  res <<= 5;
  res |= (ltime->tm_sec >> 1); 
  return(res);
}

/* match FCB-style filename to a FCB-style mask */
static int matchfile2mask(char *msk, char *fil) {
  int i;
  for (i = 0; i < 11; i++) {
    if ((upchar(fil[i]) != upchar(msk[i])) && (msk[i] != '?')) return(-1);
  }
  return(0);
}


/* provides DOS-like attributes for item i */
unsigned char getitemattr(char *i, struct fileprops *fprops, unsigned char fatflag) {
  uint32_t attr;
#if !defined(__FreeBSD__) && !defined(__APPLE__)
  int fd;
#endif
  struct stat statbuf;
  if (stat(i, &statbuf) != 0) return(0xff); /* error */
  
  /* zero out fprops and fill it out */
  if (fprops != NULL) {
    char *fname = i;
    char *ptr;
    /* set fname to the file part of i */
    for (ptr = i; *ptr != 0; ptr++) {
      if (((*ptr == '/') || (*ptr == '\\')) && (*(ptr+1) != 0)) fname = ptr + 1;
    }
    memset(fprops, 0, sizeof(struct fileprops));
    fprops->ftime = time2dos(statbuf.st_mtime);
    filename2fcb(fprops->fcbname, fname);
  }
  
  /* is this is a directory? */
  if (S_ISDIR(statbuf.st_mode)) {
    if (fprops != NULL) fprops->fattr = 16; /* ATTR_DIR */
    return(16);
  }
  
  /* not a directory, set size */
  if (fprops != NULL) fprops->fsize = statbuf.st_size;
  
  /* if not a FAT drive, return a fake attribute of 0x20 (archive) */
  if (fatflag == 0) return(0x20);
  
#if defined(__FreeBSD__) || defined(__APPLE__)
  {
    attr = 0;
    if (statbuf.st_flags & UF_READONLY) attr |= 1;
    if (statbuf.st_flags & UF_HIDDEN) attr |= 2;
    if (statbuf.st_flags & UF_SYSTEM) attr |= 4;
    if (statbuf.st_flags & UF_ARCHIVE) attr |= 32;
#else
  /* try to fetch DOS attributes by calling the FAT IOCTL API */
  fd = open(i, O_RDONLY);
  if (fd == -1) return(0xff);
  
  /* FIXED: Explicit cast to int to avoid overflow warning */
  if (ioctl(fd, (int)FAT_IOCTL_GET_ATTRIBUTES, &attr) < 0) {
    /* fprintf(stderr, "Failed to fetch attributes of '%s'\n", i); */
    close(fd);
    return(0);
  } else {
    close(fd);
#endif
    if (fprops != NULL) fprops->fattr = attr;
    return(attr);
  }
}

/* set attributes fattr on file i */
int setitemattr(char *i, unsigned char fattr) {
  int res;
#if defined(__FreeBSD__) || defined(__APPLE__)
  unsigned long flags = 0;
  if (fattr & 1)  flags |= UF_READONLY;
  if (fattr & 2)  flags |= UF_HIDDEN;
  if (fattr & 4)  flags |= UF_SYSTEM;
  if (fattr & 32) flags |= UF_ARCHIVE;
  res = chflags(i, flags);
#else
  int fd = open(i, O_RDONLY);
  if (fd == -1) return(-1);
  res = ioctl(fd, FAT_IOCTL_SET_ATTRIBUTES, &fattr);
  close(fd);
#endif
  if (res < 0) return(-1);
  return(0);
}

/* generates a directory listing for *root */
static long gendirlist(struct sfsdb *root, unsigned char fatflag) {
  char fullpath[1024];
  int fullpathoffset;
  struct dirent *diridx;
  DIR *dp;
  struct sdirlist *lastnode = NULL, *newnode;
  long res = 0;
  
  freedirlist(root->dirlist);
  root->dirlist = NULL; /* Ensure root pointer is cleared */
  
  dp = opendir(root->name);
  if (dp == NULL) return(-1);
  
  fullpathoffset = sprintf(fullpath, "%s/", root->name);
  
  for (;;) {
    diridx = readdir(dp);
    if (diridx == NULL) break;
    
    newnode = calloc(1, sizeof(struct sdirlist));
    if (newnode == NULL) {
      fprintf(stderr, "ERROR: out of mem!");
      break;
    }
    
    /* Ensure we don't overflow the buffer */
    if (fullpathoffset + strlen(diridx->d_name) < 1023) {
        sprintf(fullpath + fullpathoffset, "%s", diridx->d_name);
        getitemattr(fullpath, &(newnode->fprops), fatflag);
    }
    
    /* add new node to linked list */
    if (lastnode == NULL) {
      root->dirlist = newnode;
    } else {
      lastnode->next = newnode;
    }
    lastnode = newnode;
    res++;
  }
  closedir(dp);
  return(res);
}

/* searches for file matching the FCB-style template */
int findfile(struct fileprops *f, unsigned short dss, char *fcbtmpl, unsigned char attr, unsigned short *nth, int flags) {
  int n = 0;
  struct sdirlist *dirlist;
  
  if ((*nth == 0) || (fsdb[dss].dirlist == NULL)) {
    long count = gendirlist(&(fsdb[dss]), flags & FFILE_ISFAT);
    if (count < 0) {
      /* fprintf(stderr, "Error: failed to scan dir '%s'\n", fsdb[dss].name); */
      return(-1);
    }
  }
  
  for (dirlist = fsdb[dss].dirlist; dirlist != NULL; dirlist = dirlist->next) {
    n++;
    if (n <= *nth) continue;
    
    if ((dirlist->fprops.fcbname[0] == '.') && (flags & FFILE_ISROOT)) continue;

    if (matchfile2mask(fcbtmpl, dirlist->fprops.fcbname) != 0) continue;
    
    /* Attributes logic */
    if (attr == 0x08) { /* I want VOL */
      if ((dirlist->fprops.fattr & 0x08) == 0) continue;
    } else { 
      if ((attr | (dirlist->fprops.fattr & 0x16)) != attr) continue;
    }
    break;
  }
  
  if (dirlist != NULL) {
    *nth = n;
    memcpy(f, &(dirlist->fprops), sizeof(struct sdirlist));
    return(0);
  }
  return(-1);
}


/* creates or truncates a file f in directory d */
int createfile(struct fileprops *f, char *d, char *fn, unsigned char attr, unsigned char fatflag) {
  char fullpath[512];
  FILE *fd;
  sprintf(fullpath, "%s/%s", d, fn);
  /* try to create/truncate the file */
  fd = fopen(fullpath, "wb");
  if (fd == NULL) return(-1);
  fclose(fd);
  /* set attribs (only if FAT drive) */
  if (fatflag != 0) {
    if (setitemattr(fullpath, attr) != 0) {
        /* fprintf(stderr, "Error: failed to set attribute %02Xh to '%s'\n", attr, fullpath); */
    }
  }
  /* collect and set attributes */
  getitemattr(fullpath, f, fatflag);
  return(0);
}


/* returns disks total size in bytes */
unsigned long long diskinfo(char *path, unsigned long long *dfree) {
  struct statvfs buf;
  unsigned long long res;
  if (statvfs(path, &buf) != 0) return(0);
  res = buf.f_blocks;
  res *= buf.f_frsize;
  *dfree = buf.f_bfree;
  *dfree *= buf.f_bsize;
  return(res);
}

/* try to create directory */
int makedir(char *d) {
  return(mkdir(d, 0));
}

/* try to remove directory */
int remdir(char *d) {
  return(rmdir(d));
}

/* change to directory d */
int changedir(char *d) {
  return(chdir(d));
}

/* reads len bytes from file */
long readfile(unsigned char *buff, unsigned short fss, unsigned long offset, unsigned short len) {
  long res;
  char *fname;
  FILE *fd;
  fname = fsdb[fss].name;
  if (fname == NULL) return(-1);
  fd = fopen(fname, "rb");
  if (fd == NULL) return(-1);
  if (fseek(fd, offset, SEEK_SET) != 0) {
    fclose(fd);
    return(-1);
  }
  res = fread(buff, 1, len, fd);
  fclose(fd);
  return(res);
}


/* writes len bytes from buff to file */
long writefile(unsigned char *buff, unsigned short fss, unsigned long offset, unsigned short len) {
  long res;
  char *fname;
  FILE *fd;
  fname = fsdb[fss].name;
  if (fname == NULL) return(-1);
  /* if len is 0, then it means "truncate" or "extend" ! */
  if (len == 0) {
    /* DBG("truncate '%s' to %lu bytes\n", fname, offset); */
    if (truncate(fname, offset) != 0) { /* fprintf(stderr, "Error: truncate() failed\n"); */ }
    return(0);
  }
  /* otherwise do a regular write */
  /* DBG("write %u bytes into file '%s' at offset %lu\n", len, fname, offset); */
  fd = fopen(fname, "r+b");
  if (fd == NULL) return(-1);
  if (fseek(fd, offset, SEEK_SET) != 0) {
    /* DBG("fseek() to %lu failed!\n", offset); */
    fclose(fd);
    return(-1);
  }
  res = fwrite(buff, 1, len, fd);
  fclose(fd);
  return(res);
}


/* remove all files matching the pattern */
int delfiles(char *pattern) {
  unsigned int i, fileoffset = 0;
  int ispattern = 0;
  char patterncopy[512];
  char dirnamefcb[12];
  char *dir, *fil;
  char filfcb[12];
  struct dirent *diridx;
  DIR *dp;
  
  for (i = 0; pattern[i] != 0; i++) {
    if (pattern[i] == '?') ispattern = 1;
    if (pattern[i] == '/') fileoffset = i;
    patterncopy[i] = pattern[i];
  }
  patterncopy[i] = 0;
  
  if (ispattern == 0) {
    if (unlink(pattern) != 0) {
      /* DBG("Error: failure to delete file '%s' (%s)\n", pattern, strerror(errno)); */
      return(-1);
    }
    return(1);
  }
  
  dir = patterncopy;
  patterncopy[fileoffset] = 0;
  fil = patterncopy + fileoffset + 1;
  filename2fcb(filfcb, fil);
  
  dp = opendir(dir);
  if (dp == NULL) return(-1);
  for (;;) {
    diridx = readdir(dp);
    if (diridx == NULL) break;
    if (diridx->d_type == DT_DIR) continue;
    
    filename2fcb(dirnamefcb, diridx->d_name);
    if (matchfile2mask(filfcb, dirnamefcb) == 0) {
      char fname[512];
      sprintf(fname, "%s/%s", dir, diridx->d_name);
      if (unlink(fname) != 0) { /* fprintf(stderr, "failed to delete '%s'\n", fname); */ }
    }
  }
  closedir(dp);

  return(0);
}

/* rename fn1 into fn2 */
int renfile(char *fn1, char *fn2) {
  return(rename(fn1, fn2));
}

/* checks if a path resides on a FAT filesystem */
int isfat(char *d) {
  struct statfs buf;
  if (statfs(d, &buf) < 0) {
    return(-1);
  }
#if defined(__FreeBSD__) || defined(__APPLE__)
  if (strcmp(buf.f_fstypename, "msdosfs"))
#else
  if (buf.f_type != MSDOS_SUPER_MAGIC)
#endif
    return(-1);
  return(0);
}

/* returns the size of an open file */
long getfopsize(unsigned short fss) {
  struct fileprops fprops;
  char *fname = fsdb[fss].name;
  if (fname == NULL) return(-1);
  if (getitemattr(fname, &fprops, 0) == 0xff) return(-1);
  return(fprops.fsize);
}


/* shorttolong: Translates a DOS 8.3 path to a Linux long path (case-insensitive) */
int shorttolong(char *dst, char *src, const char *root) {
  int found = 0;

  char *writeptr = dst;
  char *tmpdir = NULL;
  char *tmpdir_next = NULL;
  char to_find_fcb [12];
  char tmp_fcb [12];

  struct dirent *entry;
  DIR *dir;

  size_t root_len = strlen(root);

  to_find_fcb[11] = 0;  /* null terminate these */
  tmp_fcb[11] = 0;

  assert(strncmp(root, src, strlen(root)) == 0);

  src += root_len;
  writeptr += sprintf(dst, "%s/", root);

  /* CRITICAL FIX: Only print if debug requested (removed for performance) */
  /* printf("shorttolong: %s %s %s\n", dst, src, root); */

  if (src[0] != '/') {
    DBG("ERROR: invalid string for shorttolong encountered: '%s'\n", src);
    return -1;
  }

  src++;

  /* get the first token */
  tmpdir = strtok(src, "/");

  /* walk through other tokens */
  while (tmpdir != NULL) {

    tmpdir_next = strtok(NULL, "/");

    /* Turn this back into an FCB string */
    filename2fcb(to_find_fcb, tmpdir);

    /* Walk the current directory depicted by destination */
    dir = opendir(dst);

    if (dir == NULL) {
      DBG("ERROR: Failed to open directory %s", dst);
      return -1;
    }

    found = 0;

    while (!found && (entry = readdir(dir)) != NULL) {
      if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0))
        continue;

      /* get FCB name for this */
      filename2fcb(tmp_fcb, entry->d_name);

      /*if if its fcb name matches what we are trying to find, this may be our destination. */
      if (strcmp(tmp_fcb, to_find_fcb) == 0) {
        
        if ((tmpdir_next != NULL) && (entry->d_type != DT_DIR)) {
          /* DBG("The name matched but isnt a directory.\n"); */
          continue;
        }

        writeptr += sprintf(writeptr, "%s", entry->d_name);

        /* it is a directory, so we must append a / to our destination */
        if (tmpdir_next != NULL) {
          writeptr += sprintf(writeptr, "/");
        }

        found = 1;
      }
    }

    closedir(dir);

    if (!found) {
      /* Print the raw version as is to the destination string */
      writeptr += sprintf(writeptr, "%s", tmpdir);
      return -1;
    }

    tmpdir = tmpdir_next;
  }

  return 0;
}
