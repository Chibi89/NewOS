using System.Drawing;
using System.Windows.Forms;

namespace SystemPackManagerApp
{
    partial class MainForm
    {
        private System.ComponentModel.IContainer components = null;

        private Panel pnlHeader;
        private Label lblTitle;
        private Label lblBadge;

        private GroupBox gbPaths;
        private Label lblWorkDir;
        private TextBox txtWorkDir;
        private Button btnBrowseWorkDir;
        private Label lblPakPath;
        private TextBox txtPakPath;
        private Button btnBrowsePak;

        private FlowLayoutPanel pnlActions;
        private Button btnUnpack;
        private Button btnAudit;
        private Button btnPack;
        private CheckBox chkWatch;
        private Button btnOpenFolder;

        private Panel pnlStats;
        private Label lblTotal;
        private Label lblTotalVal;
        private Label lblAuth;
        private Label lblAuthVal;
        private Label lblMod;
        private Label lblModVal;
        private Label lblNew;
        private Label lblNewVal;
        private Label lblMiss;
        private Label lblMissVal;
        private Label lblSearch;
        private TextBox txtSearch;
        private Label lblShowingCount;

        private SplitContainer splitMain;
        private DataGridView dgvFiles;
        private RichTextBox rtbLog;
        private ProgressBar progressBar;
        private ContextMenuStrip ctxMenuGrid;
        private ToolStripMenuItem ctxCopyPath;
        private ToolStripMenuItem ctxCopySha256;

        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();

            this.pnlHeader = new Panel();
            this.lblTitle = new Label();
            this.lblBadge = new Label();

            this.gbPaths = new GroupBox();
            this.lblWorkDir = new Label();
            this.txtWorkDir = new TextBox();
            this.btnBrowseWorkDir = new Button();
            this.lblPakPath = new Label();
            this.txtPakPath = new TextBox();
            this.btnBrowsePak = new Button();

            this.pnlActions = new FlowLayoutPanel();
            this.btnUnpack = new Button();
            this.btnAudit = new Button();
            this.btnPack = new Button();
            this.chkWatch = new CheckBox();
            this.btnOpenFolder = new Button();

            this.pnlStats = new Panel();
            this.lblTotal = new Label();
            this.lblTotalVal = new Label();
            this.lblAuth = new Label();
            this.lblAuthVal = new Label();
            this.lblMod = new Label();
            this.lblModVal = new Label();
            this.lblNew = new Label();
            this.lblNewVal = new Label();
            this.lblMiss = new Label();
            this.lblMissVal = new Label();
            this.lblSearch = new Label();
            this.txtSearch = new TextBox();
            this.lblShowingCount = new Label();

            this.splitMain = new SplitContainer();
            this.dgvFiles = new DataGridView();
            this.rtbLog = new RichTextBox();
            this.progressBar = new ProgressBar();

            this.ctxMenuGrid = new ContextMenuStrip(this.components);
            this.ctxCopyPath = new ToolStripMenuItem();
            this.ctxCopySha256 = new ToolStripMenuItem();

