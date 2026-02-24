// fs/vfs.c - 仮想ファイルシステム層
#include "../include/kernel/vfs.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

static vnode_t* vfs_root = NULL;

// ===== 文字列ユーティリティ =====
static size_t kstrlen(const char* s) { size_t n=0; while(s[n]) n++; return n; }
static void   kstrcpy(char* d, const char* s) { while((*d++=*s++)); }
static int    kstrcmp(const char* a, const char* b) {
    while(*a && *a==*b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int    kstrncmp(const char*a, const char*b, size_t n) {
    for(size_t i=0;i<n;i++){
        if(a[i]!=b[i]) return (unsigned char)a[i]-(unsigned char)b[i];
        if(!a[i]) return 0;
    }
    return 0;
}

// ===== VFS API =====
void vfs_init(void) { vfs_root = NULL; }

int vfs_mount(const char* path, vnode_t* fs_root) {
    if (kstrcmp(path, "/") == 0) {
        vfs_root = fs_root;
        return 0;
    }
    vnode_t* node = vfs_lookup(path);
    if (!node) return -ENOENT;
    node->mount_point = fs_root;
    return 0;
}

vnode_t* vfs_get_root(void) { return vfs_root; }

// パスを "/" で分割しながらたどる
vnode_t* vfs_lookup(const char* path) {
    if (!path || !vfs_root) return NULL;

    vnode_t* node = vfs_root;
    if (path[0] == '/') path++;

    char part[VFS_NAME_LEN];
    while (*path) {
        // パート切り出し
        int i = 0;
        while (*path && *path != '/') part[i++] = *path++;
        part[i] = 0;
        if (*path == '/') path++;

        if (i == 0) continue;
        if (kstrcmp(part, ".") == 0) continue;
        if (kstrcmp(part, "..") == 0) {
            // 親へ (簡易実装: rootで止まる)
            continue;
        }

        // マウントポイント解決
        if (node->mount_point) node = node->mount_point;

        if (!node->ops || !node->ops->finddir) return NULL;
        node = node->ops->finddir(node, part);
        if (!node) return NULL;
    }

    if (node && node->mount_point) node = node->mount_point;
    return node;
}

// ===== file_t 操作 =====
file_t* file_open(const char* path, int flags) {
    vnode_t* node = vfs_lookup(path);

    if (!node) {
        if (!(flags & O_CREAT)) return NULL;
        // 親ディレクトリを見つけてcreate
        char parent_path[VFS_PATH_LEN];
        const char* slash = path + kstrlen(path) - 1;
        while (slash > path && *slash != '/') slash--;
        size_t plen = slash - path;
        if (plen == 0) plen = 1;
        for (size_t i = 0; i < plen; i++) parent_path[i] = path[i];
        parent_path[plen] = 0;

        vnode_t* parent = vfs_lookup(plen > 1 ? parent_path : "/");
        if (!parent || !parent->ops || !parent->ops->create) return NULL;
        const char* fname = slash + 1;
        if (*slash == '/' && slash == path) fname = path + 1;
        if (parent->ops->create(parent, fname, VFS_FILE) < 0) return NULL;
        node = parent->ops->finddir(parent, fname);
        if (!node) return NULL;
    }

    if (node->ops && node->ops->open) node->ops->open(node, flags);

    if (flags & O_TRUNC && node->ops && node->ops->truncate)
        node->ops->truncate(node, 0);

    file_t* f = (file_t*)kmalloc(sizeof(file_t));
    f->vnode  = node;
    f->flags  = flags;
    f->offset = (flags & O_APPEND) ? (off_t)node->size : 0;
    f->ref    = 1;
    node->ref_count++;
    return f;
}

int file_close(file_t* f) {
    if (!f) return -EBADF;
    f->ref--;
    if (f->ref <= 0) {
        if (f->vnode->ops && f->vnode->ops->close)
            f->vnode->ops->close(f->vnode);
        f->vnode->ref_count--;
        kfree(f);
    }
    return 0;
}

ssize_t file_read(file_t* f, void* buf, size_t size) {
    if (!f || !f->vnode) return -EBADF;
    if (!f->vnode->ops || !f->vnode->ops->read) return -EIO;
    ssize_t n = f->vnode->ops->read(f->vnode, f->offset, size, buf);
    if (n > 0) f->offset += n;
    return n;
}

ssize_t file_write(file_t* f, const void* buf, size_t size) {
    if (!f || !f->vnode) return -EBADF;
    if (!f->vnode->ops || !f->vnode->ops->write) return -EIO;
    ssize_t n = f->vnode->ops->write(f->vnode, f->offset, size, buf);
    if (n > 0) f->offset += n;
    return n;
}

int file_readdir(file_t* f, uint32_t index, char* name_out) {
    if (!f || !f->vnode || !f->vnode->ops || !f->vnode->ops->readdir) return -EBADF;
    return f->vnode->ops->readdir(f->vnode, index, name_out);
}

off_t file_seek(file_t* f, off_t offset, int whence) {
    if (!f) return -EBADF;
    if (whence == SEEK_SET) f->offset = offset;
    else if (whence == SEEK_CUR) f->offset += offset;
    else if (whence == SEEK_END) f->offset = (off_t)f->vnode->size + offset;
    return f->offset;
}

int file_stat(file_t* f, stat_t* st) {
    if (!f || !st) return -EBADF;
    if (f->vnode->ops && f->vnode->ops->stat) return f->vnode->ops->stat(f->vnode, st);
    st->st_ino  = f->vnode->inode;
    st->st_size = f->vnode->size;
    st->st_mode = (f->vnode->type == VFS_DIR) ? S_IFDIR : S_IFREG;
    return 0;
}
