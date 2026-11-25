using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class Launcher
{
    internal const string ExeName = "CST DESIGN ENVIRONMENT_AMD64.exe";
    internal static readonly string Package = AppDomain.CurrentDomain.BaseDirectory;
    internal static readonly string UserDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "CST-Chinese-Portable");

    [STAThread]
    static int Main(string[] args)
    {
        bool check = false, launch = false;
        string root = null, report = null;
        try
        {
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "--check" || args[i] == "/check") check = true;
                else if (args[i] == "--launch") launch = true;
                else if (args[i] == "--select") { }
                else if (args[i] == "--cst-root" && i + 1 < args.Length) root = args[++i];
                else if (args[i] == "--report" && i + 1 < args.Length) report = args[++i];
                else throw new ArgumentException("未知或缺少参数：" + args[i]);
            }
            if (check && launch) throw new ArgumentException("--check 与 --launch 不能同时使用。");
            if (root == null) root = Discover(UserDirectory);
            if (check || launch)
            {
                string status = Validate(root, Package);
                if (launch) Start(root, Package, UserDirectory);
                WriteReport(report, "PASS\r\n" + status);
                return 0;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new LauncherForm(root));
            return 0;
        }
        catch (Exception e)
        {
            try { WriteReport(report, "FAIL\r\n" + e.Message); } catch { }
            if (args.Length == 0) MessageBox.Show(e.Message, "CST 中文启动器", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    static void WriteReport(string path, string text)
    {
        if (!String.IsNullOrWhiteSpace(path)) File.WriteAllText(Path.GetFullPath(path), text, Encoding.UTF8);
    }

    internal static string NormalizeRoot(string input)
    {
        if (String.IsNullOrWhiteSpace(input)) throw new IOException("请选择本机 CST 2025 的安装目录或主程序。");
        string p = Path.GetFullPath(Environment.ExpandEnvironmentVariables(input.Trim().Trim('"')));
        if (String.Equals(Path.GetFileName(p), ExeName, StringComparison.OrdinalIgnoreCase)) p = Path.GetDirectoryName(p);
        string driveRoot = Path.GetPathRoot(p);
        p = p.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        if (p.Length < driveRoot.Length) p = driveRoot;
        if (String.Equals(Path.GetFileName(p), "AMD64", StringComparison.OrdinalIgnoreCase)) p = Path.GetDirectoryName(p);
        return p;
    }

    internal static bool IsX64(string path)
    {
        using (var stream = File.OpenRead(path))
        using (var reader = new BinaryReader(stream))
        {
            if (stream.Length < 64 || reader.ReadUInt16() != 0x5A4D) return false;
            stream.Position = 60;
            long offset = reader.ReadUInt32();
            if (offset < 64 || offset > stream.Length - 6) return false;
            stream.Position = offset;
            return reader.ReadUInt32() == 0x4550 && reader.ReadUInt16() == 0x8664;
        }
    }

    internal static string Validate(string input, string package)
    {
        if (!Environment.Is64BitOperatingSystem) throw new IOException("此插件需要 64 位 Windows。");
        string bin = Path.Combine(NormalizeRoot(input), "AMD64");
        string exe = Path.Combine(bin, ExeName);
        if (!File.Exists(exe)) throw new IOException("目录内没有 CST 主程序。请选择包含 AMD64 文件夹的安装目录。");
        if (!IsX64(exe)) throw new IOException("CST 主程序不是有效的 x64 PE 文件。");
        var version = FileVersionInfo.GetVersionInfo(exe);
        if (version.FileMajorPart != 2025) throw new IOException("当前插件仅支持 CST 2025 x64；检测到版本：" + version.FileVersion);
        foreach (string module in new[] { "Core", "Gui", "Widgets" })
        {
            string path = Path.Combine(bin, "Qt5" + module + "CST_AMD64.dll");
            if (!File.Exists(path) || !IsX64(path)) throw new IOException("缺少或损坏 CST 的 x64 Qt 运行库：" + Path.GetFileName(path));
            var qt = FileVersionInfo.GetVersionInfo(path);
            if (qt.FileMajorPart != 5 || qt.FileMinorPart != 15) throw new IOException("插件要求 CST 自带的 Qt 5.15 运行库。");
        }
        string plugin = Path.Combine(package, @"CSTRibbonZh\plugins\generic\CstRibbonZh.dll");
        if (!File.Exists(plugin) || !IsX64(plugin)) throw new IOException("中文插件文件缺失或损坏，请完整解压发布包后再启动。");
        return "CST " + version.FileVersion + " · x64 · Qt 5.15\r\n检查通过。兼容性检查不代表全部界面已汉化。";
    }

    internal static ProcessStartInfo CreateStartInfo(string input, string package, string userDirectory)
    {
        string bin = Path.Combine(NormalizeRoot(input), "AMD64");
        var start = new ProcessStartInfo(Path.Combine(bin, ExeName));
        start.UseShellExecute = false;
        start.WorkingDirectory = bin;
        start.EnvironmentVariables["QT_PLUGIN_PATH"] = Path.Combine(package, @"CSTRibbonZh\plugins") + ";" + bin;
        start.EnvironmentVariables["QT_QPA_GENERIC_PLUGINS"] = "cstribbonzh";
        start.EnvironmentVariables["CST_ZH_BIN"] = bin;
        start.EnvironmentVariables["CST_ZH_USER_CATALOG"] = Path.Combine(userDirectory, "user.tsv");
        start.EnvironmentVariables.Remove("CST_ZH_DIAGNOSTICS");
        return start;
    }

    internal static void Start(string input, string package, string userDirectory)
    {
        Validate(input, package);
        using (var process = Process.Start(CreateStartInfo(input, package, userDirectory)))
        {
            if (process == null) throw new IOException("Windows 没有创建 CST 进程。");
        }
    }

    internal static string Discover(string userDirectory)
    {
        var candidates = new List<string>();
        try
        {
            string saved = Path.Combine(userDirectory, "cst-root.txt");
            if (File.Exists(saved)) candidates.Add(File.ReadAllText(saved).Trim());
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
        candidates.Add(Environment.GetEnvironmentVariable("CST_2025_ROOT"));
        foreach (RegistryView view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        foreach (RegistryHive hive in new[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser })
        {
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(hive, view))
                using (var uninstall = baseKey.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"))
                {
                    if (uninstall == null) continue;
                    foreach (string name in uninstall.GetSubKeyNames())
                    using (var key = uninstall.OpenSubKey(name))
                    {
                        if (key == null) continue;
                        string title = Convert.ToString(key.GetValue("DisplayName"));
                        if (title.IndexOf("CST", StringComparison.OrdinalIgnoreCase) >= 0 && title.Contains("2025"))
                            candidates.Add(Convert.ToString(key.GetValue("InstallLocation")));
                    }
                }
            }
            catch (System.Security.SecurityException) { }
            catch (UnauthorizedAccessException) { }
            catch (IOException) { }
        }
        foreach (string candidate in candidates)
        {
            try
            {
                string root = NormalizeRoot(candidate);
                if (File.Exists(Path.Combine(root, "AMD64", ExeName))) return root;
            }
            catch (Exception) { }
        }
        return "";
    }

    internal static void SaveRoot(string input, string userDirectory)
    {
        Directory.CreateDirectory(userDirectory);
        File.WriteAllText(Path.Combine(userDirectory, "cst-root.txt"), NormalizeRoot(input), Encoding.UTF8);
    }
}

internal sealed class LauncherForm : Form
{
    readonly TextBox path = new TextBox();
    readonly Label status = new Label();
    readonly Button launch = new Button();

    public LauncherForm(string initial)
    {
        Text = "CST 中文启动器 · 便携版";
        Font = new Font("Microsoft YaHei UI", 10F);
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(660, 355);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        var title = new Label { Text = "从这里启动中文 CST", Location = new Point(24, 20), Size = new Size(600, 35), Font = new Font(Font.FontFamily, 18F, FontStyle.Bold) };
        var hint = new Label { Text = "选择本机已安装并授权的 CST 2025。路径保存在当前 Windows 用户下。", Location = new Point(24, 65), Size = new Size(610, 30) };
        path.SetBounds(24, 105, 480, 30);
        path.Text = initial ?? "";
        path.AccessibleName = "CST 安装目录";
        var browse = new Button { Text = "选择程序…", Location = new Point(514, 103), Size = new Size(120, 34) };
        browse.Click += delegate { Browse(); };
        status.SetBounds(24, 155, 610, 75);
        var note = new Label { Text = "覆盖已收录的功能区、菜单和导航分类；部分模块仍可能显示英文。\r\n中文仅用于界面显示，工程数据保持原值。", Location = new Point(24, 235), Size = new Size(610, 48), ForeColor = Color.DimGray };
        launch.Text = "启动中文版";
        launch.SetBounds(464, 295, 170, 38);
        launch.Click += delegate { Launch(); };
        var refresh = new Button { Text = "检查路径", Location = new Point(24, 295), Size = new Size(115, 38) };
        refresh.Click += delegate { Check(); };
        Controls.AddRange(new Control[] { title, hint, path, browse, status, note, refresh, launch });
        path.TextChanged += delegate { status.Text = "路径已更改，请检查或直接启动。"; status.ForeColor = Color.DimGray; };
        Shown += delegate { Check(); };
        AcceptButton = launch;
    }

    void Browse()
    {
        using (var picker = new OpenFileDialog())
        {
            picker.Title = "选择 CST 安装目录下 AMD64 内的主程序";
            picker.Filter = "CST 主程序|" + Launcher.ExeName;
            picker.CheckFileExists = true;
            if (picker.ShowDialog(this) == DialogResult.OK) { path.Text = Launcher.NormalizeRoot(picker.FileName); Check(); }
        }
    }

    void Check()
    {
        try { status.Text = Launcher.Validate(path.Text, Launcher.Package); status.ForeColor = Color.DarkGreen; }
        catch (Exception e) { status.Text = e.Message; status.ForeColor = Color.Firebrick; }
    }

    void Launch()
    {
        try
        {
            Launcher.Validate(path.Text, Launcher.Package);
            launch.Enabled = false;
            Launcher.Start(path.Text, Launcher.Package, Launcher.UserDirectory);
            string savedWarning = "";
            try { Launcher.SaveRoot(path.Text, Launcher.UserDirectory); }
            catch (Exception) { savedWarning = "\r\n路径未能保存，下次请重新选择。"; }
            status.ForeColor = Color.DarkGreen;
            status.Text = "已发出启动请求，请等待 CST 窗口打开。" + savedWarning;
            var delay = new Timer { Interval = 5000 };
            delay.Tick += delegate { launch.Enabled = true; delay.Stop(); delay.Dispose(); };
            delay.Start();
        }
        catch (Exception e) { launch.Enabled = true; status.Text = e.Message; status.ForeColor = Color.Firebrick; }
    }
}
