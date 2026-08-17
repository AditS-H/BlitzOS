#include "elf.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "../proc/process.h"
#include "../fs/vfs/vfs.h"
#include "../boot/multiboot2.h"
#include "../core/panic.h"

static elf_image_t images[ELF_MAX_LOADED];

// The process entry point needs to know which image it belongs to, but
// process_create() takes a plain void(*)(void) with no argument. This holds the
// handoff between elf_exec() creating the process and the trampoline running.
//
// Safe because elf_exec() is only called from the shell, one at a time; a
// general solution would add a userdata pointer to process_t.
static volatile int32_t pending_image = -1;

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

static int32_t validate_header(const elf64_ehdr_t* header, size_t size)
{
    if (size < sizeof(elf64_ehdr_t)) {
        return ELF_ERR_TRUNCATED;
    }

    if (header->e_ident[0] != ELF_MAGIC0 || header->e_ident[1] != ELF_MAGIC1 ||
        header->e_ident[2] != ELF_MAGIC2 || header->e_ident[3] != ELF_MAGIC3) {
        return ELF_ERR_BAD_MAGIC;
    }

    if (header->e_ident[4] != ELFCLASS64) {
        return ELF_ERR_NOT_64BIT;
    }
    if (header->e_ident[5] != ELFDATA2LSB) {
        return ELF_ERR_BAD_ARCH;
    }
    if (header->e_machine != EM_X86_64) {
        return ELF_ERR_BAD_ARCH;
    }
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        return ELF_ERR_BAD_TYPE;
    }

    // The program header table has to be inside the file.
    uint64_t table_end = header->e_phoff +
                         (uint64_t)header->e_phnum * header->e_phentsize;
    if (header->e_phnum == 0 || table_end > size) {
        return ELF_ERR_NO_SEGMENTS;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Relocation
//
// A static PIE contains a .rela.dyn table of R_X86_64_RELATIVE entries. Each
// one says: the 64-bit word at (base + r_offset) should hold (base + r_addend).
// Without applying them, every absolute pointer the program has - string
// literals, function pointers, vtables - points at address zero plus an offset,
// and the first use faults.
// ---------------------------------------------------------------------------

static uint32_t apply_relocations(const elf64_ehdr_t* header,
                                  const uint8_t* file,
                                  uint64_t load_base)
{
    const elf64_phdr_t* phdrs =
        (const elf64_phdr_t*)(file + header->e_phoff);

    // Find PT_DYNAMIC, which points at the relocation table.
    const elf64_dyn_t* dynamic = NULL;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dynamic = (const elf64_dyn_t*)(load_base + phdrs[i].p_vaddr);
            break;
        }
    }

    if (!dynamic) {
        return 0;   // statically linked with no dynamic section; nothing to do
    }

    uint64_t rela_address = 0;
    uint64_t rela_size    = 0;
    uint64_t rela_entry   = sizeof(elf64_rela_t);

    for (const elf64_dyn_t* entry = dynamic; entry->d_tag != DT_NULL; entry++) {
        switch (entry->d_tag) {
        case DT_RELA:    rela_address = entry->d_val; break;
        case DT_RELASZ:  rela_size    = entry->d_val; break;
        case DT_RELAENT: rela_entry   = entry->d_val; break;
        default: break;
        }
    }

    if (!rela_address || !rela_size || !rela_entry) {
        return 0;
    }

    uint32_t applied = 0;
    uint64_t count   = rela_size / rela_entry;

    for (uint64_t i = 0; i < count; i++) {
        const elf64_rela_t* rela =
            (const elf64_rela_t*)(load_base + rela_address + i * rela_entry);

        if (ELF64_R_TYPE(rela->r_info) != R_X86_64_RELATIVE) {
            // Anything else needs a symbol table and a linker, which a
            // -static-pie binary should never produce.
            continue;
        }

        uint64_t* target = (uint64_t*)(load_base + rela->r_offset);
        *target = load_base + (uint64_t)rela->r_addend;
        applied++;
    }

    return applied;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

static int32_t allocate_slot(void)
{
    for (int32_t i = 0; i < ELF_MAX_LOADED; i++) {
        if (!images[i].in_use) {
            return i;
        }
    }
    return ELF_ERR_TOO_MANY;
}

