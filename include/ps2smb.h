/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
//---------------------------------------------------------------------------
//File name:   ps2smb.h
//---------------------------------------------------------------------------
/* SMB1 (CIFS) ioctl definitions for the PS2SDK smbman.irx IOP module.
 *
 * Kept local to this project so the SMB1 client does not need the full
 * ps2sdk iop/network/smbman include tree. The values and struct layouts
 * are bit-for-bit compatible with ps2sdk's iop/network/smbman/include/ps2smb.h
 * (jimmikaelkael, Academic Free License 3.0) and are also what Open PS2
 * Loader uses. smbman registers the "smb" iomanX device (aka smb0:), so
 * once LOGON + OPENSHARE succeed the share is reachable with plain
 * fileXio* calls on "smb:" paths.
 */
#ifndef _PS2SMB_H
#define _PS2SMB_H

#ifdef __cplusplus
extern "C" {
#endif

#define NO_PASSWORD          -1
#define PLAINTEXT_PASSWORD    0
#define HASHED_PASSWORD       1

// DEVCTL commands (SMB_DEVCTL_*)
#define SMB_DEVCTL_GETPASSWORDHASHES  0xC0DE0001
#define SMB_DEVCTL_LOGON              0xC0DE0002
#define SMB_DEVCTL_LOGOFF             0xC0DE0003
#define SMB_DEVCTL_GETSHARELIST       0xC0DE0004
#define SMB_DEVCTL_OPENSHARE          0xC0DE0005
#define SMB_DEVCTL_CLOSESHARE         0xC0DE0006
#define SMB_DEVCTL_ECHO               0xC0DE0007
#define SMB_DEVCTL_QUERYDISKINFO      0xC0DE0008

// LOGON error codes returned as -SMB_DEVCTL_LOGON_ERR_*
#define SMB_DEVCTL_LOGON_ERR_CONN      1
#define SMB_DEVCTL_LOGON_ERR_PROT      2
#define SMB_DEVCTL_LOGON_ERR_LOGON     3

// helper structs for DEVCTL commands (must match the IOP module exactly)
typedef struct {
    char password[256];
} smbGetPasswordHashes_in_t;

typedef struct {                 // size = 32
    unsigned char LMhash[16];
    unsigned char NTLMhash[16];
} smbGetPasswordHashes_out_t;

typedef struct {                 // size = 536
    char serverIP[16];
    int  serverPort;
    char User[256];
    char Password[256];
    int  PasswordType;           // PLAINTEXT_PASSWORD or HASHED_PASSWORD
} smbLogOn_in_t;

typedef struct {                 // size = 8
    void *EE_addr;
    int   maxent;
} smbGetShareList_in_t;

typedef struct {                 // size = 520
    char ShareName[256];
    char Password[256];
    int  PasswordType;           // PLAINTEXT_PASSWORD or HASHED_PASSWORD
} smbOpenShare_in_t;

typedef struct {                 // size = 260
    char echo[256];
    int  len;
} smbEcho_in_t;

typedef struct {                 // size = 16
    int TotalUnits;
    int BlocksPerUnit;
    int BlockSize;
    int FreeUnits;
} smbQueryDiskInfo_out_t;

typedef struct {                 // size = 512
    char ShareName[256];
    char ShareComment[256];
} ShareEntry_t;

#ifdef __cplusplus
}
#endif

#endif /* _PS2SMB_H */
//---------------------------------------------------------------------------
//End of file: ps2smb.h
//---------------------------------------------------------------------------
