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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <arpa/inet.h>
#include "mfs.h"
#if defined(USE_LIBRES)
#include "fobj.h"
#endif

const char * libmfs_id = "libmfs 1.0.1 (C)2008-2009 namedfork.net";

// printable flags
#define BINFLG8(x) ((x)&0x80?'1':'0'),((x)&0x40?'1':'0'),((x)&0x20?'1':'0'),((x)&0x10?'1':'0'),((x)&0x08?'1':'0'),((x)&0x04?'1':'0'),((x)&0x02?'1':'0'),((x)&0x01?'1':'0')
#define BINFLG16(x) BINFLG8((x>>8)), BINFLG8(x)

// private functions
int mfs_blkread (MFSVolume *vol, size_t numBlocks, size_t offset, void *buf);
int mfs_albkread (MFSVolume *vol, size_t numBlocks, uint16_t start, void *buf);
int mfs_fkread_at_appledouble (MFSFork *fk, size_t size, size_t offset, void *buf);
int mfs_fkread_at_real (MFSFork *fk, size_t size, size_t offset, void *buf);
MFSVABM mfs_vabm (MFSVolume *vol);
MFSDirectoryRecord* mfs_directory_record (MFSDirectoryRecord *src, size_t size);
int16_t mfs_comment_id (const char *flCName);
int16_t mfs_folder_id (MFSDirectoryRecord *rec);
#ifdef USE_LIBRES
RFILE * mfs_desktop (MFSVolume *vol);
#endif
int mfs_load_folders (MFSVolume *vol);
int mfs_fneq (const uint8_t *s1, const uint8_t *s2);
#if defined(LIBMFS_VERBOSE)
int mfs_printmdb (MFSMasterDirectoryBlock *mdb);
int mfs_printrecord (MFSDirectoryRecord *rec);
#endif

MFSVolume* mfs_vopen (const char *path, size_t offset, int flags) {
    FILE* fp = fopen(path, "r");
    if (fp == NULL) return NULL;
    MFSVolume* vol = malloc(sizeof(MFSVolume));
    bzero(vol, sizeof(MFSVolume));
    vol->fp = fp;
    vol->offset = offset;
    vol->openForks = 0;
    
    // read MDB
    void* mdb_block = malloc(kMFSBlockSize);
    if (-1 == mfs_blkread(vol, 1, 2, mdb_block)) goto error;
    memcpy(&vol->mdb, mdb_block, sizeof(MFSMasterDirectoryBlock));
    free(mdb_block);
    // bring to host endianness
    MFSMasterDirectoryBlock *mdb = &vol->mdb;
    mdb->drSigWord  = ntohs(mdb->drSigWord);
    mdb->drCrDate   = ntohl(mdb->drCrDate);
    mdb->drLsBkUp   = ntohl(mdb->drLsBkUp);
    mdb->drAtrb     = ntohs(mdb->drAtrb);
    mdb->drNmFls    = ntohs(mdb->drNmFls);
    mdb->drDirSt    = ntohs(mdb->drDirSt);
    mdb->drBlLen    = ntohs(mdb->drBlLen);
    mdb->drNmAlBlks = ntohs(mdb->drNmAlBlks);
    mdb->drAlBlkSiz = ntohl(mdb->drAlBlkSiz);
    mdb->drClpSiz   = ntohl(mdb->drClpSiz);
    mdb->drAlBlSt   = ntohs(mdb->drAlBlSt);
    mdb->drNxtFNum  = ntohl(mdb->drNxtFNum);
    mdb->drFreeBks  = ntohs(mdb->drFreeBks);
    strncpy(vol->name, (char*)&mdb->drVN[1], mdb->drVN[0]);
    
    // check MDB
    if (mdb->drSigWord != kMFSSignature) goto error;
    #if defined(LIBMFS_VERBOSE)
    mfs_printmdb(mdb);
    #endif
    
    // read volume allocation block map
    vol->vabm = mfs_vabm(vol);
    vol->alBkOff = mdb->drAlBlSt*kMFSBlockSize - 2*mdb->drAlBlkSiz;
    
    // read directory
    vol->directory = mfs_directory(vol);
    
    // read tree
    #if defined(USE_LIBRES)
    if (flags & MFS_FOLDERS) mfs_load_folders(vol);
    #endif
    
    return vol;
error:
#if defined(_DARWIN_C_SOURCE)
    errno = EFTYPE;
#else
    errno = EINVAL;
#endif
    free(vol);
    return NULL;
}