int32_t elf_load(const void* image, size_t size, const char* name)
{
    const uint8_t*      file   = (const uint8_t*)image;
    const elf64_ehdr_t* header = (const elf64_ehdr_t*)image;

    int32_t error = validate_header(header, size);
    if (error < 0) {
        return error;
    }

    int32_t slot = allocate_slot();
    if (slot < 0) {
        return slot;
    }

    const elf64_phdr_t* phdrs = (const elf64_phdr_t*)(file + header->e_phoff);

    // ---- Work out the memory span the program needs ----
    uint64_t lowest  = ~0ULL;
    uint64_t highest = 0;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) {
            continue;
        }
        if (phdrs[i].p_vaddr < lowest) {
            lowest = phdrs[i].p_vaddr;
        }
        if (phdrs[i].p_vaddr + phdrs[i].p_memsz > highest) {
            highest = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        }
    }

    if (lowest == ~0ULL || highest <= lowest) {
        return ELF_ERR_NO_SEGMENTS;
    }

    uint64_t span  = highest - lowest;
    uint64_t pages = (span + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t load_base = 0;
    uint64_t span_start, span_end;

    if (header->e_type == ET_DYN) {
        // Position independent: put it wherever the allocator has room.
        void* memory = pmm_alloc_pages(pages);
        if (!memory) {
            return ELF_ERR_NO_MEMORY;
        }

        // p_vaddr values in a PIE are offsets from 0, so the base is simply
        // where we put it.
        load_base  = (uint64_t)memory;
        span_start = (uint64_t)memory;
        span_end   = span_start + pages * PAGE_SIZE;
    } else {
        // Fixed address: the program must go exactly where it was linked.
        load_base  = 0;
        span_start = lowest & ~(uint64_t)(PAGE_SIZE - 1);
        span_end   = span_start + pages * PAGE_SIZE;

        if (!pmm_reserve_range(span_start, span_end - span_start)) {
            kerror("elf: address range 0x%lx-0x%lx is already in use.\n"
                   "  A fixed-address (ET_EXEC) program can only be loaded "
                   "once at a time.\n"
                   "  Rebuild with -static-pie to get a relocatable image.\n",
                   span_start, span_end);
            return ELF_ERR_OCCUPIED;
        }
    }

    // ---- Copy the segments in ----
    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf64_phdr_t* segment = &phdrs[i];

        if (segment->p_type != PT_LOAD) {
            continue;
        }

        if (segment->p_offset + segment->p_filesz > size) {
            kerror("elf: segment %u runs past the end of the file\n", i);
            return ELF_ERR_TRUNCATED;
        }

        uint8_t* destination = (uint8_t*)(load_base + segment->p_vaddr);

        memcpy(destination, file + segment->p_offset, segment->p_filesz);

        // p_memsz > p_filesz means .bss: memory the program expects to exist,
        // zeroed, that occupies no space in the file. Skipping this is a
        // classic loader bug - the program starts with garbage globals.
        if (segment->p_memsz > segment->p_filesz) {
            memset(destination + segment->p_filesz, 0,
                   segment->p_memsz - segment->p_filesz);
        }
    }

    // ---- Fix up absolute addresses ----
    uint32_t relocations = 0;
    if (header->e_type == ET_DYN) {
        relocations = apply_relocations(header, file, load_base);
    }

    images[slot].in_use     = 1;
    images[slot].load_base  = load_base;
    images[slot].entry      = load_base + header->e_entry;
    images[slot].span_start = span_start;
    images[slot].span_end   = span_end;
    images[slot].pid        = 0;
    strlcpy(images[slot].name, name ? name : "program", sizeof(images[slot].name));

    kok("elf: loaded %s - %s, %lu KB at 0x%lx, entry 0x%lx%s\n",
        images[slot].name,
        header->e_type == ET_DYN ? "position independent" : "fixed address",
        (span_end - span_start) / 1024, span_start, images[slot].entry,
        relocations ? ", relocated" : "");

    if (relocations) {
        kinfo("elf: applied %u R_X86_64_RELATIVE relocations\n", relocations);
    }

    return slot;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

// Trampoline. process_create() cannot pass an argument, so the image index
// comes through pending_image, which the trampoline claims immediately.
static void elf_process_trampoline(void)
{
    int32_t index = pending_image;
    pending_image = -1;

    if (index < 0 || index >= ELF_MAX_LOADED || !images[index].in_use) {
        kerror("elf: trampoline started with no image\n");
        process_exit(-1);
    }

    // The program's entry point. From here on we are executing code that was
    // compiled separately, linked separately, and shipped in the ISO.
    void (*entry)(void) = (void (*)(void))images[index].entry;

    entry();

    // A well-behaved program calls exit(). If it just returns, clean up here.
    elf_release(index);
    process_exit(0);
}

int32_t elf_exec(const char* path, uint32_t priority)
{
    int32_t node = vfs_resolve(path, VFS_ROOT_NODE);
    if (node < 0) {
        return ELF_ERR_TRUNCATED;
    }

    vfs_node_t* file = vfs_get_node(node);
    if (!file || file->type != VFS_NODE_FILE || file->size == 0) {
        return ELF_ERR_TRUNCATED;
    }

    int32_t index = elf_load(file->data, file->size, file->name);
    if (index < 0) {
        return index;
    }

    pending_image = index;

    process_t* proc = process_create(images[index].name,
                                     elf_process_trampoline, priority);
    if (!proc) {
        pending_image = -1;
        elf_release(index);
        return ELF_ERR_NO_MEMORY;
    }

    images[index].pid = proc->pid;
    return (int32_t)proc->pid;
}

