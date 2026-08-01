#include "vfs.h"
#include "../../lib/string.h"
#include "../../lib/kprintf.h"
#include "../../mm/kheap.h"
#include "../../proc/process.h"

// The whole filesystem: a flat pool of nodes linked by parent index. Slot 0 is
// always the root directory.
static vfs_node_t nodes[VFS_MAX_NODES];

typedef struct {
    int32_t  node;
    uint32_t offset;
    uint32_t flags;
    uint8_t  in_use;
} vfs_file_t;

static vfs_file_t open_files[VFS_MAX_OPEN_FILES];

// ---------------------------------------------------------------------------
// Node pool
// ---------------------------------------------------------------------------

static int32_t node_alloc(void)
{
    for (int32_t i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].type == VFS_NODE_FREE) {
            memset(&nodes[i], 0, sizeof(vfs_node_t));
            nodes[i].parent = VFS_INVALID;
            return i;
        }
    }
    return VFS_ERR_NO_SPACE;
}

vfs_node_t* vfs_get_node(int32_t index)
{
    if (index < 0 || index >= VFS_MAX_NODES || nodes[index].type == VFS_NODE_FREE) {
        return NULL;
    }
    return &nodes[index];
}

// Find a direct child of `dir` by name.
static int32_t find_child(int32_t dir, const char* name, size_t name_len)
{
    if (dir < 0 || dir >= VFS_MAX_NODES || nodes[dir].type != VFS_NODE_DIR) {
        return VFS_ERR_NOT_DIR;
    }

    for (int32_t i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].type == VFS_NODE_FREE || nodes[i].parent != dir) {
            continue;
        }
        if (strlen(nodes[i].name) == name_len &&
            strncmp(nodes[i].name, name, name_len) == 0) {
            return i;
        }
    }
    return VFS_ERR_NOT_FOUND;
}

int32_t vfs_read_dir(int32_t dir, uint32_t* cursor)
{
    if (!cursor || dir < 0 || dir >= VFS_MAX_NODES) {
        return VFS_INVALID;
    }

    while (*cursor < VFS_MAX_NODES) {
        int32_t i = (int32_t)(*cursor);
        (*cursor)++;

        if (nodes[i].type != VFS_NODE_FREE && nodes[i].parent == dir) {
            return i;
        }
    }
    return VFS_INVALID;
}

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

// Walks `path` one component at a time. If `stop_before_last` is set, the walk
// stops at the parent directory and *last_component points at the final name -
// which is what create and unlink need.
static int32_t resolve_internal(const char* path, int32_t cwd,
                                int stop_before_last,
                                const char** last_component,
                                size_t* last_len)
{
    if (!path) {
        return VFS_ERR_INVALID;
    }

    int32_t current = cwd;

    // A leading slash means start from the root.
    if (*path == '/') {
        current = VFS_ROOT_NODE;
        while (*path == '/') {
            path++;
        }
    }

    if (current < 0 || current >= VFS_MAX_NODES ||
        nodes[current].type == VFS_NODE_FREE) {
        current = VFS_ROOT_NODE;
    }

    if (last_component) *last_component = NULL;
    if (last_len)       *last_len = 0;

    while (*path) {
        // Measure this component.
        const char* start = path;
        size_t      len   = 0;
        while (path[len] && path[len] != '/') {
            len++;
        }
        path += len;
        while (*path == '/') {
            path++;
        }

        if (len == 0) {
            continue;
        }
        if (len >= VFS_NAME_MAX) {
            return VFS_ERR_NAME_TOO_LONG;
        }

        // If this was the last component and the caller wants the parent,
        // hand the name back instead of descending.
        if (stop_before_last && *path == '\0') {
            if (last_component) *last_component = start;
            if (last_len)       *last_len = len;
            return current;
        }

        // "." stays put; ".." climbs (and clamps at the root).
        if (len == 1 && start[0] == '.') {
            continue;
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (nodes[current].parent >= 0) {
                current = nodes[current].parent;
            }
            continue;
        }

        if (nodes[current].type != VFS_NODE_DIR) {
            return VFS_ERR_NOT_DIR;
        }

        int32_t child = find_child(current, start, len);
        if (child < 0) {
            return VFS_ERR_NOT_FOUND;
        }
        current = child;
    }

    // Path was "/" or "." - the caller asked for a name but there is none.
    if (stop_before_last && last_component && *last_component == NULL) {
        return VFS_ERR_INVALID;
    }

    return current;
}

