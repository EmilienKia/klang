@ManifestTag(17)
module modreg;

public annotation ManifestTag {
    value : int;
}

/** A plugin that registers itself in the global registry. */
public class Plugin : public Object {
    private _id : int;

    public Plugin(id : int) {
        _id = id;
    }

    public const id() : int {
        return _id;
    }
}

/** Ordered list of the plugins registered so far. */
public class Registry : public Object {
    private _items : Vector<Plugin*>!;

    public Registry() {
        _items = new Vector<Plugin*>();
    }

    public add(plugin : Plugin&) {
        p : Plugin* = &plugin;
        _items.append(p);
    }

    public const size() : int {
        return (int) _items.size();
    }

    public const idAt(index : int) : int {
        i : unsigned int = (unsigned int) index;
        p : Plugin* = _items.get(i);
        return p->id();
    }
}

registry : Registry;

public registeredCount() : int {
    return registry.size();
}

public registeredId(index : int) : int {
    return registry.idAt(index);
}

public registerPlugin(plugin : Plugin&) {
    registry.add(plugin);
}

@ManifestTag(31)
public annotatedProbe(@ManifestTag(37) value : int) : int {
    return value;
}

namespace view_fixture {
    @ManifestTag(23)
    namespace child {
        public const marker() : int {
            return 7;
        }
    }
}
