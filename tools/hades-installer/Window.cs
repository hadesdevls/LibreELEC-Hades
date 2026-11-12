// LibreELEC Hades installer for Windows: the window.
// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace Hades.Installer
{
    // "Type ERASE to go on."
    sealed class ConfirmForm : Form
    {
        readonly TextBox box = new TextBox();
        readonly PillButton ok = new PillButton(), cancel = new PillButton();
        readonly Label what = new Label();
        readonly Panel top = new Panel();
        readonly string banner;
        readonly bool danger;

        // word: what has to be typed before the button of the action can be pressed. null: nothing is typed
        // (a second question about something that was confirmed that way already), and it is Cancel that
        // the Enter key presses.
        public ConfirmForm(string banner, bool danger, string details, string word, string action)
        {
            bool typed = word != null;
            this.banner = banner;
            this.danger = danger;
            Text = L.T("Confirm", "Confirmar");
            Font = Ui.F(13f);
            BackColor = Ui.Back;
            ForeColor = Ui.Text;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowIcon = false;
            StartPosition = FormStartPosition.CenterParent;
            AutoScaleMode = AutoScaleMode.None;
            int width = 560, inner = width - 48;
            // as tall as what it has to say
            int say = TextRenderer.MeasureText(details, Font, new Size(Ui.S(inner), 0), TextFormatFlags.WordBreak).Height;
            say = Math.Max(Ui.S(60), say + Ui.S(6));
            int y = Ui.S(56 + 18);
            ClientSize = new Size(Ui.S(width), y + say + (typed ? Ui.S(14 + 24 + 4 + 30) : 0) + Ui.S(18 + 40 + 22));

            top.SetBounds(0, 0, Ui.S(width), Ui.S(56));
            top.BackColor = danger ? Color.FromArgb(150, 28, 30) : Ui.Ink;
            top.Paint += PaintBanner;

            what.Text = details;
            what.ForeColor = Ui.Text;
            what.SetBounds(Ui.S(24), y, Ui.S(inner), say);
            y += say;

            var ask = new Label();
            ask.Text = L.T("To continue, type ", "Para continuar, escribe ") + word + ":";
            ask.Font = Ui.F(13f, "semi");
            ask.SetBounds(Ui.S(24), y + Ui.S(14), Ui.S(inner), Ui.S(24));
            box.Font = Ui.F(15f);
            box.BackColor = Ui.Field;
            box.ForeColor = Ui.Text;
            box.SetBounds(Ui.S(24), y + Ui.S(14 + 28), Ui.S(220), Ui.S(30));
            box.CharacterCasing = CharacterCasing.Upper;
            box.TextChanged += delegate { ok.Enabled = box.Text.Trim() == word; };
            ask.Visible = box.Visible = typed;
            if (typed) y += Ui.S(14 + 24 + 4 + 30);

            y += Ui.S(18);
            ok.Text = action;
            ok.Enabled = !typed;
            ok.DialogResult = DialogResult.OK;
            ok.Font = Ui.F(14f, "semi");
            ok.BackColor = danger ? Ui.Danger : Ui.Primary;
            ok.ForeColor = danger ? Color.White : Ui.PrimaryText;
            ok.SetBounds(Ui.S(width - 24 - 108 - 10 - 210), y, Ui.S(210), Ui.S(40));
            cancel.Text = L.T("Cancel", "Cancelar");
            cancel.DialogResult = DialogResult.Cancel;
            cancel.Font = Ui.F(14f);
            cancel.Outline = true;
            cancel.ForeColor = Ui.Text;
            cancel.SetBounds(Ui.S(width - 24 - 108), y, Ui.S(108), Ui.S(40));
            AcceptButton = typed ? ok : cancel;
            CancelButton = cancel;
            Controls.AddRange(new Control[] { what, ask, box, ok, cancel, top });
            if (!typed) ActiveControl = cancel;
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Ui.DarkTitleBar(Handle);
        }

        void PaintBanner(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            if (!danger) Ui.DrawMark(g, Ui.Sf(36), top.Height / 2f, Ui.Sf(11), Ui.Disc, true, false);
            else
            {
                // a triangle with a bar and a point: this one erases
                float cx = Ui.Sf(36), cy = top.Height / 2f, s = Ui.Sf(13);
                using (var pen = new Pen(Color.White, Math.Max(1.5f, Ui.Sf(2f))) { LineJoin = System.Drawing.Drawing2D.LineJoin.Round })
                {
                    g.DrawPolygon(pen, new[] { new PointF(cx, cy - s), new PointF(cx + s * 1.15f, cy + s * 0.85f), new PointF(cx - s * 1.15f, cy + s * 0.85f) });
                    g.DrawLine(pen, cx, cy - s * 0.3f, cx, cy + s * 0.3f);
                }
                using (var b = new SolidBrush(Color.White)) g.FillEllipse(b, cx - Ui.Sf(1.3f), cy + s * 0.5f, Ui.Sf(2.6f), Ui.Sf(2.6f));
            }
            TextRenderer.DrawText(g, banner, Ui.F(17f, "semi"), new Rectangle(Ui.S(64), 0, top.Width - Ui.S(80), top.Height), Color.White,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
        }

        // (for the tests) whether the button of the action can be pressed without typing anything, whether
        // the Enter key presses it, and what the question says
        // (the Enter key presses the button that has the focus, and where none has it, the default one)
        public bool ActionReady { get { return ok.Enabled; } }
        public bool EnterActs { get { return AcceptButton == ok && ActiveControl != cancel; } }
        public bool EnterCancels { get { return AcceptButton == cancel && ActiveControl == cancel; } }
        public string Said { get { return what.Text; } }
    }

    sealed class VolumeChoice
    {
        public PartitionInfo Partition;
        public VolumeInfo Volume;
        public long Spare = -1; // what it can give; -1: not asked yet, 0: nothing
        public string Problem;

        public override string ToString()
        {
            string name = Partition.Letter + ":" + (Volume.Label.Length > 0 ? L.T(" “", " «") + Volume.Label + L.T("”", "»") : "");
            return name + "  ·  " + Words.Size(Volume.Size - Volume.Used) + L.T(" free", " libres");
        }
    }

    sealed class MainForm : Form
    {
        // What Hades sees of the other disks when it is inside the PC: the last line of the two texts below.
        // It depends on the image (ImagePlan.HidesOtherDisks), which is known once the image has been read
        // through: until then, nothing is said. beside: Hades shares its disk with partitions of the user.
        static string OtherDisksText(ImagePlan plan, bool beside)
        {
            if (plan == null || !plan.Scanned) return "";
            if (!plan.HidesOtherDisks)
                return L.T("\r\n•  Installed inside the PC, Hades sees the other disks read-only. From a USB drive it does not see them.",
                           "\r\n•  Dentro del PC, Hades ve los demás discos en solo lectura. Desde USB no los ve.");
            return beside
                ? L.T("\r\n•  Inside the PC, Hades cannot see the other NVMe/SATA disks, and does not open your partitions on this one.",
                      "\r\n•  Dentro del PC, Hades no ve los demás discos NVMe/SATA, y no abre tus particiones de este.")
                : L.T("\r\n•  Inside the PC, Hades cannot see the other NVMe/SATA disks, as from USB; an entry of its menu shows them.",
                      "\r\n•  Dentro del PC, Hades no ve los demás discos NVMe/SATA, como desde USB; una entrada de su menú los muestra.");
        }

        static string BesideText(ImagePlan plan)
        {
            return L.T(
                "✔  Nothing is erased: Windows shrinks a partition of that disk and Hades uses the space that frees up.\r\n" +
                "✔  It can be undone: “Remove LibreELEC Hades” gives the space back to that partition.\r\n" +
                "✘  That disk loses the space you give it. Shrinking can take a while, and Windows decides how much is possible.\r\n" +
                "✘  Back up what matters first: changing partitions carries some risk (a power cut, for instance).",
                "✔  No se borra nada: Windows encoge una partición de ese disco y Hades usa el hueco.\r\n" +
                "✔  Se puede deshacer: «Quitar LibreELEC Hades» devuelve el espacio a esa partición.\r\n" +
                "✘  Ese disco pierde el espacio que le des. Encoger puede tardar, y Windows decide cuánto se puede.\r\n" +
                "✘  Haz antes copia de lo importante: cambiar particiones tiene algún riesgo (un corte de luz).") +
                OtherDisksText(plan, true);
        }

        static string WholeText(ImagePlan plan)
        {
            return L.T(
                "✔  The simplest and most robust way: the whole disk is for Hades.\r\n" +
                "✘  Everything on that disk is ERASED, and there is no way back.\r\n" +
                "•  To undo it, “Remove LibreELEC Hades” leaves the disk empty and ready for Windows.",
                "✔  Lo más simple y robusto: todo el disco es para Hades.\r\n" +
                "✘  Se BORRA todo lo que hay en ese disco, sin vuelta atrás.\r\n" +
                "•  Para deshacerlo, «Quitar LibreELEC Hades» lo deja vacío y listo para Windows.") +
                OtherDisksText(plan, false);
        }

        readonly Panel header = new Panel();     // the band at the top; in direction B the bar at the left
        readonly LinkLabel language = new LinkLabel();
        readonly StepHead one = new StepHead(), two = new StepHead();
        readonly Panel line1 = new Panel();
        readonly Label imageLabel = new Label();
        readonly PillButton imageButton = new PillButton();
        readonly RadioButton usb = new ThemedRadio();
        readonly RadioButton inside = new ThemedRadio();
        readonly Panel listFrame = new Panel();
        readonly ListBox list = new ListBox();
        readonly LinkLabel missing = new LinkLabel();
        readonly PillButton refresh = new PillButton();
        readonly Panel options = new Panel();
        readonly Callout notice = new Callout();
        readonly RadioButton beside = new ThemedRadio();
        readonly RadioButton whole = new ThemedRadio();
        readonly Callout explain = new Callout();
        readonly Label volumeLabel = new Label();
        readonly ComboBox volumeBox = new ComboBox();
        readonly Label sizeLabel = new Label();
        readonly NumericUpDown sizeBox = new NumericUpDown();
        readonly Label sizeUnit = new Label();
        readonly LinkLabel remove = new LinkLabel();
        readonly CheckBox diagnostics = new ThemedCheck();
        readonly SlimBar bar = new SlimBar();
        readonly Label status = new Label();
        readonly PillButton go = new PillButton();
        readonly PillButton update = new PillButton();
        readonly Panel donePanel = new Panel();
        readonly PillButton close = new PillButton();
        readonly WorkList stages = new WorkList();
        bool showSteps;          // the stages are on show instead of the options while a disk is worked on
        // The panel of "done" is on show. Then what is behind it is not made again, neither for a device that comes or goes
        // nor for a display of another scale: UpdateState would put back what the panel hides, and the keyboard would reach
        // it behind the panel. What was skipped is done when the panel goes (HideDone).
        bool done, doneSkippedDevices, doneSkippedState;
        readonly List<Control> doneHid = new List<Control>();   // what ShowDone hid, to put back
        int disksLoaded;         // (for the tests) how many times the list of disks was made
        readonly Timer deviceTimer = new Timer();
        readonly Stopwatch clock = new Stopwatch();
        readonly int[] stepState = new int[3];  // of the three steps: 0 not yet, 1 now, 2 done
        readonly string[] stepSub = new string[] { "", "", "" };
        Icon bigIcon;

        string imagePath;
        ImagePlan plan;          // of imagePath; scanned once an internal disk is wanted
        // "", "checking", "ok", "unchecked", or why it cannot be used: "none" (there is no image), "damaged" (its
        // hash is another), "unreadable" or "unusable" (with imageDetail)
        string imageState = "";
        string imageDetail;      // what the system or the engine said, in the language that was in use then
        bool scanning;
        List<DiskInfo> excluded = new List<DiskInfo>();
        bool busy;
        int wordings;            // how many times the language was changed
        bool rewording;          // the volumes are being named in the other language: none was chosen anew
        bool diskDiagnostics;    // the Hades of the chosen disk has its diagnostics on (read when the disk is chosen)
        bool? diagnosticsAsked;  // what the user set the box of the diagnostics to; null: not touched, it follows the disk
        DiskInfo doneDisk;       // what the last panel says (ShowDone), to say it in the other language
        Place donePlace;
        bool doneUpdated;
        DiagKeep doneDiag;       // what became of the folder hades-diag in that update (null: not an update)

        Place Where { get { return inside.Checked ? Place.Internal : Place.Usb; } }

        public MainForm(string image)
        {
            if (!Ui.FixedScale) Ui.Scale = Ui.SystemScale();
            Ui.Apply();
            AutoScaleMode = AutoScaleMode.None;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Ui.Back;
            ForeColor = Ui.Text;
            Icon = Ui.AppIcon(true);

            header.Paint += PaintHeader;
            language.LinkClicked += delegate { SwitchLanguage(); };
            language.BackColor = Color.Transparent;
            header.Controls.Add(language);

            one.Number = 1;
            two.Number = 2;
            imageButton.Click += delegate { ChooseImage(); };
            imageButton.Outline = true;
            refresh.Outline = true;

            usb.Checked = true;
            usb.CheckedChanged += delegate { if (usb.Checked) PlaceChanged(); };
            inside.CheckedChanged += delegate { if (inside.Checked) PlaceChanged(); };

            listFrame.Controls.Add(list);
            list.Dock = DockStyle.Fill;
            list.DrawMode = DrawMode.OwnerDrawFixed;
            list.IntegralHeight = false;
            list.BorderStyle = BorderStyle.None;
            list.DrawItem += DrawDisk;
            list.SelectedIndexChanged += delegate { DiskChanged(); };
            missing.LinkClicked += delegate { ShowExcluded(); };
            refresh.Click += delegate { LoadDisks(); };

            beside.Checked = true;
            beside.CheckedChanged += delegate { if (beside.Checked) UpdateState(); };
            whole.CheckedChanged += delegate { if (whole.Checked) UpdateState(); };
            volumeBox.DropDownStyle = ComboBoxStyle.DropDownList;
            volumeBox.HandleCreated += delegate { Ui.DarkControl(volumeBox); };
            sizeBox.HandleCreated += delegate { Ui.DarkControl(sizeBox); };
            volumeBox.SelectedIndexChanged += delegate { VolumeChanged(); };
            sizeBox.Minimum = 9;
            sizeBox.Maximum = 9;
            sizeBox.ValueChanged += delegate { UpdateState(); };
            sizeUnit.Text = "GB";
            remove.LinkClicked += delegate { StartRemove(); };
            options.Controls.AddRange(new Control[] { notice, beside, whole, explain, volumeLabel, volumeBox, sizeLabel, sizeBox,
                                                      sizeUnit, remove });

            // (for every way of installing and for an update: under what each of them asks)
            // (a click or the space bar, not a change that this program makes)
            diagnostics.Click += delegate { diagnosticsAsked = diagnostics.Checked; };

            bar.Maximum = 1000;
            go.Click += delegate { Start(); };
            update.Visible = false;
            update.Click += delegate { StartUpdate(); };

            donePanel.Visible = false;
            close.Click += delegate { Close(); };
            donePanel.Controls.Add(close);

            Controls.AddRange(new Control[] { donePanel, header, one, line1, imageLabel, imageButton, two, usb, inside, listFrame, missing,
                                              refresh, options, stages, diagnostics, bar, status, go, update });
            // the header is the last stop of the Tab key: the keyboard starts where it did, not at the language link
            header.TabIndex = Controls.Count;

            deviceTimer.Interval = 1200;
            deviceTimer.Tick += delegate { deviceTimer.Stop(); DevicesChanged(); };

            if (!Native.Uefi) inside.Enabled = false;
            LayoutAll();
            SetTexts();
            SetImage(image);
            LoadDisks();
        }

        // Where things go. Every number is in pixels of a display at 100 % and goes through Ui.S: this is run
        // again when the window is moved to a display of another scale.
        int X0 { get { return 216; } }   // the bar at the side
        int Top0 { get { return 36; } }   // the top of the first step
        const int WindowHeight = 700;

        // a control of the content, x and y from the left of the content
        void B(Control c, int x, int y, int w, int h) { c.SetBounds(Ui.S(X0 + x), Ui.S(y), Ui.S(w), Ui.S(h)); }
        // a control inside another one
        static void R(Control c, int x, int y, int w, int h) { c.SetBounds(Ui.S(x), Ui.S(y), Ui.S(w), Ui.S(h)); }

        void LayoutAll()
        {
            int T = Top0;
            SuspendLayout();
            Font = Ui.F(13f);
            ClientSize = new Size(Ui.S(X0 + 760), Ui.S(WindowHeight));
            header.BackColor = Ui.Ink;
            header.SetBounds(0, 0, Ui.S(216), Ui.S(WindowHeight));
            language.Font = Ui.F(13f);
            language.LinkColor = language.VisitedLinkColor = Ui.InkSoft;
            language.ActiveLinkColor = Color.White;
            language.DisabledLinkColor = Color.FromArgb(128, 134, 150);
            R(language, 24, WindowHeight - 44, 168, 22);
            language.TextAlign = ContentAlignment.MiddleLeft;

            one.Numbered = two.Numbered = false; // the steps are numbered in the bar at the side
            B(one, 24, T, 560, 28);
            one.Font = two.Font = Ui.F(15f, "semi");
            imageLabel.Font = Ui.F(13f);
            B(imageLabel, 24, T + 30, 580, 38);
            B(imageButton, 596, T - 2, 140, 32);
            imageButton.Font = Ui.F(13f);
            line1.BackColor = Ui.Hair;
            B(line1, 24, T + 72, 712, 1);
            B(two, 24, T + 82, 560, 28);
            B(usb, 26, T + 114, 300, 24);
            B(inside, 336, T + 114, 400, 24);
            usb.ForeColor = inside.ForeColor = Ui.Text;
            B(listFrame, 24, T + 142, 712, 134);
            listFrame.BackColor = Ui.Hair;
            listFrame.Padding = new Padding(1);
            list.BackColor = Ui.Back;
            list.ItemHeight = Ui.S(66);
            B(missing, 24, T + 286, 300, 22);
            missing.LinkColor = missing.VisitedLinkColor = Ui.Dark ? Ui.WarmText : Color.FromArgb(40, 80, 170);
            missing.ActiveLinkColor = Ui.Text;
            missing.DisabledLinkColor = Ui.TextOff;
            B(refresh, 606, T + 282, 130, 30);
            refresh.Font = Ui.F(13f);

            B(options, 24, T + 318, 712, 194);
            B(stages, 24, T + 320, 712, 150);
            stages.Visible = false;
            R(notice, 0, 0, 712, 60);
            notice.Padding = new Padding(Ui.S(16), Ui.S(8), Ui.S(10), Ui.S(8));
            notice.BackColor = Ui.CalloutBack;
            notice.ForeColor = Ui.Dark ? Ui.Text : Color.FromArgb(70, 50, 10);
            notice.Bar = Ui.CalloutBar;
            R(beside, 2, 0, 330, 24);
            R(whole, 338, 0, 330, 24);
            beside.ForeColor = whole.ForeColor = Ui.Text;
            R(explain, 0, 28, 712, 102);
            explain.BackColor = Ui.Surface;
            explain.ForeColor = Ui.Text;
            explain.Bar = Ui.Hair;
            explain.Padding = new Padding(Ui.S(16), Ui.S(6), Ui.S(8), Ui.S(5));
            R(volumeLabel, 0, 142, 176, 22);
            R(volumeBox, 178, 138, 250, 26);
            R(sizeLabel, 446, 142, 132, 22);
            R(sizeBox, 580, 139, 72, 26);
            R(sizeUnit, 656, 142, 50, 22);
            R(remove, 0, 172, 400, 22);
            volumeLabel.ForeColor = sizeLabel.ForeColor = sizeUnit.ForeColor = Ui.Text;
            volumeBox.BackColor = sizeBox.BackColor = Ui.Field;
            volumeBox.ForeColor = sizeBox.ForeColor = Ui.Text;
            volumeBox.FlatStyle = FlatStyle.Standard;
            remove.LinkColor = remove.VisitedLinkColor = Ui.Dark ? Ui.WarmText : Color.FromArgb(40, 80, 170);
            remove.ActiveLinkColor = Ui.Text;
            remove.DisabledLinkColor = Ui.TextOff;

            B(diagnostics, 26, T + 512, 710, 24);
            diagnostics.ForeColor = Ui.Text;
            B(bar, 24, T + 540, 712, 16);
            B(status, 24, T + 558, 712, 22);
            status.ForeColor = Ui.Soft;

            go.Font = Ui.F(15f, "semi");
            go.ForeColor = Ui.PrimaryText;
            update.Font = go.Font;
            update.ForeColor = Color.White;
            update.BackColor = Ui.Green;
            B(update, 24, T + 586, 372, 46);
            B(go, 24, T + 586, 712, 46); // (UpdateState sets where it is)

            donePanel.SetBounds(Ui.S(X0), 0, Ui.S(760), Ui.S(WindowHeight));
            donePanel.BackColor = Ui.Back;
            if (donePanel.Visible) BuildDone();
            close.Font = Ui.F(13f);
            close.Outline = true;
            close.ForeColor = Ui.Text;
            BackColor = Ui.Back;
            ResumeLayout(true);
            header.Invalidate();
        }

        // The bar at the side: the mark, the name and the three steps.
        void PaintHeader(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            Color text = Ui.InkText, soft = Ui.InkSoft;
            using (var b = new System.Drawing.Drawing2D.LinearGradientBrush(new Rectangle(0, 0, header.Width, header.Height),
                       Ui.Ink, Color.FromArgb(18, 20, 28), System.Drawing.Drawing2D.LinearGradientMode.Vertical))
                g.FillRectangle(b, 0, 0, header.Width, header.Height);
            Ui.DrawMark(g, header.Width / 2f, Ui.Sf(92), Ui.Sf(40), Ui.Disc, true, true);
            string small = "LibreELEC", big = "HADES";
            Font fs = Ui.F(12f, "semi"), fb = Ui.F(36f, "light");
            float ws = MeasureTracked(g, small, fs, 0.42f), wb = MeasureTracked(g, big, fb, 0.34f);
            Ui.DrawTracked(g, small, fs, soft, (header.Width - ws) / 2f, Ui.Sf(160), 0.42f);
            Ui.DrawTracked(g, big, fb, text, (header.Width - wb) / 2f, Ui.Sf(180), 0.34f);
            TextRenderer.DrawText(g, L.T("Installer", "Instalador"), Ui.F(12.5f), new Rectangle(0, Ui.S(236), header.Width, Ui.S(20)), soft,
                                  TextFormatFlags.HorizontalCenter | TextFormatFlags.NoPadding);
            // the steps
            string[] names = { L.T("Image", "Imagen"), Where == Place.Internal ? L.T("Disk", "Disco") : L.T("Drive", "Unidad"), L.T("Install", "Instalar") };
            int y0 = 300, gap = 84;
            for (int i = 0; i < 3; ++i)
            {
                float cx = Ui.Sf(40), cy = Ui.Sf(y0 + i * gap + 14), d = Ui.Sf(26);
                if (i < 2)
                    using (var pen = new Pen(stepState[i] == 2 ? Color.FromArgb(120, 84, 200, 124) : Color.FromArgb(60, 255, 255, 255), Math.Max(1f, Ui.Sf(1.5f))))
                        g.DrawLine(pen, cx, cy + d / 2f + Ui.Sf(5), cx, cy + Ui.Sf(gap) - d / 2f - Ui.Sf(5));
                var box = new RectangleF(cx - d / 2f, cy - d / 2f, d, d);
                if (stepState[i] == 2)
                {
                    using (var b = new SolidBrush(Color.FromArgb(84, 200, 124))) g.FillEllipse(b, box);
                    using (var pen = new Pen(Ui.Ink, Math.Max(1.6f, Ui.Sf(2f))) { StartCap = System.Drawing.Drawing2D.LineCap.Round, EndCap = System.Drawing.Drawing2D.LineCap.Round, LineJoin = System.Drawing.Drawing2D.LineJoin.Round })
                        g.DrawLines(pen, new[] { new PointF(box.Left + d * 0.27f, box.Top + d * 0.53f), new PointF(box.Left + d * 0.44f, box.Top + d * 0.69f), new PointF(box.Left + d * 0.73f, box.Top + d * 0.33f) });
                }
                else
                {
                    if (stepState[i] == 1) using (var b = new SolidBrush(Ui.Silver)) g.FillEllipse(b, box);
                    else using (var pen = new Pen(Color.FromArgb(110, 255, 255, 255), Math.Max(1f, Ui.Sf(1.4f)))) g.DrawEllipse(pen, box);
                    TextRenderer.DrawText(g, (i + 1).ToString(), Ui.F(12.5f, "semi"), Rectangle.Round(box), stepState[i] == 1 ? Ui.Ink : soft,
                                          TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
                }
                TextRenderer.DrawText(g, names[i], Ui.F(15f, "semi"), new Point(Ui.S(66), Ui.S(y0 + i * gap + 3)), stepState[i] == 0 ? soft : text, TextFormatFlags.NoPadding);
                TextRenderer.DrawText(g, stepSub[i], Ui.F(12f), new Rectangle(Ui.S(66), Ui.S(y0 + i * gap + 26), header.Width - Ui.S(82), Ui.S(34)), soft,
                                      TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
            }
        }

        static float MeasureTracked(Graphics g, string s, Font f, float tracking)
        {
            float w = 0;
            using (var fmt = new StringFormat(StringFormat.GenericTypographic))
                foreach (char ch in s) w += g.MeasureString(ch.ToString(), f, 1000, fmt).Width + tracking * f.Size;
            return w - tracking * f.Size;
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Ui.DarkTitleBar(Handle);
            if (!Ui.FixedScale)
            {
                float now = Ui.WindowScale(Handle);
                if (Math.Abs(now - Ui.Scale) > 0.001f) ScaleChanged(now);
            }
            // the icon of the taskbar and of Alt+Tab, at its own size
            bigIcon = Ui.AppIcon(false);
            if (bigIcon != null) SendMessage(Handle, 0x0080, (IntPtr)1, bigIcon.Handle);
        }

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wparam, IntPtr lparam);

        // The texts that do not depend on what is chosen, in the language of now. (UpdateState sets the rest.)
        void SetTexts()
        {
            Text = L.T("LibreELEC Hades · Installer", "LibreELEC Hades · Instalador");
            language.Text = L.Spanish ? "English" : "Español"; // each in its own language
            one.Text = L.T("LibreELEC Hades image", "Imagen de LibreELEC Hades");
            imageButton.Text = L.T("Choose another…", "Elegir otra…");
            two.Text = L.T("Where do you want it?", "¿Dónde lo quieres?");
            usb.Text = L.T("On a USB drive (recommended)", "En una unidad USB (recomendado)");
            inside.Text = Native.Uefi ? L.T("On a secondary internal disk", "En un disco interno secundario")
                                      : L.T("On an internal disk (this PC does not start by UEFI)",
                                            "En un disco interno (este PC no arranca por UEFI)");
            refresh.Text = L.T("Refresh list", "Actualizar lista");
            beside.Text = L.T("Make room without erasing anything", "Hacer sitio sin borrar nada");
            whole.Text = L.T("Use the whole disk (everything is erased)", "Usar el disco entero (se borra todo)");
            volumeLabel.Text = L.T("Partition to shrink:", "Partición que cede el sitio:");
            sizeLabel.Text = L.T("Space for Hades:", "Espacio para Hades:");
            remove.Text = L.T("Remove LibreELEC Hades from an internal disk…", "Quitar LibreELEC Hades de un disco interno…");
            update.Text = L.T("Update LibreELEC Hades  (keeps your settings)", "Actualizar LibreELEC Hades  (conserva tus ajustes)");
            close.Text = L.T("Close", "Cerrar");
            header.Invalidate();
        }

        // The other language: every text of the window again. What was kept as a text is made again.
        public void SwitchLanguage()
        {
            if (busy) return; // (the link is only dimmed then, and does nothing: what is done to a disk says all its steps in one language)
            L.Spanish = !L.Spanish;
            ++wordings;
            SetTexts();
            // why each disk that is not offered is not
            Keep keep = Keeping();
            List<string> guarded = Disks.Protected();
            foreach (DiskInfo d in excluded)
                d.Excluded = Disks.Classify(d, Where, plan == null ? 0 : plan.Size, keep, guarded) ?? d.Excluded;
            if (donePanel.Visible)
            {
                BuildDone();
                return; // nothing else is on show, and nothing else will be
            }
            list.Invalidate();
            // the volumes: their names in the list; and why one can give nothing is asked again
            var choice = volumeBox.SelectedItem as VolumeChoice;
            rewording = true;
            for (int i = 0; i < volumeBox.Items.Count; ++i)
            {
                var c = (VolumeChoice)volumeBox.Items[i];
                if (c.Problem != null)
                {
                    c.Problem = null;
                    c.Spare = -1;
                }
                volumeBox.Items[i] = c;
            }
            rewording = false;
            // what the engine said of an image that cannot be used: it looks at the image again
            if (imageState == "unusable")
            {
                SetImage(imagePath);
                DiskChanged();
            }
            else if (choice != null && choice.Spare < 0) VolumeChanged();
            else UpdateState();
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == 0x02E0 && !Ui.FixedScale) // WM_DPICHANGED: the window is now on a display of another scale
            {
                float now = (m.WParam.ToInt64() & 0xFFFF) / 96f;
                if (Math.Abs(now - Ui.Scale) > 0.001f)
                {
                    ScaleChanged(now);
                    // (where Windows suggests that it goes: its left and top)
                    int left = System.Runtime.InteropServices.Marshal.ReadInt32(m.LParam, 0);
                    int top = System.Runtime.InteropServices.Marshal.ReadInt32(m.LParam, 4);
                    Location = new Point(left, top);
                }
                m.Result = IntPtr.Zero;
                return;
            }
            base.WndProc(ref m);
            if (m.Msg == 0x0219 && !busy) // a device came or went
            {
                deviceTimer.Stop();
                deviceTimer.Start();
            }
        }

        // A device came or went (WM_DEVICECHANGE, a moment later: deviceTimer): the list of disks again. Not while a disk is
        // worked on; and while the panel of "done" is on show, only once it is gone (see done).
        void DevicesChanged()
        {
            if (busy) return;
            if (done) { doneSkippedDevices = true; return; }
            LoadDisks();
        }

        // The window on a display of another scale (WM_DPICHANGED, or a handle made on one): everything is laid out again,
        // the panel of "done" too (LayoutAll makes it again); what is behind that panel waits until it is gone.
        void ScaleChanged(float now)
        {
            Ui.Scale = now;
            LayoutAll();
            if (done) doneSkippedState = true;
            else UpdateState();
            list.Invalidate();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (busy)
            {
                MessageBox.Show(this, L.T("The disk is being worked on. Wait until it finishes: if it is interrupted, the disk is left half done.",
                                          "Se está trabajando en el disco. Espera a que termine: si se interrumpe, queda a medias."),
                                Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                e.Cancel = true;
            }
            base.OnFormClosing(e);
        }

        public static string FindImage(string directory)
        {
            string best = null;
            foreach (string pattern in new[] { "*.img.gz", "*.img" })
            {
                foreach (string f in Directory.GetFiles(directory, pattern))
                    if (best == null || File.GetLastWriteTimeUtc(f) > File.GetLastWriteTimeUtc(best)) best = f;
                if (best != null) break;
            }
            return best;
        }

        void ChooseImage()
        {
            using (var dialog = new OpenFileDialog())
            {
                dialog.Title = L.T("LibreELEC Hades image", "Imagen de LibreELEC Hades");
                dialog.Filter = dialog.Title + " (*.img.gz; *.img)|*.img.gz;*.img";
                if (imagePath != null) dialog.InitialDirectory = Path.GetDirectoryName(imagePath);
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                SetImage(dialog.FileName);
                LoadDisks();
            }
        }

        void SetImage(string path)
        {
            imagePath = path;
            plan = null;
            imageState = "";
            imageDetail = null;
            if (path == null)
            {
                imageState = "none";
                UpdateState();
                return;
            }
            try { plan = Engine.Plan(path, false, null, null); }
            catch (Exception e) { imageState = "unusable"; imageDetail = e.Message; UpdateState(); return; }
            string expected = Engine.ExpectedHash(path);
            if (expected == null)
            {
                imageState = "unchecked";
                UpdateState();
                if (Where == Place.Internal) Scan();
                return;
            }
            imageState = "checking";
            UpdateState();
            var check = new BackgroundWorker();
            check.DoWork += delegate(object s, DoWorkEventArgs e) { e.Result = Engine.FileHash(path, null, null); };
            check.RunWorkerCompleted += delegate(object s, RunWorkerCompletedEventArgs e)
            {
                if (path != imagePath) return;
                imageState = e.Error != null ? "unreadable" : (string)e.Result == expected ? "ok" : "damaged";
                imageDetail = e.Error != null ? e.Error.Message : null;
                Log.Line("image " + path + ": " + imageState + (imageDetail != null ? ": " + imageDetail : ""));
                UpdateState();
                if (Where == Place.Internal) Scan();
            };
            check.RunWorkerAsync();
        }

        // An install on an internal disk changes the boot menus of the image: where they are is found by
        // reading the image through once.
        void Scan()
        {
            if (plan == null || plan.Scanned || scanning) return;
            string path = imagePath;
            int wording = wordings;
            scanning = true;
            UpdateState();
            var scan = new BackgroundWorker();
            scan.DoWork += delegate(object s, DoWorkEventArgs e) { e.Result = Engine.Plan(path, true, null, null); };
            scan.RunWorkerCompleted += delegate(object s, RunWorkerCompletedEventArgs e)
            {
                scanning = false;
                if (path != imagePath) return;
                if (e.Error != null && wording != wordings && plan != null)
                {
                    Scan(); // the language changed meanwhile, and what went wrong is said in the one of before: once more
                    return;
                }
                if (e.Error != null)
                {
                    imageState = "unusable";
                    imageDetail = e.Error.Message;
                }
                else plan = (ImagePlan)e.Result;
                UpdateState();
            };
            scan.RunWorkerAsync();
        }

        void PlaceChanged()
        {
            if (Where == Place.Internal) Scan();
            LoadDisks();
        }

        // What must not be erased, with the image that is chosen now. (The made-up disks of the tests are not
        // those of this PC: what is kept on them is made up with them.)
        Keep Keeping()
        {
            return Test != null ? Test.Keep : Disks.Keeping(imagePath);
        }

        void LoadDisks()
        {
            ++disksLoaded;
            var selected = list.SelectedItem as DiskInfo;
            list.BeginUpdate();
            list.Items.Clear();
            excluded = new List<DiskInfo>();
            try
            {
                Keep keep = Keeping();
                List<string> guarded = Disks.Protected();
                foreach (DiskInfo d in Test != null ? Test.Disks : Disks.List())
                {
                    if (d.Size <= 0) continue; // a card reader with no card in it
                    d.Excluded = Disks.Classify(d, Where, plan == null ? 0 : plan.Size, keep, guarded);
                    d.Kept = Disks.Kept(d, keep); // (an internal disk that is offered may be one that is never erased)
                    if (d.Excluded == null)
                    {
                        PartitionInfo a, b;
                        if (Test == null && Pc.Elevated) d.HasHades = Disks.FindHades(d, out a, out b);
                        list.Items.Add(d);
                    }
                    else excluded.Add(d);
                }
            }
            catch (Exception e)
            {
                Log.Line("disks: " + e);
                status.Text = L.T("The disks of this PC could not be read: ", "No se pudieron leer los discos de este PC: ") + e.Message;
            }
            for (int i = 0; i < list.Items.Count; ++i)
                if (((DiskInfo)list.Items[i]).SameAs(selected)) list.SelectedIndex = i;
            if (list.SelectedIndex < 0 && list.Items.Count == 1 && Where == Place.Usb) list.SelectedIndex = 0;
            list.EndUpdate();
            DiskChanged();
        }

        void DiskChanged()
        {
            volumeBox.Items.Clear();
            var disk = list.SelectedItem as DiskInfo;
            if (disk != null && disk.HasHades) Scan(); // an update changes the boot menus of the image
            // Whether the Hades of that disk has its diagnostics on: until the user sets the box, it says that,
            // so that an update keeps them (UpdateState, which also knows whether the image can turn them on).
            // Read each time a disk is chosen and each time the list is made again.
            diskDiagnostics = false;
            if (disk != null && disk.HasHades)
            {
                PartitionInfo system, storage;
                if (Test != null) diskDiagnostics = Test.Diagnostics.Contains(disk);
                else if (Disks.FindHades(disk, out system, out storage)) diskDiagnostics = Disks.DiagnosticsOn(disk.Number, system);
            }
            if (disk != null && Where == Place.Internal)
            {
                // Any NTFS volume of the disk can give room, also the one that this program, the image or
                // the files of the user are on: Windows shrinks it with what it holds, and nothing is erased.
                foreach (PartitionInfo p in disk.Partitions)
                {
                    if (p.Letter == '\0') continue;
                    VolumeInfo v = null;
                    if (Test != null)
                        foreach (VolumeInfo given in disk.Volumes) if (given.Letter == p.Letter) v = given;
                    if (v == null) v = Disks.Volume(p.Letter);
                    if (!v.Ready || v.FileSystem != "NTFS") continue;
                    var choice = new VolumeChoice();
                    choice.Partition = p;
                    choice.Volume = v;
                    volumeBox.Items.Add(choice);
                }
                // the one with most free room first
                int best = -1;
                for (int i = 0; i < volumeBox.Items.Count; ++i)
                {
                    var c = (VolumeChoice)volumeBox.Items[i];
                    if (best < 0 || c.Volume.Size - c.Volume.Used > ((VolumeChoice)volumeBox.Items[best]).Volume.Size - ((VolumeChoice)volumeBox.Items[best]).Volume.Used)
                        best = i;
                }
                if (best >= 0) volumeBox.SelectedIndex = best;
            }
            UpdateState();
        }

        void VolumeChanged()
        {
            if (rewording) return;
            var disk = list.SelectedItem as DiskInfo;
            var choice = volumeBox.SelectedItem as VolumeChoice;
            if (disk != null && choice != null && choice.Spare < 0 && plan != null)
            {
                Cursor = Cursors.WaitCursor;
                try
                {
                    char letter = choice.Partition.Letter;
                    if (Test != null) choice.Spare = Test.Spare;
                    // (a disk that holds what must be kept cannot be used whole: it is not told to be)
                    else if (disk.PartitionStyle != 2)
                    {
                        choice.Spare = 0;
                        choice.Problem = disk.Kept == null
                            ? L.T("That disk does not use GPT: it can only be used whole.",
                                  "Ese disco no usa GPT: solo se puede usar entero.")
                            : L.T("That disk does not use GPT: no room can be made on it, and it cannot be used whole.",
                                  "Ese disco no usa GPT: no se puede hacer sitio en él, y tampoco usarlo entero.");
                    }
                    else if (Disks.Encrypted(letter) == 1)
                    {
                        choice.Spare = 0;
                        choice.Problem = letter + L.T(": is encrypted with BitLocker: decrypt it first", ": está cifrada con BitLocker: descífrala antes") +
                                         (disk.Kept == null ? L.T(", or use the whole disk.", ", o usa el disco entero.") : ".");
                    }
                    else
                    {
                        long least;
                        choice.Spare = Operations.Spare(disk.Number, choice.Partition, plan, out least);
                        if (choice.Spare == 0)
                        {
                            string needed = Words.Size(plan.System.Size + Operations.StorageLeast);
                            choice.Problem = letter + L.T(
                                ": cannot give up enough room (at least " + needed + " must be free, and Windows must be able to move it).",
                                ": no puede ceder sitio suficiente (hacen falta al menos " + needed + " libres y que Windows pueda moverlos).");
                        }
                    }
                }
                catch (Exception e)
                {
                    Log.Line("spare: " + e);
                    choice.Spare = 0;
                    choice.Problem = L.T("Could not work out how much " + choice.Partition.Letter + ": can give up (" + e.Message + ")",
                                         "No se pudo calcular cuánto puede ceder " + choice.Partition.Letter + ": (" + e.Message + ")");
                }
                Cursor = Cursors.Default;
            }
            if (choice != null && choice.Spare > 0 && plan != null)
            {
                decimal most = Math.Floor((decimal)(choice.Spare - plan.System.Size) / 1073741824m);
                sizeBox.Minimum = 8;
                sizeBox.Maximum = Math.Max(8, most);
                sizeBox.Value = Math.Min(sizeBox.Maximum, 64);
            }
            UpdateState();
        }

        // The serial number is part of what says which disk this is: it is on every row, and in what is asked before it is erased.
        public static string SerialLine(DiskInfo d)
        {
            return d.Serial.Length > 0 ? L.T("Serial number: ", "N.º de serie: ") + d.Serial
                                       : L.T("Serial number: not reported by this drive", "N.º de serie: este disco no lo da");
        }

        void DrawDisk(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0) return;
            var d = (DiskInfo)list.Items[e.Index];
            bool selected = (e.State & DrawItemState.Selected) != 0;
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            Rectangle r = e.Bounds;
            Color back = selected ? Ui.Pick : Ui.Back;
            Color main = selected ? Ui.PickText : Ui.Text;
            Color soft = selected ? Ui.Blend(Ui.PickText, Ui.Pick, 0.22f) : Ui.Soft;
            Color faint = selected ? Ui.Blend(Ui.PickText, Ui.Pick, 0.42f) : Ui.Faint;
            using (var b = new SolidBrush(back)) g.FillRectangle(b, r);
            if (selected)
                using (var b = new SolidBrush(Ui.FirstLight)) g.FillRectangle(b, r.Left, r.Top, Math.Max(3, Ui.S(4)), r.Height);
            else
                using (var pen = new Pen(Ui.Hair)) g.DrawLine(pen, r.Left + Ui.S(16), r.Bottom - 1, r.Right - Ui.S(16), r.Bottom - 1);
            // a small drawing of the drive: a USB stick, or a disk inside the PC
            float x = r.Left + Ui.Sf(20), y = r.Top + r.Height / 2f;
            using (var pen = new Pen(selected ? main : Ui.Faint, Math.Max(1.5f, Ui.Sf(2f))) { LineJoin = System.Drawing.Drawing2D.LineJoin.Round })
            {
                if (d.Removable)
                {
                    using (var p = Ui.Round(new RectangleF(x, y - Ui.Sf(9), Ui.Sf(26), Ui.Sf(18)), Ui.Sf(3))) g.DrawPath(pen, p);
                    g.DrawRectangle(pen, x + Ui.Sf(26), y - Ui.Sf(5), Ui.Sf(9), Ui.Sf(10));
                }
                else
                {
                    using (var p = Ui.Round(new RectangleF(x, y - Ui.Sf(12), Ui.Sf(34), Ui.Sf(24)), Ui.Sf(4))) g.DrawPath(pen, p);
                    g.DrawLine(pen, x + Ui.Sf(6), y + Ui.Sf(5), x + Ui.Sf(16), y + Ui.Sf(5));
                    using (var b = new SolidBrush(selected ? Ui.FirstLight : Ui.Faint)) g.FillEllipse(b, x + Ui.Sf(26), y + Ui.Sf(3), Ui.Sf(4), Ui.Sf(4));
                }
            }
            int tx = r.Left + Ui.S(72), tw = r.Width - Ui.S(72) - Ui.S(14);
            const TextFormatFlags flags = TextFormatFlags.Left | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine;
            TextRenderer.DrawText(g, d.Title, Ui.F(15f, "semi"), new Rectangle(tx, r.Top + Ui.S(8), tw, Ui.S(22)), main, flags);
            TextRenderer.DrawText(g, d.Detail, Ui.F(13f), new Rectangle(tx, r.Top + Ui.S(29), tw, Ui.S(18)), soft, flags);
            TextRenderer.DrawText(g, SerialLine(d), Ui.F(12f), new Rectangle(tx, r.Top + Ui.S(47), tw, Ui.S(16)), faint, flags);
            if ((e.State & DrawItemState.Focus) != 0 && selected)
                using (var pen = new Pen(Color.FromArgb(120, main)) { DashStyle = System.Drawing.Drawing2D.DashStyle.Dot })
                    g.DrawRectangle(pen, r.Left + 2, r.Top + 2, r.Width - 5, r.Height - 5);
        }

        void ShowExcluded()
        {
            var sb = new StringBuilder();
            if (Where == Place.Usb)
            {
                sb.AppendLine(L.T("The drives offered here are the ones connected by USB (sticks, external disks, cards) that the image fits on.",
                                  "Aquí se ofrecen las unidades conectadas por USB (memorias, discos externos, tarjetas) en las que cabe la imagen."));
                sb.AppendLine(L.T("If you have just plugged yours in, wait a few seconds and press “Refresh list”.",
                                  "Si acabas de conectar la tuya, espera unos segundos y pulsa «Actualizar lista»."));
            }
            else
                sb.AppendLine(L.T("The disks offered here are the internal ones other than the Windows disk. The disk that Windows starts " +
                                  "from is never offered. The disk that this program, the image or your user files are on is offered " +
                                  "only for what erases nothing of it: to make room on it, and to update or remove the Hades that it " +
                                  "has. It can never be used whole, which would erase them.",
                                  "Aquí se ofrecen los discos internos que no son el de Windows. El disco desde el que arranca Windows no " +
                                  "se ofrece nunca. El disco donde están este programa, la imagen o tus archivos de usuario se ofrece " +
                                  "solo para lo que no borra nada de él: hacer sitio, y actualizar o quitar el Hades que tenga. Nunca " +
                                  "se puede usar entero, porque eso los borraría."));
            if (excluded.Count > 0)
            {
                sb.AppendLine().AppendLine(L.T("Disks of this PC that are not offered:", "Discos de este PC que no se ofrecen:"));
                foreach (DiskInfo d in excluded)
                    sb.AppendLine().AppendLine("•  " + d.Title)
                      .AppendLine(L.T("    It is not offered because ", "    No se ofrece porque ") + d.Excluded + ".");
            }
            sb.AppendLine().AppendLine(L.T("To keep a disk from ever being offered on this PC, write its serial number in",
                                           "Para que un disco no se ofrezca nunca en este PC, escribe su número de serie en"));
            sb.Append(@"C:\ProgramData\Hades\discos-protegidos.txt" + L.T(" (one per line).", " (uno por línea)."));
            MessageBox.Show(this, sb.ToString(), L.T("Disks", "Discos"), MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        void UpdateState()
        {
            string name = imagePath == null ? "" : Path.GetFileName(imagePath) + "\r\n";
            Color color = Ui.Text;
            bool usable = false;
            switch (imageState)
            {
                case "ok":
                    imageLabel.Text = name + Words.Size(plan.Size) + L.T("  ·  checked: it arrived complete", "  ·  comprobada: ha llegado entera");
                    color = Ui.Ok;
                    usable = true;
                    break;
                case "checking":
                    imageLabel.Text = name + L.T("Checking that it arrived complete…", "Comprobando que ha llegado entera…");
                    break;
                case "unchecked":
                    imageLabel.Text = name + Words.Size(plan.Size) +
                                      L.T("  ·  its .sha256 file is missing: there is no way to check that it arrived complete",
                                          "  ·  sin su archivo .sha256: no se puede comprobar que llegó entera");
                    color = Ui.Warn;
                    usable = true;
                    break;
                default:
                    imageLabel.Text = name + ImageTrouble();
                    color = Ui.Danger;
                    break;
            }
            imageLabel.ForeColor = color;

            var disk = list.SelectedItem as DiskInfo;
            var choice = volumeBox.SelectedItem as VolumeChoice;
            bool internalPlace = Where == Place.Internal;
            bool besideMode = internalPlace && beside.Checked;
            options.Visible = !(busy && showSteps);
            stages.Visible = busy && showSteps;
            notice.Visible = !internalPlace;
            beside.Visible = whole.Visible = explain.Visible = internalPlace;
            volumeLabel.Visible = volumeBox.Visible = sizeLabel.Visible = sizeBox.Visible = sizeUnit.Visible = besideMode && disk != null;
            remove.Visible = internalPlace;
            explain.Text = beside.Checked ? BesideText(plan) : WholeText(plan);
            missing.Text = internalPlace ? L.T("Don't see your disk?", "¿No aparece tu disco?")
                                         : L.T("Don't see your drive?", "¿No aparece tu unidad?");

            if (Pc.SecureBoot == 1)
                notice.Text = L.T("This PC has Secure Boot turned on. Hades does not start with Secure Boot yet: to try it here you will " +
                                  "have to turn it off in the BIOS, and turn it back on for the games that require it.",
                                  "Este PC tiene Secure Boot activado. Hades todavía no arranca con Secure Boot: para probarlo aquí " +
                                  "habrá que desactivarlo en la BIOS, y volver a activarlo para los juegos que lo exigen.");
            else if (disk != null && disk.HasHades)
                notice.Text = L.T("This drive already has Hades. “Update” replaces only the system and keeps your settings and add-ons. " +
                                  "“Install from scratch” erases the whole drive, with everything Hades has stored on it.",
                                  "Esta unidad ya tiene Hades. «Actualizar» cambia solo el sistema y conserva tus ajustes y addons. " +
                                  "«Instalar de cero» la borra entera, con lo que Hades tenga guardado.");
            else
                notice.Text = L.T("The whole drive is erased and is left for Hades alone. Windows and the disks of this PC are not touched. " +
                                  "It is the safest way to try it, and the drive can be taken to another PC.",
                                  "La unidad se borra entera y queda solo para Hades. Windows y los discos de este PC no se tocan. " +
                                  "Es la forma más segura de probarlo, y se puede llevar a otro PC.");

            string problem = null;
            if (busy) problem = null;
            else if (!usable) problem = "";
            else if (list.Items.Count == 0)
                problem = Disks.NothingToOffer(excluded, Where);
            else if (disk == null)
                problem = internalPlace ? L.T("Choose a disk from the list.", "Elige un disco de la lista.")
                                        : L.T("Choose a drive from the list.", "Elige una unidad de la lista.");
            else if (internalPlace && scanning)
                problem = L.T("Reading the image to prepare it for an internal disk…", "Leyendo la imagen para prepararla para un disco interno…");
            else if (internalPlace && (plan == null || !plan.Scanned)) problem = "";
            // (each of these is one line of the window: it has to fit)
            else if (internalPlace && !besideMode && disk.Kept != null)
            {
                string at = disk.Kept.Length > 0 ? " (" + disk.Kept + ")" : "";
                problem = L.T("That disk cannot be used whole: it holds this program, the image or your files" + at +
                              ". Make room on it instead.",
                              "Ese disco no se puede usar entero: tiene este programa, la imagen o tus archivos" + at +
                              ". Haz sitio en él.");
            }
            else if (besideMode && disk.HasHades) problem = Operations.AlreadyThere;
            // (a disk that holds what must be kept is not told to be used whole, which it cannot be)
            else if (besideMode && !plan.StorageAnywhere)
                problem = disk.Kept == null
                    ? L.T("This Hades image is older than this option: with it, only the whole disk can be used.",
                          "Esta imagen de Hades es anterior a esta opción: con ella solo se puede usar el disco entero.")
                    : L.T("This Hades image is older than this option, and that disk cannot be used whole: use another disk.",
                          "Esta imagen de Hades es anterior a esta opción, y ese disco no se puede usar entero: usa otro disco.");
            else if (besideMode && choice == null)
                problem = disk.Kept == null
                    ? L.T("That disk has no NTFS partition with a drive letter to take space from: it can only be used whole.",
                          "Ese disco no tiene ninguna partición NTFS con letra de la que sacar sitio: solo se puede usar entero.")
                    : L.T("That disk has no NTFS partition with a drive letter to take space from, and it cannot be used whole.",
                          "Ese disco no tiene ninguna partición NTFS con letra de la que sacar sitio, y no se puede usar entero.");
            else if (besideMode && choice.Spare <= 0) problem = choice.Problem ?? "";
            if (!busy && !donePanel.Visible) status.Text = problem ?? "";
            if (besideMode && choice != null && choice.Spare > 0 && problem == null && !busy)
                status.Text = choice.Partition.Letter +
                              L.T(": can give up to " + Words.Size(choice.Spare) + ". Hades will use " + sizeBox.Value +
                                  " GB for your data and 1 GB for the system.",
                                  ": puede ceder hasta " + Words.Size(choice.Spare) + ". Hades usará " + sizeBox.Value +
                                  " GB para tus datos y 1 GB para el sistema.");

            // a disk that already has Hades: updating it comes first, and installing anew is the other button
            bool has = disk != null && disk.HasHades;
            update.Visible = has;
            update.Enabled = !busy && has && usable && plan != null && plan.Scanned;
            update.BackColor = Ui.Green;
            B(go, has ? 404 : 24, Top0 + 586, has ? 332 : 712, 46);
            go.Text = has ? (!internalPlace ? L.T("Install from scratch  (erases the drive)", "Instalar de cero  (borra la unidad)")
                             : besideMode ? L.T("Make room and install", "Hacer sitio e instalar")
                             : L.T("Erase the disk and install from scratch", "Borrar el disco e instalar de cero"))
                      : !internalPlace ? L.T("Install LibreELEC Hades on the chosen drive", "Instalar LibreELEC Hades en la unidad elegida")
                      : besideMode ? L.T("Make room and install LibreELEC Hades on the chosen disk",
                                         "Hacer sitio e instalar LibreELEC Hades en el disco elegido")
                      : L.T("Erase the chosen disk and install LibreELEC Hades", "Borrar el disco elegido e instalar LibreELEC Hades");
            go.Enabled = !busy && problem == null;
            go.BackColor = Ui.Primary;
            // (with an image that cannot be used, what is wrong with it is said next to the image)
            if (has && usable && !busy && !donePanel.Visible && (problem == null || problem.Length == 0 || besideMode))
                status.Text = scanning || plan == null || !plan.Scanned
                    ? L.T("Reading the image…", "Leyendo la imagen…")
                    : L.T("This drive already has Hades. Updating replaces the system and keeps your settings and add-ons.",
                          "Esta unidad ya tiene Hades. Actualizar cambia el sistema y conserva tus ajustes y addons.");
            imageButton.Enabled = refresh.Enabled = list.Enabled = usb.Enabled = !busy;
            // (the two links stay enabled while a disk is worked on, only dimmed: Windows would draw them in a grey that cannot be
            // read; what they do is refused while busy, SwitchLanguage and StartRemove)
            language.LinkColor = language.VisitedLinkColor = busy ? Ui.TextOffOnInk : Ui.InkSoft;
            remove.LinkColor = remove.VisitedLinkColor = busy ? Ui.TextOff : (Ui.Dark ? Ui.WarmText : Color.FromArgb(40, 80, 170));
            // The box of the diagnostics. An image from before them has no switch to turn on: the box is off
            // and says why. One that has them on already writes them on whatever is asked: the box is on,
            // cannot be changed, and says so. Else it is what the user set it to, and until then what the
            // chosen disk has.
            bool switchable = plan != null && plan.SwitchOffset >= 0;
            bool always = plan != null && plan.SwitchOn;
            if (!busy) diagnostics.Checked = always || (switchable && (diagnosticsAsked ?? diskDiagnostics));
            diagnostics.Enabled = !busy && switchable;
            diagnostics.Text = always
                ? L.T("Save diagnostics there (this image always does)", "Guardar diagnóstico ahí (esta imagen lo hace siempre)")
                : switchable || plan == null
                ? L.T("Save diagnostics there, to find out why something does not work (see README.txt)",
                      "Guardar diagnóstico ahí, para averiguar por qué algo no funciona (mira LEEME.txt)")
                : L.T("Save diagnostics there (this image cannot: a more recent one is needed)",
                      "Guardar diagnóstico ahí (esta imagen no sabe: hace falta una más reciente)");
            inside.Enabled = !busy && Native.Uefi;
            beside.Enabled = whole.Enabled = volumeBox.Enabled = sizeBox.Enabled = !busy;

            // where the user is in the three steps: the headings and the bar at the side say it
            bool ready = usable && disk != null && problem == null;
            stepState[0] = usable ? 2 : 1;
            stepState[1] = !usable ? 0 : ready ? 2 : 1;
            stepState[2] = busy ? 1 : ready ? 1 : 0;
            if (donePanel.Visible) stepState[0] = stepState[1] = stepState[2] = 2;
            stepSub[0] = imagePath == null ? "" : Path.GetFileName(imagePath);
            stepSub[1] = disk == null ? (internalPlace ? L.T("Choose a disk", "Elige un disco") : L.T("Choose a drive", "Elige una unidad")) : disk.Title;
            stepSub[2] = donePanel.Visible ? L.T("Done", "Hecho") : busy ? L.T("Working…", "Trabajando…")
                         : ready ? L.T("Ready", "Listo") : "";
            one.SetState(stepState[0]);
            two.SetState(stepState[1]);
            header.Invalidate();
        }

        // Why the image cannot be used, when it cannot.
        string ImageTrouble()
        {
            switch (imageState)
            {
                case "none":
                    return L.T("There is no Hades image (.img.gz) next to this program. Choose one.",
                               "No hay ninguna imagen de Hades (.img.gz) junto a este programa. Elige una.");
                case "damaged":
                    return L.T("The image is damaged or incomplete (its checksum does not match). Copy it again.",
                               "La imagen está dañada o incompleta (su huella no coincide). Vuelve a copiarla.");
                case "unreadable":
                    return L.T("The image cannot be read: ", "No se puede leer la imagen: ") + imageDetail;
                case "unusable":
                    return L.T("This image cannot be used: ", "No se puede usar esa imagen: ") + imageDetail;
                default:
                    return "";
            }
        }

        // failed: asked when the work ends with an error; true when it has dealt with it (nothing more is
        // said then). null: every error is shown.
        void Run(DoWorkEventHandler work, Action<object> done, Func<Exception, bool> failed = null)
        {
            busy = true;
            stages.Permille = 0;
            SetStages(removing);
            UpdateState();
            clock.Restart();
            var worker = new BackgroundWorker();
            worker.WorkerReportsProgress = true;
            worker.ProgressChanged += delegate(object s, ProgressChangedEventArgs e)
            {
                bar.Value = Math.Max(0, Math.Min(1000, e.ProgressPercentage));
                stages.Permille = bar.Value;
                stages.Invalidate();
                status.Text = (string)e.UserState;
            };
            worker.DoWork += work;
            worker.RunWorkerCompleted += delegate(object s, RunWorkerCompletedEventArgs e)
            {
                busy = false;
                showSteps = false;
                removing = false;
                if (e.Error != null)
                {
                    Log.Line("failed: " + e.Error);
                    bar.Value = 0;
                    UpdateState();
                    status.Text = L.T("It was not completed.", "No se completó.");
                    if (failed != null && failed(e.Error)) return;
                    MessageBox.Show(this, e.Error.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    LoadDisks();
                    return;
                }
                Log.Line("done in " + clock.Elapsed.TotalSeconds.ToString("0") + " s");
                done(e.Result);
            };
            current = worker;
            worker.RunWorkerAsync();
        }

        bool removing;           // what is done is removing Hades: its two stages, not those of writing an image

        // The stages that are on show while a disk is worked on, and where in the progress each of them starts (the
        // operations report writing as the first part of the bar, and reading it back as the rest).
        void SetStages(bool removal)
        {
            if (removal)
            {
                stages.Stages = new[] { L.T("Deleting the Hades partitions", "Borrando las particiones de Hades"),
                                        L.T("Giving the space back, or leaving the disk ready for Windows", "Devolviendo el espacio, o dejando el disco listo para Windows") };
                stages.Cuts = new[] { 600 };
            }
            else
            {
                stages.Stages = new[] { L.T("Preparing the drive", "Preparando la unidad"),
                                        L.T("Checking the image and writing it", "Comprobando la imagen y escribiéndola"),
                                        L.T("Reading it back and comparing it with the image", "Releyéndolo y comparándolo con la imagen") };
                stages.Cuts = new[] { 50, 600 };
            }
            stages.Warning = L.T("Do not unplug the drive or turn the PC off.", "No desconectes la unidad ni apagues el PC.");
        }

        BackgroundWorker current;

        void Report(int permille, string text)
        {
            if (current != null) current.ReportProgress(permille, text);
        }

        void Start()
        {
            var disk = list.SelectedItem as DiskInfo;
            if (disk == null || busy || plan == null) return;
            Place place = Where;
            bool besideMode = place == Place.Internal && beside.Checked;
            var choice = volumeBox.SelectedItem as VolumeChoice;
            string image = imagePath;
            ImagePlan chosen = plan;
            bool diagnose = diagnostics.Checked && chosen.SwitchOffset >= 0;

            if (besideMode)
            {
                if (choice == null || choice.Spare <= 0) return;
                long storage = (long)sizeBox.Value * 1073741824L;
                using (ConfirmForm confirm = AskBeside(disk, choice.Partition.Letter, storage + chosen.System.Size))
                    if (confirm.ShowDialog(this) != DialogResult.OK) return;
                Log.Line("start beside: disk " + disk.Number + " " + disk.Name + ", volume " + choice.Partition.Letter + ", storage " + storage);
                showSteps = true;
                Run(delegate { Operations.InstallBeside(disk, choice.Partition, storage, chosen, diagnose, Report); },
                    delegate { ShowDone(disk, place, false); });
                return;
            }

            // (the button is off for such a disk, and Operations.InstallWhole looks at the disk again and refuses)
            if (disk.Kept != null) return;
            using (ConfirmForm confirm = AskErase(disk))
                if (confirm.ShowDialog(this) != DialogResult.OK) return;
            Log.Line("start whole " + place + ": disk " + disk.Number + " " + disk.Name + " <- " + image);
            showSteps = true;
            Run(delegate { Operations.InstallWhole(disk, chosen, place, diagnose, Report); }, delegate { ShowDone(disk, place, false); });
        }

        void StartUpdate()
        {
            var disk = list.SelectedItem as DiskInfo;
            if (disk == null || busy || plan == null || !plan.Scanned || !disk.HasHades) return;
            Place place = Where;
            ImagePlan chosen = plan;
            bool diagnose = diagnostics.Checked && chosen.SwitchOffset >= 0;
            using (ConfirmForm confirm = AskUpdate(disk, Path.GetFileName(imagePath), place == Place.Internal && chosen.HidesOtherDisks))
                if (confirm.ShowDialog(this) != DialogResult.OK) return;
            RunUpdate(disk, chosen, place, diagnose, imagePath, false);
        }

        // take: the system partition is taken from the programs that have it open. Never the first time: a
        // partition that is in use is left as it was, and the user is asked (AskTake). Either they close
        // what has it open and update again, or they have it taken, and the update is done once more.
        void RunUpdate(DiskInfo disk, ImagePlan chosen, Place place, bool diagnose, string image, bool take)
        {
            Log.Line("start update " + place + (take ? ", taking the partition" : "") + ": disk " + disk.Number + " " + disk.Name +
                     " <- " + image);
            showSteps = true;
            Run(delegate(object s, DoWorkEventArgs e) { e.Result = Operations.Update(disk, chosen, place, diagnose, Report, take); },
                delegate(object result) { ShowDone(disk, place, true, result as DiagKeep); },
                delegate(Exception error)
                {
                    if (take || !(error is PartitionInUseException)) return false;
                    PartitionInfo system, storage;
                    char letter = Disks.FindHades(disk, out system, out storage) ? system.Letter : '\0';
                    bool wsl = Wsl.Running;
                    bool taken;
                    using (ConfirmForm ask = AskTake(disk, letter, wsl))
                        taken = ask.ShowDialog(this) == DialogResult.OK;
                    Log.Line("the partition is in use (WSL running: " + wsl + "): the user " +
                             (taken ? "has it taken" : "leaves it as it is"));
                    if (taken) RunUpdate(disk, chosen, place, diagnose, image, true);
                    else LoadDisks();
                    return true;
                });
        }

        void StartRemove()
        {
            var disk = list.SelectedItem as DiskInfo;
            if (busy) return;
            if (disk == null || !disk.HasHades)
            {
                MessageBox.Show(this, L.T(
                    "In the list, choose the internal disk that has Hades (it says so under its name: “has LibreELEC Hades”).\r\n\r\n" +
                    "Removing it deletes the Hades partitions and gives their space back to the partition that it came from. " +
                    "If the whole disk was for Hades, it is left empty and ready for Windows.",
                    "Elige en la lista el disco interno que tiene Hades (lo dice bajo su nombre: «tiene LibreELEC Hades»).\r\n\r\n" +
                    "Quitarlo borra las particiones de Hades y devuelve su espacio a la partición de la que salió. " +
                    "Si el disco entero era de Hades, queda vacío y listo para Windows."),
                                L.T("Remove LibreELEC Hades", "Quitar LibreELEC Hades"), MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            using (ConfirmForm confirm = AskRemove(disk))
                if (confirm.ShowDialog(this) != DialogResult.OK) return;
            Log.Line("start uninstall: disk " + disk.Number + " " + disk.Name);
            string image = imagePath; // (the image that is chosen is not erased either)
            showSteps = removing = true;
            Run(delegate(object s, DoWorkEventArgs e) { e.Result = Operations.Uninstall(disk, image, Report); },
                delegate(object result)
                {
                    bar.Value = 0;
                    UpdateState();
                    status.Text = (string)result;
                    MessageBox.Show(this, (string)result, Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                    LoadDisks();
                });
        }

        // The questions before a disk is changed. (Made here, so that the screenshots of the tests show them as they are asked.)

        public static ConfirmForm AskErase(DiskInfo disk)
        {
            var sb = new StringBuilder();
            sb.AppendLine(disk.Title).AppendLine(disk.Detail).AppendLine(SerialLine(disk));
            if (disk.UsedBytes > 0)
                sb.AppendLine().AppendLine(L.T("It holds " + Words.Size(disk.UsedBytes) + " of files, which will be lost.",
                                               "Tiene " + Words.Size(disk.UsedBytes) + " de archivos, que se perderán."));
            sb.AppendLine().Append(L.T("No other disk of this PC is touched.", "No se toca ningún otro disco de este PC."));
            return new ConfirmForm(L.T("EVERYTHING on this disk will be ERASED", "Se va a BORRAR TODO lo que hay en este disco"), true,
                                   sb.ToString(), L.T("ERASE", "BORRAR"), L.T("Erase and install", "Borrar e instalar"));
        }

        // smaller: how much smaller the partition of that letter will be
        public static ConfirmForm AskBeside(DiskInfo disk, char letter, long smaller)
        {
            string details = disk.Title + "\r\n" + SerialLine(disk) + "\r\n\r\n" + L.T(
                "Partition " + letter + ": will be made " + Words.Size(smaller) + " smaller, and the two Hades partitions will be " +
                "created in the space that frees up.\r\n\r\n" +
                "Your files on " + letter + ": are not erased. No other disk is touched.\r\n" +
                "Do not turn the PC off while it lasts.",
                "La partición " + letter + ": se hará " + Words.Size(smaller) + " más pequeña y en ese hueco se crearán las dos " +
                "particiones de Hades.\r\n\r\n" +
                "Tus archivos de " + letter + ": no se borran. Ningún otro disco se toca.\r\n" +
                "No apagues el PC mientras dure.");
            return new ConfirmForm(L.T("Make room on " + letter + ": and install LibreELEC Hades",
                                       "Hacer sitio en " + letter + ": e instalar LibreELEC Hades"), false, details,
                                   L.T("INSTALL", "INSTALAR"), L.T("Make room and install", "Hacer sitio e instalar"));
        }

        // hidesOthers: the disk is inside the PC and the image hides the other internal disks by itself (the
        // Hades that is replaced may be one that saw them)
        public static ConfirmForm AskUpdate(DiskInfo disk, string imageName, bool hidesOthers = false)
        {
            string others = !hidesOthers ? "" : L.T(
                "\r\n\r\nWith this version, Hades inside the PC does not see the other NVMe and SATA disks. An entry of its " +
                "start menu shows them, read-only.",
                "\r\n\r\nCon esta versión, Hades dentro del PC no ve los demás discos NVMe y SATA. Una entrada de su menú de " +
                "inicio los muestra, en solo lectura.");
            string details = disk.Title + "\r\n" + SerialLine(disk) + "\r\n\r\n" + L.T(
                "The Hades system on this drive is replaced by the one in the image (" + imageName + ").\r\n\r\n" +
                "Your settings, your add-ons and whatever you have saved in Hades are not touched. No other disk is touched.\r\n\r\n" +
                "The boot partition of Hades (Windows calls it LIBREELEC) is replaced whole: if you put anything there by hand, " +
                "copy it first. What its diagnostics saved there (the folder hades-diag) is kept: the installer copies it into the " +
                "new partition, checks it, and says so when it is done.\r\n" +
                "Do not unplug the drive or turn the PC off while it lasts.",
                "Se cambia el sistema de Hades de esta unidad por el de la imagen (" + imageName + ").\r\n\r\n" +
                "Tus ajustes, tus addons y lo que tengas guardado en Hades no se tocan. Ningún otro disco se toca.\r\n\r\n" +
                "La partición de arranque de Hades (Windows la llama LIBREELEC) se sustituye entera: si guardaste " +
                "algo ahí a mano, cópialo antes. Lo que su diagnóstico haya guardado ahí (la carpeta hades-diag) se conserva: el " +
                "instalador lo copia a la partición nueva, lo comprueba y lo dice al terminar.\r\n" +
                "No desconectes la unidad ni apagues el PC mientras dure.") + others;
            return new ConfirmForm(L.T("Update LibreELEC Hades on this drive", "Actualizar LibreELEC Hades en esta unidad"), false, details,
                                   L.T("UPDATE", "ACTUALIZAR"), L.T("Update", "Actualizar"));
        }

        // The system partition of the Hades that is being updated is open in some program, and Windows did
        // not let it be locked: whether to take it from that program. Nothing is typed: the update was
        // confirmed that way a moment ago. letter: the one that Windows gives the partition ('\0': none).
        // wsl: WSL is running (Wsl.Running), which holds drives open without showing it anywhere.
        public static ConfirmForm AskTake(DiskInfo disk, char letter, bool wsl)
        {
            string where = letter == '\0' ? "" : " (" + letter + ":)";
            string details = disk.Title + "\r\n" + SerialLine(disk) + "\r\n\r\n" + L.T(
                "A program has the Hades system partition of this drive" + where + " open, and Windows does not let it be " +
                "replaced meanwhile. Nothing has been written.\r\n\r\n",
                "Un programa tiene abierta la partición de sistema de Hades de esta unidad" + where + ", y Windows no deja " +
                "sustituirla mientras tanto. No se ha escrito nada.\r\n\r\n");
            // (what WSL does when a partition is taken from it has not been seen: the worst that can be
            // expected is said, as something that may happen)
            if (wsl)
                details += L.T(
                    "WSL (Linux on Windows) is running on this PC. It keeps open the disk drives that were connected when it " +
                    "started, even if nothing there uses them, and closing windows does not change that: it may well be what " +
                    "holds this one. Taking the partition from WSL may leave WSL without its Windows drives until WSL is " +
                    "restarted: if you have work in progress there, finish it first, or close WSL and update again.\r\n\r\n",
                    "WSL (Linux en Windows) está en marcha en este PC. Mantiene abiertas las unidades de disco que estaban " +
                    "conectadas cuando arrancó, aunque nada las use ahí, y cerrar ventanas no lo cambia: bien puede ser lo " +
                    "que la retiene. Quitarle la partición puede dejar a WSL sin sus unidades de Windows hasta que se reinicie " +
                    "WSL: si tienes trabajo a medias ahí, termínalo antes, o cierra WSL y vuelve a actualizar.\r\n\r\n");
            details += L.T(
                "You can close whatever has it open and update again, or let the installer take the partition from those " +
                "programs: what they have open on it stops working for them. Only that partition is taken, the one that is " +
                "replaced whole.\r\n\r\n" +
                "If you are copying something from it right now, cancel and wait for the copy to end.",
                "Puedes cerrar lo que la tenga abierta y volver a actualizar, o dejar que el instalador se la quite a esos " +
                "programas: lo que tengan abierto en ella dejará de funcionarles. Solo se toma esa partición, la que se " +
                "sustituye entera.\r\n\r\n" +
                "Si ahora mismo estás copiando algo desde ella, cancela y espera a que termine la copia.");
            return new ConfirmForm(L.T("Windows is using that partition", "Windows tiene en uso esa partición"), false, details, null,
                                   L.T("Take it and update", "Tomarla y actualizar"));
        }

        public static ConfirmForm AskRemove(DiskInfo disk)
        {
            string details = disk.Title + "\r\n" + SerialLine(disk) + "\r\n\r\n" + L.T(
                "The Hades partitions on this disk are deleted, with everything Hades has stored (settings, add-ons).\r\n\r\n" +
                "If Hades shares the disk with your files, they are not touched and their partition gets the space back. " +
                "If the whole disk was for Hades, it is left empty and ready for Windows.",
                "Se borran las particiones de Hades de este disco, con lo que Hades tenga guardado (ajustes, addons).\r\n\r\n" +
                "Si Hades comparte el disco con tus archivos, estos no se tocan y su partición recupera el espacio. " +
                "Si el disco entero era de Hades, queda vacío y listo para Windows.");
            return new ConfirmForm(L.T("Remove LibreELEC Hades from this disk", "Quitar LibreELEC Hades de este disco"), true, details,
                                   L.T("REMOVE", "QUITAR"), L.T("Remove LibreELEC Hades", "Quitar LibreELEC Hades"));
        }

        // What the last panel says: that it is done, how to start it, and what the entries of the start menu of Hades do.
        // It is made again when the language or the scale changes.
        void BuildDone()
        {
            DiskInfo disk = doneDisk;
            if (disk == null) return;
            Place place = donePlace;
            bool updated = doneUpdated;
            string name = disk.ShownName;
            for (int i = donePanel.Controls.Count - 1; i >= 0; --i)
            {
                Control c = donePanel.Controls[i];
                if (c == close) continue;
                donePanel.Controls.RemoveAt(i);
                c.Dispose();
            }
            int W = 696, x = 32, y = 24;
            Func<string, float, string, Color, Label> text = delegate(string t, float size, string kind, Color color)
            {
                var l = new Label();
                l.Text = t;
                l.Font = Ui.F(size, kind);
                l.ForeColor = color;
                l.BackColor = Ui.Back;
                donePanel.Controls.Add(l);
                return l;
            };
            // the check, and what is done
            var badge = new Panel();
            badge.BackColor = Ui.Back;
            R(badge, x, y, 48, 48);
            badge.Paint += delegate(object s, PaintEventArgs e)
            {
                e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                float d = Ui.Sf(46);
                using (var b = new SolidBrush(Ui.Ok)) e.Graphics.FillEllipse(b, 1, 1, d, d);
                using (var pen = new Pen(Color.White, Math.Max(2f, Ui.Sf(3.2f))) { StartCap = System.Drawing.Drawing2D.LineCap.Round, EndCap = System.Drawing.Drawing2D.LineCap.Round, LineJoin = System.Drawing.Drawing2D.LineJoin.Round })
                    e.Graphics.DrawLines(pen, new[] { new PointF(1 + d * 0.27f, 1 + d * 0.53f), new PointF(1 + d * 0.43f, 1 + d * 0.68f), new PointF(1 + d * 0.74f, 1 + d * 0.34f) });
            };
            donePanel.Controls.Add(badge);
            Label head = text(updated ? L.T("LibreELEC Hades is updated", "LibreELEC Hades está actualizado")
                                      : L.T("LibreELEC Hades is installed", "LibreELEC Hades está instalado"), 21f, "semi", Ui.Text);
            R(head, x + 64, y - 2, W - 64 - 140, 30);
            Label sub = text((updated ? L.T("On " + name + ", with your settings as they were. ", "En " + name + ", con tus ajustes como estaban. ")
                                      : L.T("On " + name + ". ", "En " + name + ". ")) +
                             L.T("Checked by reading back what was written.", "Comprobado releyendo lo escrito."), 13f, "", Ui.Soft);
            R(sub, x + 64, y + 30, W - 64 - 140, 40);
            y += 76;
            string diag = updated && doneDiag != null ? doneDiag.Text() : "";
            // (and what became of the choices of the old boot menus: BootMenus)
            string boot = updated && doneDiag != null && doneDiag.Boot != null ? doneDiag.Boot.Text() : "";
            if (boot.Length > 0) diag = diag.Length > 0 ? boot + " " + diag : boot;
            if (diag.Length > 0)
            {
                Label d = text(diag, 12.5f, "", Ui.Soft);
                int h = TextRenderer.MeasureText(diag, d.Font, new Size(Ui.S(W), 0), TextFormatFlags.WordBreak).Height;
                d.SetBounds(Ui.S(x), Ui.S(y), Ui.S(W), h + Ui.S(2));
                y += (int)Math.Ceiling(h / Ui.Scale) + 8;
            }

            // how to start it
            y += 8;
            Label how = text(L.T("How to start it", "Cómo arrancarlo"), 15f, "semi", Ui.Text);
            R(how, x, y, W, 24);
            y += 32;
            int cw = (W - 24) / 3;
            string[] titles =
            {
                L.T("Restart the PC", "Reinicia el PC"),
                L.T("Open the boot menu", "Abre el menú de arranque"),
                place == Place.Usb ? L.T("Choose the USB drive", "Elige la unidad USB") : L.T("Choose the disk", "Elige el disco")
            };
            string[] bodies =
            {
                place == Place.Usb ? L.T("Leave the drive plugged in.", "Deja la unidad conectada.") : L.T("Nothing else to do first.", "No hay que hacer nada antes."),
                L.T("As soon as it turns on, press the key of the boot menu several times.", "Nada más encender, pulsa varias veces la tecla del menú de arranque."),
                place == Place.Usb ? L.T("The entry that says UEFI.", "La entrada que diga UEFI.")
                                   : L.T(name + ", not “Windows Boot Manager”.", name + ", no «Windows Boot Manager».")
            };
            for (int i = 0; i < 3; ++i)
            {
                var card = new StepCard();
                card.Number = i + 1;
                card.Title = titles[i];
                card.Body = bodies[i];
                R(card, x + i * (cw + 12), y, cw, 116);
                donePanel.Controls.Add(card);
            }
            y += 116 + 10;
            var chips = new Chips();
            chips.Items = new[] { "ASUS  F8", "MSI  F11", "Gigabyte  F12", "ASRock  F11", L.T("Laptops  F12 / Esc", "Portátiles  F12 / Esc") };
            chips.BackColor = Ui.Back;
            R(chips, x, y, W, 32);
            int chipH;
            using (Graphics g = chips.CreateGraphics()) chipH = chips.Flow(g, null);
            chips.Height = chipH;
            donePanel.Controls.Add(chips);
            y += (int)Math.Ceiling(chipH / Ui.Scale) + 12;
            if (!updated)
            {
                var first = new Callout();
                first.Text = L.T("The first time, Hades prepares its space and restarts by itself: choose it again in the menu.",
                                 "La primera vez, Hades prepara su espacio y se reinicia solo: vuelve a elegirlo en el menú.");
                first.Font = Ui.F(13f);
                first.ForeColor = Ui.Dark ? Ui.Text : Color.FromArgb(70, 50, 10);
                first.BackColor = Ui.CalloutBack;
                first.Bar = Ui.CalloutBar;
                first.Padding = new Padding(Ui.S(16), Ui.S(5), Ui.S(8), Ui.S(4));
                first.TextAlign = ContentAlignment.MiddleLeft;
                R(first, x, y, W, 30);
                donePanel.Controls.Add(first);
                y += 30 + 14;
            }

            // what the entries of its menu do (the words of the menu itself; as the image says it)
            Label menu = text(L.T("What each entry of the Hades menu does", "Qué hace cada entrada del menú de Hades"), 15f, "semi", Ui.Text);
            R(menu, x, y, W, 24);
            y += 30;
            bool hides = plan != null && plan.HidesOtherDisks;
            var rows = new List<string[]>();
            rows.Add(new[] { L.T("Run (recommended)", "Run (recomendado)"),
                             hides ? L.T("The normal start. The other NVMe and SATA disks stay hidden.", "El arranque normal. Los demás discos NVMe y SATA quedan ocultos.")
                                   : L.T("The normal start.", "El arranque normal.") });
            if (hides)
                rows.Add(new[] { L.T("Run (internal disks read-only)", "Run (discos internos en solo lectura)"),
                                 L.T("Like Run, and shows the internal disks, mounted read-only.", "Como Run, y muestra los discos internos, montados en solo lectura.") });
            rows.Add(new[] { "Live", L.T("Settings and add-ons stay in memory only, gone at power off.", "Los ajustes y addons solo quedan en memoria: se pierden al apagar.") });
            rows.Add(new[] { "Installer", L.T("Installs Hades on a disk you choose, erasing it. Asks twice first.", "Instala Hades en un disco que elijas, borrándolo. Pregunta dos veces antes.") });
            foreach (string[] row in rows)
            {
                Label a = text(row[0], 13f, "semi", Ui.Text);
                R(a, x + 8, y, 230, 22);
                Label b = text(row[1], 13f, "", Ui.Soft);
                R(b, x + 244, y, W - 244, 22);
                y += 26;
            }
            y += 10;

            // the rest
            var sb = new StringBuilder();
            sb.AppendLine(L.T("To go back to Windows, shut Hades down and turn the PC on without pressing anything.",
                              "Para volver a Windows, apaga Hades y enciende el PC sin pulsar nada."));
            if (Pc.SecureBoot == 1)
                sb.AppendLine(L.T("Secure Boot is turned on in this PC: Hades will not start until it is turned off in the BIOS. Turn it back on whenever you want to play something that requires it.",
                                  "Secure Boot está activado en este PC: Hades no arrancará hasta desactivarlo en la BIOS. Vuelve a activarlo cuando quieras jugar a algo que lo exija."));
            if (place == Place.Usb)
                sb.Append(L.T("If Windows says that a drive has to be formatted, press Cancel: it is the part of Hades, which Windows cannot read.",
                              "Si Windows dice que hay que formatear una unidad, pulsa Cancelar: es la parte de Hades, que Windows no sabe leer."));
            else
            {
                sb.AppendLine(L.T("To remove it: open this program, choose “internal disk”, the disk, and “Remove LibreELEC Hades”.",
                                  "Para quitarlo: abre este programa, elige «disco interno», el disco, y «Quitar LibreELEC Hades»."));
                sb.Append(L.T("If you ever reinstall Windows, unplug this disk first, or Windows may put its own startup files on it.",
                              "Si algún día reinstalas Windows, desconecta antes este disco o Windows puede usar su arranque."));
            }
            Label notes = text(sb.ToString().TrimEnd(), 12.5f, "", Ui.Soft);
            R(notes, x, y, W, 100);
            R(close, 598, 30, 130, 38);
            close.BringToFront();
        }

        // diag: what became of the folder hades-diag in an update
        public void ShowDone(DiskInfo disk, Place place, bool updated, DiagKeep diag = null)
        {
            doneDisk = disk;
            donePlace = place;
            doneUpdated = updated;
            doneDiag = diag;
            done = true;
            foreach (Control c in Controls)
                if (c != donePanel && c != header && c.Top >= donePanel.Top)
                {
                    if (c.Visible) doneHid.Add(c);
                    c.Visible = false;
                }
            language.LinkColor = language.VisitedLinkColor = Ui.InkSoft;
            donePanel.Visible = true;
            BuildDone();
            donePanel.BringToFront();
            // the steps of the bar at the side: all done (UpdateState is not called: it would put back what is hidden)
            stepState[0] = stepState[1] = stepState[2] = 2;
            stepSub[2] = L.T("Done", "Hecho");
            one.SetState(2);
            two.SetState(2);
            header.Invalidate();
        }

        // The panel of "done" goes: what it hid comes back, and what was skipped while it was on show is done now (the list of
        // disks again where a device came or went, else the state of what is chosen). Nothing in this window takes the panel
        // away yet (its button closes the window); this is the one way to do it.
        public void HideDone()
        {
            if (!done) return;
            done = false;
            donePanel.Visible = false;
            foreach (Control c in doneHid) c.Visible = true;
            doneHid.Clear();
            bool devices = doneSkippedDevices;
            doneSkippedDevices = doneSkippedState = false;
            if (devices) LoadDisks();
            else UpdateState();
            list.Invalidate();
        }

        // For the screenshots of the tests: disks that are not those of this PC.
        public sealed class TestData
        {
            public List<DiskInfo> Disks = new List<DiskInfo>();
            public Keep Keep = new Keep(); // what must not be erased, on those disks
            public long Spare;
            public List<DiskInfo> Diagnostics = new List<DiskInfo>(); // those whose Hades has its diagnostics on
        }
        public static TestData Test;
        public void ShowInternal() { inside.Checked = true; }
        public void SelectFirst() { if (list.Items.Count > 0) list.SelectedIndex = 0; }
        public void ChooseWhole() { whole.Checked = true; }
        public void ChooseBeside() { beside.Checked = true; }
        public bool SelectDisk(DiskInfo disk)
        {
            for (int i = 0; i < list.Items.Count; ++i)
                if (((DiskInfo)list.Items[i]).SameAs(disk)) { list.SelectedIndex = i; return true; }
            return false;
        }
        // (for the screenshots) the window as it is while a disk is worked on, at that point of the progress; and back
        public void ShowWorking(int permille, bool removal = false)
        {
            busy = true;
            showSteps = true;
            stages.Permille = permille;
            SetStages(removal);
            bar.Value = permille;
            UpdateState();
            // (what Operations says: the stage and its own byte count, as the step that is marked)
            long total = 3000L << 20, part = permille < 600 ? (permille - 50) * total / 550 : (permille - 600) * total / 400;
            if (removal) { status.Text = stages.Stages[stages.Stage]; return; }
            status.Text = permille < 600 ? L.T("Writing…  ", "Escribiendo…  ") + Words.Size(part) + L.T(" of ", " de ") + Words.Size(total)
                                         : L.T("Verifying…  ", "Verificando…  ") + Words.Size(part) + L.T(" of ", " de ") + Words.Size(total);
        }
        public void EndWorking()
        {
            busy = false;
            showSteps = false;
            bar.Value = 0;
            UpdateState();
        }
        // (for the tests) a device that came or went, and a display of another scale, as the window takes them from Windows
        public void DevicesChangedForTest() { DevicesChanged(); }
        public void ScaleChangedForTest(float scale) { ScaleChanged(scale); }
        // whether all that is on show is the panel of "done" and the bar at the side; whether something waits for the panel to go;
        // how many times the list of disks was made; where the panel and its button are
        public bool OnlyDoneShows
        {
            get
            {
                foreach (Control c in Controls)
                    if (c != donePanel && c != header && c.Visible) return false;
                return donePanel.Visible && close.Visible;
            }
        }
        public bool DoneWaits { get { return doneSkippedDevices || doneSkippedState; } }
        public int DisksLoaded { get { return disksLoaded; } }
        public Rectangle DoneBounds { get { return donePanel.Bounds; } }
        public Rectangle CloseBounds { get { return close.Bounds; } }
        public bool ListShows { get { return listFrame.Visible && list.Items.Count > 0; } }
        public bool GoReady { get { return go.Enabled; } }
        // the box of the diagnostics: what it shows, whether it can be set, and a click of the user on it
        public bool DiagnosticsTicked { get { return diagnostics.Checked; } }
        public bool DiagnosticsCanBeSet { get { return diagnostics.Enabled; } }
        public void ClickDiagnostics()
        {
            // what a click of the mouse does: the box changes and then its Click is raised (a box that is
            // off takes no click)
            if (diagnostics.Enabled) InvokeOnClick(diagnostics, EventArgs.Empty);
            UpdateState();
        }
        public void ChooseImageForTest(string path) { SetImage(path); LoadDisks(); }
        public bool SwitchKnown { get { return plan != null && plan.SwitchOffset >= 0; } }
        public bool SwitchAlways { get { return plan != null && plan.SwitchOn; } }
        public string DiagnosticsText { get { return diagnostics.Text; } }
        public DiskInfo FirstOffered() { return list.Items.Count > 0 ? (DiskInfo)list.Items[0] : null; }
        public List<DiskInfo> ExcludedDisks { get { return excluded; } }
        public int Offered { get { return list.Items.Count; } }
        public bool Scanning { get { return scanning || imageState == "checking"; } }
        public bool UpdateReady { get { return update.Visible && update.Enabled; } }
        public string StatusText { get { return status.Text; } }
    }
}
