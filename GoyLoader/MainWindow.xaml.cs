using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using GoyLoader.Services;

namespace GoyLoader;

public partial class MainWindow : Window
{
    private readonly CancellationTokenSource _lifetimeCts = new();
    private DispatcherTimer? _rlTimer;
    private Process? _hostProcess;
    private bool _injected;

    // Animations driven from code so they can be toggled with state changes
    // (the idle ring on the host row, the inject-button spinner).
    private Storyboard? _hostIdleSpinSb;
    private Storyboard? _injectSpinSb;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnTitleBarMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is DependencyObject d && FindChromeButtonAncestor(d) != null)
            return;
        if (e.LeftButton == MouseButtonState.Pressed)
        {
            try
            {
                DragMove();
            }
            catch
            {
                // Ignore: can throw if the button was released before DragMove runs.
            }
        }
    }

    private static Button? FindChromeButtonAncestor(DependencyObject? child)
    {
        while (child != null)
        {
            if (child is Button b)
                return b;
            child = VisualTreeHelper.GetParent(child);
        }

        return null;
    }

    private void OnChromeMinimizeClick(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnChromeCloseClick(object sender, RoutedEventArgs e) => Close();

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= OnLoaded;
        LoaderSessionLog.WriteGlobal($"Loader UI started from '{Environment.ProcessPath}'.");

        BeginWindowEnterAnimation();
        StartHostIdleSpinner();

        var steam = ProcessFinder.TryGetSteamInstalledExe();
        SteamHint.Text = steam != null
            ? $"Steam: {Path.GetDirectoryName(steam)}"
            : "Start the client manually if not using Steam.";
        // Footer shows the same source-of-truth path in compressed form.
        FooterSteamPath.Text = steam != null
            ? $"STEAM: {ShortenPath(Path.GetDirectoryName(steam) ?? string.Empty)}"
            : string.Empty;

        await RunBootstrapAsync();
    }

    private static string ShortenPath(string path)
    {
        // Compresses "C:\Steam\steamapps\common\rocketleague" → "C:\Steam\…\rocketleague".
        if (string.IsNullOrEmpty(path)) return string.Empty;
        var parts = path.Split(System.IO.Path.DirectorySeparatorChar);
        if (parts.Length <= 3) return path;
        return $"{parts[0]}\\{parts[1]}\\…\\{parts[^1]}";
    }

    private void BeginWindowEnterAnimation()
    {
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        var dur = TimeSpan.FromMilliseconds(300);

        // Header (title block + version subtitle slide in together).
        // The HeaderMark logo is ambient and fades with the window itself.
        AnimateIn((UIElement)SubtitleLine.Parent!, SubtitleTrans, 6, TimeSpan.Zero, dur, ease);

        // Cards stagger in from below for a layered settle.
        AnimateIn(StatusCard, StatusCardTrans, 8, TimeSpan.FromMilliseconds(60), dur, ease);
        AnimateIn(HostCard, HostCardTrans, 8, TimeSpan.FromMilliseconds(120), dur, ease);
        AnimateIn(AutoInjectCard, AutoInjectCardTrans, 8, TimeSpan.FromMilliseconds(160), dur, ease);
        AnimateIn(SessionLogCard, SessionLogTrans, 8, TimeSpan.FromMilliseconds(200), dur, ease);

        // Status pill drops down from above for a different feel than the cards.
        AnimateIn(HostStatusPill, HostStatusPillTrans, -4,
            TimeSpan.FromMilliseconds(140), dur, ease);

        AnimateIn(FooterBar, null, 0,
            TimeSpan.FromMilliseconds(240), TimeSpan.FromMilliseconds(220), ease);
    }

    private void StartHostIdleSpinner()
    {
        // The hollow ring on the "Host process" prereq row spins while the host
        // hasn't been detected yet. Stops when host comes online (the row swaps
        // to a green check via Visibility on HostIconOk / HostIconIdle).
        if (_hostIdleSpinSb != null) return;

        var anim = new DoubleAnimation
        {
            From = 0,
            To = 360,
            Duration = new Duration(TimeSpan.FromSeconds(1.1)),
            RepeatBehavior = RepeatBehavior.Forever,
        };
        _hostIdleSpinSb = new Storyboard();
        Storyboard.SetTarget(anim, HostIconIdleRotate);
        Storyboard.SetTargetProperty(anim, new PropertyPath(RotateTransform.AngleProperty));
        _hostIdleSpinSb.Children.Add(anim);
        _hostIdleSpinSb.Begin();
    }

    private void StartInjectButtonSpinner()
    {
        if (_injectSpinSb != null) return;

        var anim = new DoubleAnimation
        {
            From = 0,
            To = 360,
            Duration = new Duration(TimeSpan.FromSeconds(0.8)),
            RepeatBehavior = RepeatBehavior.Forever,
        };
        _injectSpinSb = new Storyboard();
        Storyboard.SetTarget(anim, InjectSpinnerRotate);
        Storyboard.SetTargetProperty(anim, new PropertyPath(RotateTransform.AngleProperty));
        _injectSpinSb.Children.Add(anim);
        _injectSpinSb.Begin();
    }

    private void StopInjectButtonSpinner()
    {
        if (_injectSpinSb == null) return;
        _injectSpinSb.Stop();
        _injectSpinSb.Children.Clear();
        _injectSpinSb = null;
        InjectSpinnerRotate.Angle = 0;
    }

    private enum InjectButtonState { Idle, Working, Done }

    private void SetInjectButtonVisual(InjectButtonState state, string? workingLabel = null)
    {
        switch (state)
        {
            case InjectButtonState.Idle:
                InjectIcon.Visibility = Visibility.Visible;
                InjectSpinner.Visibility = Visibility.Collapsed;
                InjectCheckIcon.Visibility = Visibility.Collapsed;
                BtnInjectLabel.Text = "Inject";
                StopInjectButtonSpinner();
                break;
            case InjectButtonState.Working:
                InjectIcon.Visibility = Visibility.Collapsed;
                InjectSpinner.Visibility = Visibility.Visible;
                InjectCheckIcon.Visibility = Visibility.Collapsed;
                BtnInjectLabel.Text = workingLabel ?? "Working…";
                StartInjectButtonSpinner();
                break;
            case InjectButtonState.Done:
                InjectIcon.Visibility = Visibility.Collapsed;
                InjectSpinner.Visibility = Visibility.Collapsed;
                InjectCheckIcon.Visibility = Visibility.Visible;
                BtnInjectLabel.Text = "Injected";
                StopInjectButtonSpinner();
                break;
        }
    }

    private void UpdateHostStatusPill(bool hostRunning)
    {
        if (hostRunning)
        {
            HostStatusPillText.Text = "HOST ONLINE";
            HostStatusPillText.Foreground = (Brush)FindResource("BrushImGuiGreen")!;
            HostStatusPillDot.Fill = (Brush)FindResource("BrushImGuiGreen")!;
            HostStatusPill.Background = (Brush)FindResource("BrushLoaderPillOkBg")!;
            HostStatusPill.BorderBrush = (Brush)FindResource("BrushLoaderPillOkBorder")!;
        }
        else
        {
            HostStatusPillText.Text = "IDLE";
            HostStatusPillText.Foreground = (Brush)FindResource("BrushImGuiTextMuted")!;
            HostStatusPillDot.Fill = (Brush)FindResource("BrushImGuiTextMuted")!;
            HostStatusPill.Background = new SolidColorBrush(Color.FromArgb(0x14, 0xFF, 0xFF, 0xFF));
            HostStatusPill.BorderBrush = new SolidColorBrush(Color.FromArgb(0x26, 0xFF, 0xFF, 0xFF));
        }
    }

    private void AppendHostFoundLogLine(int? pid)
    {
        if (pid is int p)
        {
            HostLogLine.Text = $"Host found · PID {p}.";
            HostLogRow.Visibility = Visibility.Visible;
        }
        else
        {
            HostLogRow.Visibility = Visibility.Collapsed;
        }
    }

    private static void AnimateIn(UIElement el, TranslateTransform? trans, double slideY,
        TimeSpan delay, TimeSpan duration, IEasingFunction ease)
    {
        var fade = new DoubleAnimation(0, 1, duration)
        {
            EasingFunction = ease,
            BeginTime = delay
        };
        el.BeginAnimation(UIElement.OpacityProperty, fade);

        if (trans != null && slideY > 0)
        {
            var slide = new DoubleAnimation(slideY, 0, duration)
            {
                EasingFunction = ease,
                BeginTime = delay
            };
            trans.BeginAnimation(TranslateTransform.YProperty, slide);
        }
    }

    private void BeginSetupDialogEntranceAnimation()
    {
        SetupDialogScale.ScaleX = 0.94;
        SetupDialogScale.ScaleY = 0.94;
        SetupDialog.BeginAnimation(UIElement.OpacityProperty, null);
        SetupDialog.Opacity = 0;
        var ease = new QuadraticEase { EasingMode = EasingMode.EaseOut };
        var dur = TimeSpan.FromMilliseconds(280);
        SetupDialog.BeginAnimation(UIElement.OpacityProperty,
            new DoubleAnimation(0, 1, dur) { EasingFunction = ease });
        SetupDialogScale.BeginAnimation(ScaleTransform.ScaleXProperty,
            new DoubleAnimation(0.94, 1, dur) { EasingFunction = ease });
        SetupDialogScale.BeginAnimation(ScaleTransform.ScaleYProperty,
            new DoubleAnimation(0.94, 1, dur) { EasingFunction = ease });
    }

    private async Task HideSetupOverlayAnimatedAsync()
    {
        var ease = new QuadraticEase { EasingMode = EasingMode.EaseIn };
        var dur = TimeSpan.FromMilliseconds(220);
        var tcs = new TaskCompletionSource();

        var fade = new DoubleAnimation(1, 0, dur) { EasingFunction = ease };
        fade.Completed += (_, _) => tcs.SetResult();
        SetupOverlay.BeginAnimation(UIElement.OpacityProperty, fade);

        SetupDialogScale.BeginAnimation(ScaleTransform.ScaleXProperty,
            new DoubleAnimation(1, 0.96, dur) { EasingFunction = ease });
        SetupDialogScale.BeginAnimation(ScaleTransform.ScaleYProperty,
            new DoubleAnimation(1, 0.96, dur) { EasingFunction = ease });

        await tcs.Task;
        SetupOverlay.BeginAnimation(UIElement.OpacityProperty, null);
        SetupOverlay.Opacity = 1;
        SetupDialog.BeginAnimation(UIElement.OpacityProperty, null);
        SetupDialog.Opacity = 1;
        SetupDialogScale.ScaleX = 1;
        SetupDialogScale.ScaleY = 1;
    }

    private async Task RunBootstrapAsync()
    {
        SetupOverlay.Visibility = Visibility.Visible;
        SetupOverlay.Opacity = 1;
        BeginSetupDialogEntranceAnimation();
        SetupTitle.Text = "Preparing your system…";
        SetupDetail.Text = "Checking prerequisites…";
        SetupProgress.IsIndeterminate = true;
        SetupBytes.Text = "";
        SetupPercentLabel.Text = "WORKING…";
        SetupMark.Progress = 0.0;

        var status = new Progress<string>(t =>
        {
            Dispatcher.Invoke(() =>
            {
                SetupDetail.Text = t;
                if (t.Contains("silent", StringComparison.OrdinalIgnoreCase) ||
                    t.Contains("Installing ", StringComparison.OrdinalIgnoreCase))
                {
                    SetupProgress.IsIndeterminate = true;
                    SetupBytes.Text = "";
                    SetupPercentLabel.Text = "WORKING…";
                }

                if (t.StartsWith("Downloading", StringComparison.OrdinalIgnoreCase) ||
                    t.StartsWith("Fetching", StringComparison.OrdinalIgnoreCase))
                {
                    if (t.StartsWith("Fetching", StringComparison.OrdinalIgnoreCase))
                    {
                        SetupProgress.IsIndeterminate = true;
                        SetupBytes.Text = "";
                        SetupPercentLabel.Text = "WORKING…";
                    }
                    else
                    {
                        SetupProgress.IsIndeterminate = false;
                        SetupProgress.Value = 0;
                        SetupPercentLabel.Text = "0%";
                    }
                }
            });
        });

        var download = new Progress<DownloadProgress>(p =>
        {
            Dispatcher.Invoke(() =>
            {
                SetupProgress.IsIndeterminate = false;
                if (p.TotalBytes is long total && total > 0)
                {
                    var pct = 100.0 * p.BytesReceived / total;
                    SetupProgress.Value = pct;
                    SetupBytes.Text = $"{BytesToMb(p.BytesReceived):F1} / {BytesToMb(total):F1} MB";
                    SetupPercentLabel.Text = $"{pct:F0}%";
                    // Mirror download progress on the dialog's animated G mark
                    // so the logo visibly fills along with the progress bar.
                    SetupMark.Progress = pct / 100.0;
                }
                else
                {
                    SetupProgress.IsIndeterminate = true;
                    SetupBytes.Text = $"{BytesToMb(p.BytesReceived):F1} MB";
                    SetupPercentLabel.Text = "WORKING…";
                }
            });
        });

        try
        {
            await DependencySetupService.EnsurePrerequisitesAsync(status, download, _lifetimeCts.Token);
            SetupTitle.Text = "Ready";
            SetupDetail.Text = "Prerequisites are satisfied.";
            SetupProgress.IsIndeterminate = false;
            SetupProgress.Value = 100;
            SetupBytes.Text = "";
            SetupPercentLabel.Text = "100%";
            SetupMark.Progress = 1.0;
        }
        catch (OperationCanceledException)
        {
            SetupDetail.Text = "Cancelled.";
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                this,
                ex.Message + "\n\nYou can fix issues manually or click \"Retry dependency setup\".",
                "GoyLoader — setup",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }
        finally
        {
            await HideSetupOverlayAnimatedAsync();
            Dispatcher.Invoke(() => SetupOverlay.Visibility = Visibility.Collapsed);
        }

        RefreshPrerequisiteLabels();
        StartHostProcessTimer();
    }

    private static double BytesToMb(long bytes) => bytes / 1024.0 / 1024.0;

    private void StartHostProcessTimer()
    {
        _rlTimer ??= new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _rlTimer.Tick -= OnHostProcessTick;
        _rlTimer.Tick += OnHostProcessTick;
        _rlTimer.Start();
        OnHostProcessTick(null, EventArgs.Empty);
    }

    private void OnHostProcessTick(object? sender, EventArgs e) => PollHostProcess();

    private void PollHostProcess()
    {
        try
        {
            try
            {
                _hostProcess?.Dispose();
            }
            catch
            {
                // ignore
            }

            _hostProcess = null;
            var found = ProcessFinder.FindHostProcesses();
            if (found.Count > 0)
            {
                _hostProcess = found[0];
                for (var i = 1; i < found.Count; i++)
                {
                    try
                    {
                        found[i].Dispose();
                    }
                    catch
                    {
                        // ignore
                    }
                }
            }

            if (_hostProcess != null)
            {
                try
                {
                    if (_hostProcess.HasExited)
                    {
                        try
                        {
                            _hostProcess.Dispose();
                        }
                        catch
                        {
                            // ignore
                        }

                        _hostProcess = null;
                    }
                }
                catch
                {
                    try
                    {
                        _hostProcess?.Dispose();
                    }
                    catch
                    {
                        // ignore
                    }

                    _hostProcess = null;
                }
            }

            if (_hostProcess == null)
            {
                _injected = false;
                HostStatusLine.Text = "Waiting for RocketLeague.exe…";
                HostStatusLine.Foreground = (Brush)FindResource("BrushImGuiTextDim")!;
                TargetPid.Text = "Not detected";
                TargetPid.Foreground = (Brush)FindResource("BrushImGuiTextMuted")!;
                LogLine.Text = "Start the target game, then inject.";
                AppendHostFoundLogLine(null);
                UpdateHostStatusPill(hostRunning: false);
                // Header logo dims slightly when no target is present.
                HeaderMark.Progress = 0.4;
                HeaderMark.Speed = 1.0;
                // Reset the inject button if the user yanked the host mid-flight.
                if (HeaderMark != null) SetInjectButtonVisual(InjectButtonState.Idle);
            }
            else
            {
                HostStatusLine.Text = $"RocketLeague.exe · PID {_hostProcess.Id} · Steam";
                HostStatusLine.Foreground = (Brush)FindResource("BrushImGuiGreen")!;
                TargetPid.Text = $"Running · PID {_hostProcess.Id}";
                TargetPid.Foreground = (Brush)FindResource("BrushImGuiGreen")!;
                LogLine.Text = "Ready.";
                AppendHostFoundLogLine(_hostProcess.Id);
                UpdateHostStatusPill(hostRunning: true);
                HeaderMark.Progress = 1.0;
                HeaderMark.Speed = 1.0;

                if (AutoInjectToggle.IsChecked == true && !_injected)
                {
                    _injected = true;
                    Dispatcher.InvokeAsync(async () => await PerformInjectionAsync(silent: true));
                }
            }
        }
        catch
        {
            HostStatusLine.Text = "Waiting for RocketLeague.exe…";
            HostStatusLine.Foreground = (Brush)FindResource("BrushImGuiTextDim")!;
            TargetPid.Text = "Not detected";
            TargetPid.Foreground = (Brush)FindResource("BrushImGuiTextMuted")!;
            UpdateHostStatusPill(hostRunning: false);
            _hostProcess = null;
        }

        UpdateInjectEnabled();
        RefreshStatusIcons();
    }

    private void RefreshStatusIcons()
    {
        var r = DependencyChecker.Evaluate();
        var embedded = PayloadExtractor.IsPayloadEmbedded();

        VcIconOk.Visibility = r.VcRedistX64Ok ? Visibility.Visible : Visibility.Collapsed;
        VcIconErr.Visibility = r.VcRedistX64Ok ? Visibility.Collapsed : Visibility.Visible;

        ViGEmIconOk.Visibility = r.ViGEm == ViGEmBusStatus.Running ? Visibility.Visible : Visibility.Collapsed;
        ViGEmIconWarn.Visibility = (r.ViGEm == ViGEmBusStatus.Stopped || r.ViGEm == ViGEmBusStatus.NotInstalled)
            ? Visibility.Visible
            : Visibility.Collapsed;
        ViGEmIconErr.Visibility = r.ViGEm == ViGEmBusStatus.Unknown ? Visibility.Visible : Visibility.Collapsed;

        NvGpuIconOk.Visibility = r.NvidiaGpu == NvidiaGpuStatus.Present ? Visibility.Visible : Visibility.Collapsed;
        NvGpuIconWarn.Visibility = r.NvidiaGpu == NvidiaGpuStatus.NotFound ? Visibility.Visible : Visibility.Collapsed;
        NvGpuIconErr.Visibility = r.NvidiaGpu == NvidiaGpuStatus.Unknown ? Visibility.Visible : Visibility.Collapsed;

        PayloadIconOk.Visibility = embedded ? Visibility.Visible : Visibility.Collapsed;
        PayloadIconWarn.Visibility = embedded ? Visibility.Collapsed : Visibility.Visible;

        var hostRunning = _hostProcess != null;
        if (hostRunning)
        {
            try
            {
                hostRunning = !_hostProcess!.HasExited;
            }
            catch
            {
                hostRunning = false;
            }
        }

        HostIconOk.Visibility = hostRunning ? Visibility.Visible : Visibility.Collapsed;
        HostIconIdle.Visibility = hostRunning ? Visibility.Collapsed : Visibility.Visible;
    }

    private void RefreshPrerequisiteLabels()
    {
        var r = DependencyChecker.Evaluate();

        VcDetail.Text = r.VcRedistDetail;
        VcDetail.Foreground = r.VcRedistX64Ok
            ? (Brush)FindResource("BrushImGuiGreen")!
            : (Brush)FindResource("BrushImGuiAccent")!;

        ViGEmDetail.Text = r.ViGEmDetail;
        ViGEmDetail.Foreground = r.ViGEm switch
        {
            ViGEmBusStatus.Running => (Brush)FindResource("BrushImGuiGreen")!,
            ViGEmBusStatus.Stopped => (Brush)FindResource("BrushImGuiWarn")!,
            ViGEmBusStatus.NotInstalled => (Brush)FindResource("BrushImGuiWarn")!,
            _ => (Brush)FindResource("BrushImGuiAccent")!
        };

        NvidiaGpuDetail.Text = r.NvidiaGpuDetail;
        NvidiaGpuDetail.Foreground = r.NvidiaGpu switch
        {
            NvidiaGpuStatus.Present => (Brush)FindResource("BrushImGuiGreen")!,
            NvidiaGpuStatus.NotFound => (Brush)FindResource("BrushImGuiWarn")!,
            _ => (Brush)FindResource("BrushImGuiAccent")!
        };

        var embedded = PayloadExtractor.IsPayloadEmbedded();
        PayloadStatus.Text = embedded
            ? "Embedded"
            : "Not embedded";
        PayloadStatus.Foreground = embedded
            ? (Brush)FindResource("BrushImGuiGreen")!
            : (Brush)FindResource("BrushImGuiWarn")!;

        UpdateInjectEnabled();
        RefreshStatusIcons();
    }

    private void UpdateInjectEnabled()
    {
        var r = DependencyChecker.Evaluate();
        var embedded = PayloadExtractor.IsPayloadEmbedded();
        // ViGEmBus is optional: bot uses Internal (SDK) input without it; virtual controller requires ViGEm.
        var ok = embedded && r.VcRedistX64Ok && _hostProcess != null;
        if (_hostProcess != null)
        {
            try
            {
                ok = ok && !_hostProcess.HasExited;
            }
            catch
            {
                ok = false;
            }
        }

        BtnInject.IsEnabled = ok;
    }

    private async void OnRetryDepsClick(object sender, RoutedEventArgs e)
    {
        BtnRetryDeps.IsEnabled = false;
        try
        {
            await RunBootstrapAsync();
        }
        finally
        {
            BtnRetryDeps.IsEnabled = true;
        }
    }

    private void OnLaunchHostClick(object sender, RoutedEventArgs e)
    {
        if (ProcessFinder.TryLaunchSteamGame())
            MessageBox.Show(this, "Client launched — wait for the main menu.",
                "GoyLoader", MessageBoxButton.OK, MessageBoxImage.Information);
        else
            MessageBox.Show(this, "Could not launch via Steam. Start the game manually.",
                "GoyLoader", MessageBoxButton.OK, MessageBoxImage.Warning);
    }

    private async void OnInjectClick(object sender, RoutedEventArgs e)
    {
        await PerformInjectionAsync(silent: false);
    }

    private void OnAutoInjectChanged(object sender, RoutedEventArgs e)
    {
        if (AutoInjectToggle.IsChecked != true)
            _injected = false;
    }

    private async Task PerformInjectionAsync(bool silent)
    {
        LoaderSessionLog.WriteGlobal($"PerformInjectionAsync called. silent={silent}, host_present={_hostProcess != null}.");
        if (_hostProcess == null)
        {
            LoaderSessionLog.WriteGlobal("Injection aborted: target process was not available.");
            if (!silent)
                MessageBox.Show(this, "The host process is not running.", "GoyLoader", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            if (_hostProcess.HasExited)
            {
                LoaderSessionLog.WriteGlobal($"Injection aborted: process PID {_hostProcess.Id} had already exited.");
                PollHostProcess();
                if (!silent)
                    MessageBox.Show(this, "That session ended — start the target game again.", "GoyLoader", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
        }
        catch
        {
            LoaderSessionLog.WriteGlobal("Injection aborted: failed while checking target process state.");
            PollHostProcess();
            return;
        }

        BtnInject.IsEnabled = false;
        SetInjectButtonVisual(InjectButtonState.Working, "Unpacking…");
        // Header logo speeds up + de-fills slightly to communicate "working".
        HeaderMark.Progress = 0.7;
        HeaderMark.Speed = 2.0;
        try
        {
            if (!PayloadExtractor.IsPayloadEmbedded())
            {
                LoaderSessionLog.WriteGlobal("Injection aborted: embedded payload is missing.");
                SetInjectButtonVisual(InjectButtonState.Idle);
                if (!silent)
                    MessageBox.Show(this,
                        "Bot payload is not embedded in this build. Run scripts/Package-Payload.ps1 after building internal_bot, then rebuild GoyLoader.",
                        "GoyLoader",
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                else
                    LogLine.Text = "Payload not embedded.";
                return;
            }

            var status = new Progress<string>(m => Dispatcher.Invoke(() => LogLine.Text = m));
            var download = new Progress<DownloadProgress>(p =>
            {
                Dispatcher.Invoke(() =>
                {
                    if (p.TotalBytes is long total && total > 0)
                        LogLine.Text = $"Downloading… {100.0 * p.BytesReceived / total:F0}%";
                });
            });

            await DependencySetupService.EnsureVcRedistPresentAsync(status, download, _lifetimeCts.Token);
            DependencySetupService.TryStartViGEmIfInstalled(status);
            RefreshPrerequisiteLabels();

            if (!DependencyChecker.Evaluate().VcRedistX64Ok)
            {
                if (!silent)
                    MessageBox.Show(this,
                        "Visual C++ 2015–2022 (x64) is still missing after setup. Reboot if Windows just finished updates, then click \"Retry dependency setup\" or try Inject again.",
                        "GoyLoader",
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                else
                    LogLine.Text = "VC++ missing — inject manually.";
                return;
            }

            LogLine.Text = "Unpacking bot files…";
            SetInjectButtonVisual(InjectButtonState.Working, "Unpacking…");
            var dir = PayloadExtractor.ExtractPayloadToCache();
            LoaderSessionLog.WriteGlobal($"Payload extracted to '{dir}'.");
            LoaderSessionLog.WritePayload(dir, $"Injection attempt started. silent={silent}, pid={_hostProcess.Id}.");
            var reportPath = PayloadDiagnostics.WriteReport(dir);
            LoaderSessionLog.WritePayload(dir, $"Payload diagnostic report refreshed at '{reportPath}'.");
            var dll = Path.Combine(dir, "GoySDK.dll");
            LogLine.Text = "Injecting…";
            SetInjectButtonVisual(InjectButtonState.Working, "Injecting…");
            var handle = DllInjector.InjectLoadLibrary(_hostProcess.Id, dll);
            LoaderSessionLog.WritePayload(dir, $"InjectLoadLibrary returned handle 0x{handle.ToInt64():X}.");
            var dllName = Path.GetFileName(dll);
            // verify the module shows up in the target process
            var loaded = DllInjector.IsModuleLoaded(_hostProcess.Id, dllName);
            if (!loaded)
            {
                LogLine.Text = "Injection reported success but module not found in target process.";
                LoaderSessionLog.WritePayload(dir, "Injection failed: GoySDK.dll was not visible in the target process after LoadLibrary.");
                throw new InvalidOperationException("Injection reported success but module not found in target process. Check AV, process protections, or architecture.");
            }

            // Verify the bridge loader successfully pulled in the real core module.
            // GoySDKCore.dll is ~64 MB and depends on ~260 MB of LibTorch DLLs, so
            // the bridge thread may need several seconds to finish LoadLibraryW.
            // Poll every 500 ms for up to 10 seconds before giving up.
            SetInjectButtonVisual(InjectButtonState.Working, "Loading core…");
            var coreLoaded = false;
            for (var coreAttempt = 0; coreAttempt < 20 && !coreLoaded; coreAttempt++)
            {
                await Task.Delay(500);
                try
                {
                    coreLoaded = DllInjector.IsModuleLoaded(_hostProcess.Id, "GoySDKCore.dll");
                }
                catch
                {
                    // Process may have exited; break and let the check below handle it.
                    break;
                }

                if (!coreLoaded && coreAttempt % 4 == 3)
                    LogLine.Text = $"Waiting for GoySDKCore.dll… ({coreAttempt + 1}/20)";
            }

            if (!coreLoaded)
            {
                var bridgeLog = Path.Combine(dir, "GoyLoaderBridge.log");
                LoaderSessionLog.WritePayload(dir, $"Injection failed: GoySDKCore.dll did not load after retries. Bridge log: {bridgeLog}");
                throw new InvalidOperationException(
                    "GoySDK.dll injected, but GoySDKCore.dll did not load after 10 seconds.\n" +
                    $"Check bridge log: {bridgeLog}\n" +
                    $"Check payload report: {reportPath}");
            }
            LoaderSessionLog.WritePayload(dir, $"Injection path completed through core load for PID {_hostProcess.Id}.");
            LogLine.Text = $"Injected (PID {_hostProcess.Id})";
            LogLine.Foreground = (Brush)FindResource("BrushImGuiGreen")!;
            SetInjectButtonVisual(InjectButtonState.Done);
            HeaderMark.Progress = 1.0;
            HeaderMark.Speed = 1.0;

            if (!silent)
                MessageBox.Show(this,
                    "Injection completed.\n\nUse only where you have permission to do so.",
                    "GoyLoader", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (OperationCanceledException)
        {
            LoaderSessionLog.WriteGlobal("Injection cancelled.");
            LogLine.Text = "Cancelled.";
            SetInjectButtonVisual(InjectButtonState.Idle);
        }
        catch (Exception ex)
        {
            LoaderSessionLog.WriteGlobal($"Injection exception: {ex.GetType().Name}: {ex.Message}");
            LogLine.Text = ex.Message;
            LogLine.Foreground = (Brush)FindResource("BrushImGuiErr")!;
            SetInjectButtonVisual(InjectButtonState.Idle);
            if (!silent)
                MessageBox.Show(this, ex.Message, "Injection failed", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            PollHostProcess();
        }
    }

    protected override void OnClosed(EventArgs e)
    {
        _lifetimeCts.Cancel();
        _lifetimeCts.Dispose();
        _rlTimer?.Stop();
        _rlTimer = null;

        // Stop UI storyboards so the dispatcher doesn't hold them past close.
        try { _hostIdleSpinSb?.Stop(); } catch { /* ignore */ }
        try { _injectSpinSb?.Stop(); } catch { /* ignore */ }
        _hostIdleSpinSb = null;
        _injectSpinSb = null;
        try
        {
            _hostProcess?.Dispose();
        }
        catch
        {
            // ignore
        }

        _hostProcess = null;
        base.OnClosed(e);
    }
}
