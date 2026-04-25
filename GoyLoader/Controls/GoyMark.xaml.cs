using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace GoyLoader.Controls;

/// <summary>
/// Animated G mark used as the loader's logo. Concentric ring fills as Progress
/// goes 0→1; a small comet head sweeps continuously around the ring at a rate
/// controlled by Speed. The accent color is brand-driven.
/// </summary>
public partial class GoyMark : UserControl
{
    public static readonly DependencyProperty AccentProperty = DependencyProperty.Register(
        nameof(Accent), typeof(Brush), typeof(GoyMark),
        new PropertyMetadata(new SolidColorBrush(Color.FromRgb(0xFF, 0x3B, 0x3B)), OnAccentChanged));

    public static readonly DependencyProperty ProgressProperty = DependencyProperty.Register(
        nameof(Progress), typeof(double), typeof(GoyMark),
        new PropertyMetadata(1.0, OnProgressChanged));

    public static readonly DependencyProperty SpinningProperty = DependencyProperty.Register(
        nameof(Spinning), typeof(bool), typeof(GoyMark),
        new PropertyMetadata(true, OnSpinningChanged));

    public static readonly DependencyProperty SpeedProperty = DependencyProperty.Register(
        nameof(Speed), typeof(double), typeof(GoyMark),
        new PropertyMetadata(1.0, OnSpeedChanged));

    public Brush Accent
    {
        get => (Brush)GetValue(AccentProperty);
        set => SetValue(AccentProperty, value);
    }

    /// <summary>0 = empty ring, 1 = ring fully drawn (matching the visible-arc cap).</summary>
    public double Progress
    {
        get => (double)GetValue(ProgressProperty);
        set => SetValue(ProgressProperty, value);
    }

    public bool Spinning
    {
        get => (bool)GetValue(SpinningProperty);
        set => SetValue(SpinningProperty, value);
    }

    /// <summary>Multiplier on the comet rotation rate. 1.0 = 2.4s per revolution.</summary>
    public double Speed
    {
        get => (double)GetValue(SpeedProperty);
        set => SetValue(SpeedProperty, value);
    }

    private Storyboard? _cometStoryboard;

    // 76px-diameter ring → r = 38, circumference C = 2πr ≈ 238.76. The visible
    // arc covers 86% of the circumference (the rest forms the G's opening).
    private const double Radius = 38.0;
    private const double Circumference = 2.0 * System.Math.PI * Radius;
    private const double VisibleArcFraction = 0.86;

    public GoyMark()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ApplyAccent(Accent);
        ApplyProgress(Progress);
        StartCometIfNeeded();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        StopComet();
    }

    private static void OnAccentChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is GoyMark m && e.NewValue is Brush b) m.ApplyAccent(b);
    }

    private static void OnProgressChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is GoyMark m) m.ApplyProgress((double)e.NewValue);
    }

    private static void OnSpinningChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is GoyMark m) m.StartCometIfNeeded();
    }

    private static void OnSpeedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is GoyMark m) m.StartCometIfNeeded();
    }

    private void ApplyAccent(Brush accent)
    {
        var color = accent is SolidColorBrush scb
            ? scb.Color
            : Color.FromRgb(0xFF, 0x3B, 0x3B);

        FgRing.Stroke = new SolidColorBrush(color);
        StemLine.Stroke = new SolidColorBrush(color);
        StemDot.Fill = new SolidColorBrush(color);

        // Halo: same hue, soft inner→transparent outer. Inner alpha 0x73 ≈ 0.45.
        HaloStopInner.Color = Color.FromArgb(0x73, color.R, color.G, color.B);
        HaloStopOuter.Color = Color.FromArgb(0x00, color.R, color.G, color.B);
    }

    private void ApplyProgress(double progress)
    {
        var p = System.Math.Max(0.0, System.Math.Min(1.0, progress));
        var thickness = FgRing.StrokeThickness <= 0 ? 3.5 : FgRing.StrokeThickness;

        // WPF StrokeDashArray units are multiples of stroke thickness, while we
        // think in pixel arc-lengths. Convert before assigning.
        var fullVisible = Circumference * VisibleArcFraction;
        var fillLen = fullVisible * p;
        var gapLen = Circumference - fillLen;

        // A near-zero dash with stroke-line-cap=Round still paints a small dot.
        // Hide the ring entirely until progress crosses a tiny threshold so the
        // logo doesn't show a stray pip when state is "idle".
        if (fillLen < 0.5)
        {
            FgRing.StrokeDashArray = new DoubleCollection { 0.0001, 9999.0 };
        }
        else
        {
            FgRing.StrokeDashArray = new DoubleCollection
            {
                fillLen / thickness,
                gapLen / thickness,
            };
        }

        // The static background ring also needs to show the visible-arc gutter.
        var bgThickness = BgRing.StrokeThickness <= 0 ? 3.0 : BgRing.StrokeThickness;
        BgRing.StrokeDashArray = new DoubleCollection
        {
            fullVisible / bgThickness,
            (Circumference - fullVisible) / bgThickness,
        };
    }

    private void StartCometIfNeeded()
    {
        StopComet();
        if (!Spinning) return;

        var revolutionSeconds = 2.4 / System.Math.Max(0.1, Speed);

        var anim = new DoubleAnimation
        {
            From = 0,
            To = 360,
            Duration = new Duration(System.TimeSpan.FromSeconds(revolutionSeconds)),
            RepeatBehavior = RepeatBehavior.Forever,
        };

        _cometStoryboard = new Storyboard();
        Storyboard.SetTarget(anim, CometRotate);
        Storyboard.SetTargetProperty(anim, new PropertyPath(RotateTransform.AngleProperty));
        _cometStoryboard.Children.Add(anim);
        _cometStoryboard.Begin();
    }

    private void StopComet()
    {
        if (_cometStoryboard != null)
        {
            _cometStoryboard.Stop();
            _cometStoryboard.Children.Clear();
            _cometStoryboard = null;
        }
        CometRotate.Angle = 0;
    }
}
