using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Windows;
using Forms = System.Windows.Forms;

namespace SetupApp;

public partial class MainWindow : Window
{
    private const string DefaultGameRoot = @"C:\Program Files (x86)\Warcraft II Remastered";

    public MainWindow()
    {
        InitializeComponent();
        PathBox.Text = Directory.Exists(Path.Combine(DefaultGameRoot, "x86", "Data"))
            ? DefaultGameRoot
            : string.Empty;
        StatusText.Text = "Choose your Warcraft II Remastered folder, then press Install.";
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
        if (!Directory.Exists(Path.Combine(gameRoot, "x86", "Data")))
        {
            StatusText.Text = "That folder does not look like Warcraft II Remastered (missing x86\\Data).";
            return;
        }

        InstallButton.IsEnabled = false;
        StatusText.Text = "Installing…";

        try
        {
            await Task.Run(() => InstallTo(gameRoot));
            var exe = Path.Combine(gameRoot, "x86", "Mods", "PlayerColorStudio", "app", "PlayerColorStudio.exe");
            StatusText.Text = "Installed. Starting the app…";
            if (File.Exists(exe))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = exe,
                    WorkingDirectory = Path.GetDirectoryName(exe)!,
                    UseShellExecute = true,
                });
            }
            StatusText.Text = "Done. Press Apply in the app, then restart Warcraft II.";
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

    private static void InstallTo(string gameRoot)
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
