module staticmanifestapp;

import staticused;

main() : int {
    if (staticused::staticUsed() != 0) {
        return 1;
    }
    units : Vector<UnitView> = linkedUnits();
    if (units.size() < 3u) {
        return 2;
    }
    used_found : bool = false;
    unused_found : bool = false;
    i : unsigned int = 0;
    while (i < units.size()) {
        unit_name : String = String(units.get(i).getDescriptor().getName());
        if (unit_name == String("staticused")) {
            used_found = true;
        }
        if (unit_name == String("staticunused")) {
            unused_found = true;
        }
        ++i;
    }
    if (!used_found || unused_found) {
        return 3;
    }
    return 0;
}
