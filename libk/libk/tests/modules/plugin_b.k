module modplugin_b;

import modreg;

/** Global object registering itself in the registry of the modreg library. */
public class Plugin_b : public modreg::Plugin {
    public Plugin_b() : Plugin(2) {
        modreg::registerPlugin(this);
    }
}

public instance : Plugin_b;