int mfs_vclose (MFSVolume* vol) {
    if (vol->openForks) {
        errno = EBUSY;
        return -1;
    }
    mfs_directory_free(vol->directory);
    free(vol->vabm);
    fclose(vol->fp);
#ifdef USE_LIBRES
    if (vol->desktop) res_close(vol->desktop);
    if (vol->folders) free(vol->folders);
#endif
    free(vol);
    return 0;
}

int mfs_blkread (MFSVolume *vol, size_t numBlocks, size_t offset, void *buf) {
    if (-1 == fseek(vol->fp, vol->offset+(kMFSBlockSize*offset), SEEK_SET)) return -1;
    if (numBlocks != fread(buf, kMFSBlockSize, numBlocks, vol->fp)) return -1;
    return 0;
}

int mfs_albkread (MFSVolume *vol, size_t numBlocks, uint16_t start, void *buf) {
    if (-1 == fseek(vol->fp, (vol->offset)+(vol->alBkOff)+(vol->mdb.drAlBlkSiz*start), SEEK_SET)) return -1;
    if (numBlocks != fread(buf, vol->mdb.drAlBlkSiz, numBlocks, vol->fp)) return -1;
    return 0;
}

time_t mfs_time (uint32_t mfsDate) {
    return mfsDate - kMFSTimeDelta;
}

struct timespec mfs_timespec (uint32_t mfsDate) {
    struct timespec ts;
    ts.tv_sec = mfsDate - kMFSTimeDelta;
    ts.tv_nsec = 0;
    return ts;
}

#if defined(LIBMFS_VERBOSE)
int mfs_printmdb (MFSMasterDirectoryBlock *mdb) {
    time_t t;
    printf("MASTER DIRECTORY BLOCK:\n");
    printf("  signature:  $%04X\n", mdb->drSigWord);
    t = mfs_time(mdb->drCrDate);
    printf("  creation:   %s", asctime(localtime(&t)));
    t = mfs_time(mdb->drLsBkUp);
    printf("  backup:     %s", asctime(localtime(&t)));
    printf("  attributes: %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n", BINFLG16(mdb->drAtrb));
    printf("  files:      %d\n", mdb->drNmFls);
    printf("  dir.start:  %d\n", mdb->drDirSt);
    printf("  dir.len:    %d\n", mdb->drBlLen);
    printf("  al.bks:     %d\n", mdb->drNmAlBlks);
    printf("  al.bksz:    %d\n", mdb->drAlBlkSiz);
    printf("  al.bytes:   %d\n", mdb->drClpSiz);
    printf("  al.first:   %d\n", mdb->drAlBlSt);
    printf("  fn.next:    %d\n", mdb->drNxtFNum);
    printf("  free:       %d\n", mdb->drFreeBks);
    char volName[28];
    strncpy(volName, (char*)&mdb->drVN[1], mdb->drVN[0]);
    printf("  name:       %s\n", volName);
    
}

int mfs_printrecord (MFSDirectoryRecord *rec) {
    printf("DIRECTORY RECORD:\n");
    printf("  name:     %s\n", rec->flCName);
    printf("  flags:    %c%c%c%c%c%c%c%c\n", BINFLG8(rec->flFlags));
    printf("  version:  %d\n", rec->flTyp);
    printf("  inode:    %d\n", rec->flFlNum);
    printf("  data.blk: %d\n", rec->flStBlk);
    printf("  data.lgl: %d\n", rec->flLgLen);
    printf("  data.pyl: %d\n", rec->flPyLen);
    printf("  rsrc.blk: %d\n", rec->flRStBlk);
    printf("  rsrc.lgl: %d\n", rec->flRLgLen);
    printf("  rsrc.pyl: %d\n", rec->flRPyLen);
    time_t t = mfs_time(rec->flCrDat);
    printf("  created:  %s", asctime(localtime(&t)));
    t = mfs_time(rec->flMdDat);
    printf("  modified: %s", asctime(localtime(&t)));
    // user words
    printf("  folder: %d\n", ntohs(rec->flUsrWds.folder));
    uint16_t fflags = ntohs(rec->flUsrWds.flags);
    printf("  fflags: %04X\n%s%s%s%s%s%s%s%s%s", fflags,
        ((fflags & kIsOnDesk)?          "          on desktop\n":""),
        ((fflags & kSwitchLaunch)?      "          switch launch\n":""),
        ((fflags & kIsShared)?          "          shared\n":""),
        ((fflags & kHasNoINITs)?        "          no INITs\n":""),
        ((fflags & kHasBeenInited)?     "          inited\n":""),
        ((fflags & kChanged)?           "          changed\n":""),
        ((fflags & kNameLocked)?        "          name locked\n":""),
        ((fflags & kHasBundle)?         "          bundle\n":""),
        ((fflags & kIsInvisible)?       "          invisible\n":"")
        );
}
#endif

