using System;
using Lib.Shapes;

namespace Lib.Shapes {
    public class Item { public string Name() => "Shapes.Item"; }
    public class Circle { public Item Make() { return new Item(); } public string Who() => new Item().Name(); }
}
namespace Lib.Tools {
    public class Item { public string Name() => "Tools.Item"; }
    public class Hammer {
        public Item Make() { return new Item(); }
        public string Who() => new Item().Name();
        public class Handle { public Item Grip() { return new Item(); } public string Who() => new Item().Name(); }
    }
    namespace Inner {
        public class Item { public string Name() => "Inner.Item"; }
        public class Probe { public string Who() => new Item().Name(); public string Outer() => new Lib.Tools.Item().Name(); }
        public class Other { public string Who() => new Hammer().Who(); }
    }
}
namespace Lib.Misc {
    using Lib.Tools;
    public class Spy {
        public string Who() => new Item().Name();
        public string Many() { string s = ""; for (int i = 0; i < 3; i++) { Item it = new Item(); s += it.Name() + ";"; } return s; }
    }
}
namespace Lib.Misc2 {
    public class Item { public string Name() => "Misc2.Item"; }
    public class Spy { public string Who() => new Item().Name(); public string Sys() => new System.Text.StringBuilder().Append("sb").ToString(); }
}
class Top {
    public string Who() => new Item().Name();
}
Console.WriteLine(new Lib.Shapes.Circle().Who());
Console.WriteLine(new Lib.Tools.Hammer().Who());
Console.WriteLine(new Lib.Tools.Hammer.Handle().Who());
Console.WriteLine(new Lib.Tools.Inner.Probe().Who());
Console.WriteLine(new Lib.Tools.Inner.Probe().Outer());
Console.WriteLine(new Lib.Tools.Inner.Other().Who());
Console.WriteLine(new Lib.Misc.Spy().Who());
Console.WriteLine(new Lib.Misc.Spy().Many());
Console.WriteLine(new Lib.Misc2.Spy().Who());
Console.WriteLine(new Lib.Misc2.Spy().Sys());
Console.WriteLine(new Top().Who());
Console.WriteLine(new Item().Name());
Console.WriteLine(new Lib.Shapes.Circle().Make().Name());
Console.WriteLine(new Lib.Tools.Hammer().Make().Name());
Console.WriteLine(new Lib.Tools.Hammer.Handle().Grip().Name());
Console.WriteLine(new List<int>().Count);
