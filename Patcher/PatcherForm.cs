/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

using System.Diagnostics;

namespace UnrealLongjuPatcher;

/// <summary>
/// Minimal patcher window: on open it fetches the manifest, verifies/downloads changed files, then
/// enables Play, which launches the game and closes the patcher. UI is built in code (no designer).
/// </summary>
internal sealed class PatcherForm : Form
{
    private readonly Label _statusLabel;
    private readonly Label _versionLabel;
    private readonly ProgressBar _progress;
    private readonly Button _playButton;
    private readonly Updater _updater;
    private readonly CancellationTokenSource _cts = new();

    private Manifest? _manifest;

    public PatcherForm()
    {
        _updater = new Updater(AppContext.BaseDirectory);

        Text = PatchConfig.AppTitle;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(520, 180);
        BackColor = Color.FromArgb(28, 28, 30);
        ForeColor = Color.Gainsboro;

        var title = new Label
        {
            Text = "UnrealLongju",
            Font = new Font("Segoe UI", 20f, FontStyle.Bold),
            AutoSize = true,
            Location = new Point(20, 16)
        };

        _versionLabel = new Label
        {
            Text = "",
            AutoSize = true,
            ForeColor = Color.Gray,
            Location = new Point(24, 58)
        };

        _statusLabel = new Label
        {
            Text = "Connecting…",
            AutoSize = false,
            Location = new Point(24, 92),
            Size = new Size(472, 20)
        };

        _progress = new ProgressBar
        {
            Location = new Point(24, 116),
            Size = new Size(472, 18),
            Style = ProgressBarStyle.Continuous,
            Minimum = 0,
            Maximum = 1000
        };

        _playButton = new Button
        {
            Text = "Play",
            Location = new Point(396, 142),
            Size = new Size(100, 30),
            Enabled = false,
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(70, 60, 45),
            ForeColor = Color.White
        };
        _playButton.Click += (_, _) => LaunchAndExit();

        Controls.Add(title);
        Controls.Add(_versionLabel);
        Controls.Add(_statusLabel);
        Controls.Add(_progress);
        Controls.Add(_playButton);

        Shown += async (_, _) => await RunAsync();
        FormClosing += (_, _) => _cts.Cancel();
    }

    private async Task RunAsync()
    {
        var progress = new Progress<PatchProgress>(p =>
        {
            _statusLabel.Text = p.Status;
            _progress.Value = Math.Clamp((int)(p.TotalFraction * 1000), 0, 1000);
        });

        try
        {
            SetStatus("Checking for updates…");
            _manifest = await _updater.FetchManifestAsync(_cts.Token);
            _versionLabel.Text = $"Version {_manifest.Version}";

            List<FileEntry> outdated = await Task.Run(
                () => _updater.GetOutdatedFiles(_manifest, progress, _cts.Token), _cts.Token);

            if (outdated.Count > 0)
            {
                await Task.Run(() => _updater.DownloadAsync(_manifest, outdated, progress, _cts.Token), _cts.Token);
            }

            _progress.Value = _progress.Maximum;
            SetStatus(outdated.Count > 0 ? "Update complete. Ready to play." : "Up to date. Ready to play.");
            _playButton.Enabled = true;
        }
        catch (OperationCanceledException)
        {
            // Window closing; nothing to do.
        }
        catch (Exception ex)
        {
            SetStatus("Update failed: " + ex.Message);
            MessageBox.Show(this,
                "The update could not be completed:\n\n" + ex.Message +
                "\n\nCheck your internet connection and try again.",
                PatchConfig.AppTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void LaunchAndExit()
    {
        if (_manifest is null) return;
        LaunchInfo launch = _updater.ResolveLaunch(_manifest);
        string exePath;
        try
        {
            exePath = _updater.ResolveInstallPath(launch.Exe);
        }
        catch (InvalidDataException ex)
        {
            MessageBox.Show(this, ex.Message, PatchConfig.AppTitle,
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        if (!File.Exists(exePath))
        {
            MessageBox.Show(this, "Game executable not found:\n" + exePath,
                PatchConfig.AppTitle, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }

        string launchArguments = Environment.GetEnvironmentVariable(
            PatchConfig.LaunchArgumentsOverrideEnvironmentVariable) ?? launch.Args;
        string token = Guid.NewGuid().ToString("N");
        var startInfo = new ProcessStartInfo
        {
            FileName = exePath,
            Arguments = string.IsNullOrWhiteSpace(launchArguments)
                ? $"-MT2PatcherToken={token}"
                : $"{launchArguments} -MT2PatcherToken={token}",
            WorkingDirectory = _updater.InstallDir,
            UseShellExecute = false
        };
        startInfo.Environment[PatchConfig.LaunchTokenEnvironmentVariable] = token;
        startInfo.Environment.Remove(PatchConfig.LaunchArgumentsOverrideEnvironmentVariable);
        Process.Start(startInfo);
        Close();
    }

    private void SetStatus(string text) => _statusLabel.Text = text;
}
