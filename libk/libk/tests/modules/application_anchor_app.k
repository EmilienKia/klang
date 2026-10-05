module application_anchor_app;

import modreg;

@ffi::Extern("C") private __k_application_activate(application: k::Application*): int;
@ffi::Extern("C") private abort();

global_constructor_saw_empty : bool = false;
main_saw_active : bool = false;
destructor_saw_empty : bool = false;

struct ApplicationGlobalProbe {
    static ApplicationGlobalProbe() {
        current : OptionalRef<k::Application> = k::Application::getApplication();
        global_constructor_saw_empty = !current.hasValue();
        if (current.hasValue()) {
            abort();
        }
    }

    static ~ApplicationGlobalProbe() {
        current : OptionalRef<k::Application> = k::Application::getApplication();
        if (current.hasValue()) {
            abort();
        }
    }
}

main() : int {
    if (!global_constructor_saw_empty) {
        return 1;
    }

    current : OptionalRef<k::Application> = k::Application::getApplication();
    main_saw_active = current.hasValue();
    if (!current.hasValue()) {
        return 2;
    }

    before : int = modreg::registeredCount();
    loaded : Expected<Shared<Module>, LoadModuleError> =
        loadModule(String("libapplication_probe.so"));
    if (!loaded.hasResult()) {
        return 3;
    }
    if (modreg::registeredCount() != before + 1 ||
        modreg::registeredId(before) != 4) {
        return 4;
    }
    return 0;
}