MFSVABM mfs_vabm (MFSVolume *vol) {
    // VABM is 12-bit packed and comes after MDB
    // read blocks containing VABM
    MFSMasterDirectoryBlock *mdb = &vol->mdb;
    size_t vabm_size = (mdb->drNmAlBlks*3)/2;
    size_t vabm_span = vabm_size + sizeof(MFSMasterDirectoryBlock);
    size_t vabm_blks = vabm_span/kMFSBlockSize + (vabm_span%kMFSBlockSize?1:0);
    void* vabm_bits = malloc(vabm_blks*kMFSBlockSize);
    if (-1 == mfs_blkread(vol, vabm_blks, 2, vabm_bits)) return NULL;
    
    // parse VABM
    void* vabm_base = vabm_bits + sizeof(MFSMasterDirectoryBlock);
    MFSVABM vabm = malloc(sizeof(uint16_t)*(mdb->drNmAlBlks+2));
    vabm[0] = mdb->drNmAlBlks;
    vabm[1] = 0x1337;
    
    size_t offset;
    uint16_t val;
    for(int n=2; n < 2+mdb->drNmAlBlks; n++) {
        offset = ((n-2)*3)/2;
        val = ntohs(*(uint16_t*)(vabm_base+offset));
        if (n%2) vabm[n] = val & 0xFFF;
        else vabm[n] = (val & 0xFFF0) >> 4;
    }
    
    return vabm;
}

// read directory
MFSDirectoryRecord ** mfs_directory (MFSVolume *vol) {
    MFSMasterDirectoryBlock *mdb = &vol->mdb;
    MFSBlock *dir_blk = calloc(mdb->drBlLen, kMFSBlockSize);
    // array of pointers to records
    MFSDirectoryRecord ** dir = calloc(mdb->drNmFls+1, sizeof(MFSDirectoryRecord*));
    dir[mdb->drNmFls] = NULL;
    
    // read directory blocks
    mfs_blkread(vol, mdb->drBlLen, mdb->drDirSt, dir_blk);
    
    // parse
    MFSDirectoryRecord *rec;
    size_t block, rec_offset;
    size_t rec_count = 0, rec_size;
    for(block = 0; block < mdb->drBlLen; block++) {
        // read records in a block
        rec_offset = 0;
        for(;;) {
            rec = (MFSDirectoryRecord*)&dir_blk[block][rec_offset];
            rec_size = 51 + rec->flNam[0];
            if (rec->flFlags) {
                // record is used, copy it
                dir[rec_count++] = mfs_directory_record(rec, rec_size);
                rec_offset += rec_size;
                if (rec_offset%2) rec_offset++;
            } else break;
        }
        if (rec_count == mdb->drNmFls) break;
    }
    
    free(dir_blk);
    return dir;
}

void mfs_directory_free (MFSDirectoryRecord ** dir) {
    for(int i=0; dir[i]; i++) free(dir[i]);
    free(dir);
}

