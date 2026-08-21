using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using Forms = System.Windows.Forms;

namespace SetupApp;

public partial class MainWindow : Window
{
    private const string DefaultGameRoot = @"C:\Program Files (x86)\Warcraft II Remastered";

    public MainWindow()
    {
        InitializeComponent();
        PathBox.Text = FindGameRoot() ?? DefaultGameRoot;
        StatusText.Text = IsGameRoot(PathBox.Text)
            ? "Ready to install. Your game was found automatically."
            : "Game not found automatically. Browse to its install folder, then install.";
    }

    private static bool IsGameRoot(string path) =>
        Directory.Exists(Path.Combine(path, "x86", "Data"));

    private static string? FindGameRoot()
    {
        var candidates = new[]
        {
            DefaultGameRoot,
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Warcraft II Remastered"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Warcraft II Remastered"),
        };

        return candidates.FirstOrDefault(IsGameRoot);
    }

    private void Browse_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new Forms.FolderBrowserDialog
        {
            Description = "Select the Warcraft II Remastered install folder",
            UseDescriptionForTitle = true,
            SelectedPath = Directory.Exists(PathBox.Text) ? PathBox.Text : DefaultGameRoot,
        };
        if (dialog.ShowDialog() == Forms.DialogResult.OK)
            PathBox.Text = dialog.SelectedPath;
    }

    private async void Install_Click(object sender, RoutedEventArgs e)
    {
        var gameRoot = PathBox.Text.Trim().Trim('"');
        if (!IsGameRoot(gameRoot))
        {
            StatusText.Text = "That folder does not look like Warcraft II Remastered. Select the folder containing x86\\Data.";
            return;
        }

        InstallButton.IsEnabled = false;
        StatusText.Text = "Installing...";
        var createDesktopShortcut = DesktopShortcutCheckBox.IsChecked == true;

        try
        {
            await Task.Run(() => InstallTo(gameRoot, createDesktopShortcut));
            var exe = Path.Combine(gameRoot, "x86", "Mods", "PlayerColorStudio", "app", "PlayerColorStudio.exe");
            StatusText.Text = "Installed. Starting the app...";
            if (File.Exists(exe))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = exe,
                    WorkingDirectory = Path.GetDirectoryName(exe)!,
                    UseShellExecute = true,
                });
            }
            Close();
        }
        catch (Exception ex)
        {
            StatusText.Text = "Install failed: " + ex.Message;
        }
        finally
        {
            InstallButton.IsEnabled = true;
        }
    }

    private static void InstallTo(string gameRoot, bool createDesktopShortcut)
    {
        var installRoot = Path.Combine(gameRoot, "x86", "Mods", "PlayerColorStudio");
        var appTarget = Path.Combine(installRoot, "app");
        var modTarget = Path.Combine(installRoot, "mod");
        Directory.CreateDirectory(appTarget);
        Directory.CreateDirectory(modTarget);

        using var zip = OpenPayloadZip();
        foreach (var entry in zip.Entries)
        {
            if (string.IsNullOrEmpty(entry.Name) && entry.FullName.EndsWith('/'))
                continue;

            var relative = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
            string dest;
            if (relative.StartsWith("app" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                dest = Path.Combine(installRoot, relative);
            else if (relative.StartsWith("mod" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                dest = Path.Combine(installRoot, relative);
            else
                continue;

            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            if (entry.Name.Length > 0)
                entry.ExtractToFile(dest, overwrite: true);
        }

        if (!File.Exists(Path.Combine(appTarget, "PlayerColorStudio.exe")))
            throw new InvalidOperationException("PlayerColorStudio.exe missing from package payload.");

        var exe = Path.Combine(appTarget, "PlayerColorStudio.exe");
        var icon = Path.Combine(appTarget, "app.ico");
        if (!File.Exists(icon)) icon = exe;
        var shortcutName = "Quality of Life Modding.lnk";
        CreateShortcut(Path.Combine(installRoot, shortcutName), exe, appTarget, icon);
        if (createDesktopShortcut)
        {
            var desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            CreateShortcut(Path.Combine(desktop, shortcutName), exe, appTarget, icon);
        }
    }

    private static void CreateShortcut(string shortcutPath, string targetPath, string workingDirectory, string iconPath)
    {
        try
        {
            var shellLink = (IShellLinkW)new ShellLink();
            try
            {
                shellLink.SetPath(targetPath);
                shellLink.SetWorkingDirectory(workingDirectory);
                shellLink.SetIconLocation(iconPath, 0);
                ((IPersistFile)shellLink).Save(shortcutPath, true);
            }
            finally
            {
                Marshal.FinalReleaseComObject(shellLink);
            }
        }
        catch
        {
            var targetUri = new Uri(targetPath).AbsoluteUri;
            var shortcut = string.Join(Environment.NewLine,
                "[InternetShortcut]",
                $"URL={targetUri}",
                $"WorkingDirectory={workingDirectory}",
                $"IconFile={iconPath}",
                "IconIndex=0",
                string.Empty);
            File.WriteAllText(Path.ChangeExtension(shortcutPath, ".url"), shortcut, new UTF8Encoding(false));
        }
    }

    [ComImport]
    [Guid("00021401-0000-0000-C000-000000000046")]
    private class ShellLink
    {
    }

    [ComImport]
    [Guid("000214F9-0000-0000-C000-000000000046")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IShellLinkW
    {
        void GetPath(StringBuilder path, int maxPath, IntPtr findData, uint flags);
        void GetIDList(out IntPtr idList);
        void SetIDList(IntPtr idList);
        void GetDescription(StringBuilder description, int maxDescription);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string description);
        void GetWorkingDirectory(StringBuilder directory, int maxDirectory);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string directory);
        void GetArguments(StringBuilder arguments, int maxArguments);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string arguments);
        void GetHotkey(out short hotkey);
        void SetHotkey(short hotkey);
        void GetShowCmd(out int showCommand);
        void SetShowCmd(int showCommand);
        void GetIconLocation(StringBuilder iconPath, int maxPath, out int iconIndex);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string iconPath, int iconIndex);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
        void Resolve(IntPtr windowHandle, uint flags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string path);
    }

    [ComImport]
    [Guid("0000010B-0000-0000-C000-000000000046")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPersistFile
    {
        void GetClassID(out Guid classId);
        void IsDirty();
        void Load([MarshalAs(UnmanagedType.LPWStr)] string fileName, uint mode);
        void Save([MarshalAs(UnmanagedType.LPWStr)] string fileName, [MarshalAs(UnmanagedType.Bool)] bool remember);
        void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string fileName);
        void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string fileName);
    }

    private static ZipArchive OpenPayloadZip()
    {
        var asm = Assembly.GetExecutingAssembly();
        var name = asm.GetManifestResourceNames()
            .FirstOrDefault(n => n.EndsWith("payload.zip", StringComparison.OrdinalIgnoreCase));
        if (name is not null)
        {
            var stream = asm.GetManifestResourceStream(name)
                ?? throw new InvalidOperationException("Could not open embedded payload.");
            return new ZipArchive(stream, ZipArchiveMode.Read, leaveOpen: false);
        }

        var beside = Path.Combine(AppContext.BaseDirectory, "payload.zip");
        if (File.Exists(beside))
            return ZipFile.OpenRead(beside);

        throw new InvalidOperationException("payload.zip not found. Rebuild with Build-SharePackage.ps1.");
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
