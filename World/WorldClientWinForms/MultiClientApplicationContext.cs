using WorldClientWinForms.Configuration;

namespace WorldClientWinForms;

internal sealed class MultiClientApplicationContext : ApplicationContext
{
    private int m_openFormCount;

    public MultiClientApplicationContext(ClientSettings settings, int clientCount)
    {
        Rectangle workingArea = Screen.PrimaryScreen?.WorkingArea ?? new Rectangle(0, 0, 1920, 1080);
        for (int index = 0; index < clientCount; ++index)
        {
            var form = new MainForm(settings, $"Client {index + 1}");
            form.FormClosed += HandleFormClosed;
            if (clientCount == 2 && workingArea.Width >= form.MinimumSize.Width * 2)
            {
                int width = workingArea.Width / 2;
                form.Bounds = new Rectangle(
                    workingArea.Left + index * width,
                    workingArea.Top,
                    width,
                    workingArea.Height);
            }
            else
            {
                form.Location = CalculateLocation(workingArea, index, form.Size);
            }
            ++m_openFormCount;
            form.Show();
        }
    }

    private void HandleFormClosed(object? sender, FormClosedEventArgs e)
    {
        if (--m_openFormCount == 0)
        {
            ExitThread();
        }
    }

    private static Point CalculateLocation(Rectangle workingArea, int index, Size formSize)
    {
        int columns = Math.Max(1, workingArea.Width / Math.Max(1, formSize.Width / 2));
        int offsetX = (index % columns) * 42;
        int offsetY = (index / columns) * 42;
        int maxX = Math.Max(workingArea.Left, workingArea.Right - formSize.Width);
        int maxY = Math.Max(workingArea.Top, workingArea.Bottom - formSize.Height);
        return new Point(
            Math.Clamp(workingArea.Left + offsetX, workingArea.Left, maxX),
            Math.Clamp(workingArea.Top + offsetY, workingArea.Top, maxY));
    }
}
