#include <stdint.h>

extern void* __k_application_current(void);

static int constructor_saw_active_application;

__attribute__((constructor))
static void application_static_copy_probe_constructor(void) {
    constructor_saw_active_application = __k_application_current() != 0;
}

int application_static_copy_constructor_saw_active(void) {
    return constructor_saw_active_application;
}

int application_static_copy_observes(void* expected) {
    return __k_application_current() == expected;
}