void elf_release(int32_t index)
{
    if (index < 0 || index >= ELF_MAX_LOADED || !images[index].in_use) {
        return;
    }

    uint64_t pages = (images[index].span_end - images[index].span_start) / PAGE_SIZE;
    pmm_free_pages((void*)images[index].span_start, pages);

    memset(&images[index], 0, sizeof(images[index]));
}

const elf_image_t* elf_image_at(uint32_t index)
{
    return index < ELF_MAX_LOADED ? &images[index] : NULL;
}

// ---------------------------------------------------------------------------
// Boot modules
// ---------------------------------------------------------------------------

void elf_load_boot_modules(void)
{
    uint32_t count = multiboot2_module_count();

    if (count == 0) {
        kinfo("elf: no boot modules - add `module2 /boot/bin/hello hello` to "
              "grub.cfg to ship a program\n");
        return;
    }

    vfs_create("/bin", VFS_ROOT_NODE, VFS_NODE_DIR);

    for (uint32_t i = 0; i < count; i++) {
        const multiboot_module_t* module = multiboot2_module_at(i);
        if (!module || module->end <= module->start) {
            continue;
        }

        // The command line is the module's name. Fall back to a generated one
        // if grub.cfg did not supply anything.
        char name[64];
        if (module->cmdline && module->cmdline[0]) {
            ksnprintf(name, sizeof(name), "/bin/%s", module->cmdline);
        } else {
            ksnprintf(name, sizeof(name), "/bin/module%u", i);
        }

        uint32_t size = module->end - module->start;

        int32_t node = vfs_create(name, VFS_ROOT_NODE, VFS_NODE_FILE);
        if (node < 0) {
            kwarn("elf: could not create %s in the ramfs (%s)\n",
                  name, vfs_error_string(node));
            continue;
        }

        int32_t written = vfs_write_node(node, 0,
                                         (const void*)(uint64_t)module->start,
                                         size);
        if (written < 0) {
            kwarn("elf: could not copy %s into the ramfs (%s)\n",
                  name, vfs_error_string(written));
            continue;
        }

        kok("elf: %s (%u bytes) available from the ramfs\n", name, size);
    }
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

void elf_describe(const void* image, size_t size)
{
    const uint8_t*      file   = (const uint8_t*)image;
    const elf64_ehdr_t* header = (const elf64_ehdr_t*)image;

    int32_t error = validate_header(header, size);
    if (error < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "  Not a loadable ELF64 image: %s\n",
                      elf_error_string(error));
        return;
    }

    kprintf("  Type       : %s\n",
            header->e_type == ET_EXEC ? "ET_EXEC (fixed address)"
                                      : "ET_DYN (position independent)");
    kprintf("  Machine    : x86-64\n");
    kprintf("  Entry      : 0x%lx\n", header->e_entry);
    kprintf("  Segments   : %u\n", header->e_phnum);

    const elf64_phdr_t* phdrs = (const elf64_phdr_t*)(file + header->e_phoff);

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf64_phdr_t* segment = &phdrs[i];

        const char* type;
        switch (segment->p_type) {
        case PT_LOAD:    type = "LOAD";    break;
        case PT_DYNAMIC: type = "DYNAMIC"; break;
        case PT_INTERP:  type = "INTERP";  break;
        case PT_NOTE:    type = "NOTE";    break;
        case PT_PHDR:    type = "PHDR";    break;
        default:         type = "other";   break;
        }

        kprintf("    [%u] %-8s vaddr 0x%-10lx file %-7lu mem %-7lu %c%c%c\n",
                i, type, segment->p_vaddr, segment->p_filesz, segment->p_memsz,
                (segment->p_flags & PF_R) ? 'r' : '-',
                (segment->p_flags & PF_W) ? 'w' : '-',
                (segment->p_flags & PF_X) ? 'x' : '-');

        // Call out .bss, since a loader that ignores it is a classic bug.
        if (segment->p_type == PT_LOAD && segment->p_memsz > segment->p_filesz) {
            kprintf_color(VGA_COLOR_DARK_GREY,
                          "          %lu bytes of .bss to zero on load\n",
                          segment->p_memsz - segment->p_filesz);
        }
    }
}

const char* elf_error_string(int32_t error)
{
    switch (error) {
    case ELF_ERR_BAD_MAGIC:   return "not an ELF file";
    case ELF_ERR_NOT_64BIT:   return "not a 64-bit ELF";
    case ELF_ERR_BAD_ARCH:    return "wrong architecture or endianness";
    case ELF_ERR_BAD_TYPE:    return "not an executable (need ET_EXEC or ET_DYN)";
    case ELF_ERR_NO_MEMORY:   return "out of memory";
    case ELF_ERR_TRUNCATED:   return "file truncated or missing";
    case ELF_ERR_NO_SEGMENTS: return "no loadable segments";
    case ELF_ERR_OCCUPIED:    return "load address already occupied";
    case ELF_ERR_TOO_MANY:    return "too many programs loaded";
    default:                  return "unknown error";
    }
}