int32_t vfs_resolve(const char* path, int32_t cwd)
{
    return resolve_internal(path, cwd, 0, NULL, NULL);
}

char* vfs_get_path(int32_t node, char* buf, size_t size)
{
    if (!buf || size == 0) {
        return buf;
    }

    if (node == VFS_ROOT_NODE || !vfs_get_node(node)) {
        strlcpy(buf, "/", size);
        return buf;
    }

    // Collect names from the node up to the root, then emit them reversed.
    const char* parts[VFS_MAX_NODES];
    int         count = 0;
    int32_t     cur   = node;

    while (cur > VFS_ROOT_NODE && count < VFS_MAX_NODES) {
        parts[count++] = nodes[cur].name;
        cur = nodes[cur].parent;
        if (cur < 0) {
            break;
        }
    }

    size_t pos = 0;
    for (int i = count - 1; i >= 0; i--) {
        if (pos + 1 < size) buf[pos++] = '/';

        size_t len = strlen(parts[i]);
        for (size_t j = 0; j < len && pos + 1 < size; j++) {
            buf[pos++] = parts[i][j];
        }
    }

    if (pos == 0 && size > 1) {
        buf[pos++] = '/';
    }
    buf[pos] = '\0';
    return buf;
}

// ---------------------------------------------------------------------------
// Create / remove
// ---------------------------------------------------------------------------

int32_t vfs_create(const char* path, int32_t cwd, vfs_node_type_t type)
{
    const char* name = NULL;
    size_t      name_len = 0;

    int32_t parent = resolve_internal(path, cwd, 1, &name, &name_len);
    if (parent < 0) {
        return parent;
    }
    if (!name || name_len == 0) {
        return VFS_ERR_INVALID;
    }
    if (nodes[parent].type != VFS_NODE_DIR) {
        return VFS_ERR_NOT_DIR;
    }
    if (find_child(parent, name, name_len) >= 0) {
        return VFS_ERR_EXISTS;
    }

    int32_t index = node_alloc();
    if (index < 0) {
        return index;
    }

    nodes[index].type   = type;
    nodes[index].parent = parent;
    memcpy(nodes[index].name, name, name_len);
    nodes[index].name[name_len] = '\0';

    nodes[index].data          = NULL;
    nodes[index].size          = 0;
    nodes[index].capacity      = 0;
    nodes[index].created_tick  = scheduler_get_ticks();
    nodes[index].modified_tick = nodes[index].created_tick;

    return index;
}

int32_t vfs_unlink(const char* path, int32_t cwd)
{
    int32_t node = vfs_resolve(path, cwd);
    if (node < 0) {
        return node;
    }
    if (node == VFS_ROOT_NODE) {
        return VFS_ERR_INVALID;   // cannot remove the root
    }

    // Refuse to orphan children.
    if (nodes[node].type == VFS_NODE_DIR) {
        uint32_t cursor = 0;
        if (vfs_read_dir(node, &cursor) != VFS_INVALID) {
            return VFS_ERR_NOT_EMPTY;
        }
    }

    // Invalidate any descriptors still pointing at it, so a later read/write
    // through a stale fd cannot touch freed memory.
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (open_files[i].in_use && open_files[i].node == node) {
            open_files[i].in_use = 0;
        }
    }

    if (nodes[node].data) {
        kfree(nodes[node].data);
    }
    memset(&nodes[node], 0, sizeof(vfs_node_t));
    nodes[node].type   = VFS_NODE_FREE;
    nodes[node].parent = VFS_INVALID;

    return 0;
}

// ---------------------------------------------------------------------------
// Read / write
// ---------------------------------------------------------------------------

// Grow a file's buffer to hold at least `needed` bytes. Capacity doubles to
// keep repeated appends from being quadratic.
static int32_t ensure_capacity(vfs_node_t* node, uint32_t needed)
{
    if (needed <= node->capacity) {
        return 0;
    }
    if (needed > VFS_MAX_FILE_SIZE) {
        return VFS_ERR_TOO_LARGE;
    }

    uint32_t new_capacity = node->capacity ? node->capacity : 64;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity > VFS_MAX_FILE_SIZE) {
        new_capacity = VFS_MAX_FILE_SIZE;
    }

    uint8_t* buffer = (uint8_t*)kmalloc(new_capacity);
    if (!buffer) {
        return VFS_ERR_NO_SPACE;
    }

    memset(buffer, 0, new_capacity);
    if (node->data && node->size) {
        memcpy(buffer, node->data, node->size);
    }
    if (node->data) {
        kfree(node->data);
    }

    node->data     = buffer;
    node->capacity = new_capacity;
    return 0;
}