MFSDirectoryRecord* mfs_directory_record (MFSDirectoryRecord *src, size_t size) {
    MFSDirectoryRecord *rec = malloc(size+1);
    memcpy(rec, src, size);
    // null-terminate name
    ((uint8_t*)rec)[size] = '\0';
    
    // bring to host endianness
    rec->flFlNum  = ntohl(rec->flFlNum);
    rec->flStBlk  = ntohs(rec->flStBlk);
    rec->flLgLen  = ntohl(rec->flLgLen);
    rec->flPyLen  = ntohl(rec->flPyLen);
    rec->flRStBlk = ntohs(rec->flRStBlk);
    rec->flRLgLen = ntohl(rec->flRLgLen);
    rec->flRPyLen = ntohl(rec->flRPyLen);
    rec->flCrDat  = ntohl(rec->flCrDat);
    rec->flMdDat  = ntohl(rec->flMdDat);
    
    #if defined(LIBMFS_VERBOSE)
    mfs_printrecord(rec);
    #endif
    
    return rec;
}

MFSDirectoryRecord* mfs_directory_find_name (MFSDirectoryRecord **dir, const char *name) {
    MFSDirectoryRecord *rec;
    size_t namelen = strlen(name);
    
    for(int i=0; dir[i]; i++) {
        rec = dir[i];
        if (rec->flNam[0] != namelen) continue;
        if (mfs_fneq((const uint8_t*)rec->flCName, (const uint8_t*)name)) return rec;
    }
    return NULL;
}

// http://developer.apple.com/technotes/tb/tb_06.html
// Comments are in Desktop's FCMT resources, as a Str255
int16_t mfs_comment_id (const char *flCName) {
    int16_t hash = 0;
    
    for(int i = 0; flCName[i]; i++) {
        hash ^= flCName[i];
        // ROR.W
        if (hash & 1) hash = (hash >> 1) | 0x8000;
        else hash = ((hash >> 1) & 0x7fff);
        if (hash > 0) hash = - hash;
    }
    return hash;
}

// returns newly allocated C-string in MacRoman encoding, or NULL if it fails
// pass rec as NULL for the disk's comment
char * mfs_comment (MFSVolume *vol, MFSDirectoryRecord *rec) {
    if (vol == NULL) return NULL;
#if defined(USE_LIBRES)
    int16_t cmtID = mfs_comment_id(rec? rec->flCName : vol->name);
    unsigned char cmtLen;
    size_t readBytes;
    res_read(mfs_desktop(vol), 'FCMT', cmtID, &cmtLen, 0, 1, &readBytes, NULL);
    if (readBytes == 0) return NULL;
    char * comment = malloc((int)cmtLen+1);
    res_read(mfs_desktop(vol), 'FCMT', cmtID, comment, 1, cmtLen, &readBytes, NULL);
    comment[cmtLen] = '\0';
    return comment;
#else
    return NULL;
#endif
}

