// demo.cpp - A C++ program running on BlitzOS.
//
// This exists to answer "can it run C++?" honestly. The answer is yes, with
// specific limits worth understanding:
//
// WHAT WORKS
//   Classes, constructors, destructors, member functions
//   Templates, references, operator overloading
//   Inheritance and virtual functions (vtables are just data, and the ELF
//   loader's relocation pass fixes up the vtable pointers)
//   constexpr and everything else resolved at compile time
//
// WHAT DOES NOT, AND WHY
//   Exceptions      need libgcc's unwinder and a personality routine, so we
//                   build with -fno-exceptions
//   RTTI            dynamic_cast and typeid need type_info objects from
//                   libstdc++, so -fno-rtti
//   new / delete    need a heap. There is no user-space allocator yet, so
//                   objects live on the stack or in static storage
//   The STL         std::vector, std::string and friends are libstdc++, which
//                   assumes a hosted environment
//   Global ctors    objects with non-trivial constructors at namespace scope
//                   are registered in .init_array, which something has to walk
//                   before main. run_global_constructors() below does exactly
//                   that - it is the job a normal crt1.o would do for you.
//
// So: real C++, freestanding. The same subset the Linux kernel would allow if
// it were written in C++.
//
//   Build:  make -C userland
//   Run:    run /bin/demo

#include <blitz.h>

// ---------------------------------------------------------------------------
// The runtime bits the compiler assumes exist
//
// These are not optional decoration - without them the link fails, and the
// error message is confusing enough to be worth explaining.
//
// operator delete: any class with a virtual destructor gets TWO destructors
// emitted. The complete object destructor (D1) just runs the destructor body.
// The *deleting* destructor (D0) runs the body and then calls operator delete,
// because `delete basePointer` has to free the right amount of memory and only
// the vtable knows the dynamic type. D0 goes in the vtable whether or not the
// program ever writes `delete` - so the reference exists even here, where every
// object is on the stack. The linker error reads:
//
//     undefined reference to `operator delete(void*, unsigned long)'
//
// Since there is no user-space heap, these are stubs. If a program ever
// genuinely deletes something they would need to be real.
//
// __cxa_pure_virtual: what a pure virtual call resolves to if it somehow
// happens (calling one during base-class construction, for instance). libstdc++
// would abort; we exit with an error.
// ---------------------------------------------------------------------------

void operator delete(void*) noexcept                { }
void operator delete[](void*) noexcept              { }
void operator delete(void*, size_t) noexcept        { }
void operator delete[](void*, size_t) noexcept      { }

extern "C" void __cxa_pure_virtual()
{
    print("fatal: pure virtual function called\n");
    exit(-1);
}

// ---------------------------------------------------------------------------
// A class with a vtable, to prove virtual dispatch survives loading
// ---------------------------------------------------------------------------

class Shape {
public:
    virtual ~Shape() { }
    virtual const char* name() const = 0;
    virtual uint32_t    area() const = 0;

    void describe() const
    {
        print("    ");
        print(name());
        print(" has area ");
        print_uint(area());
        print("\n");
    }
};

class Rectangle : public Shape {
public:
    Rectangle(uint32_t w, uint32_t h) : width_(w), height_(h) { }

    const char* name() const override { return "Rectangle"; }
    uint32_t    area() const override { return width_ * height_; }

private:
    uint32_t width_;
    uint32_t height_;
};

class Square : public Rectangle {
public:
    explicit Square(uint32_t side) : Rectangle(side, side) { }
    const char* name() const override { return "Square"; }
};

// ---------------------------------------------------------------------------
// A template, resolved entirely at compile time
// ---------------------------------------------------------------------------

template <typename T>
static T maximum(T a, T b)
{
    return a > b ? a : b;
}

// ---------------------------------------------------------------------------
// Global constructors
//
// A namespace-scope object with a real constructor cannot be initialised at
// compile time, so the compiler emits an initialiser function and records a
// pointer to it in the .init_array section. On a hosted system the C runtime
// walks that array before calling main. Freestanding, nobody does - unless we
// do it ourselves.
//
// Skip this and `counter` below stays zero-initialised, the constructor never
// runs, and the bug looks like "my global object is empty for no reason".
// ---------------------------------------------------------------------------

extern "C" {
    typedef void (*constructor_fn)();
    extern constructor_fn __init_array_start[];
    extern constructor_fn __init_array_end[];
}

static void run_global_constructors()
{
    for (constructor_fn* fn = __init_array_start; fn != __init_array_end; fn++) {
        (*fn)();
    }
}

class Counter {
public:
    Counter() : value_(42) { }   // non-trivial: needs .init_array
    uint32_t get() const { return value_; }
private:
    uint32_t value_;
};

static Counter global_counter;

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" void _start()
{
    run_global_constructors();

    print("\n");
    rainbow("C++ on BlitzOS");
    print("\n\n");

    print("  Virtual dispatch through a vtable:\n");

    Rectangle rectangle(8, 5);
    Square    square(6);

    // Polymorphism: the call goes through each object's vtable pointer, which
    // the ELF loader relocated when it placed the image in memory.
    const Shape* shapes[] = { &rectangle, &square };
    for (const Shape* shape : shapes) {
        shape->describe();
    }

    print("\n  Templates: maximum(17, 4) = ");
    print_uint(maximum<uint32_t>(17, 4));
    print("\n");

    print("  Global constructor ran: counter = ");
    print_uint(global_counter.get());
    print(global_counter.get() == 42 ? "  (correct)\n" : "  (WRONG - .init_array was skipped)\n");

    print("\n  Compiled with -fno-exceptions -fno-rtti -nostdlib -static-pie.\n");
    print("  Exiting.\n\n");

    exit(0);
}
