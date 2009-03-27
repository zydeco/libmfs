/*
 * libmfs - library for reading Macintosh MFS volumes
 * Copyright (C) 2008-2009 Jesus A. Alvarez
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MFS_H_
#define _MFS_H_

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#ifdef USE_LIBRES
#include <libres/res.h>
#define DESKTOP_TYPE RFILE*
#else
#define DESKTOP_TYPE void*
#endif
#include "appledouble.h"

#define kMFSBlockSize       512
#define kMFSSignature       0xD2D7
#define kMFSTimeDelta       2082844800
#define kMFSAlBkEmpty       0
#define kMFSAlBkLast        1
#define kMFSAlBkDir         0xFFF
#define kMFSFolderTrash     -3
#define kMFSFolderDesktop   -2 // only used for root dir
#define kMFSFolderTemplate  -1
#define kMFSFolderRoot      0

extern const char * libmfs_id;

// fork types
enum {
    kMFSForkData,
    kMFSForkRsrc,
    kMFSForkAppleDouble
};

// result from mfs_path_info
enum {
    kMFSPathError = 0,
    kMFSPathFile,
    kMFSPathFolder
};

// finder flags
#ifndef __MACTYPES__
enum {
   kIsOnDesk            = 0x0001, // system 6 or earlier
   kColor               = 0x000E,
   kRequireSwitchLaunch = 0x0020,
   kIsShared            = 0x0040,
   kHasNoINITs          = 0x0080,
   kHasBeenInited       = 0x0100,
   kHasCustomIcon       = 0x0400, // system 7 and later
   kLetter              = 0x0200,
   kChanged             = 0x0200, // old
   kIsStationery        = 0x0800, // system 7 and later
   kNameLocked          = 0x1000,
   kHasBundle           = 0x2000,
   kIsInvisible         = 0x4000,
   kIsAlias             = 0x8000 // system 7 and later
};
#endif

// flags for mfs_vopen
enum {
    MFS_FOLDERS = 1
};

struct __attribute__ ((__packed__)) MFSMasterDirectoryBlock {
    uint16_t    drSigWord;      // always 0xD2D7
    uint32_t    drCrDate;       // date and time of initialization
    uint32_t    drLsBkUp;       // date and time of last backup
    uint16_t    drAtrb;         // volume attributes
    uint16_t    drNmFls;        // number of files in directory
    uint16_t    drDirSt;        // first block of directory
    uint16_t    drBlLen;        // lenght of directory in blocks
    uint16_t    drNmAlBlks;     // number of allocation blocks on volume
    uint32_t    drAlBlkSiz;     // size of allocation blocks
    uint32_t    drClpSiz;       // number of bytes to allocate
    uint16_t    drAlBlSt;       // first allocation block in block map
    uint32_t    drNxtFNum;      // next unused file number
    uint16_t    drFreeBks;      // number of unused allocation blocks
    uint8_t     drVN[28];       // volume name (pascal string, MacRoman)
};
typedef struct MFSMasterDirectoryBlock MFSMasterDirectoryBlock;

// use ntohl/ntohs when accessing this structure, but don't change it
struct __attribute__ ((__packed__)) MFSFInfo {
    uint32_t    type;
    uint32_t    creator;
    uint16_t    flags;  // finder flags
    struct __attribute__ ((__packed__)) {
        int16_t v;
        int16_t h;
    } loc;
    int16_t     folder; // FOBJ resource ID
};
typedef struct MFSFInfo MFSFInfo;

struct __attribute__ ((__packed__)) MFSDirectoryRecord {
    uint8_t     flFlags;        // file flags
    uint8_t     flTyp;          // version number
    MFSFInfo    flUsrWds;       // information used by the Finder
    uint32_t    flFlNum;        // file number
    uint16_t    flStBlk;        // first allocation block of data fork
    uint32_t    flLgLen;        // logical EOF of data fork
    uint32_t    flPyLen;        // physical EOF of data fork
    uint16_t    flRStBlk;       // first allocation block of resource fork
    uint32_t    flRLgLen;       // logical EOF of resource fork
    uint32_t    flRPyLen;       // physical EOF of resource fork
    uint32_t    flCrDat;        // date and time of creation, since 1904
    uint32_t    flMdDat;        // date and time of last modification, since 1904
    uint8_t     flNam[1];       // file name (pascal string, MacRoman)
    char        flCName[];
};
typedef struct MFSDirectoryRecord MFSDirectoryRecord;

// Volume Allocation Block Map, first item is length, 2nd item is unused
typedef uint16_t* MFSVABM;

typedef uint8_t MFSBlock[kMFSBlockSize];

struct MFSFolder {
    int16_t     fdID;           // 0 is disk root, -1 is empty folder
    int16_t     fdParent;       // root has parent -2
    int16_t     fdSubdirs;      // number of subdirectories
    uint32_t    fdCrDat;        // date and time of creation, since 1904
    uint32_t    fdMdDat;        // date and time of last modification, since 1904
    int16_t     fdFlags;        // finder flags
    int16_t     fdLocV, fdLocH; // icon position
    char        fdCNam[65];     // MacRoman C string
};
typedef struct MFSFolder MFSFolder;

struct MFSVolume {
    FILE                    *fp;
    size_t                  offset;     // offset to start of volume (for mounting disk images with header)
    size_t                  alBkOff;    // offset to allocation block 0
    size_t                  openForks;  // number of open forks
    MFSMasterDirectoryBlock mdb;
    MFSVABM                 vabm;
    MFSDirectoryRecord      **directory;
    size_t                  numFolders;
    MFSFolder               *folders;
    DESKTOP_TYPE            desktop;
    char                    name[28];
};
typedef struct MFSVolume MFSVolume;

#define kMFSForkSignature 0x1337D00D
struct MFSFork {
    uint32_t            _fkSgn;     // signature
    MFSVolume           *fkVol;     // parent volume
    MFSDirectoryRecord  *fkDrRec;   // directory record
    uint32_t            fkLgLen;    // fork length (bytes)
    uint16_t            fkNmBks;    // number of blocks
    int                 fkMode;     // mode (kMFSFork*)
    AppleDouble         *fkAppleDouble;
    unsigned long       fkOffset;   // mfs_fkseek, mfs_fkread
    uint16_t            fkAlMap[];  // allocation map
};
typedef struct MFSFork MFSFork;

#define kAppleDoubleHeaderLength        0x300
#define kAppleDoubleResourceForkOffset  kAppleDoubleHeaderLength
#define kAppleDoubleFileInfoOffset      0x70
#define kAppleDoubleFileInfoLength      0x10
#define kAppleDoubleFinderInfoOffset    0x80
#define kAppleDoubleFinderInfoLength    0x20
#define kAppleDoubleRealNameOffset      0xA0
#define kAppleDoubleCommentOffset       0x1A0

// open/close volume
MFSVolume* mfs_vopen (const char *path, size_t offset, int flags);
int mfs_vclose (MFSVolume* vol);

// convert time
time_t mfs_time (uint32_t mfsDate);
struct timespec mfs_timespec (uint32_t mfsDate);

// directory
MFSDirectoryRecord ** mfs_directory (MFSVolume *vol);
void mfs_directory_free (MFSDirectoryRecord ** dir);
MFSDirectoryRecord* mfs_directory_find_name (MFSDirectoryRecord **dir, const char *name);
char * mfs_comment (MFSVolume *vol, MFSDirectoryRecord *rec);

// folders
MFSFolder* mfs_folder_find (MFSVolume *vol, int16_t fdID);
MFSFolder* mfs_folder_find_name (MFSVolume *vol, const char *name);
int mfs_path_info (MFSVolume *vol, const char *path);

// fork mgmt
MFSFork* mfs_fkopen (MFSVolume *vol, MFSDirectoryRecord *rec, int mode, int flags);
MFSFork* mfs_dhopen (MFSVolume *vol, MFSFolder *folder);
int mfs_fkclose (MFSFork *fk);
int mfs_fkread_at (MFSFork *fk, size_t size, size_t offset, void *buf);

// for librsrc/libres compatibility
unsigned long mfs_fkread (void *fk, void *buf, unsigned long length);
unsigned long mfs_fkseek (void *fk, long offset, int whence);

#endif /* _MFS_H_ */
