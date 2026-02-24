// include/kernel/vfs.h - 仮想ファイルシステム
#pragma once
#include "types.h"

#define VFS_NAME_LEN 128
#define VFS_PATH_LEN 256

#define VFS_FILE    1
#define VFS_DIR     2
#define VFS_CHARDEV 3
#define VFS_PIPE    4

struct vnode;

typedef struct stat_t {
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_size;
    uint32_t st_uid;
    uint32_t st_gid;
} stat_t;

typedef struct vnode_ops {
    int  (*open)   (struct vnode*, int flags);
    int  (*close)  (struct vnode*);
    ssize_t (*read)(struct vnode*, off_t offset, size_t size, void* buf);
    ssize_t (*write)(struct vnode*, off_t offset, size_t size, const void* buf);
    int  (*readdir)(struct vnode*, uint32_t index, char* name_out);
    struct vnode* (*finddir)(struct vnode*, const char* name);
    int  (*create) (struct vnode*, const char* name, uint32_t type);
    int  (*unlink) (struct vnode*, const char* name);
    int  (*stat)   (struct vnode*, stat_t* st);
    int  (*truncate)(struct vnode*, size_t size);
} vnode_ops_t;

typedef struct vnode {
    char         name[VFS_NAME_LEN];
    uint32_t     type;
    uint32_t     size;
    uint32_t     inode;
    uint32_t     uid, gid;
    uint32_t     permissions;
    vnode_ops_t* ops;
    void*        data;       // FS固有データ
    struct vnode* mount_point; // マウント先
    uint32_t     ref_count;
} vnode_t;

typedef struct {
    off_t    offset;
    vnode_t* vnode;
    int      flags;
    int      ref;
} file_t;

// VFS API
void     vfs_init(void);
vnode_t* vfs_lookup(const char* path);
vnode_t* vfs_lookup_from(vnode_t* base, const char* path);
int      vfs_mount(const char* path, vnode_t* fs_root);
vnode_t* vfs_get_root(void);

// ファイル操作
file_t*  file_open(const char* path, int flags);
int      file_close(file_t* f);
ssize_t  file_read(file_t* f, void* buf, size_t size);
ssize_t  file_write(file_t* f, const void* buf, size_t size);
int      file_readdir(file_t* f, uint32_t index, char* name_out);
off_t    file_seek(file_t* f, off_t offset, int whence);
int      file_stat(file_t* f, stat_t* st);

// ramfs
vnode_t* ramfs_create_root(void);
