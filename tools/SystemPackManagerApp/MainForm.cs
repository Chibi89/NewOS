#nullable enable
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace SystemPackManagerApp
{
    public partial class MainForm : Form
    {
        private List<FileAuditItem> _allAuditItems = new();
        private FileSystemWatcher? _watcher;
        private bool _isBusy = false;

        public MainForm()
        {
            InitializeComponent();
            SetupCustomUI();
            InitializeDefaultPaths();
        }

        private void InitializeDefaultPaths()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            // Cerca la root MyOS
            string current = baseDir;
            string rootMyOS = "";
            for (int i = 0; i < 6; i++)
            {
                if (Directory.Exists(Path.Combine(current, "SystemPackFileWach")) || File.Exists(Path.Combine(current, "rules", "comportamenti.md")))
                {
                    rootMyOS = current;
                    break;
                }
                var parent = Directory.GetParent(current);
                if (parent == null) break;
                current = parent.FullName;
            }

            if (string.IsNullOrEmpty(rootMyOS))
            {
                rootMyOS = @"C:\Users\morga\Desktop\MyOS";
            }

            txtWorkDir.Text = Path.Combine(rootMyOS, "SystemPackFileWach");
            string pak1 = Path.Combine(rootMyOS, "build", "bin", "BootSetup", "SYSTEM.PAK");
            string pak2 = Path.Combine(rootMyOS, "build", "SYSTEM.PAK");
            txtPakPath.Text = File.Exists(pak1) ? pak1 : (File.Exists(pak2) ? pak2 : pak1);

            Log("Benvenuto in NewOS V1 — System Package Manager Desktop.", Color.FromArgb(70, 130, 180));
            Log("Crittografia: AES-256-CBC • Master Key FIPS-197 • Controllo Ciphertext NOSPAK10", Color.ForestGreen);

            // Avvio audit iniziale automatico
            this.Shown += async (s, e) =>
            {
                await RunAuditAsync();
            };
        }

        private void SetupCustomUI()
        {
            this.Text = "NewOS V1 — SYSTEM.PAK Package & Security Integrity Audit";
            this.Size = new Size(1180, 760);
            this.StartPosition = FormStartPosition.CenterScreen;
            this.MinimumSize = new Size(900, 600);

            // Double buffering su DataGridView
            typeof(DataGridView).InvokeMember("DoubleBuffered",
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.SetProperty,
                null, dgvFiles, new object[] { true });

            dgvFiles.CellFormatting += DgvFiles_CellFormatting;
            dgvFiles.CellDoubleClick += DgvFiles_CellDoubleClick;
        }

        private void DgvFiles_CellFormatting(object? sender, DataGridViewCellFormattingEventArgs e)
        {
            if (e.RowIndex < 0 || e.RowIndex >= dgvFiles.Rows.Count) return;
            var row = dgvFiles.Rows[e.RowIndex];
            if (row.DataBoundItem is FileAuditItem item)
            {
                if (dgvFiles.Columns[e.ColumnIndex].Name == "colStatus")
                {
                    if (e.CellStyle != null)
                    {
                        switch (item.Status)
                        {
                            case FileAuditStatus.Authentic:
                                e.Value = "✔ AUTENTICO";
                                e.CellStyle.ForeColor = Color.ForestGreen;
                                e.CellStyle.Font = new Font(dgvFiles.Font, FontStyle.Bold);
                                break;
                            case FileAuditStatus.Modified:
                                e.Value = "⚠ MODIFICATO";
                                e.CellStyle.ForeColor = Color.DarkOrange;
                                e.CellStyle.Font = new Font(dgvFiles.Font, FontStyle.Bold);
                                break;
                            case FileAuditStatus.NewFile:
                                e.Value = "★ NUOVO";
                                e.CellStyle.ForeColor = Color.DodgerBlue;
                                e.CellStyle.Font = new Font(dgvFiles.Font, FontStyle.Bold);
                                break;
                            case FileAuditStatus.Missing:
                                e.Value = "✖ MANCANTE";
                                e.CellStyle.ForeColor = Color.Crimson;
                                e.CellStyle.Font = new Font(dgvFiles.Font, FontStyle.Bold);
                                break;
                            case FileAuditStatus.Corrupted:
                                e.Value = "⛔ CORROTTO";
                                e.CellStyle.ForeColor = Color.DarkRed;
                                e.CellStyle.Font = new Font(dgvFiles.Font, FontStyle.Bold);
                                break;
                        }
                        e.FormattingApplied = true;
                    }
                }
                else if (dgvFiles.Columns[e.ColumnIndex].Name == "colSize")
                {
                    if (item.Size > 1024 * 1024)
                        e.Value = $"{item.Size / 1024.0 / 1024.0:F2} MB";
                    else if (item.Size > 1024)
                        e.Value = $"{item.Size / 1024.0:F1} KB";
                    else
                        e.Value = $"{item.Size} B";
                    e.FormattingApplied = true;
                }
            }
        }

        private void DgvFiles_CellDoubleClick(object? sender, DataGridViewCellEventArgs e)
        {
            if (e.RowIndex < 0) return;
            if (dgvFiles.Rows[e.RowIndex].DataBoundItem is FileAuditItem item)
            {
                string fullPath = Path.Combine(txtWorkDir.Text, item.RelPath.Replace('/', Path.DirectorySeparatorChar));
                if (File.Exists(fullPath))
                {
                    try
                    {
                        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
                        {
                            FileName = fullPath,
                            UseShellExecute = true
                        });
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show($"Impossibile aprire il file: {ex.Message}", "Errore Apertura", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }
                }
                else
                {
                    MessageBox.Show($"File non presente fisicamente sul disco:\n{fullPath}", "File Assente", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            }
        }

        private void Log(string message, Color? color = null)
        {
            if (InvokeRequired)
            {
                Invoke(new Action(() => Log(message, color)));
                return;
            }

            rtbLog.SelectionStart = rtbLog.TextLength;
            rtbLog.SelectionLength = 0;
            rtbLog.SelectionColor = color ?? Color.Black;
            rtbLog.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}\n");
            rtbLog.ScrollToCaret();
        }

        private async Task RunAuditAsync()
        {
            if (_isBusy) return;
            _isBusy = true;
            SetControlsEnabled(false);
            progressBar.Style = ProgressBarStyle.Marquee;
            progressBar.Visible = true;

            string workDir = txtWorkDir.Text;
            string pakPath = txtPakPath.Text;

            Log("Avvio Audit di Integrità e Rilevamento Manomissioni...", Color.Navy);

            try
            {
                var list = await Task.Run(() => PakEngine.AuditFolder(workDir, pakPath, (msg, err) => Log(msg, err ? Color.Red : Color.DarkSlateGray)));
                _allAuditItems = list;
                ApplyFilter();

                int auth = list.Count(x => x.Status == FileAuditStatus.Authentic);
                int mod = list.Count(x => x.Status == FileAuditStatus.Modified);
                int newFiles = list.Count(x => x.Status == FileAuditStatus.NewFile);
                int missing = list.Count(x => x.Status == FileAuditStatus.Missing);

                lblTotalVal.Text = list.Count.ToString();
                lblAuthVal.Text = auth.ToString();
                lblModVal.Text = mod.ToString();
                lblNewVal.Text = newFiles.ToString();
                lblMissVal.Text = missing.ToString();

                Log($"Audit completato: {list.Count} file analizzati. Autentici: {auth}, Modificati: {mod}, Nuovi: {newFiles}, Rimossi: {missing}",
                    (mod > 0 || missing > 0) ? Color.DarkOrange : Color.ForestGreen);
            }
            catch (Exception ex)
            {
                Log($"ERRORE AUDIT: {ex.Message}", Color.Red);
                MessageBox.Show($"Errore durante l'audit: {ex.Message}", "Errore", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                progressBar.Visible = false;
                SetControlsEnabled(true);
                _isBusy = false;
            }
        }

        private async void BtnUnpack_Click(object? sender, EventArgs e)
        {
            if (_isBusy) return;
            string pak = txtPakPath.Text;
            string dest = txtWorkDir.Text;

            if (!File.Exists(pak))
            {
                MessageBox.Show($"File pacchetto non trovato:\n{pak}", "File Mancante", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            var confirm = MessageBox.Show($"Sei sicuro di voler estrarre e decifrare '{Path.GetFileName(pak)}' nella cartella:\n{dest}?\n\nEventuali file locali verranno sovrascritti.",
                "Conferma Estrazione", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (confirm != DialogResult.Yes) return;

            _isBusy = true;
            SetControlsEnabled(false);
            progressBar.Style = ProgressBarStyle.Continuous;
            progressBar.Value = 0;
            progressBar.Visible = true;

            Log("--> Avvio procedura di decifratura ed estrazione AES-256...", Color.Navy);

            try
            {
                var list = await Task.Run(() =>
                {
                    return PakEngine.Unpack(pak, dest,
                        (msg, err) => Log(msg, err ? Color.Red : Color.Black),
                        (curr, tot) =>
                        {
                            if (tot > 0)
                            {
                                int pct = (int)((curr * 100.0) / tot);
                                Invoke(new Action(() => progressBar.Value = Math.Clamp(pct, 0, 100)));
                            }
                        });
                });

                _allAuditItems = list;
                ApplyFilter();
                Log($"ESTRAZIONE COMPLETATA: {list.Count} file estratti e certificati con successo!", Color.ForestGreen);
                MessageBox.Show($"Estrazione e decifratura completata!\n{list.Count} file estratti in:\n{dest}", "Successo", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                Log($"ERRORE ESTRAZIONE: {ex.Message}", Color.Red);
                MessageBox.Show($"Errore critico durante l'estrazione:\n{ex.Message}", "Errore Crittografico", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                progressBar.Visible = false;
                SetControlsEnabled(true);
                _isBusy = false;
                await RunAuditAsync();
            }
        }

        private async void BtnPack_Click(object? sender, EventArgs e)
        {
            if (_isBusy) return;
            string workDir = txtWorkDir.Text;
            string outPak = txtPakPath.Text;

            if (!Directory.Exists(workDir))
            {
                MessageBox.Show($"Cartella di lavoro non trovata:\n{workDir}", "Cartella Mancante", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            var confirm = MessageBox.Show($"Confermi la generazione, cifratura AES-256 e sincronizzazione di:\n{outPak}?",
                "Conferma Impacchettamento", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (confirm != DialogResult.Yes) return;

            _isBusy = true;
            SetControlsEnabled(false);
            progressBar.Style = ProgressBarStyle.Continuous;
            progressBar.Value = 0;
            progressBar.Visible = true;

            Log("--> Avvio scansione, serializzazione e cifratura AES-256 di SYSTEM.PAK...", Color.Navy);

            try
            {
                var res = await Task.Run(() =>
                {
                    return PakEngine.Pack(workDir, outPak, true,
                        (msg, err) => Log(msg, err ? Color.Red : Color.Black),
                        (curr, tot) =>
                        {
                            if (tot > 0)
                            {
                                int pct = (int)((curr * 100.0) / tot);
                                Invoke(new Action(() => progressBar.Value = Math.Clamp(pct, 0, 100)));
                            }
                        });
                });

                Log($"IMPACCHETTAMENTO OK! {res.totalFiles} file cifrati in AES-256 ({res.encrypted / 1024.0 / 1024.0:F2} MB). Checksum CRC32: 0x{res.payloadCrc:X8}", Color.ForestGreen);
                MessageBox.Show($"Pacchetto SYSTEM.PAK generato con successo!\n\nFile inclusi: {res.totalFiles}\nDimensione cifrata: {res.encrypted / 1024.0 / 1024.0:F2} MB\nPayload CRC32: 0x{res.payloadCrc:X8}\n\nSincronizzato in build/bin/BootSetup/SYSTEM.PAK.",
                    "Operazione Riuscita", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                Log($"ERRORE IMPACCHETTAMENTO: {ex.Message}", Color.Red);
                MessageBox.Show($"Errore durante la creazione del pacchetto:\n{ex.Message}", "Errore", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                progressBar.Visible = false;
                SetControlsEnabled(true);
                _isBusy = false;
                await RunAuditAsync();
            }
        }

        private async void BtnAudit_Click(object? sender, EventArgs e)
        {
            await RunAuditAsync();
        }

        private void ChkWatch_CheckedChanged(object? sender, EventArgs e)
        {
            if (chkWatch.Checked)
            {
                string dir = txtWorkDir.Text;
                if (!Directory.Exists(dir))
                {
                    chkWatch.Checked = false;
                    MessageBox.Show("La cartella specificata non esiste.", "Attenzione", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }

                try
                {
                    _watcher = new FileSystemWatcher(dir)
                    {
                        IncludeSubdirectories = true,
                        NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.Size
                    };

                    _watcher.Changed += OnFileSystemActivity;
                    _watcher.Created += OnFileSystemActivity;
                    _watcher.Deleted += OnFileSystemActivity;
                    _watcher.Renamed += OnFileSystemActivity;
                    _watcher.EnableRaisingEvents = true;

                    chkWatch.ForeColor = Color.ForestGreen;
                    chkWatch.Text = "👁 Watcher Attivo (Monitoraggio On)";
                    Log($"[WATCHER] Monitoraggio in tempo reale attivato su: {dir}", Color.ForestGreen);
                }
                catch (Exception ex)
                {
                    chkWatch.Checked = false;
                    Log($"[WATCHER ERRORE] {ex.Message}", Color.Red);
                }
            }
            else
            {
                if (_watcher != null)
                {
                    _watcher.EnableRaisingEvents = false;
                    _watcher.Dispose();
                    _watcher = null;
                }
                chkWatch.ForeColor = Color.Black;
                chkWatch.Text = "👁 Monitoraggio Attivo (Watch)";
                Log("[WATCHER] Monitoraggio in tempo reale disattivato.", Color.DarkSlateGray);
            }
        }

        private System.Threading.Timer? _debounceTimer;
        private void OnFileSystemActivity(object sender, FileSystemEventArgs e)
        {
            string name = e.Name ?? "";
            if (name == "SYSTEM.PAK" || name == ".pack_manifest.json" || name.EndsWith(".tmp", StringComparison.OrdinalIgnoreCase))
                return;

            _debounceTimer?.Dispose();
            _debounceTimer = new System.Threading.Timer(async _ =>
            {
                Invoke(new Action(() =>
                {
                    Log($"[EVENTO FILE] Rilevata modifica su: {name} ({e.ChangeType})", Color.DarkOrange);
                }));
                await RunAuditAsync();
            }, null, 500, System.Threading.Timeout.Infinite);
        }

        private void ApplyFilter()
        {
            string q = txtSearch.Text.Trim().ToLowerInvariant();
            if (string.IsNullOrEmpty(q))
            {
                dgvFiles.DataSource = _allAuditItems.ToList();
            }
            else
            {
                dgvFiles.DataSource = _allAuditItems
                    .Where(x => x.RelPath.ToLowerInvariant().Contains(q) || x.Status.ToString().ToLowerInvariant().Contains(q))
                    .ToList();
            }
            lblShowingCount.Text = $"Visualizzati: {dgvFiles.Rows.Count} di {_allAuditItems.Count}";
        }

        private void TxtSearch_TextChanged(object? sender, EventArgs e)
        {
            ApplyFilter();
        }

        private void BtnOpenFolder_Click(object? sender, EventArgs e)
        {
            string dir = txtWorkDir.Text;
            if (Directory.Exists(dir))
            {
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
                {
                    FileName = dir,
                    UseShellExecute = true
                });
            }
        }

        private void BtnBrowseWorkDir_Click(object? sender, EventArgs e)
        {
            using var dlg = new FolderBrowserDialog();
            dlg.SelectedPath = txtWorkDir.Text;
            if (dlg.ShowDialog() == DialogResult.OK)
            {
                txtWorkDir.Text = dlg.SelectedPath;
            }
        }

        private void BtnBrowsePak_Click(object? sender, EventArgs e)
        {
            using var dlg = new OpenFileDialog();
            dlg.Filter = "NewOS System Package (*.PAK)|*.PAK|Tutti i file (*.*)|*.*";
            dlg.FileName = txtPakPath.Text;
            if (dlg.ShowDialog() == DialogResult.OK)
            {
                txtPakPath.Text = dlg.FileName;
            }
        }

        private void SetControlsEnabled(bool enabled)
        {
            btnUnpack.Enabled = enabled;
            btnPack.Enabled = enabled;
            btnAudit.Enabled = enabled;
            btnBrowseWorkDir.Enabled = enabled;
            btnBrowsePak.Enabled = enabled;
        }

        private void CtxMenuCopySha256_Click(object? sender, EventArgs e)
        {
            if (dgvFiles.CurrentRow?.DataBoundItem is FileAuditItem item)
            {
                Clipboard.SetText(item.Sha256);
                Log($"Copiato hash SHA-256 negli appunti: {item.Sha256}", Color.Navy);
            }
        }

        private void CtxMenuCopyPath_Click(object? sender, EventArgs e)
        {
            if (dgvFiles.CurrentRow?.DataBoundItem is FileAuditItem item)
            {
                Clipboard.SetText(item.RelPath);
                Log($"Copiato percorso negli appunti: {item.RelPath}", Color.Navy);
            }
        }
    }
}
