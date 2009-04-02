// reverse engineered FOBJ resource
// values are big endian
struct __attribute__ ((__packed__)) FOBJrsrc {
    int16_t     fdType;         // 8 = folder, 4 = disk
    struct {
        int16_t v,h;
    } fdIconPos;                // icon position in window
    char _rsv1[6];  // unknown
    int16_t     parent;         // parent folder, kMFSFolder*
    char _rsv2[12]; // unknown
    uint32_t    fdCrDat;        // creation date (mac)
    uint32_t    fdMdDat;        // modification date (mac)
    char _rsv3[4];  // unknown (backup date?)
    uint16_t    fdFlags;        // finder flags
    char _rsv4[]; // the resource is bigger
};
typedef struct FOBJrsrc FOBJrsrc;