int32_t vfs_read_node(int32_t index, uint32_t offset, void* buf, uint32_t len)
{
    vfs_node_t* node = vfs_get_node(index);
    if (!node)                       return VFS_ERR_NOT_FOUND;
    if (node->type == VFS_NODE_DIR)  return VFS_ERR_IS_DIR;
    if (!buf)                        return VFS_ERR_INVALID;

    if (offset >= node->size) {
        return 0;  // reading at or past EOF
    }

    uint32_t available = node->size - offset;
    if (len > available) {
        len = available;
    }

    memcpy(buf, node->data + offset, len);
    return (int32_t)len;
}

int32_t vfs_write_node(int32_t index, uint32_t offset, const void* buf, uint32_t len)
{
    vfs_node_t* node = vfs_get_node(index);
    if (!node)                       return VFS_ERR_NOT_FOUND;
    if (node->type == VFS_NODE_DIR)  return VFS_ERR_IS_DIR;
    if (!buf)                        return VFS_ERR_INVALID;
    if (len == 0)                    return 0;

    if (offset > VFS_MAX_FILE_SIZE || len > VFS_MAX_FILE_SIZE - offset) {
        return VFS_ERR_TOO_LARGE;
    }

    int32_t result = ensure_capacity(node, offset + len);
    if (result < 0) {
        return result;
    }

    // Writing past EOF leaves a hole; zero it so callers never see stale heap.
    if (offset > node->size) {
        memset(node->data + node->size, 0, offset - node->size);
    }

    memcpy(node->data + offset, buf, len);

    if (offset + len > node->size) {
        node->size = offset + len;
    }
    node->modified_tick = scheduler_get_ticks();

    return (int32_t)len;
}

int32_t vfs_truncate(int32_t index)
{
    vfs_node_t* node = vfs_get_node(index);
    if (!node)                      return VFS_ERR_NOT_FOUND;
    if (node->type == VFS_NODE_DIR) return VFS_ERR_IS_DIR;

    node->size          = 0;
    node->modified_tick = scheduler_get_ticks();
    return 0;
}

// ---------------------------------------------------------------------------
// File descriptors
//
// Descriptors 0/1/2 are reserved for stdin/stdout/stderr, which the syscall
// layer handles directly, so real files start at fd 3.
// ---------------------------------------------------------------------------

#define FD_FIRST_REAL 3

int32_t vfs_open(const char* path, int32_t cwd, uint32_t flags)
{
    int32_t node = vfs_resolve(path, cwd);

    if (node < 0) {
        if (!(flags & VFS_O_CREATE)) {
            return VFS_ERR_NOT_FOUND;
        }
        node = vfs_create(path, cwd, VFS_NODE_FILE);
        if (node < 0) {
            return node;
        }
    }

    if (nodes[node].type == VFS_NODE_DIR) {
        return VFS_ERR_IS_DIR;
    }

    if (flags & VFS_O_TRUNC) {
        vfs_truncate(node);
    }

    for (int32_t fd = FD_FIRST_REAL; fd < VFS_MAX_OPEN_FILES; fd++) {
        if (open_files[fd].in_use) {
            continue;
        }

        open_files[fd].in_use = 1;
        open_files[fd].node   = node;
        open_files[fd].flags  = flags;
        open_files[fd].offset = (flags & VFS_O_APPEND) ? nodes[node].size : 0;
        return fd;
    }

    return VFS_ERR_NO_SPACE;
}

static vfs_file_t* fd_lookup(int32_t fd)
{
    if (fd < FD_FIRST_REAL || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].in_use) {
        return NULL;
    }
    return &open_files[fd];
}

int32_t vfs_close(int32_t fd)
{
    vfs_file_t* file = fd_lookup(fd);
    if (!file) {
        return VFS_ERR_BAD_FD;
    }
    file->in_use = 0;
    return 0;
}

int32_t vfs_read(int32_t fd, void* buf, uint32_t len)
{
    vfs_file_t* file = fd_lookup(fd);
    if (!file) {
        return VFS_ERR_BAD_FD;
    }
    if (!(file->flags & VFS_O_READ)) {
        return VFS_ERR_INVALID;
    }

    int32_t read = vfs_read_node(file->node, file->offset, buf, len);
    if (read > 0) {
        file->offset += (uint32_t)read;
    }
    return read;
}

