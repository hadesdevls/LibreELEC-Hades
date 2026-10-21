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
        readonly Button ok = new Button(), cancel = new Button();
        readonly Label what = new Label();

        // word: what has to be typed before the button of the action can be pressed. null: nothing is typed
        // (a second question about something that was confirmed that way already), and it is Cancel that
        // the Enter key presses.
        public ConfirmForm(string banner, bool danger, string details, string word, string action)
        {
            bool typed = word != null;
            Text = L.T("Confirm", "Confirmar");
            Font = new Font("Segoe UI", 9.75f);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            // as tall as what it has to say (and without the room of what is not typed)
            int more = Math.Max(0, TextRenderer.MeasureText(details, Font, new Size(492, 0), TextFormatFlags.WordBreak).Height + 10 - 156);
            if (!typed) more -= 62;
            ClientSize = new Size(540, 350 + more);
            AutoScaleMode = AutoScaleMode.Font;

            var top = new Label();
            top.Text = banner;
            top.Font = new Font("Segoe UI Semibold", 12f);
            top.ForeColor = Color.White;
            top.BackColor = danger ? Color.FromArgb(176, 32, 32) : Color.FromArgb(34, 108, 214);
            top.TextAlign = ContentAlignment.MiddleCenter;
            top.Dock = DockStyle.Top;
            top.Height = 54;

            what.Text = details;
            what.SetBounds(24, 68, 492, 156 + more + (typed ? 0 : 62));

            var ask = new Label();
            ask.Text = L.T("To continue, type ", "Para continuar, escribe ") + word + ":";
            ask.SetBounds(24, 228 + more, 492, 24);
            box.SetBounds(24, 254 + more, 200, 28);
            box.CharacterCasing = CharacterCasing.Upper;
            box.TextChanged += delegate { ok.Enabled = box.Text.Trim() == word; };
            ask.Visible = box.Visible = typed;

            ok.Text = action;
            ok.Enabled = !typed;
            ok.DialogResult = DialogResult.OK;
            ok.SetBounds(256, 300 + more, 170, 34);
            cancel.Text = L.T("Cancel", "Cancelar");
            cancel.DialogResult = DialogResult.Cancel;
            cancel.SetBounds(434, 300 + more, 90, 34);
            AcceptButton = typed ? ok : cancel;
            CancelButton = cancel;
            Controls.AddRange(new Control[] { what, ask, box, ok, cancel, top });
            if (!typed) ActiveControl = cancel;
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

        readonly Label title = new Label();
        readonly Label subtitle = new Label();
        readonly LinkLabel language = new LinkLabel();
        readonly Label one, two;
        readonly Label imageLabel = new Label();
        readonly Button imageButton = new Button();
        readonly RadioButton usb = new RadioButton();
        readonly RadioButton inside = new RadioButton();
        readonly ListBox list = new ListBox();
        readonly LinkLabel missing = new LinkLabel();
        readonly Button refresh = new Button();
        readonly Panel options = new Panel();
        readonly Label notice = new Label();
        readonly RadioButton beside = new RadioButton();
        readonly RadioButton whole = new RadioButton();
        readonly Label explain = new Label();
        readonly Label volumeLabel = new Label();
        readonly ComboBox volumeBox = new ComboBox();
        readonly Label sizeLabel = new Label();
        readonly NumericUpDown sizeBox = new NumericUpDown();
        readonly Label sizeUnit = new Label();
        readonly LinkLabel remove = new LinkLabel();
        readonly CheckBox diagnostics = new CheckBox();
        readonly ProgressBar bar = new ProgressBar();
        readonly Label status = new Label();
        readonly Button go = new Button();
        readonly Button update = new Button();
        readonly Panel donePanel = new Panel();
        readonly Label doneText = new Label();
        readonly Button close = new Button();
        readonly Timer deviceTimer = new Timer();
        readonly Stopwatch clock = new Stopwatch();

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
            Font = new Font("Segoe UI", 9.75f);
            AutoScaleMode = AutoScaleMode.Font;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(760, 688);
            BackColor = Color.White;

            var header = new Panel();
            header.BackColor = Color.FromArgb(22, 24, 30);
            header.SetBounds(0, 0, 760, 60);
            title.Font = new Font("Segoe UI Semibold", 17f);
            title.ForeColor = Color.White;
            title.SetBounds(22, 4, 480, 36);
            subtitle.ForeColor = Color.FromArgb(190, 196, 208);
            subtitle.SetBounds(25, 37, 480, 20);
            // the other language, at the right of the header
            language.SetBounds(598, 21, 140, 20);
            language.TextAlign = ContentAlignment.MiddleRight;
            language.LinkColor = language.VisitedLinkColor = Color.FromArgb(190, 196, 208);
            language.ActiveLinkColor = Color.White;
            language.DisabledLinkColor = Color.FromArgb(96, 100, 110);
            language.LinkClicked += delegate { SwitchLanguage(); };
            header.Controls.Add(title);
            header.Controls.Add(subtitle);
            header.Controls.Add(language);

            one = Heading(68);
            imageLabel.SetBounds(24, 92, 590, 42);
            imageButton.SetBounds(616, 94, 120, 30);
            imageButton.Click += delegate { ChooseImage(); };

            two = Heading(136);
            usb.SetBounds(26, 162, 300, 24);
            usb.Checked = true;
            inside.SetBounds(336, 162, 400, 24);
            usb.CheckedChanged += delegate { if (usb.Checked) PlaceChanged(); };
            inside.CheckedChanged += delegate { if (inside.Checked) PlaceChanged(); };

            list.SetBounds(24, 190, 712, 132);
            list.DrawMode = DrawMode.OwnerDrawFixed;
            list.ItemHeight = 62;
            list.IntegralHeight = false;
            list.BorderStyle = BorderStyle.FixedSingle;
            list.DrawItem += DrawDisk;
            list.SelectedIndexChanged += delegate { DiskChanged(); };
            missing.SetBounds(24, 328, 300, 22);
            missing.LinkClicked += delegate { ShowExcluded(); };
            refresh.SetBounds(606, 326, 130, 28);
            refresh.Click += delegate { LoadDisks(); };

            options.SetBounds(24, 360, 712, 186);
            notice.SetBounds(0, 0, 712, 60);
            notice.BackColor = Color.FromArgb(255, 247, 214);
            notice.Padding = new Padding(10, 8, 10, 8);
            notice.BorderStyle = BorderStyle.FixedSingle;
            beside.SetBounds(2, 0, 330, 24);
            beside.Checked = true;
            whole.SetBounds(338, 0, 330, 24);
            beside.CheckedChanged += delegate { if (beside.Checked) UpdateState(); };
            whole.CheckedChanged += delegate { if (whole.Checked) UpdateState(); };
            explain.SetBounds(0, 26, 712, 96);
            explain.BackColor = Color.FromArgb(244, 246, 250);
            explain.Padding = new Padding(8, 5, 8, 5);
            volumeLabel.SetBounds(0, 132, 176, 22);
            volumeBox.DropDownStyle = ComboBoxStyle.DropDownList;
            volumeBox.SetBounds(178, 128, 250, 26);
            volumeBox.SelectedIndexChanged += delegate { VolumeChanged(); };
            sizeLabel.SetBounds(446, 132, 132, 22);
            sizeBox.SetBounds(580, 129, 72, 26);
            sizeBox.Minimum = 9;
            sizeBox.Maximum = 9;
            sizeBox.ValueChanged += delegate { UpdateState(); };
            sizeUnit.Text = "GB";
            sizeUnit.SetBounds(656, 132, 50, 22);
            remove.SetBounds(0, 162, 400, 22);
            remove.LinkClicked += delegate { StartRemove(); };
            options.Controls.AddRange(new Control[] { notice, beside, whole, explain, volumeLabel, volumeBox, sizeLabel, sizeBox,
                                                      sizeUnit, remove });

            // (for every way of installing and for an update: under what each of them asks)
            diagnostics.SetBounds(26, 552, 710, 24);
            // (a click or the space bar, not a change that this program makes)
            diagnostics.Click += delegate { diagnosticsAsked = diagnostics.Checked; };

            bar.SetBounds(24, 582, 712, 16);
            bar.Maximum = 1000;
            status.SetBounds(24, 602, 712, 22);
            status.ForeColor = Color.FromArgb(70, 70, 70);

            go.Font = new Font("Segoe UI Semibold", 11f);
            go.SetBounds(24, ButtonsAt, 712, 44);
            go.ForeColor = Color.White;
            go.FlatStyle = FlatStyle.Flat;
            go.FlatAppearance.BorderSize = 0;
            go.Click += delegate { Start(); };
            update.Font = go.Font;
            update.SetBounds(24, ButtonsAt, 372, 44);
            update.ForeColor = Color.White;
            update.BackColor = Color.FromArgb(22, 132, 74);
            update.FlatStyle = FlatStyle.Flat;
            update.FlatAppearance.BorderSize = 0;
            update.Visible = false;
            update.Click += delegate { StartUpdate(); };

            donePanel.SetBounds(0, 60, 760, 628);
            donePanel.BackColor = Color.White;
            donePanel.Visible = false;
            doneText.SetBounds(32, 22, 696, 480);
            doneText.Font = new Font("Segoe UI", 10.5f);
            close.SetBounds(606, 540, 130, 40);
            close.Click += delegate { Close(); };
            donePanel.Controls.Add(doneText);
            donePanel.Controls.Add(close);

            Controls.AddRange(new Control[] { donePanel, header, one, imageLabel, imageButton, two, usb, inside, list, missing,
                                              refresh, options, diagnostics, bar, status, go, update });
            // the header is the last stop of the Tab key: the keyboard starts where it did, not at the language link
            header.TabIndex = Controls.Count;

            deviceTimer.Interval = 1200;
            deviceTimer.Tick += delegate { deviceTimer.Stop(); if (!busy) LoadDisks(); };

            if (!Native.Uefi) inside.Enabled = false;
            SetTexts();
            SetImage(image);
            LoadDisks();
        }

        const int ButtonsAt = 630; // the top of the two buttons at the bottom

        Label Heading(int y)
        {
            var l = new Label();
            l.Font = new Font("Segoe UI Semibold", 11f);
            l.SetBounds(24, y, 600, 24);
            return l;
        }

        // The texts that do not depend on what is chosen, in the language of now. (UpdateState sets the rest.)
        void SetTexts()
        {
            Text = title.Text = L.T("LibreELEC Hades · Installer", "LibreELEC Hades · Instalador");
            subtitle.Text = L.T("Dolby Vision on your PC", "Dolby Vision en tu PC");
            language.Text = L.Spanish ? "English" : "Español"; // each in its own language
            one.Text = L.T("1.  LibreELEC Hades image", "1.  Imagen de LibreELEC Hades");
            imageButton.Text = L.T("Choose another…", "Elegir otra…");
            two.Text = L.T("2.  Where do you want it?", "2.  ¿Dónde lo quieres?");
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
        }

        // The other language: every text of the window again. What was kept as a text is made again.
        public void SwitchLanguage()
        {
            if (busy) return; // (the link is off then: what is done to a disk says all its steps in one language)
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
                doneText.Text = DoneText();
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
            base.WndProc(ref m);
            if (m.Msg == 0x0219 && !busy) // a device came or went
            {
                deviceTimer.Stop();
                deviceTimer.Start();
            }
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

        void DrawDisk(object sender, DrawItemEventArgs e)
        {
            e.DrawBackground();
            if (e.Index < 0) return;
            var d = (DiskInfo)list.Items[e.Index];
            bool selected = (e.State & DrawItemState.Selected) != 0;
            Color main = selected ? SystemColors.HighlightText : Color.FromArgb(20, 20, 20);
            Color soft = selected ? SystemColors.HighlightText : Color.FromArgb(96, 96, 96);
            Rectangle r = e.Bounds;
            using (var big = new Font("Segoe UI Semibold", 11f))
            using (var mainBrush = new SolidBrush(main))
            using (var softBrush = new SolidBrush(soft))
            using (var pen = new Pen(selected ? SystemColors.HighlightText : Color.FromArgb(120, 120, 120), 2f))
            {
                int x = r.Left + 16, y = r.Top + r.Height / 2;
                if (d.Removable)
                {
                    // a small drawing of a USB drive
                    e.Graphics.DrawRectangle(pen, x, y - 9, 26, 18);
                    e.Graphics.DrawRectangle(pen, x + 26, y - 5, 9, 10);
                }
                else
                {
                    // ... and of a disk inside the PC
                    e.Graphics.DrawRectangle(pen, x, y - 12, 34, 24);
                    e.Graphics.DrawLine(pen, x + 6, y + 5, x + 16, y + 5);
                }
                e.Graphics.DrawString(d.Title, big, mainBrush, r.Left + 64, r.Top + 9);
                e.Graphics.DrawString(d.Detail, Font, softBrush, r.Left + 65, r.Top + 34);
            }
            e.DrawFocusRectangle();
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
            Color color = Color.FromArgb(40, 40, 40);
            bool usable = false;
            switch (imageState)
            {
                case "ok":
                    imageLabel.Text = name + Words.Size(plan.Size) + L.T("  ·  checked: it arrived complete", "  ·  comprobada: ha llegado entera");
                    color = Color.FromArgb(18, 120, 50);
                    usable = true;
                    break;
                case "checking":
                    imageLabel.Text = name + L.T("Checking that it arrived complete…", "Comprobando que ha llegado entera…");
                    break;
                case "unchecked":
                    imageLabel.Text = name + Words.Size(plan.Size) +
                                      L.T("  ·  its .sha256 file is missing: there is no way to check that it arrived complete",
                                          "  ·  sin su archivo .sha256: no se puede comprobar que llegó entera");
                    color = Color.FromArgb(160, 100, 0);
                    usable = true;
                    break;
                default:
                    imageLabel.Text = name + ImageTrouble();
                    color = Color.FromArgb(176, 32, 32);
                    break;
            }
            imageLabel.ForeColor = color;

            var disk = list.SelectedItem as DiskInfo;
            var choice = volumeBox.SelectedItem as VolumeChoice;
            bool internalPlace = Where == Place.Internal;
            bool besideMode = internalPlace && beside.Checked;
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
            update.BackColor = update.Enabled ? Color.FromArgb(22, 132, 74) : Color.FromArgb(170, 176, 186);
            go.SetBounds(has ? 404 : 24, ButtonsAt, has ? 332 : 712, 44);
            go.Text = has ? (!internalPlace ? L.T("Install from scratch  (erases the drive)", "Instalar de cero  (borra la unidad)")
                             : besideMode ? L.T("Make room and install", "Hacer sitio e instalar")
                             : L.T("Erase the disk and install from scratch", "Borrar el disco e instalar de cero"))
                      : !internalPlace ? L.T("Install LibreELEC Hades on the chosen drive", "Instalar LibreELEC Hades en la unidad elegida")
                      : besideMode ? L.T("Make room and install LibreELEC Hades on the chosen disk",
                                         "Hacer sitio e instalar LibreELEC Hades en el disco elegido")
                      : L.T("Erase the chosen disk and install LibreELEC Hades", "Borrar el disco elegido e instalar LibreELEC Hades");
            go.Enabled = !busy && problem == null;
            go.BackColor = go.Enabled ? Color.FromArgb(34, 108, 214) : Color.FromArgb(170, 176, 186);
            // (with an image that cannot be used, what is wrong with it is said next to the image)
            if (has && usable && !busy && !donePanel.Visible && (problem == null || problem.Length == 0 || besideMode))
                status.Text = scanning || plan == null || !plan.Scanned
                    ? L.T("Reading the image…", "Leyendo la imagen…")
                    : L.T("This drive already has Hades. Updating replaces the system and keeps your settings and add-ons.",
                          "Esta unidad ya tiene Hades. Actualizar cambia el sistema y conserva tus ajustes y addons.");
            imageButton.Enabled = refresh.Enabled = list.Enabled = usb.Enabled = language.Enabled = !busy;
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
            beside.Enabled = whole.Enabled = volumeBox.Enabled = sizeBox.Enabled = remove.Enabled = !busy;
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
            UpdateState();
            clock.Restart();
            var worker = new BackgroundWorker();
            worker.WorkerReportsProgress = true;
            worker.ProgressChanged += delegate(object s, ProgressChangedEventArgs e)
            {
                bar.Value = Math.Max(0, Math.Min(1000, e.ProgressPercentage));
                status.Text = (string)e.UserState;
            };
            worker.DoWork += work;
            worker.RunWorkerCompleted += delegate(object s, RunWorkerCompletedEventArgs e)
            {
                busy = false;
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
                Run(delegate { Operations.InstallBeside(disk, choice.Partition, storage, chosen, diagnose, Report); },
                    delegate { ShowDone(disk, place, false); });
                return;
            }

            // (the button is off for such a disk, and Operations.InstallWhole looks at the disk again and refuses)
            if (disk.Kept != null) return;
            using (ConfirmForm confirm = AskErase(disk))
                if (confirm.ShowDialog(this) != DialogResult.OK) return;
            Log.Line("start whole " + place + ": disk " + disk.Number + " " + disk.Name + " <- " + image);
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
            sb.AppendLine(disk.Title).AppendLine(disk.Detail);
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
            string details = disk.Title + "\r\n\r\n" + L.T(
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
            string details = disk.Title + "\r\n\r\n" + L.T(
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
            string details = disk.Title + "\r\n\r\n" + L.T(
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
            string details = disk.Title + "\r\n\r\n" + L.T(
                "The Hades partitions on this disk are deleted, with everything Hades has stored (settings, add-ons).\r\n\r\n" +
                "If Hades shares the disk with your files, they are not touched and their partition gets the space back. " +
                "If the whole disk was for Hades, it is left empty and ready for Windows.",
                "Se borran las particiones de Hades de este disco, con lo que Hades tenga guardado (ajustes, addons).\r\n\r\n" +
                "Si Hades comparte el disco con tus archivos, estos no se tocan y su partición recupera el espacio. " +
                "Si el disco entero era de Hades, queda vacío y listo para Windows.");
            return new ConfirmForm(L.T("Remove LibreELEC Hades from this disk", "Quitar LibreELEC Hades de este disco"), true, details,
                                   L.T("REMOVE", "QUITAR"), L.T("Remove LibreELEC Hades", "Quitar LibreELEC Hades"));
        }

        // What the last panel says: that it is done, and how to start it.
        string DoneText()
        {
            DiskInfo disk = doneDisk;
            Place place = donePlace;
            bool updated = doneUpdated;
            string name = disk.ShownName;
            var sb = new StringBuilder();
            sb.AppendLine(updated ? L.T("LibreELEC Hades has been updated on " + name + ", with your settings as they were, and this was " +
                                        "checked by reading back what was written.",
                                        "LibreELEC Hades está actualizado en " + name + ", con tus ajustes como estaban, y se ha " +
                                        "comprobado releyendo lo escrito.")
                                  : L.T("LibreELEC Hades is installed on " + name + ", and this was checked by reading back what was written.",
                                        "LibreELEC Hades está instalado en " + name + " y se ha comprobado releyendo lo escrito."));
            string diag = updated && doneDiag != null ? doneDiag.Text() : "";
            if (diag.Length > 0) sb.AppendLine(diag);
            sb.AppendLine();
            sb.AppendLine(L.T("How to start it", "Cómo arrancarlo"));
            sb.AppendLine(place == Place.Usb ? L.T("  1.  Leave the drive plugged in and restart the PC.", "  1.  Deja la unidad conectada y reinicia el PC.")
                                             : L.T("  1.  Restart the PC.", "  1.  Reinicia el PC."));
            sb.AppendLine(L.T("  2.  As soon as it turns on, press the key of the boot menu several times:",
                              "  2.  Nada más encender, pulsa varias veces la tecla del menú de arranque:"));
            sb.AppendLine(L.T("       ASUS F8  ·  MSI F11  ·  Gigabyte F12  ·  ASRock F11  ·  laptops: F12 or Esc",
                              "       ASUS F8  ·  MSI F11  ·  Gigabyte F12  ·  ASRock F11  ·  portátiles: F12 o Esc"));
            sb.AppendLine(place == Place.Usb ? L.T("  3.  Choose the USB drive (the entry that says UEFI).", "  3.  Elige la unidad USB (la entrada que diga UEFI).")
                                             : L.T("  3.  Choose the disk " + name + " (not “Windows Boot Manager”).",
                                                   "  3.  Elige el disco " + name + " (no «Windows Boot Manager»)."));
            sb.AppendLine();
            if (!updated)
                sb.AppendLine(L.T("The first time, Hades prepares its space and restarts by itself: choose it again in the menu.",
                                  "La primera vez, Hades prepara su espacio y se reinicia solo: vuelve a elegirlo en el menú."));
            sb.AppendLine(L.T("To go back to Windows, shut Hades down and turn the PC on without pressing anything.",
                              "Para volver a Windows, apaga Hades y enciende el PC sin pulsar nada."));
            sb.AppendLine();
            if (Pc.SecureBoot == 1)
            {
                sb.AppendLine(L.T("Secure Boot is turned on in this PC: Hades will not start until it is turned off in the BIOS.",
                                  "Secure Boot está activado en este PC: Hades no arrancará hasta desactivarlo en la BIOS."));
                sb.AppendLine(L.T("Turn it back on whenever you want to play something that requires it.",
                                  "Vuelve a activarlo cuando quieras jugar a algo que lo exija."));
                sb.AppendLine();
            }
            if (place == Place.Usb)
            {
                sb.AppendLine(L.T("If Windows says that a drive has to be formatted, press Cancel: it is the part of Hades,",
                                  "Si Windows dice que hay que formatear una unidad, pulsa Cancelar: es la parte de Hades,"));
                sb.AppendLine(L.T("which Windows cannot read.", "que Windows no sabe leer."));
            }
            else
            {
                sb.AppendLine(L.T("To remove it: open this program, choose “internal disk”, the disk, and “Remove LibreELEC Hades”.",
                                  "Para quitarlo: abre este programa, elige «disco interno», el disco, y «Quitar LibreELEC Hades»."));
                sb.AppendLine(L.T("If you ever reinstall Windows, unplug this disk first, or Windows may put its own startup files on it.",
                                  "Si algún día reinstalas Windows, desconecta antes este disco o Windows puede usar su arranque."));
            }
            return sb.ToString();
        }

        // diag: what became of the folder hades-diag in an update
        public void ShowDone(DiskInfo disk, Place place, bool updated, DiagKeep diag = null)
        {
            doneDisk = disk;
            donePlace = place;
            doneUpdated = updated;
            doneDiag = diag;
            doneText.Text = DoneText();
            language.Enabled = !busy; // (it was off while the disk was worked on, and nothing else puts it on again here)
            foreach (Control c in Controls)
                if (c != donePanel && c.Top >= donePanel.Top) c.Visible = false;
            donePanel.Visible = true;
            donePanel.BringToFront();
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