MFSFork* mfs_fkopen (MFSVolume *vol, MFSDirectoryRecord *rec, int mode, int write) {
    if (vol == NULL || rec == NULL) {errno = ENOENT; return NULL;}
    int isResourceFork = ((mode == kMFSForkRsrc) || (mode == kMFSForkAppleDouble));
    // cannot open non-existant resource forks
    // non-existant data forks behave like empty files
    if ((mode == kMFSForkRsrc) && (rec->flRStBlk == 0)) {errno = ENOENT; return NULL;}
    
    uint16_t fkNmBks = (isResourceFork?rec->flRPyLen:rec->flPyLen)/vol->mdb.drAlBlkSiz;
    MFSFork* fk = malloc(sizeof(MFSFork) + (sizeof(uint16_t)*(fkNmBks+1)));
    fk->_fkSgn  = 0;
    fk->fkVol   = vol;
    fk->fkDrRec = rec;
    fk->fkMode  = mode;
    fk->fkLgLen = (isResourceFork?rec->flRLgLen:rec->flLgLen);
    fk->fkNmBks = fkNmBks;
    fk->fkAppleDouble = NULL;
    fk->fkOffset = 0;
    
    // read allocation map
    if (fkNmBks) {
        fk->fkAlMap[0] = (isResourceFork?rec->flRStBlk:rec->flStBlk);
        uint16_t lastAlBk = fk->fkAlMap[0];
        for(int i=1; i < fk->fkNmBks; i++) {
            fk->fkAlMap[i] = vol->vabm[lastAlBk];
            lastAlBk = fk->fkAlMap[i];
        }
        fk->fkAlMap[fkNmBks] = 0;
        if (vol->vabm[lastAlBk] != kMFSAlBkLast) {
            fprintf(stderr, "Invalid allocation block map for %s\n", rec->flCName);
            errno = EFBIG;
            free(fk);
            return NULL;
        };
    }
    
    // construct AppleDouble header
    if (mode == kMFSForkAppleDouble) {
        AppleDouble *as = malloc(kAppleDoubleHeaderLength);
        fk->fkAppleDouble = as;
        bzero(as, kAppleDoubleHeaderLength);
        
        // header
        as->magic = htonl(kAppleDoubleMagic);
        as->version = htonl(kAppleDoubleVersion);
        memcpy(&as->filesystem, "Macintosh       ", 16);
        int e = 0;
        
        // resource fork
        if (fk->fkLgLen) {
            as->entry[e].type = htonl(kAppleDoubleResourceForkEntry);
            as->entry[e].offset = htonl(kAppleDoubleResourceForkOffset);
            as->entry[e].length = htonl((uint32_t)fk->fkLgLen);
            e++;
        }
        
        // real name
        as->entry[e].type = htonl(kAppleDoubleRealNameEntry);
        as->entry[e].offset = htonl(kAppleDoubleRealNameOffset);
        as->entry[e].length = htonl((uint32_t)(rec->flNam[0]));
        strcpy((void*)as+kAppleDoubleRealNameOffset, rec->flCName);
        e++;
        
        // file info
        as->entry[e].type = htonl(kAppleDoubleFileInfoEntry);
        as->entry[e].offset = htonl(kAppleDoubleFileInfoOffset);
        as->entry[e].length = htonl(kAppleDoubleFileInfoLength);
        AppleDoubleMacFileInfo *mfi = (void*)as+kAppleDoubleFileInfoOffset;
        mfi->creationDate = htonl(rec->flCrDat);
        mfi->modificationDate = htonl(rec->flMdDat);
        mfi->backupDate = htonl(0);
        mfi->attributes = htonl((uint32_t)(rec->flFlags & 0x7F));
        e++;
        
        // finder info
        as->entry[e].type = htonl(kAppleDoubleFinderInfoEntry);
        as->entry[e].offset = htonl(kAppleDoubleFinderInfoOffset);
        as->entry[e].length = htonl(kAppleDoubleFinderInfoLength);
        memcpy((void*)as+kAppleDoubleFinderInfoOffset, &rec->flUsrWds, 16);
        e++;
        
        // finder comment
#ifdef USE_LIBRES
        size_t commentLength;
        if (mfs_desktop(vol) && res_read(vol->desktop, 'FCMT', mfs_comment_id(rec->flCName), (void*)as+kAppleDoubleCommentOffset, 0, 256, &commentLength, NULL)) {
            as->entry[e].type = htonl(kAppleDoubleCommentEntry);
            as->entry[e].offset = htonl(kAppleDoubleCommentOffset);
            as->entry[e].length = htonl(commentLength);
            e++;
        }
#endif
        
        // number of entries written
        as->numEntries = htons(e);
    }
    
    // set signature and open forks
    vol->openForks++;
    fk->_fkSgn = kMFSForkSignature;
    return fk;
}

