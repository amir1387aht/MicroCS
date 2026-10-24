void Assert(bool ok) { if (!ok) throw new Exception("JSON regression"); }
class BaseData { public int Base = 7; public virtual string Kind => "base"; }
class Data : BaseData {
    public int Count {get;set;} = 21;
    public override string Kind => "derived";
    public int Twice => Count * 2;
    public static int Static => 100;
    public string _secret = "hidden";
    public List<object> Items = new List<object> {1, null, true, "text"};
}
var text = Json.Serialize(new Data());
var data = Json.Deserialize(text);
Assert(data["Base"] == 7 && data["Twice"] == 42 && data["Kind"] == "derived" && data["Items"].Count == 4);
Assert(!data.ContainsKey("Static") && !data.ContainsKey("_secret"));
Assert(Json.Parse(Json.Serialize(new Data(), false, true))["_secret"] == "hidden");
Assert(Json.Escape("a\0b") == "\"a\\u0000b\"");
Assert(Json.Stringify(new Dictionary<char,int>{['x']=3}) == "{\"x\":3}");
Assert(Json.TryParse("null",out var parsed) && parsed == null);
Assert(!Json.TryParse(null,out parsed) && parsed == null);
Assert(Json.TryParse("[1,2,3]",out parsed) && parsed.Count == 3);
foreach (var bad in new[]{"01","-01","+1",".1","1.","1e","1e+","1e9999","1e-9999","true false","[1,]","{\"a\":1,}","\"\\ud800\"","\"\\udc00\"","\"\\ud800\\u0041\""}) {
    Assert(!Json.TryParse(bad, out parsed) && parsed == null);
    bool caught=false;try {Json.Parse(bad);} catch(FormatException){caught=true;} Assert(caught);
}
Assert(Json.Parse("-2147483648") == -2147483648);
Assert(Json.Parse("2147483647") == 2147483647);
Assert(Json.Stringify(Json.Parse("-0")) == "-0");
string longNumber = "0." + new string('0',60) + "1";
Assert(Json.Parse(longNumber) > 0);
Assert(Json.Parse("\"\\ud83d\\ude00\"") == "😀");
var shared = new List<int>{1,2};
Assert(Json.Parse(Json.Stringify(new object[]{shared,shared})).Count == 2);
var cycle = new List<object>();cycle.Add(cycle);
bool rejected=false;try{Json.Serialize(cycle);}catch(InvalidOperationException){rejected=true;}Assert(rejected);
var dict = new Dictionary<string,object>();dict["self"]=dict;
rejected=false;try{Json.Serialize(dict);}catch(InvalidOperationException){rejected=true;}Assert(rejected);
Func<int> fn=()=>1;rejected=false;try{Json.Serialize(fn);}catch(NotSupportedException){rejected=true;}Assert(rejected);
class BadGetter { public int Value {get{throw new IOException("getter");}} }
rejected=false;try{Json.Serialize(new BadGetter());}catch(IOException){rejected=true;}Assert(rejected);
Assert(Json.Stringify(new int[]{1,2}) == "[1,2]");
Assert(Json.Parse(Json.Stringify(new byte[]{0,255}))[1] == 255);
Assert(Json.Parse(Json.Stringify(new Queue<int>(new[]{1,2}))).Count == 2);
Assert(Json.Parse(Json.Stringify(new Stack<int>(new[]{1,2}))).Count == 2);
Assert(Json.Parse(Json.Stringify(new HashSet<int>{1,2})).Count == 2);
Console.WriteLine("JSON extended: passed");
