@ManifestTag(61)
module staticmanifestapp;

import staticused;

annotation ManifestTag {
    value : int;
}

@ManifestTag(67)
namespace manifest_fixture {}

@ManifestTag(71)
annotatedFree(@ManifestTag(73) value : int) : int {
    return value;
}

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
    annotations_checked : bool = false;
    i : unsigned int = 0;
    while (i < units.size()) {
        unit_name : String = String(units.get(i).getDescriptor().getName());
        if (unit_name == String("staticused")) {
            used_found = true;
        }
        if (unit_name == String("staticunused")) {
            unused_found = true;
        }
        if (unit_name == String("staticmanifestapp")) {
            unit_view : UnitView = units.get(i);
            descriptor : const Unit& = unit_view.getDescriptor();
            unit_annotations : const Annotation?[]? = descriptor.getAnnotations();
            if (unit_annotations == null || unit_annotations->size != 1u) { return 4; }
            unit_tag : const ManifestTag? = unit_annotations[0];
            if (unit_tag == null || unit_tag->value != 61) { return 5; }
            root_namespace : const Namespace? = descriptor.getRootNamespace();
            if (root_namespace == null || root_namespace->getAnnotations() != null) { return 18; }

            functions : const Function?[]? = descriptor.getFunctions();
            if (functions == null) { return 6; }
            found_free : bool = false;
            j : unsigned int = 0;
            while (j < functions->size) {
                function : const Function? = functions[j];
                if (function != null && String(function->getName()) == String("annotatedFree")) {
                    function_annotations : const Annotation?[]? = function->getAnnotations();
                    if (function_annotations == null || function_annotations->size != 1u) { return 7; }
                    function_tag : const ManifestTag? = function_annotations[0];
                    if (function_tag == null || function_tag->value != 71) { return 8; }
                    parameters : const Parameter?[]? = function->getParameters();
                    if (parameters == null || parameters->size != 1u) { return 9; }
                    parameter : const Parameter? = parameters[0];
                    if (parameter == null) { return 10; }
                    parameter_annotations : const Annotation?[]? = parameter->getAnnotations();
                    if (parameter_annotations == null || parameter_annotations->size != 1u) { return 11; }
                    parameter_tag : const ManifestTag? = parameter_annotations[0];
                    if (parameter_tag == null || parameter_tag->value != 73) { return 12; }
                    found_free = true;
                }
                ++j;
            }
            if (!found_free) { return 13; }

            pending_namespaces : Vector<NamespaceView>;
            pending_namespaces.append(unit_view.rootNamespace());
            namespace_found : bool = false;
            n : unsigned int = 0;
            while (n < pending_namespaces.size()) {
                namespace_view : NamespaceView = pending_namespaces.get(n);
                namespace_annotations : const Annotation?[]? = namespace_view.getDescriptor().getAnnotations();
                if (namespace_annotations != null) {
                    if (namespace_annotations->size != 1u) { return 14; }
                    namespace_tag : const ManifestTag? = namespace_annotations[0];
                    if (namespace_tag == null || namespace_tag->value != 67) { return 15; }
                    namespace_found = true;
                }
                children : Vector<NamespaceView> = namespace_view.children();
                c : unsigned int = 0;
                while (c < children.size()) {
                    pending_namespaces.append(children.get(c));
                    ++c;
                }
                ++n;
            }
            if (!namespace_found) { return 16; }
            annotations_checked = true;
        }
        ++i;
    }
    if (!used_found || unused_found) {
        return 3;
    }
    if (!annotations_checked) { return 17; }
    return 0;
}
