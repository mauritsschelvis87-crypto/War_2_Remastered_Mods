using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using Microsoft.Web.WebView2.Core;

namespace PlayerColorStudio;

public partial class Warcraft2MapDownloadWindow : Window
{
    private readonly string _outDir;
    private readonly IReadOnlyList<(string Filename, string DownloadUrl)> _jobs;
    private readonly CancellationToken _token;
    private bool _running;
    private int _ok;
    private int _fail;
    private int _skipped;
    private TaskCompletionSource<bool>? _downloadTcs;
    private string? _expectedName;

    public int Ok => _ok;
    public int Fail => _fail;
    public int Skipped => _skipped;
    public bool Completed { get; private set; }

    public Warcraft2MapDownloadWindow(
        Window? owner,
        string outDir,
        IReadOnlyList<(string Filename, string DownloadUrl)> jobs,
        CancellationToken token)
    {
        InitializeComponent();
        if (owner is { IsLoaded: true, IsVisible: true })
            Owner = owner;
        _outDir = outDir;
        _jobs = jobs;
        _token = token;
        Loaded += async (_, _) => await InitAsync();
    }

    private async Task InitAsync()
    {
        try
        {
            var profile = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "PlayerColorStudio", "WebView2Warcraft2");
            Directory.CreateDirectory(profile);
            var env = await CoreWebView2Environment.CreateAsync(userDataFolder: profile);
            await Browser.EnsureCoreWebView2Async(env);
            Browser.CoreWebView2.Settings.AreDefaultContextMenusEnabled = true;
            Browser.CoreWebView2.DownloadStarting += CoreWebView2_DownloadStarting;
            Browser.CoreWebView2.Navigate("https://warcraft2.site/");
            StatusText.Text = "Browser ready. Log in on the site, then click Start download.";
            StartButton.IsEnabled = true;
        }
        catch (Exception ex)
        {
            StatusText.Text = "Could not start embedded browser: " + ex.Message;
            StudioDialog.Show(this, ex.Message, "WebView2", StudioDialogKind.Error);
        }
    }

    private void CoreWebView2_DownloadStarting(object? sender, CoreWebView2DownloadStartingEventArgs e)
    {
        try
        {
            var name = _expectedName;
            if (string.IsNullOrWhiteSpace(name))
            {
                name = Path.GetFileName(e.ResultFilePath);
            }
            if (string.IsNullOrWhiteSpace(name))
                name = "map.pud";

            var dest = Path.Combine(_outDir, name);
            e.ResultFilePath = dest;
            e.Handled = true;

            var op = e.DownloadOperation;
            void OnState()
            {
                if (op.State is CoreWebView2DownloadState.Completed
                    or CoreWebView2DownloadState.Interrupted)
                {
                    op.StateChanged -= OnStateHandler;
                    _downloadTcs?.TrySetResult(op.State == CoreWebView2DownloadState.Completed
                                               && File.Exists(dest)
                                               && new FileInfo(dest).Length >= 256);
                }
            }
            void OnStateHandler(object? s, object e2) => Dispatcher.BeginInvoke(OnState);
            op.StateChanged += OnStateHandler;
            // In case it already finished very quickly
            Dispatcher.BeginInvoke(OnState);
        }
        catch (Exception ex)
        {
            _downloadTcs?.TrySetException(ex);
        }
    }

    private async void Start_Click(object sender, RoutedEventArgs e)
    {
        if (_running) return;
        _running = true;
        StartButton.IsEnabled = false;
        Directory.CreateDirectory(_outDir);

        try
        {
            for (var i = 0; i < _jobs.Count; i++)
            {
                _token.ThrowIfCancellationRequested();
                var (filename, url) = _jobs[i];
                var dest = Path.Combine(_outDir, filename);
                if (File.Exists(dest) && new FileInfo(dest).Length >= 256)
                {
                    _skipped++;
                    StatusText.Text = $"Skipping existing {i + 1}/{_jobs.Count}: {filename}";
                    continue;
                }

                StatusText.Text = $"Downloading {i + 1}/{_jobs.Count}: {filename}";
                _expectedName = filename;
                _downloadTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                Browser.CoreWebView2.Navigate(url);

                var completed = await Task.WhenAny(_downloadTcs.Task, Task.Delay(120_000, _token));
                if (completed != _downloadTcs.Task)
                {
                    _fail++;
                    StatusText.Text = $"Timed out: {filename}";
                    continue;
                }

                if (await _downloadTcs.Task)
                    _ok++;
                else
                    _fail++;
            }

            Completed = true;
            StatusText.Text = $"Done. New={_ok}, skipped={_skipped}, failed={_fail}. You can close this window.";
        }
        catch (OperationCanceledException)
        {
            StatusText.Text = "Cancelled.";
        }
        catch (Exception ex)
        {
            StatusText.Text = "Download error: " + ex.Message;
        }
        finally
        {
            _running = false;
            StartButton.IsEnabled = true;
        }
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
