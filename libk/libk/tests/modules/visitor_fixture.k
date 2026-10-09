@VisitTag(1)
module visitor_fixture;

public annotation VisitTag {
    value : int;
}

// Root-function traversal must depend on fixture declarations, not imported
// execute templates that older KDI imports incorrectly homed in this unit.
public rootThreeParameters(first : int, second : int, third : int) : int {
    return first + second + third;
}

public rootTwoParameters(first : int, second : int) : int {
    return first + second;
}

@VisitTag(2)
namespace container {
    @VisitTag(3)
    public interface Alpha {
        @VisitTag(4)
        public run(p : int) : int;
    }

    @VisitTag(5)
    public annotation Beta {}

    @VisitTag(6)
    public class Gamma : public Alpha {
        public class Inner {}

        @VisitTag(7)
        public Gamma(@VisitTag(8) p : int) {}

        @VisitTag(9)
        public override run(@VisitTag(10) p : int) : int {
            return p;
        }
    }

    @VisitTag(11)
    public answer(@VisitTag(12) p : int) : int {
        return p;
    }
}
