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

// http://users.phg-online.de/tk/netatalk/doc/Apple/v1/AppleSingle_AppleDouble_v1.pdf

#define kAppleDoubleMagic               0x00051607
#define kAppleDoubleVersion             0x00020000
#define kAppleDoubleResourceForkEntry   2
#define kAppleDoubleRealNameEntry       3
#define kAppleDoubleCommentEntry        4
#define kAppleDoubleIconEntry           5
#define kAppleDoubleColorIconEntry      6
#define kAppleDoubleFileInfoEntry       7
#define kAppleDoubleFinderInfoEntry     9

struct __attribute__ ((__packed__)) AppleDoubleEntry {
    uint32_t    type;
    uint32_t    offset;
    uint32_t    length;
};
typedef struct AppleDoubleEntry AppleDoubleEntry;

struct __attribute__ ((__packed__)) AppleDoubleMacFileInfo {
    uint32_t    creationDate;
    uint32_t    modificationDate;
    uint32_t    backupDate;
    uint32_t    attributes;
};
typedef struct AppleDoubleMacFileInfo AppleDoubleMacFileInfo;

struct __attribute__  ((__packed__)) AppleDouble {
    uint32_t            magic;
    uint32_t            version;
    char                filesystem[16];
    uint16_t            numEntries;
    AppleDoubleEntry    entry[];
};
typedef struct AppleDouble AppleDouble;
