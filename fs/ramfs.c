// fs/ramfs.c - メモリ上のファイルシステム
#include "../include/kernel/vfs.h"
#include "../include/kernel/mm.h"
#include "../include/kernel/types.h"

#define RAMFS_MAX_CHILDREN 64
#define RAMFS_BLOCK_SIZE   4096

typedef struct ramfs_node {
    char     name[VFS_NAME_LEN];
    uint32_t type;    // VFS_FILE, VFS_DIR
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
    uint32_t inode;

    struct ramfs_node* children[RAMFS_MAX_CHILDREN];
    int                nchildren;

    vnode_t  vnode;
} ramfs_node_t;

static uint32_t next_inode = 1;

// 文字列操作
static size_t kstrlen(const char* s) { size_t n=0; while(s[n]) n++; return n; }
static void   kstrcpy(char* d, const char* s) { while((*d++=*s++)); }
static int    kstrcmp(const char* a, const char* b) {
    while(*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static void   kmemcpy(void* d, const void* s, size_t n) {
    uint8_t* dd=(uint8_t*)d; const uint8_t* ss=(const uint8_t*)s;
    for(size_t i=0;i<n;i++) dd[i]=ss[i];
}
static void   kmemset(void* d, int v, size_t n) {
    uint8_t* p=(uint8_t*)d; for(size_t i=0;i<n;i++) p[i]=(uint8_t)v;
}

// Vnode操作プロトタイプ
static int      ramfs_open(vnode_t* v, int flags) { (void)v;(void)flags; return 0; }
static int      ramfs_close(vnode_t* v) { (void)v; return 0; }
static ssize_t  ramfs_read(vnode_t* v, off_t off, size_t sz, void* buf);
static ssize_t  ramfs_write(vnode_t* v, off_t off, size_t sz, const void* buf);
static int      ramfs_readdir(vnode_t* v, uint32_t idx, char* name_out);
static vnode_t* ramfs_finddir(vnode_t* v, const char* name);
static int      ramfs_create(vnode_t* v, const char* name, uint32_t type);
static int      ramfs_unlink(vnode_t* v, const char* name);
static int      ramfs_stat(vnode_t* v, stat_t* st);
static int      ramfs_truncate(vnode_t* v, size_t size);

static vnode_ops_t ramfs_ops = {
    .open    = ramfs_open,
    .close   = ramfs_close,
    .read    = ramfs_read,
    .write   = ramfs_write,
    .readdir = ramfs_readdir,
    .finddir = ramfs_finddir,
    .create  = ramfs_create,
    .unlink  = ramfs_unlink,
    .stat    = ramfs_stat,
    .truncate = ramfs_truncate,
};

static ramfs_node_t* new_ramfs_node(const char* name, uint32_t type) {
    ramfs_node_t* n = (ramfs_node_t*)kmalloc(sizeof(ramfs_node_t));
    kmemset(n, 0, sizeof(ramfs_node_t));
    kstrcpy(n->name, name);
    n->type  = type;
    n->inode = next_inode++;

    // vnode 設定
    kstrcpy(n->vnode.name, name);
    n->vnode.type        = type;
    n->vnode.inode       = n->inode;
    n->vnode.ops         = &ramfs_ops;
    n->vnode.data        = n;
    n->vnode.permissions = (type == VFS_DIR) ?
        (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR) :
        (S_IFREG | S_IRUSR | S_IWUSR);
    return n;
}

static ssize_t ramfs_read(vnode_t* v, off_t off, size_t sz, void* buf) {
    ramfs_node_t* n = (ramfs_node_t*)v->data;
    if (n->type != VFS_FILE) return -EISDIR;
    if ((uint32_t)off >= n->size) return 0;
    size_t avail = n->size - (uint32_t)off;
    size_t to_read = (sz < avail) ? sz : avail;
    kmemcpy(buf, n->data + off, to_read);
    return (ssize_t)to_read;
}

static ssize_t ramfs_write(vnode_t* v, off_t off, size_t sz, const void* buf) {
    ramfs_node_t* n = (ramfs_node_t*)v->data;
    if (n->type != VFS_FILE) return -EISDIR;

    uint32_t needed = (uint32_t)off + (uint32_t)sz;
    if (needed > n->capacity) {
        uint32_t new_cap = needed + RAMFS_BLOCK_SIZE;
        n->data = (uint8_t*)krealloc(n->data, new_cap);
        n->capacity = new_cap;
    }
    kmemcpy(n->data + off, buf, sz);
    if (needed > n->size) n->size = needed;
    v->size = n->size;
    return (ssize_t)sz;
}

static int ramfs_readdir(vnode_t* v, uint32_t idx, char* name_out) {
    ramfs_node_t* n = (ramfs_node_t*)v->data;
    if (n->type != VFS_DIR) return -ENOTDIR;
    if ((int)idx >= n->nchildren) return -1;
    kstrcpy(name_out, n->children[idx]->name);
    return 0;
}

static vnode_t* ramfs_finddir(vnode_t* v, const char* name) {
    ramfs_node_t* n = (ramfs_node_t*)v->data;
    if (n->type != VFS_DIR) return NULL;
    for (int i = 0; i < n->nchildren; i++) {
        if (kstrcmp(n->children[i]->name, name) == 0)
            return &n->children[i]->vnode;
    }
    return NULL;
}

static int ramfs_create(vnode_t* v, const char* name, uint32_t type) {
    ramfs_node_t* parent = (ramfs_node_t*)v->data;
    if (parent->type != VFS_DIR) return -ENOTDIR;
    if (parent->nchildren >= RAMFS_MAX_CHILDREN) return -ENOSPC;

    ramfs_node_t* child = new_ramfs_node(name, type);
    parent->children[parent->nchildren++] = child;
    return 0;
}

static int ramfs_unlink(vnode_t* v, const char* name) {
    ramfs_node_t* parent = (ramfs_node_t*)v->data;
    for (int i = 0; i < parent->nchildren; i++) {
        if (kstrcmp(parent->children[i]->name, name) == 0) {
            // TODO: 再帰削除
            kfree(parent->children[i]);
            parent->children[i] = parent->children[--parent->nchildren];
            return 0;
        }
    }
    return -ENOENT;
}

static int ramfs_stat(vnode_t* v, stat_t* st) {
    ramfs_node_t* n = (ramfs_node_t*)v->data;
    st->st_ino  = n->inode;
    st->st_size = n->size;
    st->st_mode = (n->type == VFS_DIR) ? S_IFDIR : S_IFREG;
    st->st_uid  = 0; st->st_gid = 0;
    return 0;
}

static int ramfs_truncate(vnode_t* v, size_t size) {
    ramfs_node_t* n = (ramfs_node_t*)v->data;
    if (size == 0) { kfree(n->data); n->data = NULL; n->size = 0; n->capacity = 0; }
    else if (size < n->size) n->size = (uint32_t)size;
    v->size = n->size;
    return 0;
}

// ===== 公開API =====
vnode_t* ramfs_create_root(void) {
    ramfs_node_t* root = new_ramfs_node("/", VFS_DIR);
    return &root->vnode;
}

// ディレクトリ作成ヘルパ
void ramfs_mkdir(vnode_t* parent, const char* name) {
    if (parent->ops && parent->ops->create)
        parent->ops->create(parent, name, VFS_DIR);
}

// ファイル書き込みヘルパ
void ramfs_write_file(vnode_t* parent, const char* name, const char* data, size_t size) {
    if (!parent->ops) return;
    parent->ops->create(parent, name, VFS_FILE);
    vnode_t* f = parent->ops->finddir(parent, name);
    if (f && f->ops && f->ops->write)
        f->ops->write(f, 0, size, data);
}
