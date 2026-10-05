module application_probe;

import modreg;

@ffi::Extern("C") private __k_application_activate(application: k::Application*): int;

applicationProbeStatus() : int {
    current : OptionalRef<k::Application> = k::Application::getApplication();
    if (!current.hasValue()) {
        return 0;
    }
    return __k_application_activate(&current.get());
}

public class ApplicationProbe : modreg::Plugin {
    public ApplicationProbe() : Plugin(applicationProbeStatus()) {
        modreg::registerPlugin(this);
    }
}

public instance : ApplicationProbe;