int32_t vfs_write(int32_t fd, const void* buf, uint32_t len)
{
    vfs_file_t* file = fd_lookup(fd);
    if (!file) {
        return VFS_ERR_BAD_FD;
    }
    if (!(file->flags & VFS_O_WRITE)) {
        return VFS_ERR_INVALID;
    }

    int32_t written = vfs_write_node(file->node, file->offset, buf, len);
    if (written > 0) {
        file->offset += (uint32_t)written;
    }
    return written;
}

int32_t vfs_seek(int32_t fd, int64_t offset, int whence)
{
    vfs_file_t* file = fd_lookup(fd);
    if (!file) {
        return VFS_ERR_BAD_FD;
    }

    vfs_node_t* node = vfs_get_node(file->node);
    if (!node) {
        return VFS_ERR_NOT_FOUND;
    }

    int64_t base;
    switch (whence) {
    case 0:  base = 0;                  break;  // SEEK_SET
    case 1:  base = (int64_t)file->offset; break;  // SEEK_CUR
    case 2:  base = (int64_t)node->size;   break;  // SEEK_END
    default: return VFS_ERR_INVALID;
    }

    int64_t target = base + offset;
    if (target < 0 || target > VFS_MAX_FILE_SIZE) {
        return VFS_ERR_INVALID;
    }

    file->offset = (uint32_t)target;
    return (int32_t)file->offset;
}

// ---------------------------------------------------------------------------
// Setup and statistics
// ---------------------------------------------------------------------------

uint32_t vfs_used_nodes(void)
{
    uint32_t used = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].type != VFS_NODE_FREE) {
            used++;
        }
    }
    return used;
}

uint32_t vfs_total_bytes(void)
{
    uint32_t total = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].type == VFS_NODE_FILE) {
            total += nodes[i].size;
        }
    }
    return total;
}

const char* vfs_error_string(int32_t error)
{
    switch (error) {
    case VFS_ERR_NOT_FOUND:      return "no such file or directory";
    case VFS_ERR_EXISTS:         return "already exists";
    case VFS_ERR_NOT_DIR:        return "not a directory";
    case VFS_ERR_IS_DIR:         return "is a directory";
    case VFS_ERR_NO_SPACE:       return "filesystem full";
    case VFS_ERR_NAME_TOO_LONG:  return "name too long";
    case VFS_ERR_NOT_EMPTY:      return "directory not empty";
    case VFS_ERR_BAD_FD:         return "bad file descriptor";
    case VFS_ERR_TOO_LARGE:      return "file too large";
    case VFS_ERR_INVALID:        return "invalid argument";
    default:                     return "unknown error";
    }
}

void vfs_init(void)
{
    memset(nodes, 0, sizeof(nodes));
    memset(open_files, 0, sizeof(open_files));

    // Root directory.
    nodes[VFS_ROOT_NODE].type   = VFS_NODE_DIR;
    nodes[VFS_ROOT_NODE].parent = VFS_INVALID;
    strlcpy(nodes[VFS_ROOT_NODE].name, "/", VFS_NAME_MAX);

    // A small starting tree, so `ls` shows something on first boot and there
    // is an obvious place for future subsystems to publish state.
    vfs_create("/home", VFS_ROOT_NODE, VFS_NODE_DIR);
    vfs_create("/tmp",  VFS_ROOT_NODE, VFS_NODE_DIR);
    vfs_create("/dev",  VFS_ROOT_NODE, VFS_NODE_DIR);

    int32_t readme = vfs_create("/home/readme.txt", VFS_ROOT_NODE, VFS_NODE_FILE);
    if (readme >= 0) {
        const char* text =
            "BlitzOS ramfs\n"
            "-------------\n"
            "This filesystem lives entirely in RAM and is rebuilt on every boot.\n"
            "Try: ls /home, cat /home/readme.txt, write /tmp/note hello, cat /tmp/note\n";
        vfs_write_node(readme, 0, text, (uint32_t)strlen(text));
    }

    kok("ramfs mounted at / (%u nodes, %u KB max file size)\n",
        (uint32_t)VFS_MAX_NODES, (uint32_t)(VFS_MAX_FILE_SIZE / 1024));
}
