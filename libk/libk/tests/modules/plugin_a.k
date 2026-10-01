module modplugin_a;

import modreg;

/** Global object registering itself in the registry of the modreg library. */
public class Plugin_a : public modreg::Plugin {
    public Plugin_a() : Plugin(1) {
        modreg::registerPlugin(this);
    }
}

public instance : Plugin_a;
