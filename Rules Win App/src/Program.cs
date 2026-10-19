using System;
using System.IO;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using System.Collections.Generic;

namespace NewOS.RulesViewer
{
    public class NorFile
    {
        public string Magic { get; set; }
        public uint Version { get; set; }
        public uint HeaderSize { get; set; }
        public uint Flags { get; set; }
        public string RuleId { get; set; }
        public string Category { get; set; }
        public string Severity { get; set; }
        public string AuthTag { get; set; }
        public uint Timestamp { get; set; }
        public uint PayloadCrc32 { get; set; }
        public uint PayloadSize { get; set; }
        public uint Reserved { get; set; }
        public string Content { get; set; }
        public bool IsVerified { get; set; }
        public string FilePath { get; set; }

        private static readonly byte[] CIPHER_KEY = Encoding.ASCII.GetBytes("NewSoft-NewOS-RuleSecurityKey-26");

        public static NorFile Load(string path)
        {
            if (!File.Exists(path))
                throw new FileNotFoundException("File .NOR non trovato: " + path);

            byte[] data = File.ReadAllBytes(path);
            if (data.Length < 128)
                throw new InvalidDataException("File troppo piccolo: intestazione .NOR non valida (minimo 128 byte).");

            NorFile nor = new NorFile();
            nor.FilePath = path;

            using (MemoryStream ms = new MemoryStream(data))
            using (BinaryReader br = new BinaryReader(ms))
            {
                byte[] magicBytes = br.ReadBytes(4);
                nor.Magic = Encoding.ASCII.GetString(magicBytes);
                if (nor.Magic != "NOSR")
                    throw new InvalidDataException("Magic header errato: '" + nor.Magic + "' invece di 'NOSR'.");

                nor.Version = br.ReadUInt32();
                nor.HeaderSize = br.ReadUInt32();
                nor.Flags = br.ReadUInt32();

                byte[] idBytes = br.ReadBytes(32);
                nor.RuleId = Encoding.ASCII.GetString(idBytes).TrimEnd('\0');

                byte[] catBytes = br.ReadBytes(32);
                nor.Category = Encoding.ASCII.GetString(catBytes).TrimEnd('\0');

                byte[] sevBytes = br.ReadBytes(16);
                nor.Severity = Encoding.ASCII.GetString(sevBytes).TrimEnd('\0');

                byte[] authBytes = br.ReadBytes(16);
                nor.AuthTag = Encoding.ASCII.GetString(authBytes).TrimEnd('\0');

                nor.Timestamp = br.ReadUInt32();
                nor.PayloadCrc32 = br.ReadUInt32();
                nor.PayloadSize = br.ReadUInt32();
                nor.Reserved = br.ReadUInt32();

                if (data.Length < 128 + nor.PayloadSize)
                    throw new InvalidDataException("Dimensione payload dichiarata (" + nor.PayloadSize + ") eccede la dimensione reale del file.");

                byte[] cipherPayload = br.ReadBytes((int)nor.PayloadSize);
                byte[] plainPayload = new byte[nor.PayloadSize];

                for (int i = 0; i < (int)nor.PayloadSize; i++)
                {
                    byte k = CIPHER_KEY[i % 32];
                    byte mix = (byte)((i * 11 + 0x6A) & 0xFF);
                    plainPayload[i] = (byte)(cipherPayload[i] ^ k ^ mix);
                }

                uint calcCrc = ComputeCrc32(plainPayload);
                nor.IsVerified = (calcCrc == nor.PayloadCrc32);
                nor.Content = Encoding.UTF8.GetString(plainPayload);
            }

            return nor;
        }

