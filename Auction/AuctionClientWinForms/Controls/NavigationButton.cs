using System.Drawing.Drawing2D;
using System.ComponentModel;

namespace AuctionClientWinForms.Controls;

internal sealed class NavigationButton : Button
{
    private bool m_hasNotification;

    [Browsable(false)]
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    public bool HasNotification
    {
        get => m_hasNotification;
        set
        {
            if (m_hasNotification == value)
            {
                return;
            }
            m_hasNotification = value;
            Invalidate();
        }
    }

    protected override void OnPaint(PaintEventArgs eventArgs)
    {
        base.OnPaint(eventArgs);
        if (!HasNotification)
        {
            return;
        }

        const int diameter = 10;
        Rectangle dotBounds = new(Width - diameter - 14, 14, diameter, diameter);
        eventArgs.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        using SolidBrush brush = new(Color.FromArgb(229, 57, 53));
        eventArgs.Graphics.FillEllipse(brush, dotBounds);
    }
}
