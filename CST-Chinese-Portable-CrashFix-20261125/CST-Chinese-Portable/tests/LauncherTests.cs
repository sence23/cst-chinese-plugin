using System;
using System.IO;
using System.Diagnostics;

class LauncherTests
{
    static int count;
    static void Assert(bool ok, string name) { if (!ok) throw new Exception(name); count++; Console.WriteLine("PASS " + name); }
    static void Reject(Action action, string name) { bool rejected = false; try { action(); } catch { rejected = true; } Assert(rejected, name); }
    static int Main(string[] args)
    {
        try
        {
            string fixture = Path.Combine(Path.GetFullPath(args[0]), Guid.NewGuid().ToString("N"), "中文路径 with spaces");
            Directory.CreateDirectory(fixture);
            string bin = Path.Combine(fixture, "AMD64");
            Assert(Launcher.NormalizeRoot(fixture) == fixture, "root path");
            Assert(Launcher.NormalizeRoot(Path.GetPathRoot(fixture)) == Path.GetPathRoot(fixture), "drive root remains absolute");
            Assert(Launcher.NormalizeRoot(bin + "\\") == fixture, "AMD64 with trailing slash");
            Assert(Launcher.NormalizeRoot(Path.Combine(bin, Launcher.ExeName)) == fixture, "exe path");
            Assert(Launcher.NormalizeRoot("\"" + fixture + "\"") == fixture, "quoted root");
            Reject(delegate { Launcher.NormalizeRoot(""); }, "empty path rejected");
            Reject(delegate { Launcher.Validate(fixture, fixture); }, "missing CST rejected");
            Directory.CreateDirectory(bin);
            File.WriteAllText(Path.Combine(bin, Launcher.ExeName), "not an executable");
            Reject(delegate { Launcher.Validate(fixture, fixture); }, "invalid PE rejected");
            string user = Path.Combine(fixture, "用户设置");
            Launcher.SaveRoot(bin, user);
            Assert(File.ReadAllText(Path.Combine(user, "cst-root.txt")).Trim() == fixture, "per-user config roundtrip");
            Assert(Launcher.Discover(user) == fixture, "saved path discovery");
            string envBefore = Environment.GetEnvironmentVariable("QT_PLUGIN_PATH");
            string diagBefore = Environment.GetEnvironmentVariable("CST_ZH_DIAGNOSTICS");
            try
            {
                Environment.SetEnvironmentVariable("QT_PLUGIN_PATH", "unrelated-qt-path");
                Environment.SetEnvironmentVariable("CST_ZH_DIAGNOSTICS", "private-diagnostics");
                var start = Launcher.CreateStartInfo(bin, fixture, user);
                Assert(start.FileName == Path.Combine(bin, Launcher.ExeName) && !start.UseShellExecute, "direct executable without shell");
                Assert(start.WorkingDirectory == bin && start.Arguments == "", "CST working directory");
                Assert(start.EnvironmentVariables["QT_QPA_GENERIC_PLUGINS"] == "cstribbonzh", "generic plugin enabled");
                Assert(start.EnvironmentVariables["QT_PLUGIN_PATH"] == Path.Combine(fixture, @"CSTRibbonZh\plugins") + ";" + bin, "moved package path");
                Assert(start.EnvironmentVariables["CST_ZH_USER_CATALOG"] == Path.Combine(user, "user.tsv"), "per-user custom catalog");
                Assert(!start.EnvironmentVariables.ContainsKey("CST_ZH_DIAGNOSTICS"), "diagnostics disabled in child");
                Assert(Environment.GetEnvironmentVariable("QT_PLUGIN_PATH") == "unrelated-qt-path", "parent environment unchanged");
            }
            finally
            {
                Environment.SetEnvironmentVariable("QT_PLUGIN_PATH", envBefore);
                Environment.SetEnvironmentVariable("CST_ZH_DIAGNOSTICS", diagBefore);
            }
            Console.WriteLine("PASS " + count + " checks");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
