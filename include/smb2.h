/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
//---------------------------------------------------------------------------
//File name:   smb2.h
//---------------------------------------------------------------------------
#include "netfs_compat.h"

/* One configured SMB share.
 * SMB1 (smbman) backend: the URL is parsed into server/port/sharename once at
 * CNF load time so LOGON/OPENSHARE devctls can use them. Layout/UI untouched. */
struct smb2_share {
        struct smb2_share *next;
        char *name;
        const char *user;
        const char *password;
        char *url;
        char server[64];
        int port;
        char sharename[256];
};

extern struct smb2_share *smb2_shares;

int init_smb2(const char *ip, const char *netmask, const char *gw);
void deinit_smb2(void);
/* fix43 diagnostic logger (EE-side fileXio, writes mass:/SMBDEBUG.LOG).
 * Safe to call from any NETFS code; never touches the smb: device itself. */
void smbDiagLog(const char *fmt, ...);
int readSMB2(const char *path, FILEINFO *info, int max);
int SMB2mkdir(const char *dir, int fileMode);
int SMB2rmdir(const char *dir);
int SMB2unlink(const char *dir);
int SMB2rename(const char *path, const char *newpath);

struct SMB2FH {
        int fd;   /* fileXio handle on the smb: device (SMB1 backend) */
};
struct SMB2FH *SMB2open(const char *path, int mode);
int SMB2close(struct SMB2FH *fh);
int SMB2read(struct SMB2FH *fh, char *buf, int size);
int SMB2write(struct SMB2FH *fh, char *buf, int size);
int SMB2lseek(struct SMB2FH *fh, int where, int how);
int SMB2stat(const char *path, int *is_dir);

//---------------------------------------------------------------------------
//End of file: smb2.h
//---------------------------------------------------------------------------