MFSFork* mfs_dhopen (MFSVolume *vol, MFSFolder *folder) {
    // open AppleDouble header for folder
    if (folder == NULL) return NULL;
    MFSFork* fk = malloc(sizeof(MFSFork));
    fk->_fkSgn  = 0;
    fk->fkVol   = vol;
    fk->fkDrRec = NULL;
    fk->fkMode  = kMFSForkAppleDouble;
    fk->fkLgLen = 0;
    fk->fkNmBks = 0;
    fk->fkAppleDouble = NULL;
    fk->fkOffset = 0;
    
    // construct AppleDouble header
    AppleDouble *as = malloc(kAppleDoubleHeaderLength);
    fk->fkAppleDouble = as;
    bzero(as, kAppleDoubleHeaderLength);
    
    // header
    as->magic = htonl(kAppleDoubleMagic);
    as->version = htonl(kAppleDoubleVersion);
    memcpy(&as->filesystem, "Macintosh       ", 16);
    int e = 0;
    
    // real name
    as->entry[e].type = htonl(kAppleDoubleRealNameEntry);
    as->entry[e].offset = htonl(kAppleDoubleRealNameOffset);
    as->entry[e].length = htonl(strlen(folder->fdCNam));
    strcpy((void*)as+kAppleDoubleRealNameOffset, folder->fdCNam);
    e++;
    
    // file info
    as->entry[e].type = htonl(kAppleDoubleFileInfoEntry);
    as->entry[e].offset = htonl(kAppleDoubleFileInfoOffset);
    as->entry[e].length = htonl(kAppleDoubleFileInfoLength);
    AppleDoubleMacFileInfo *mfi = (void*)as+kAppleDoubleFileInfoOffset;
    mfi->creationDate = htonl(folder->fdCrDat);
    mfi->modificationDate = htonl(folder->fdMdDat);
    mfi->backupDate = htonl(0);
    mfi->attributes = htonl(0);
    e++;
    
    // finder info
    MFSFInfo finfo = {0, 0, 0, {0, 0}, 0};
    finfo.flags = htons(folder->fdFlags);
    finfo.loc.v = htons(folder->fdLocV);
    finfo.loc.h = htons(folder->fdLocH);
    as->entry[e].type = htonl(kAppleDoubleFinderInfoEntry);
    as->entry[e].offset = htonl(kAppleDoubleFinderInfoOffset);
    as->entry[e].length = htonl(kAppleDoubleFinderInfoLength);
    memcpy((void*)as+kAppleDoubleFinderInfoOffset, &finfo, 16);
    e++;
    
    // finder comment
#ifdef USE_LIBRES
    size_t commentLength;
    if (mfs_desktop(vol) && res_read(vol->desktop, 'FCMT', mfs_comment_id(folder->fdCNam), (void*)as+kAppleDoubleCommentOffset, 0, 256, &commentLength, NULL)) {
        as->entry[e].type = htonl(kAppleDoubleCommentEntry);
        as->entry[e].offset = htonl(kAppleDoubleCommentOffset);
        as->entry[e].length = htonl(commentLength);
        e++;
    }
#endif
    
    // number of entries written
    as->numEntries = htons(e);
    
    // set signature and open forks
    vol->openForks++;
    fk->_fkSgn = kMFSForkSignature;
    return fk;
}

int mfs_fkclose (MFSFork *fk) {
    if (fk->_fkSgn != kMFSForkSignature) {
        errno = EBADF;
        return -1;
    }
    fk->_fkSgn = 0;
    if (fk->fkAppleDouble) free(fk->fkAppleDouble);
    fk->fkVol->openForks--;
    free(fk);
    return 0;
}

int mfs_fkread_at (MFSFork *fk, size_t size, size_t offset, void *buf) {
    switch(fk->fkMode) {
        case kMFSForkData:
        case kMFSForkRsrc:
            return mfs_fkread_at_real(fk, size, offset, buf);
        case kMFSForkAppleDouble:
            return mfs_fkread_at_appledouble(fk, size, offset, buf);
    }
}

unsigned long mfs_fkread (void *fk, void *buf, unsigned long length) {
    return mfs_fkread_at((MFSFork*)fk, length, ((MFSFork*)fk)->fkOffset, buf);
}

unsigned long mfs_fkseek (void *fk, long offset, int whence) {
    // set offset
    uint32_t fkLen = ((MFSFork*)fk)->fkLgLen + ((((MFSFork*)fk)->fkMode == kMFSForkAppleDouble)? kAppleDoubleHeaderLength : 0);
    
    switch(whence) {
        case SEEK_SET:
            ((MFSFork*)fk)->fkOffset = offset;
            break;
        case SEEK_END:
            ((MFSFork*)fk)->fkOffset = fkLen + offset;
            break;
        case SEEK_CUR:
            ((MFSFork*)fk)->fkOffset += offset;
            break;
    }
    return (unsigned long)((MFSFork*)fk)->fkOffset;
}

long mfs_ftell (MFSFork *fk) {
    return fk->fkOffset;
}

