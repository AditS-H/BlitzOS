// vfs.h - Virtual filesystem layer, backed by an in-memory ramfs.
//
// The design is deliberately small: a fixed pool of nodes, each pointing at a
// heap-allocated data buffer, with parent indices forming the directory tree.
// No blocks, no inodes on disk, no journalling. It exists so that "everything
// is a file" becomes true in BlitzOS, and so the shell has something real to
// operate on.
//
// When a disk driver lands, the node operations below become the interface
// that an on-disk filesystem plugs into - that is why paths, descriptors and
// the node abstraction are separated from the ramfs storage.

#ifndef KERNEL_FS_VFS_H
#define KERNEL_FS_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_MAX_NODES      128    // total files + directories
#define VFS_NAME_MAX       32     // per path component, including NUL
#define VFS_PATH_MAX       256
#define VFS_MAX_FILE_SIZE  65536  // 64 KB ceiling per file
#define VFS_MAX_OPEN_FILES 32

#define VFS_ROOT_NODE 0
#define VFS_INVALID   (-1)

// Open flags
#define VFS_O_READ   0x01
#define VFS_O_WRITE  0x02
#define VFS_O_CREATE 0x04
#define VFS_O_APPEND 0x08
#define VFS_O_TRUNC  0x10

// Error codes (negative, so they never collide with a valid node index or
// byte count).
#define VFS_ERR_NOT_FOUND   (-2)
#define VFS_ERR_EXISTS      (-3)
#define VFS_ERR_NOT_DIR     (-4)
#define VFS_ERR_IS_DIR      (-5)
#define VFS_ERR_NO_SPACE    (-6)
#define VFS_ERR_NAME_TOO_LONG (-7)
#define VFS_ERR_NOT_EMPTY   (-8)
#define VFS_ERR_BAD_FD      (-9)
#define VFS_ERR_TOO_LARGE   (-10)
#define VFS_ERR_INVALID     (-11)

typedef enum {
    VFS_NODE_FREE = 0,
    VFS_NODE_FILE = 1,
    VFS_NODE_DIR  = 2
} vfs_node_type_t;

typedef struct {
    vfs_node_type_t type;
    char            name[VFS_NAME_MAX];
    int32_t         parent;         // index of the containing directory, -1 for root
    uint8_t*        data;           // file contents (NULL for directories)
    uint32_t        size;           // bytes currently used
    uint32_t        capacity;       // bytes allocated
    uint64_t        created_tick;
    uint64_t        modified_tick;
} vfs_node_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void vfs_init(void);

// ---------------------------------------------------------------------------
// Path operations
//
// Every function takes a `cwd` node index so relative paths work. Pass
// VFS_ROOT_NODE if you only ever use absolute paths.
// Supported syntax: "/abs/path", "relative/path", "." and "..".
// ---------------------------------------------------------------------------

// Returns a node index, or a negative VFS_ERR_* code.
int32_t vfs_resolve(const char* path, int32_t cwd);

// Create a file or directory. Parent directories must already exist.
int32_t vfs_create(const char* path, int32_t cwd, vfs_node_type_t type);

// Remove a file, or an empty directory.
int32_t vfs_unlink(const char* path, int32_t cwd);

// Write the absolute path of `node` into `buf`. Returns buf.
char* vfs_get_path(int32_t node, char* buf, size_t size);

// ---------------------------------------------------------------------------
// Node access
// ---------------------------------------------------------------------------

vfs_node_t* vfs_get_node(int32_t index);

// Iterate the children of `dir`. Pass *cursor = 0 to start; the function
// updates it. Returns the child index, or VFS_INVALID when the directory is
// exhausted.
int32_t vfs_read_dir(int32_t dir, uint32_t* cursor);

// Byte counts on success, negative VFS_ERR_* on failure.
int32_t vfs_read_node(int32_t node, uint32_t offset, void* buf, uint32_t len);
int32_t vfs_write_node(int32_t node, uint32_t offset, const void* buf, uint32_t len);
int32_t vfs_truncate(int32_t node);

// ---------------------------------------------------------------------------
// File descriptors (what the read/write/open/close syscalls use)
// ---------------------------------------------------------------------------

int32_t vfs_open(const char* path, int32_t cwd, uint32_t flags);
int32_t vfs_close(int32_t fd);
int32_t vfs_read(int32_t fd, void* buf, uint32_t len);
int32_t vfs_write(int32_t fd, const void* buf, uint32_t len);
int32_t vfs_seek(int32_t fd, int64_t offset, int whence);  // 0=SET 1=CUR 2=END

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

uint32_t    vfs_used_nodes(void);
uint32_t    vfs_total_bytes(void);
const char* vfs_error_string(int32_t error);

#endif // KERNEL_FS_VFS_H
