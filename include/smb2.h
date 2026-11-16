/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
//---------------------------------------------------------------------------
//File name:   smb2.h
//---------------------------------------------------------------------------
#include "netfs_compat.h"

/* One configured SMB share. The public fields (name/user/password/url/next)
 * are unchanged from the libsmb2 version; the server/port/sharename fields
 * are the parsed SMB1 target used by the smbman backend.
 * 'smb2' is retained only so existing consumers keep compiling; the SMB1
 * backend does not use it. */
struct smb2_share {
        struct smb2_share *next;
        void *smb2;
        char *name;
        const char *user;
        const char *password;
        char *url;
        char server[64];
        int  port;
        char sharename[256];
};

extern struct smb2_share *smb2_shares;

int init_smb2(const char *ip, const char *netmask, const char *gw);
void deinit_smb2(void);
int readSMB2(const char *path, FILEINFO *info, int max);
int SMB2mkdir(const char *dir, int fileMode);
int SMB2rmdir(const char *dir);
int SMB2unlink(const char *dir);
int SMB2rename(const char *path, const char *newpath);

/* SMB1 backend handle: just a fileXio fd (the rest of wLE treats this as an
 * opaque pointer, so no caller needs to change). */
struct SMB2FH {
        int fd;
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