int mfs_fkread_at_appledouble (MFSFork *fk, size_t size, size_t offset, void *buf) {
    if (size == 0) return 0;
    size_t asLgLen = kAppleDoubleHeaderLength + fk->fkLgLen;
    if (offset >= asLgLen) return 0;
    if (offset + size > asLgLen) size = asLgLen - offset;
    
    // read in resource fork only
    if (offset >= kAppleDoubleResourceForkOffset)
        return mfs_fkread_at_real(fk, size, offset-kAppleDoubleResourceForkOffset, buf);
    
    // read from AppleDouble header:
    size_t btr = size;
    size_t hdBtr = kAppleDoubleHeaderLength - offset;
    if (hdBtr > size) hdBtr = size;
    memcpy(buf, fk->fkAppleDouble, hdBtr);
    btr -= hdBtr;
    buf += hdBtr;
    
    // read from fork
    if (btr) return hdBtr + mfs_fkread_at_real(fk, btr, 0, buf);
    return size;
}

int mfs_fkread_at_real (MFSFork *fk, size_t size, size_t offset, void *buf) {
    if (size == 0) return 0;
    if (offset >= fk->fkLgLen) return 0;
    if (offset + size > fk->fkLgLen) size = fk->fkLgLen - offset;
    
    // read blocks and copy data
    size_t btr = size;  // total bytes to read
    size_t bkBtr;       // bytes to read from block
    uint16_t bkn = offset / (fk->fkVol->mdb.drAlBlkSiz); // block index
    size_t bk1Off = offset % (fk->fkVol->mdb.drAlBlkSiz); // offset in first block
    void *bk = malloc(fk->fkVol->mdb.drAlBlkSiz);
    
    // read first block
    mfs_albkread(fk->fkVol, 1, fk->fkAlMap[bkn], bk);
    bkBtr = (fk->fkVol->mdb.drAlBlkSiz - bk1Off); // maximum bytes readable from first block
    if (bkBtr > btr) bkBtr = btr;
    memcpy(buf, bk+bk1Off, bkBtr);
    btr -= bkBtr;
    buf += bkBtr;
    bkn++;
    
    // read other blocks
    while(btr) {
        // read block
        mfs_albkread(fk->fkVol, 1, fk->fkAlMap[bkn], bk);
        // bytes to read
        if (btr >= fk->fkVol->mdb.drAlBlkSiz) bkBtr = fk->fkVol->mdb.drAlBlkSiz;
        else bkBtr = btr;
        // copy
        memcpy(buf, bk, bkBtr);
        // advance
        btr -= bkBtr;
        buf += bkBtr;
        bkn++;
    }
    
    free(bk);
    return (int)size;
}

#ifdef USE_LIBRES
RFILE * mfs_desktop (MFSVolume *vol) {
    if (vol->desktop == NULL) {
        MFSDirectoryRecord *dr = mfs_directory_find_name(vol->directory, "Desktop");
        MFSFork *df = mfs_fkopen(vol, dr, kMFSForkRsrc, 0);
        void* desktopData = malloc(df->fkLgLen);
        mfs_fkread_at(df, df->fkLgLen, 0, desktopData);
        vol->desktop = res_open_mem(desktopData, df->fkLgLen, 0);
        mfs_fkclose(df);
    }
    return vol->desktop;
}

int mfs_load_folders (MFSVolume *vol) {
    size_t  count;
    int     i;
    
    if (vol->desktop == NULL) vol->desktop = mfs_desktop(vol);
    if (vol->desktop == NULL) return 0;
    ResAttr * fobj = res_list(vol->desktop, 'FOBJ', NULL, 0, 0, &count, NULL);
    if (fobj == NULL) return 0;
    vol->folders = calloc(count, sizeof(struct MFSFolder));
    if (vol->folders == NULL) return 0;
    bzero(vol->folders, sizeof(struct MFSFolder)*count);
    vol->numFolders = count;
    
    // fill
    for(i=0; i < count; i++) {
        vol->folders[i].fdID = fobj[i].ID;
        strncpy(vol->folders[i].fdCNam, fobj[i].name, 65); // stupid linux has no strlcpy
        vol->folders[i].fdCNam[64] = '\0';
        
        // FOBJ resource
        FOBJrsrc * fr = res_read(vol->desktop, 'FOBJ', fobj[i].ID, NULL, 0, sizeof(FOBJrsrc), NULL, NULL);
        if (fr) {
            vol->folders[i].fdParent = ntohs(fr->parent);
            vol->folders[i].fdCrDat = ntohl(fr->fdCrDat);
            vol->folders[i].fdMdDat = ntohl(fr->fdMdDat);
            vol->folders[i].fdFlags = ntohl(fr->fdFlags);
            vol->folders->fdLocV = ntohs(fr->fdIconPos.v);
            vol->folders->fdLocH = ntohs(fr->fdIconPos.h);
        }
        
        vol->folders[i].fdSubdirs = 0;
    }
    
    // set # of subdirs in each
    for(i=0; i < count; i++) {
        MFSFolder *parent = mfs_folder_find(vol, vol->folders[i].fdParent);
        if (parent == NULL) continue;
        ++parent->fdSubdirs;
    }
    
    // print folders
    #if defined(LIBMFS_VERBOSE)
    printf("FOLDERS:\n#      PAR#   SUB NAME\n");
    for(i=0; i < count; i++)
        printf("%-7hd%-7hd%-4hd%s\n", vol->folders[i].fdID, vol->folders[i].fdParent, 
                vol->folders[i].fdSubdirs, vol->folders[i].fdCNam);
    #endif
    
    free(fobj);
    return count;
}
#endif