            ((System.ComponentModel.ISupportInitialize)(this.splitMain)).BeginInit();
            this.splitMain.Panel1.SuspendLayout();
            this.splitMain.Panel2.SuspendLayout();
            this.splitMain.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.dgvFiles)).BeginInit();
            this.SuspendLayout();

            // pnlHeader
            this.pnlHeader.BackColor = Color.FromArgb(14, 30, 58);
            this.pnlHeader.Dock = DockStyle.Top;
            this.pnlHeader.Height = 56;
            this.pnlHeader.Padding = new Padding(16, 10, 16, 10);

            this.lblTitle.AutoSize = true;
            this.lblTitle.Font = new Font("Segoe UI", 13.5F, FontStyle.Bold);
            this.lblTitle.ForeColor = Color.White;
            this.lblTitle.Location = new Point(14, 14);
            this.lblTitle.Text = "🛡️ NewOS V1 — SYSTEM.PAK Security & Integrity Manager";

            this.lblBadge.AutoSize = true;
            this.lblBadge.BackColor = Color.FromArgb(20, 80, 40);
            this.lblBadge.ForeColor = Color.FromArgb(100, 255, 150);
            this.lblBadge.Font = new Font("Segoe UI", 8.5F, FontStyle.Bold);
            this.lblBadge.Location = new Point(590, 19);
            this.lblBadge.Padding = new Padding(6, 2, 6, 2);
            this.lblBadge.Text = "AES-256-CBC • NOSPAK10";

            this.pnlHeader.Controls.Add(this.lblTitle);
            this.pnlHeader.Controls.Add(this.lblBadge);

            // gbPaths
            this.gbPaths.Dock = DockStyle.Top;
            this.gbPaths.Height = 84;
            this.gbPaths.Padding = new Padding(12, 6, 12, 6);
            this.gbPaths.Text = " Percorsi di Lavoro & Pacchetto Cifrato ";
            this.gbPaths.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblWorkDir.AutoSize = true;
            this.lblWorkDir.Font = new Font("Segoe UI", 9F, FontStyle.Regular);
            this.lblWorkDir.Location = new Point(16, 24);
            this.lblWorkDir.Text = "Cartella di Staging (Watch):";

            this.txtWorkDir.Location = new Point(165, 21);
            this.txtWorkDir.Width = 350;
            this.txtWorkDir.Font = new Font("Segoe UI", 9F, FontStyle.Regular);

            this.btnBrowseWorkDir.Text = "Sfoglia...";
            this.btnBrowseWorkDir.Location = new Point(522, 20);
            this.btnBrowseWorkDir.Size = new Size(75, 26);
            this.btnBrowseWorkDir.Font = new Font("Segoe UI", 8.5F);
            this.btnBrowseWorkDir.Click += new System.EventHandler(this.BtnBrowseWorkDir_Click);

            this.lblPakPath.AutoSize = true;
            this.lblPakPath.Font = new Font("Segoe UI", 9F, FontStyle.Regular);
            this.lblPakPath.Location = new Point(620, 24);
            this.lblPakPath.Text = "File SYSTEM.PAK:";

            this.txtPakPath.Location = new Point(725, 21);
            this.txtPakPath.Width = 330;
            this.txtPakPath.Font = new Font("Segoe UI", 9F, FontStyle.Regular);

            this.btnBrowsePak.Text = "Sfoglia...";
            this.btnBrowsePak.Location = new Point(1062, 20);
            this.btnBrowsePak.Size = new Size(75, 26);
            this.btnBrowsePak.Font = new Font("Segoe UI", 8.5F);
            this.btnBrowsePak.Click += new System.EventHandler(this.BtnBrowsePak_Click);

            this.gbPaths.Controls.Add(this.lblWorkDir);
            this.gbPaths.Controls.Add(this.txtWorkDir);
            this.gbPaths.Controls.Add(this.btnBrowseWorkDir);
            this.gbPaths.Controls.Add(this.lblPakPath);
            this.gbPaths.Controls.Add(this.txtPakPath);
            this.gbPaths.Controls.Add(this.btnBrowsePak);

            // pnlActions
            this.pnlActions.Dock = DockStyle.Top;
            this.pnlActions.Height = 50;
            this.pnlActions.Padding = new Padding(10, 8, 10, 6);
            this.pnlActions.BackColor = Color.FromArgb(245, 247, 250);

            this.btnUnpack.Text = "📥 1. Estrai da SYSTEM.PAK";
            this.btnUnpack.Size = new Size(190, 34);
            this.btnUnpack.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            this.btnUnpack.BackColor = Color.White;
            this.btnUnpack.Click += new System.EventHandler(this.BtnUnpack_Click);

            this.btnAudit.Text = "🔍 2. Audit Integrità (Checksum)";
            this.btnAudit.Size = new Size(210, 34);
            this.btnAudit.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            this.btnAudit.BackColor = Color.White;
            this.btnAudit.Click += new System.EventHandler(this.BtnAudit_Click);

            this.btnPack.Text = "🔒 3. Re-Impacchetta e Cifra";
            this.btnPack.Size = new Size(190, 34);
            this.btnPack.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            this.btnPack.BackColor = Color.FromArgb(220, 245, 220);
            this.btnPack.ForeColor = Color.DarkGreen;
            this.btnPack.Click += new System.EventHandler(this.BtnPack_Click);

            this.chkWatch.Text = "👁 Monitoraggio Attivo (Watch)";
            this.chkWatch.Appearance = Appearance.Button;
            this.chkWatch.Size = new Size(220, 34);
            this.chkWatch.TextAlign = ContentAlignment.MiddleCenter;
            this.chkWatch.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            this.chkWatch.BackColor = Color.White;
            this.chkWatch.CheckedChanged += new System.EventHandler(this.ChkWatch_CheckedChanged);

            this.btnOpenFolder.Text = "📂 Apri Cartella";
            this.btnOpenFolder.Size = new Size(130, 34);
            this.btnOpenFolder.Font = new Font("Segoe UI", 9F);
            this.btnOpenFolder.BackColor = Color.White;
            this.btnOpenFolder.Click += new System.EventHandler(this.BtnOpenFolder_Click);

            this.pnlActions.Controls.Add(this.btnUnpack);
            this.pnlActions.Controls.Add(this.btnAudit);
            this.pnlActions.Controls.Add(this.btnPack);
            this.pnlActions.Controls.Add(this.chkWatch);
            this.pnlActions.Controls.Add(this.btnOpenFolder);

            // pnlStats
            this.pnlStats.Dock = DockStyle.Top;
            this.pnlStats.Height = 40;
            this.pnlStats.BackColor = Color.FromArgb(238, 242, 246);
            this.pnlStats.Padding = new Padding(14, 8, 14, 6);

            this.lblTotal.AutoSize = true;
            this.lblTotal.Location = new Point(14, 11);
            this.lblTotal.Text = "Totale:";
            this.lblTotal.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblTotalVal.AutoSize = true;
            this.lblTotalVal.Location = new Point(62, 11);
            this.lblTotalVal.Text = "0";
            this.lblTotalVal.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblAuth.AutoSize = true;
            this.lblAuth.ForeColor = Color.ForestGreen;
            this.lblAuth.Location = new Point(100, 11);
            this.lblAuth.Text = "Autentici:";
            this.lblAuth.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblAuthVal.AutoSize = true;
            this.lblAuthVal.ForeColor = Color.ForestGreen;
            this.lblAuthVal.Location = new Point(165, 11);
            this.lblAuthVal.Text = "0";
            this.lblAuthVal.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblMod.AutoSize = true;
            this.lblMod.ForeColor = Color.DarkOrange;
            this.lblMod.Location = new Point(200, 11);
            this.lblMod.Text = "Modificati:";
            this.lblMod.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblModVal.AutoSize = true;
            this.lblModVal.ForeColor = Color.DarkOrange;
            this.lblModVal.Location = new Point(270, 11);
            this.lblModVal.Text = "0";
            this.lblModVal.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblNew.AutoSize = true;
            this.lblNew.ForeColor = Color.DodgerBlue;
            this.lblNew.Location = new Point(310, 11);
            this.lblNew.Text = "Nuovi:";
            this.lblNew.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblNewVal.AutoSize = true;
            this.lblNewVal.ForeColor = Color.DodgerBlue;
            this.lblNewVal.Location = new Point(356, 11);
            this.lblNewVal.Text = "0";
            this.lblNewVal.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblMiss.AutoSize = true;
            this.lblMiss.ForeColor = Color.Crimson;
            this.lblMiss.Location = new Point(390, 11);
            this.lblMiss.Text = "Rimossi:";
            this.lblMiss.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblMissVal.AutoSize = true;
            this.lblMissVal.ForeColor = Color.Crimson;
            this.lblMissVal.Location = new Point(448, 11);
            this.lblMissVal.Text = "0";
            this.lblMissVal.Font = new Font("Segoe UI", 9F, FontStyle.Bold);

            this.lblSearch.AutoSize = true;
            this.lblSearch.Location = new Point(620, 11);
            this.lblSearch.Text = "Filtra:";
            this.lblSearch.Font = new Font("Segoe UI", 9F, FontStyle.Regular);

            this.txtSearch.Location = new Point(660, 8);
            this.txtSearch.Width = 240;
            this.txtSearch.PlaceholderText = "Cerca per nome o cartella...";
            this.txtSearch.TextChanged += new System.EventHandler(this.TxtSearch_TextChanged);

            this.lblShowingCount.AutoSize = true;
            this.lblShowingCount.Location = new Point(915, 11);
            this.lblShowingCount.Text = "Visualizzati: 0";
            this.lblShowingCount.ForeColor = Color.Gray;

            this.pnlStats.Controls.Add(this.lblTotal);
            this.pnlStats.Controls.Add(this.lblTotalVal);
            this.pnlStats.Controls.Add(this.lblAuth);
            this.pnlStats.Controls.Add(this.lblAuthVal);
            this.pnlStats.Controls.Add(this.lblMod);
            this.pnlStats.Controls.Add(this.lblModVal);
            this.pnlStats.Controls.Add(this.lblNew);
            this.pnlStats.Controls.Add(this.lblNewVal);
            this.pnlStats.Controls.Add(this.lblMiss);
            this.pnlStats.Controls.Add(this.lblMissVal);
            this.pnlStats.Controls.Add(this.lblSearch);
            this.pnlStats.Controls.Add(this.txtSearch);
            this.pnlStats.Controls.Add(this.lblShowingCount);

            // splitMain
            this.splitMain.Dock = DockStyle.Fill;
            this.splitMain.Orientation = Orientation.Horizontal;
            this.splitMain.SplitterDistance = 400;
            this.splitMain.SplitterWidth = 6;

            // dgvFiles
            this.dgvFiles.Dock = DockStyle.Fill;
            this.dgvFiles.BackgroundColor = Color.White;
            this.dgvFiles.BorderStyle = BorderStyle.None;
            this.dgvFiles.AllowUserToAddRows = false;
            this.dgvFiles.AllowUserToDeleteRows = false;
            this.dgvFiles.ReadOnly = true;
            this.dgvFiles.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            this.dgvFiles.MultiSelect = false;
            this.dgvFiles.RowHeadersVisible = false;
            this.dgvFiles.AutoGenerateColumns = false;
            this.dgvFiles.ContextMenuStrip = this.ctxMenuGrid;

            var colStatus = new DataGridViewTextBoxColumn
            {
                Name = "colStatus",
                HeaderText = "Stato Integrità",
                DataPropertyName = "Status",
                Width = 135
            };
            var colPath = new DataGridViewTextBoxColumn
            {
                Name = "colPath",
                HeaderText = "Percorso Relativo",
                DataPropertyName = "RelPath",
                Width = 320
            };
            var colSize = new DataGridViewTextBoxColumn
            {
                Name = "colSize",
                HeaderText = "Dimensione",
                DataPropertyName = "Size",
                Width = 100
            };
            var colCrc = new DataGridViewTextBoxColumn
            {
                Name = "colCrc",
                HeaderText = "CRC32",
                DataPropertyName = "Crc32Hex",
                Width = 110
            };
            var colSha = new DataGridViewTextBoxColumn
            {
                Name = "colSha",
                HeaderText = "Impronta SHA-256",
                DataPropertyName = "Sha256",
                Width = 260
            };
            var colDetail = new DataGridViewTextBoxColumn
            {
                Name = "colDetail",
                HeaderText = "Dettaglio / Diagnostica",
                DataPropertyName = "Detail",
                AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill
            };

            this.dgvFiles.Columns.AddRange(new DataGridViewColumn[] {
                colStatus, colPath, colSize, colCrc, colSha, colDetail
            });

            this.splitMain.Panel1.Controls.Add(this.dgvFiles);

            // rtbLog
            this.rtbLog.Dock = DockStyle.Fill;
            this.rtbLog.BackColor = Color.FromArgb(20, 24, 30);
            this.rtbLog.ForeColor = Color.FromArgb(200, 220, 240);
            this.rtbLog.Font = new Font("Consolas", 9.5F);
            this.rtbLog.ReadOnly = true;
            this.rtbLog.BorderStyle = BorderStyle.None;

            // progressBar
            this.progressBar.Dock = DockStyle.Bottom;
            this.progressBar.Height = 16;
            this.progressBar.Visible = false;

            this.splitMain.Panel2.Controls.Add(this.rtbLog);
            this.splitMain.Panel2.Controls.Add(this.progressBar);

            // Context Menu
            this.ctxCopyPath.Text = "Copia Percorso";
            this.ctxCopyPath.Click += new System.EventHandler(this.CtxMenuCopyPath_Click);

            this.ctxCopySha256.Text = "Copia Hash SHA-256";
            this.ctxCopySha256.Click += new System.EventHandler(this.CtxMenuCopySha256_Click);

            this.ctxMenuGrid.Items.AddRange(new ToolStripItem[] {
                this.ctxCopyPath,
                this.ctxCopySha256
            });

            // Form
            this.ClientSize = new Size(1160, 720);
            this.Controls.Add(this.splitMain);
            this.Controls.Add(this.pnlStats);
            this.Controls.Add(this.pnlActions);
            this.Controls.Add(this.gbPaths);
            this.Controls.Add(this.pnlHeader);

            this.splitMain.Panel1.ResumeLayout(false);
            this.splitMain.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.splitMain)).EndInit();
            this.splitMain.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.dgvFiles)).EndInit();
            this.ResumeLayout(false);
        }
    }
}