        private static uint ComputeCrc32(byte[] data)
        {
            uint crc = 0xFFFFFFFF;
            for (int i = 0; i < data.Length; i++)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; j++)
                {
                    crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xEDB88320 : 0);
                }
            }
            return crc ^ 0xFFFFFFFF;
        }
    }

    public class MainForm : Form
    {
        private ListBox lstRules;
        private TextBox txtContent;
        private Label lblTitle;
        private Label lblCategory;
        private Label lblSeverity;
        private Label lblIntegrity;
        private Label lblDetails;
        private StatusStrip statusStrip;
        private ToolStripStatusLabel statusLabel;
        private ToolStripStatusLabel statusCrc;
        private List<string> rulePaths = new List<string>();

        public MainForm(string initialFile)
        {
            InitializeComponent();
            PopulateRuleCatalog();

            if (!string.IsNullOrEmpty(initialFile) && File.Exists(initialFile))
            {
                LoadRuleFile(initialFile);
            }
            else if (lstRules.Items.Count > 0)
            {
                lstRules.SelectedIndex = 0;
            }
        }

        private void InitializeComponent()
        {
            this.Text = "NewOS V1 - Visualizzatore Ufficiale Regole di Sistema (.NOR)";
            this.Size = new Size(1000, 680);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.Font = new Font("Segoe UI", 9.5f, FontStyle.Regular);
            this.BackColor = Color.FromArgb(240, 242, 245);
            this.AllowDrop = true;
            this.DragEnter += MainForm_DragEnter;
            this.DragDrop += MainForm_DragDrop;

            // Barra superiore con comandi
            ToolStrip toolStrip = new ToolStrip();
            toolStrip.GripStyle = ToolStripGripStyle.Hidden;
            toolStrip.BackColor = Color.White;
            toolStrip.Padding = new Padding(8, 4, 8, 4);

            ToolStripButton btnOpen = new ToolStripButton("Apri file .NOR...");
            btnOpen.Click += (s, e) => OpenCustomFile();
            toolStrip.Items.Add(btnOpen);

            ToolStripButton btnRefresh = new ToolStripButton("Aggiorna Catalogo");
            btnRefresh.Click += (s, e) => PopulateRuleCatalog();
            toolStrip.Items.Add(btnRefresh);

            toolStrip.Items.Add(new ToolStripSeparator());

            ToolStripButton btnCopy = new ToolStripButton("Copia Testo");
            btnCopy.Click += (s, e) => {
                if (!string.IsNullOrEmpty(txtContent.Text)) {
                    Clipboard.SetText(txtContent.Text);
                    MessageBox.Show(this, "Testo della regola copiato negli appunti!", "Copia", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            };
            toolStrip.Items.Add(btnCopy);

            ToolStripButton btnExport = new ToolStripButton("Esporta in chiaro (.TXT)...");
            btnExport.Click += (s, e) => ExportTextFile();
            toolStrip.Items.Add(btnExport);

            toolStrip.Items.Add(new ToolStripSeparator());

            ToolStripButton btnInfo = new ToolStripButton("Informazioni sul Formato .NOR");
            btnInfo.Click += (s, e) => ShowFormatInfo();
            toolStrip.Items.Add(btnInfo);

            this.Controls.Add(toolStrip);

            // Split Container principale (Sinistra: Catalogo, Destra: Dettaglio Regola)
            SplitContainer split = new SplitContainer();
            split.Dock = DockStyle.Fill;
            split.SplitterDistance = 300;
            split.SplitterWidth = 5;
            this.Controls.Add(split);
            split.BringToFront();

            // Pannello Sinistro
            Panel pnlLeft = new Panel();
            pnlLeft.Dock = DockStyle.Fill;
            pnlLeft.Padding = new Padding(12);

            Label lblCatalogHeader = new Label();
            lblCatalogHeader.Text = "REGOLE DI SISTEMA (.NOR)";
            lblCatalogHeader.Font = new Font("Segoe UI", 9.0f, FontStyle.Bold);
            lblCatalogHeader.ForeColor = Color.FromArgb(70, 80, 95);
            lblCatalogHeader.Dock = DockStyle.Top;
            lblCatalogHeader.Height = 26;
            pnlLeft.Controls.Add(lblCatalogHeader);

            lstRules = new ListBox();
            lstRules.Dock = DockStyle.Fill;
            lstRules.Font = new Font("Segoe UI", 9.5f);
            lstRules.ItemHeight = 24;
            lstRules.BorderStyle = BorderStyle.FixedSingle;
            lstRules.SelectedIndexChanged += LstRules_SelectedIndexChanged;
            pnlLeft.Controls.Add(lstRules);
            lstRules.BringToFront();

            split.Panel1.Controls.Add(pnlLeft);

            // Pannello Destro
            Panel pnlRight = new Panel();
            pnlRight.Dock = DockStyle.Fill;
            pnlRight.Padding = new Padding(12);

            // Scheda intestazione metadati
            Panel pnlHeader = new Panel();
            pnlHeader.Dock = DockStyle.Top;
            pnlHeader.Height = 90;
            pnlHeader.BackColor = Color.White;
            pnlHeader.BorderStyle = BorderStyle.FixedSingle;
            pnlHeader.Padding = new Padding(12);

            lblTitle = new Label();
            lblTitle.Text = "Seleziona una regola...";
            lblTitle.Font = new Font("Segoe UI Semibold", 13.0f, FontStyle.Bold);
            lblTitle.ForeColor = Color.FromArgb(20, 30, 50);
            lblTitle.Dock = DockStyle.Top;
            lblTitle.Height = 30;
            pnlHeader.Controls.Add(lblTitle);

            Panel pnlBadges = new Panel();
            pnlBadges.Dock = DockStyle.Fill;

            lblCategory = new Label();
            lblCategory.Text = "Categoria: -";
            lblCategory.Font = new Font("Segoe UI", 9.0f, FontStyle.Regular);
            lblCategory.ForeColor = Color.FromArgb(80, 90, 105);
            lblCategory.AutoSize = true;
            lblCategory.Location = new Point(0, 8);
            pnlBadges.Controls.Add(lblCategory);

            lblSeverity = new Label();
            lblSeverity.Text = "[SEVERITA: -]";
            lblSeverity.Font = new Font("Segoe UI", 9.0f, FontStyle.Bold);
            lblSeverity.ForeColor = Color.DarkOrange;
            lblSeverity.AutoSize = true;
            lblSeverity.Location = new Point(230, 8);
            pnlBadges.Controls.Add(lblSeverity);

            lblIntegrity = new Label();
            lblIntegrity.Text = "[INTEGRITA: IN ATTESA]";
            lblIntegrity.Font = new Font("Segoe UI", 9.0f, FontStyle.Bold);
            lblIntegrity.ForeColor = Color.Gray;
            lblIntegrity.AutoSize = true;
            lblIntegrity.Location = new Point(410, 8);
            pnlBadges.Controls.Add(lblIntegrity);

            lblDetails = new Label();
            lblDetails.Text = "Header: 128 byte | Magic: NOSR | Autore: NEWSOFT-RULE-V1";
            lblDetails.Font = new Font("Consolas", 8.5f);
            lblDetails.ForeColor = Color.FromArgb(120, 130, 145);
            lblDetails.AutoSize = true;
            lblDetails.Location = new Point(0, 32);
            pnlBadges.Controls.Add(lblDetails);

            pnlHeader.Controls.Add(pnlBadges);
            pnlRight.Controls.Add(pnlHeader);

            // Editor / Viewer del testo decifrato
            txtContent = new TextBox();
            txtContent.Multiline = true;
            txtContent.ReadOnly = true;
            txtContent.ScrollBars = ScrollBars.Both;
            txtContent.Font = new Font("Consolas", 10.0f);
            txtContent.BackColor = Color.FromArgb(253, 254, 255);
            txtContent.ForeColor = Color.FromArgb(30, 35, 45);
            txtContent.Dock = DockStyle.Fill;
            txtContent.WordWrap = false;
            txtContent.BorderStyle = BorderStyle.FixedSingle;
            pnlRight.Controls.Add(txtContent);
            txtContent.BringToFront();

            split.Panel2.Controls.Add(pnlRight);

            // Status Strip
            statusStrip = new StatusStrip();
            statusStrip.BackColor = Color.White;
            statusLabel = new ToolStripStatusLabel("Pronto");
            statusLabel.Spring = true;
            statusLabel.TextAlign = ContentAlignment.MiddleLeft;
            statusCrc = new ToolStripStatusLabel("CRC32: -");
            statusStrip.Items.Add(statusLabel);
            statusStrip.Items.Add(statusCrc);
            this.Controls.Add(statusStrip);
        }

        private void PopulateRuleCatalog()
        {
            lstRules.Items.Clear();
            rulePaths.Clear();

            string appDir = AppDomain.CurrentDomain.BaseDirectory;
            List<string> candidateFolders = new List<string> {
                Path.Combine(appDir, "rules"),
                appDir,
                Path.Combine(appDir, @"..\build\bin\NewOS\Rules"),
                Path.Combine(appDir, @"..\..\build\bin\NewOS\Rules"),
                @"C:\NewOS\Rules"
            };

            HashSet<string> seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            foreach (string folder in candidateFolders)
            {
                try
                {
                    if (Directory.Exists(folder))
                    {
                        string[] files = Directory.GetFiles(folder, "*.NOR", SearchOption.TopDirectoryOnly);
                        Array.Sort(files);
                        foreach (string f in files)
                        {
                            string fileName = Path.GetFileName(f);
                            if (!seen.Contains(fileName))
                            {
                                seen.Add(fileName);
                                lstRules.Items.Add(fileName);
                                rulePaths.Add(f);
                            }
                        }
                    }
                }
                catch { }
            }

            if (lstRules.Items.Count > 0)
            {
                lstRules.SelectedIndex = 0;
                statusLabel.Text = "Caricate " + lstRules.Items.Count + " regole ufficiali NewOS.";
            }
            else
            {
                statusLabel.Text = "Nessuna regola trovata nelle cartelle standard. Usa 'Apri file .NOR...'";
            }
        }

        private void LstRules_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (lstRules.SelectedIndex >= 0 && lstRules.SelectedIndex < rulePaths.Count)
            {
                LoadRuleFile(rulePaths[lstRules.SelectedIndex]);
            }
        }

        private void LoadRuleFile(string path)
        {
            try
            {
                NorFile nor = NorFile.Load(path);
                lblTitle.Text = nor.RuleId;
                lblCategory.Text = "Categoria: " + nor.Category;
                lblSeverity.Text = "[SEVERITA: " + nor.Severity + "]";

                if (nor.Severity.IndexOf("CRITICA", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    nor.Severity.IndexOf("MASSIMA", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    lblSeverity.ForeColor = Color.Crimson;
                }
                else if (nor.Severity.IndexOf("SICUREZZA", StringComparison.OrdinalIgnoreCase) >= 0 ||
                         nor.Severity.IndexOf("ALTA", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    lblSeverity.ForeColor = Color.DarkOrange;
                }
                else
                {
                    lblSeverity.ForeColor = Color.RoyalBlue;
                }

                if (nor.IsVerified)
                {
                    lblIntegrity.Text = "[VERIFICATO: CRC32 MATCH]";
                    lblIntegrity.ForeColor = Color.Green;
                    statusCrc.Text = string.Format("CRC32: 0x{0:X8} (Valido)", nor.PayloadCrc32);
                    statusCrc.ForeColor = Color.DarkGreen;
                }
                else
                {
                    lblIntegrity.Text = "[ERRORE: CHECKSUM FALLITO]";
                    lblIntegrity.ForeColor = Color.Red;
                    statusCrc.Text = string.Format("CRC32: 0x{0:X8} (Mancata corrispondenza!)", nor.PayloadCrc32);
                    statusCrc.ForeColor = Color.Red;
                }

                lblDetails.Text = string.Format("Header: {0} byte | Magic: {1} | Versione: 0x{2:X8} | Dimensione: {3} byte | Path: {4}",
                    nor.HeaderSize, nor.Magic, nor.Version, nor.PayloadSize, Path.GetFileName(path));

                txtContent.Text = nor.Content.Replace("\r\n", "\n").Replace("\n", "\r\n");
                statusLabel.Text = "File aperto con successo: " + Path.GetFileName(path);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Errore durante l'apertura del file .NOR:\n" + ex.Message, "Errore Formato .NOR", MessageBoxButtons.OK, MessageBoxIcon.Error);
                statusLabel.Text = "Errore apertura: " + ex.Message;
            }
        }

        private void OpenCustomFile()
        {
            using (OpenFileDialog ofd = new OpenFileDialog())
            {
                ofd.Filter = "Regole Ufficiali NewOS (*.NOR)|*.NOR|Tutti i file (*.*)|*.*";
                ofd.Title = "Seleziona file di regola .NOR";
                if (ofd.ShowDialog(this) == DialogResult.OK)
                {
                    string f = ofd.FileName;
                    if (!rulePaths.Contains(f))
                    {
                        lstRules.Items.Add(Path.GetFileName(f));
                        rulePaths.Add(f);
                        lstRules.SelectedIndex = lstRules.Items.Count - 1;
                    }
                    else
                    {
                        lstRules.SelectedIndex = rulePaths.IndexOf(f);
                    }
                }
            }
        }

        private void ExportTextFile()
        {
            if (string.IsNullOrEmpty(txtContent.Text))
            {
                MessageBox.Show(this, "Nessun contenuto da esportare.", "Avviso", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            using (SaveFileDialog sfd = new SaveFileDialog())
            {
                sfd.Filter = "Documento di testo (*.TXT)|*.TXT|Tutti i file (*.*)|*.*";
                sfd.FileName = (lblTitle.Text.Replace(" ", "_") + ".TXT");
                if (sfd.ShowDialog(this) == DialogResult.OK)
                {
                    File.WriteAllText(sfd.FileName, txtContent.Text, Encoding.UTF8);
                    MessageBox.Show(this, "File esportato correttamente in chiaro:\n" + sfd.FileName, "Esportazione Completata", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            }
        }

        private void ShowFormatInfo()
        {
            string info =
                "SPECIFICHE FORMATO PROPRIETARIO .NOR (NewOS Official Rule)\n\n" +
                "- Header: 128 byte esatti, Magic 'NOSR'\n" +
                "- Version: 0x00010000 (v1.0)\n" +
                "- Flags: 0x00000001 (NOR_FLAG_DESKTOP_ONLY)\n" +
                "- Cifratura: Rolling Key simmetrica 'NewSoft-NewOS-RuleSecurityKey-26'\n" +
                "- Controllo integrita: CRC32 IEEE 802.3 del testo in chiaro\n\n" +
                "Vincolo di Sicurezza NewOS:\n" +
                "Il file non e' apribile in chiaro prima dell'avvio dell'ambiente Desktop.\n" +
                "Su Windows, puo' essere visualizzato solo tramite questa applicazione dedicata.";
            MessageBox.Show(this, info, "Specifiche Formato .NOR", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private void MainForm_DragEnter(object sender, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
                e.Effect = DragDropEffects.Copy;
        }

        private void MainForm_DragDrop(object sender, DragEventArgs e)
        {
            string[] files = (string[])e.Data.GetData(DataFormats.FileDrop);
            if (files != null && files.Length > 0)
            {
                LoadRuleFile(files[0]);
            }
        }
    }

    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            string initial = args != null && args.Length > 0 ? args[0] : null;
            Application.Run(new MainForm(initial));
        }
    }
}