MFSFolder* mfs_folder_find (MFSVolume *vol, int16_t fdID) {
    if (fdID == -2) return NULL;
    if (vol->folders == NULL) return NULL;
    for(int i=0; i < vol->numFolders; i++)
        if (vol->folders[i].fdID == fdID) return &vol->folders[i];
    return NULL;
}

MFSFolder* mfs_folder_find_name (MFSVolume *vol, const char *name) {
    if (vol->folders == NULL) return NULL;
    for(int i=0; i < vol->numFolders; i++)
        if (mfs_fneq((const uint8_t*)name, (const uint8_t*)vol->folders[i].fdCNam)) return &vol->folders[i];
    return NULL;
}

int mfs_path_info (MFSVolume *vol, const char *path) {
    if (*path == ':') ++path;
    if (*path == '\0') return kMFSPathFolder;
    const char *last = strrchr(path, ':');
    if (last == NULL) last=path; else ++last;
    MFSDirectoryRecord *rec;
    
    // check if last item exists
    rec = mfs_directory_find_name(vol->directory, last);
    if (vol->folders == NULL) return rec?kMFSPathFile:kMFSPathError;
    if ((mfs_folder_find_name(vol, last) == NULL) && (rec == NULL))
        return kMFSPathError;
    
    // check every item
    char *pathsep = strdup(path);
    char *item, *next;
    MFSFolder *parent = mfs_folder_find(vol, kMFSFolderRoot);
    MFSFolder *folder;
    last = strrchr(pathsep, ':');
    if (last == NULL) last=pathsep; else ++last;
    item = next = pathsep;
    for(;;) {
        item = strsep(&next, ":");
        if (item == last) {
            folder = mfs_folder_find_name(vol, item);
            if (rec && ntohs(rec->flUsrWds.folder) != parent->fdID)
                // file wasn't inside parent folder
                goto fail;
            else if ((rec == NULL) && folder && (folder->fdParent != parent->fdID))
                // folder wasn't inside parent folder
                goto fail;
            break;
        }
        
        // check that the folder exists, and it's inside it's parent
        folder = mfs_folder_find_name(vol, item);
        if ((folder == NULL) || (folder->fdParent != parent->fdID)) goto fail;
        parent = folder;
    }
    
    free(pathsep);
    if (rec) return kMFSPathFile;
    return kMFSPathFolder;
fail:
    free(pathsep);
    return kMFSPathError;
}

int mfs_fneq (const uint8_t *s1, const uint8_t *s2) {
    // return 1 if MFS filenames are equal, 0 otherwise
    static const uint8_t mfs_chars_toupper[256] = {
        // array of MacRoman uppercase equivalents, taken from system 6
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
        0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0xCB, 0x89, 0x80, 0xCC, 0x81, 0x82, 0x83, 0x8F,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x84, 0x97, 0x98, 0x99, 0x85, 0xCD, 0x9C, 0x9D, 0x9E, 0x86,
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xAE, 0xAF,
        0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
        0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
    };
    while (mfs_chars_toupper[*s1] == mfs_chars_toupper[*s2++])
        if (*s1++ == 0) return 1;
    return 0;
}
